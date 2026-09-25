#include "stdafx.h"
#include <exception> //set_terminate
#include <cstdlib> //_set_invalid_parameter_handler
#include <cstdarg>
#pragma warning(push,0)
#include <DbgHelp.h> //Call stack symbolisation
#include <TlHelp32.h> //Loaded module list
#pragma warning(pop)
#include "Misc.h"
#include "CrashReport.h"

#pragma comment(lib,"dbghelp.lib")

namespace
{
    const UObject* g_pCrashObject = nullptr;
    const UStruct* g_pCrashFunction = nullptr;

    //The crash handler can run on another thread, so these are never shortened: the final element stays zero,
    //which keeps them terminated no matter where a concurrent write got to.
    wchar_t g_szCrashPhase[256] = {};
    wchar_t g_szCrashMap[128] = {};

    //Without an age, a stale stage reads like the thing that crashed: the sample report blamed a font package
    //loaded minutes earlier.
    ULONGLONG g_iCrashPhaseTicks = 0;
    ULONGLONG g_iCrashMapTicks = 0;
    ULONGLONG g_iStartTicks = 0;
    DWORD g_dwMainThreadId = 0;

    //File opens never reach the log, so this is the only record of which package the engine was actually reading
    wchar_t g_szRecentFiles[16][MAX_PATH] = {};
    size_t g_iRecentFileNext = 0;

    //A fault the engine's guard chain catches never reaches the unhandled filter, and the registers and the faulting
    //stack are long gone by the time it rethrows its way out to the launcher. A vectored handler sees them first.
    EXCEPTION_RECORD g_FaultRecord = {};
    CONTEXT g_FaultContext = {};
    bool g_bHaveFault = false;
    bool g_bReporting = false; //Reporting walks memory the crash already corrupted, so it must not record its own faults

    const INT g_iMaxObjectIndex = 262144; //The object table is walked by index, so this caps a corrupt one

    double SecondsSince(const ULONGLONG iTicks)
    {
        return iTicks != 0 ? (GetTickCount64() - iTicks) / 1000.0 : 0.0;
    }

    //ReadProcessMemory on our own process reports unreadable addresses instead of faulting on them, which is what
    //makes it safe to chase engine pointers a crash has already proven to be suspect.
    bool ReadMemory(const void* const pAddress, void* const pBuffer, const size_t iSize)
    {
        SIZE_T iRead = 0;
        return pAddress != nullptr
            && ReadProcessMemory(GetCurrentProcess(), pAddress, pBuffer, iSize, &iRead) != FALSE
            && iRead == iSize;
    }

    bool SafeCopyString(wchar_t* const pszBuffer, const size_t count, const wchar_t* const pszSource)
    {
        for(size_t i = 0; i + 1 < count; ++i)
        {
            wchar_t c = 0;
            if(!ReadMemory(pszSource + i, &c, sizeof(c)) || (c != 0 && (c < L' ' || c > 0xFFFD))) //Reject anything that isn't plausibly a name
            {
                pszBuffer[i] = 0;
                return i > 0;
            }

            pszBuffer[i] = c;
            if(c == 0)
            {
                return true;
            }
        }

        pszBuffer[count - 1] = 0;
        return true;
    }

    //The object table is gone by the time a shutdown crash reports, and can be corrupt in any other one
    const UObject* IndexedObject(const INT iIndex)
    {
        __try
        {
            return UObject::GetIndexedObject(iIndex);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    //Only pointers the object table itself confirms are accepted, which is what stops a stack scan from turning
    //every stray integer into an object name.
    const UObject* AsObject(const void* const pAddress)
    {
        BYTE Header[sizeof(UObject)];
        if((reinterpret_cast<uintptr_t>(pAddress) & 3) != 0 || !ReadMemory(pAddress, Header, sizeof(Header)))
        {
            return nullptr;
        }

        const UObject* const pObject = static_cast<const UObject*>(pAddress);
        const DWORD iIndex = pObject->GetIndex();
        return iIndex < static_cast<DWORD>(MAXINT) && IndexedObject(static_cast<INT>(iIndex)) == pObject ? pObject : nullptr;
    }

    const wchar_t* NameText(wchar_t* const pszBuffer, const size_t count, const FName Name)
    {
        const INT iIndex = Name.GetIndex();
        if(!FName::GetInitialized() || iIndex < 0 || iIndex >= FName::GetMaxNames() || !SafeCopyString(pszBuffer, count, *Name))
        {
            _snwprintf_s(pszBuffer, count, _TRUNCATE, L"<name %i>", iIndex);
        }

        return pszBuffer;
    }

    //Deliberately not UObject::GetFullName(): that walks Outer and the name table with no validation whatsoever, so
    //on a corrupt object it faults - which is why the engine's own unwind text names no object at all.
    const wchar_t* SafeObjectName(wchar_t* const pszBuffer, const size_t count, const UObject* const pObject)
    {
        BYTE Header[sizeof(UObject)];
        if(pObject == nullptr)
        {
            wcsncpy_s(pszBuffer, count, L"None", _TRUNCATE);
            return pszBuffer;
        }
        if(!ReadMemory(pObject, Header, sizeof(Header)))
        {
            _snwprintf_s(pszBuffer, count, _TRUNCATE, L"<unreadable object at 0x%p>", pObject);
            return pszBuffer;
        }

        wchar_t szClass[128] = L"<unknown class>";
        const UObject* const pClass = AsObject(pObject->GetClass());
        if(pClass != nullptr)
        {
            NameText(szClass, _countof(szClass), pClass->GetFName());
        }

        const UObject* Outers[8] = {}; //Capped, so a looping Outer chain can't hang the report
        size_t iOuters = 0;
        for(const UObject* pOuter = AsObject(pObject->GetOuter()); pOuter != nullptr && iOuters < _countof(Outers); pOuter = AsObject(pOuter->GetOuter()))
        {
            Outers[iOuters++] = pOuter;
        }

        wchar_t szPath[512] = L""; //Outermost first, so it reads like the engine's own Package.Group.Name
        while(iOuters > 0)
        {
            wchar_t szOuter[128];
            wcsncat_s(szPath, NameText(szOuter, _countof(szOuter), Outers[--iOuters]->GetFName()), _TRUNCATE);
            wcsncat_s(szPath, L".", _TRUNCATE);
        }

        wchar_t szName[128];
        _snwprintf_s(pszBuffer, count, _TRUNCATE, L"%s %s%s (object %u, flags 0x%08X)", szClass, szPath,
            NameText(szName, _countof(szName), pObject->GetFName()), pObject->GetIndex(), pObject->GetFlags());
        return pszBuffer;
    }

    struct FObjectScan
    {
        size_t iObjects;
        const UObject* pNearest;
        uintptr_t iNearestOffset;
        INT iNearestSize;
    };

    //One pass over the object table: how many objects are live, and which one an address falls inside
    void ScanObjectTable(const void* const pAddress, FObjectScan& Scan)
    {
        Scan.iObjects = 0;
        Scan.pNearest = nullptr;
        Scan.iNearestOffset = 0;
        Scan.iNearestSize = 0;

        const uintptr_t iAddress = reinterpret_cast<uintptr_t>(pAddress);
        __try
        {
            for(INT i = 0; i < g_iMaxObjectIndex; ++i)
            {
                const UObject* const pObject = UObject::GetIndexedObject(i);
                if(pObject == nullptr) //Also how an index past the end of the table answers
                {
                    continue;
                }
                Scan.iObjects++;

                //Unsigned, so an object above the address wraps to something far larger than any object
                const uintptr_t iOffset = iAddress - reinterpret_cast<uintptr_t>(pObject);
                if(iOffset < 65536 && (Scan.pNearest == nullptr || iOffset < Scan.iNearestOffset))
                {
                    Scan.pNearest = pObject;
                    Scan.iNearestOffset = iOffset;
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
        }

        BYTE Header[sizeof(UStruct)];
        if(Scan.pNearest != nullptr && AsObject(Scan.pNearest->GetClass()) != nullptr && ReadMemory(Scan.pNearest->GetClass(), Header, sizeof(Header)))
        {
            Scan.iNearestSize = Scan.pNearest->GetClass()->GetPropertiesSize();
        }
    }

    const wchar_t* DescribeProtection(const DWORD dwProtect)
    {
        switch(dwProtect & 0xFF)
        {
        case PAGE_NOACCESS:          return L"no access";
        case PAGE_READONLY:          return L"read-only";
        case PAGE_READWRITE:         return L"read/write";
        case PAGE_WRITECOPY:         return L"copy-on-write";
        case PAGE_EXECUTE:           return L"execute";
        case PAGE_EXECUTE_READ:      return L"execute/read";
        case PAGE_EXECUTE_READWRITE: return L"execute/read/write";
        case PAGE_EXECUTE_WRITECOPY: return L"execute/copy-on-write";
        default:                     return L"unknown protection";
        }
    }

    //Everything below formats with _snwprintf_s/_TRUNCATE rather than swprintf_s: overflowing the latter calls the
    //invalid parameter handler, which is one of the things that ends up reporting through here.
    const wchar_t* DescribeException(wchar_t* const pszBuffer, const size_t count, const EXCEPTION_RECORD* const pRecord)
    {
        if(pRecord == nullptr)
        {
            wcsncpy_s(pszBuffer, count, L"<no exception record>", _TRUNCATE);
            return pszBuffer;
        }

        static const struct { DWORD dwCode; const wchar_t* pszName; } Names[] =
        {
            { EXCEPTION_ACCESS_VIOLATION,      L"access violation" },
            { EXCEPTION_ARRAY_BOUNDS_EXCEEDED, L"array bounds exceeded" },
            { EXCEPTION_DATATYPE_MISALIGNMENT, L"datatype misalignment" },
            { EXCEPTION_FLT_DIVIDE_BY_ZERO,    L"float divide by zero" },
            { EXCEPTION_FLT_INVALID_OPERATION, L"invalid float operation" },
            { EXCEPTION_ILLEGAL_INSTRUCTION,   L"illegal instruction" },
            { EXCEPTION_IN_PAGE_ERROR,         L"in-page error" },
            { EXCEPTION_INT_DIVIDE_BY_ZERO,    L"integer divide by zero" },
            { EXCEPTION_PRIV_INSTRUCTION,      L"privileged instruction" },
            { EXCEPTION_STACK_OVERFLOW,        L"stack overflow" },
            { EXCEPTION_BREAKPOINT,            L"breakpoint" },
            { 0xE06D7363,                      L"unhandled C++ exception" },
        };

        const wchar_t* pszName = L"unknown";
        for(const auto& Name : Names)
        {
            if(Name.dwCode == pRecord->ExceptionCode)
            {
                pszName = Name.pszName;
                break;
            }
        }

        //An access violation carries the operation and the address that was touched, which is what pins down a null/freed object
        if((pRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || pRecord->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && pRecord->NumberParameters >= 2)
        {
            const ULONG_PTR iOperation = pRecord->ExceptionInformation[0];
            _snwprintf_s(pszBuffer, count, _TRUNCATE, L"Exception 0x%08X (%s: %s address 0x%p)", pRecord->ExceptionCode, pszName,
                iOperation == 0 ? L"read from" : iOperation == 1 ? L"write to" : L"execute at",
                reinterpret_cast<const void*>(pRecord->ExceptionInformation[1]));
        }
        else
        {
            _snwprintf_s(pszBuffer, count, _TRUNCATE, L"Exception 0x%08X (%s)", pRecord->ExceptionCode, pszName);
        }

        return pszBuffer;
    }

    //Without this the address alone says nothing: the offset can be looked up in the map file of whichever module faulted
    const wchar_t* DescribeCodeAddress(wchar_t* const pszBuffer, const size_t count, const void* const pAddress)
    {
        HMODULE hModule = NULL;
        wchar_t szModule[MAX_PATH];
        if(pAddress != nullptr
            && GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(pAddress), &hModule)
            && GetModuleFileName(hModule, szModule, _countof(szModule)) != 0)
        {
            const size_t iOffset = static_cast<size_t>(static_cast<const BYTE*>(pAddress) - reinterpret_cast<const BYTE*>(hModule));
            _snwprintf_s(pszBuffer, count, _TRUNCATE, L"0x%p (%s+0x%08IX)", pAddress, PathFindFileName(szModule), iOffset);
        }
        else
        {
            _snwprintf_s(pszBuffer, count, _TRUNCATE, L"0x%p (unknown module)", pAddress);
        }

        return pszBuffer;
    }

    void LogDetail(const wchar_t* const pszFormat, ...)
    {
        static wchar_t szLine[2048]; //Static: a stack overflow report has no room left to format on the stack

        va_list Args;
        va_start(Args, pszFormat);
        _vsnwprintf_s(szLine, _countof(szLine), _TRUNCATE, pszFormat, Args);
        va_end(Args);

        if(GLog != nullptr)
        {
            GLog->Log(NAME_Critical, szLine); //NAME_Critical so the log device flushes it before the process ends
        }
        else
        {
            OutputDebugString(szLine);
            OutputDebugString(L"\r\n");
        }
    }

    //A bare address says nothing about whether it was freed memory, a null dereference or an unmapped page
    void LogFaultAddress(const void* const pAddress, const FObjectScan& Scan)
    {
        MEMORY_BASIC_INFORMATION Info;
        if(VirtualQuery(pAddress, &Info, sizeof(Info)) != sizeof(Info))
        {
            LogDetail(L"  0x%p is not part of this process's address space.", pAddress);
        }
        else if(Info.State == MEM_FREE)
        {
            LogDetail(L"  0x%p is unallocated: nothing mapped from 0x%p for %Iu bytes.", pAddress, Info.BaseAddress, Info.RegionSize);
        }
        else
        {
            wchar_t szModule[MAX_PATH] = L"";
            if(Info.Type == MEM_IMAGE)
            {
                GetModuleFileName(static_cast<HMODULE>(Info.AllocationBase), szModule, _countof(szModule));
            }

            LogDetail(L"  0x%p is %s %s memory (%s), in a %Iu byte region at 0x%p%s%s.", pAddress,
                Info.State == MEM_COMMIT ? L"committed" : L"reserved but uncommitted",
                Info.Type == MEM_IMAGE ? L"image" : Info.Type == MEM_MAPPED ? L"file-mapped" : L"private",
                DescribeProtection(Info.Protect), Info.RegionSize, Info.BaseAddress,
                szModule[0] != 0 ? L", part of " : L"", szModule[0] != 0 ? PathFindFileName(szModule) : L"");
        }

        if(Scan.pNearest != nullptr)
        {
            wchar_t szObject[1024];
            if(Scan.iNearestSize > 0 && Scan.iNearestOffset < static_cast<uintptr_t>(Scan.iNearestSize))
            {
                LogDetail(L"  0x%p is offset 0x%IX of %s.", pAddress, Scan.iNearestOffset, SafeObjectName(szObject, _countof(szObject), Scan.pNearest));
            }
            else
            {
                LogDetail(L"  Nearest object below 0x%p is 0x%IX bytes lower: %s.", pAddress, Scan.iNearestOffset,
                    SafeObjectName(szObject, _countof(szObject), Scan.pNearest));
            }
        }
    }

    void LogRegisters(const CONTEXT& Context)
    {
        LogDetail(L"  EIP=0x%08X ESP=0x%08X EBP=0x%08X EFL=0x%08X", Context.Eip, Context.Esp, Context.Ebp, Context.EFlags);
        LogDetail(L"  EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X ESI=0x%08X EDI=0x%08X",
            Context.Eax, Context.Ebx, Context.Ecx, Context.Edx, Context.Esi, Context.Edi);

        //A method's own object arrives in ECX on x86, so naming the registers usually names the culprit outright
        const struct { const wchar_t* pszName; DWORD dwValue; } Registers[] =
        {
            { L"EAX", Context.Eax }, { L"EBX", Context.Ebx }, { L"ECX", Context.Ecx },
            { L"EDX", Context.Edx }, { L"ESI", Context.Esi }, { L"EDI", Context.Edi },
        };

        for(const auto& Register : Registers)
        {
            const UObject* const pObject = AsObject(reinterpret_cast<const void*>(Register.dwValue));
            if(pObject != nullptr)
            {
                wchar_t szObject[1024];
                LogDetail(L"  %s -> %s", Register.pszName, SafeObjectName(szObject, _countof(szObject), pObject));
            }
        }
    }

    //The engine's unwind chain only lists functions that happen to be guarded, in engine DLLs, and stops at the
    //first frame that isn't. A real walk covers the renderer, the CRT, injected overlays and our own code too.
    void LogCallStack(const CONTEXT& Context)
    {
        const HANDLE hProcess = GetCurrentProcess();
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);

        //An explicit search path, so a machine with _NT_SYMBOL_PATH set doesn't spend a crash report waiting on a symbol server
        wchar_t szSearchPath[MAX_PATH] = L".";
        Misc::GetGameSystemDir(szSearchPath);
        const bool bSymbols = SymInitializeW(hProcess, szSearchPath, TRUE) != FALSE;

        //Even with no symbol files at all the engine's exports resolve, which is already more than the guard chain gives
        ULONG64 SymbolBuffer[(sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t)) / sizeof(ULONG64) + 1] = {};
        SYMBOL_INFOW* const pSymbol = reinterpret_cast<SYMBOL_INFOW*>(SymbolBuffer);

        CONTEXT Walk = Context; //StackWalk64 updates the context it walks with
        STACKFRAME64 Frame = {};
        Frame.AddrPC.Offset = Walk.Eip;
        Frame.AddrPC.Mode = AddrModeFlat;
        Frame.AddrFrame.Offset = Walk.Ebp;
        Frame.AddrFrame.Mode = AddrModeFlat;
        Frame.AddrStack.Offset = Walk.Esp;
        Frame.AddrStack.Mode = AddrModeFlat;

        __try
        {
            for(int i = 0; i < 64; i++)
            {
                if(StackWalk64(IMAGE_FILE_MACHINE_I386, hProcess, GetCurrentThread(), &Frame, &Walk, nullptr,
                    SymFunctionTableAccess64, SymGetModuleBase64, nullptr) == FALSE || Frame.AddrPC.Offset == 0)
                {
                    break;
                }

                wchar_t szAddress[MAX_PATH + 64];
                DescribeCodeAddress(szAddress, _countof(szAddress), reinterpret_cast<const void*>(static_cast<uintptr_t>(Frame.AddrPC.Offset)));

                pSymbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
                pSymbol->MaxNameLen = 512;
                DWORD64 iDisplacement = 0;
                if(bSymbols && SymFromAddrW(hProcess, Frame.AddrPC.Offset, &iDisplacement, pSymbol) != FALSE)
                {
                    LogDetail(L"  %s %s+0x%IX", szAddress, pSymbol->Name, static_cast<size_t>(iDisplacement));
                }
                else
                {
                    LogDetail(L"  %s", szAddress);
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
        }

        if(bSymbols)
        {
            SymCleanup(hProcess);
        }
    }

    //Locals and arguments still hold the objects the crashing code was working on, so this names them even when
    //the fault address itself belongs to nothing recognisable.
    void LogStackObjects(const CONTEXT& Context)
    {
        const uintptr_t iStart = Context.Esp;
        const UObject* Seen[16] = {};
        size_t iSeen = 0;

        for(uintptr_t i = iStart; i < iStart + 16384 && iSeen < _countof(Seen); i += sizeof(void*))
        {
            const void* pValue = nullptr;
            if(!ReadMemory(reinterpret_cast<const void*>(i), &pValue, sizeof(pValue)))
            {
                break;
            }

            const UObject* const pObject = AsObject(pValue);
            if(pObject == nullptr)
            {
                continue;
            }

            bool bKnown = false;
            for(size_t j = 0; j < iSeen; j++)
            {
                bKnown = bKnown || Seen[j] == pObject;
            }
            if(bKnown)
            {
                continue;
            }

            Seen[iSeen++] = pObject;
            wchar_t szObject[1024];
            LogDetail(L"  [esp+0x%04IX] %s", i - iStart, SafeObjectName(szObject, _countof(szObject), pObject));
        }
    }

    //Which renderer, sound driver or injected overlay is loaded, and what module+offset addresses resolve against
    void LogModules()
    {
        const HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if(hSnapshot == INVALID_HANDLE_VALUE)
        {
            return;
        }

        MODULEENTRY32W Entry = {};
        Entry.dwSize = sizeof(Entry);
        for(BOOL bMore = Module32FirstW(hSnapshot, &Entry); bMore != FALSE; bMore = Module32NextW(hSnapshot, &Entry))
        {
            LogDetail(L"  0x%p + 0x%08X  %s", Entry.modBaseAddr, Entry.modBaseSize, Entry.szModule);
        }

        CloseHandle(hSnapshot);
    }

    //A 32 bit game runs out of contiguous address space long before it runs out of memory, and that surfaces as an
    //allocation failure somewhere entirely unrelated to the cause
    void LogAddressSpace()
    {
        MEMORYSTATUSEX Status = {};
        Status.dwLength = sizeof(Status);
        if(GlobalMemoryStatusEx(&Status) != FALSE)
        {
            LogDetail(L"  Address space: %I64u of %I64u MB free. Physical memory: %I64u of %I64u MB free.",
                Status.ullAvailVirtual / (1024 * 1024), Status.ullTotalVirtual / (1024 * 1024),
                Status.ullAvailPhys / (1024 * 1024), Status.ullTotalPhys / (1024 * 1024));
        }

        SYSTEM_INFO SystemInfo = {};
        GetSystemInfo(&SystemInfo);

        SIZE_T iCommitted = 0;
        SIZE_T iLargestFree = 0;
        MEMORY_BASIC_INFORMATION Info;
        for(uintptr_t i = reinterpret_cast<uintptr_t>(SystemInfo.lpMinimumApplicationAddress);
            i < reinterpret_cast<uintptr_t>(SystemInfo.lpMaximumApplicationAddress)
                && VirtualQuery(reinterpret_cast<const void*>(i), &Info, sizeof(Info)) == sizeof(Info) && Info.RegionSize != 0;
            i += Info.RegionSize)
        {
            if(Info.State == MEM_COMMIT)
            {
                iCommitted += Info.RegionSize;
            }
            else if(Info.State == MEM_FREE)
            {
                iLargestFree = std::max(iLargestFree, Info.RegionSize);
            }
        }

        LogDetail(L"  Committed: %Iu MB. Largest free block: %Iu MB.", iCommitted / (1024 * 1024), iLargestFree / (1024 * 1024));
    }

    void LogRecentFiles()
    {
        for(size_t i = 0; i < _countof(g_szRecentFiles); i++)
        {
            const wchar_t* const pszPath = g_szRecentFiles[(g_iRecentFileNext + i) % _countof(g_szRecentFiles)];
            if(pszPath[0] != 0)
            {
                LogDetail(L"  %s", pszPath);
            }
        }
    }

    void ReportFatal(const wchar_t* const pszReason, const EXCEPTION_POINTERS* const pExceptionInfo)
    {
        if(g_bReporting) //A second fault while reporting would otherwise loop through the handlers below
        {
            return;
        }
        g_bReporting = true;

        const EXCEPTION_RECORD* const pRecord = pExceptionInfo != nullptr ? pExceptionInfo->ExceptionRecord : nullptr;

        //Static rather than local: a stack overflow leaves too little room to format a report on the stack
        static wchar_t szException[256];
        static wchar_t szAddress[MAX_PATH + 64];
        static wchar_t szContext[2048];
        static wchar_t szMessage[8192];

        DescribeException(szException, _countof(szException), pRecord);
        DescribeCodeAddress(szAddress, _countof(szAddress), pRecord != nullptr ? pRecord->ExceptionAddress : nullptr);
        CrashReport::GetContext(szContext, _countof(szContext));

        _snwprintf_s(szMessage, _TRUNCATE,
            L"%s has crashed.\r\n\r\n%s%s\r\nAt %s\r\n\r\n%s\r\n\r\nEngine error history:\r\n%s",
            PROJECTNAME, pszReason != nullptr ? pszReason : L"", szException, szAddress, szContext,
            GErrorHist[0] != 0 ? GErrorHist : L"(none)");

        //GLog can already be unusable this far into a crash
        __try
        {
            if (GLog)
            {
                GLog->Log(NAME_Critical, szMessage); //NAME_Critical so the log device flushes it before the process ends
            }
            else
            {
                OutputDebugString(szMessage);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        CrashReport::LogFault(pExceptionInfo);

        MessageBox(nullptr, szMessage, PROJECTNAME L" - Fatal Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);

        g_bReporting = false;
    }

    LONG WINAPI CrashHandler(EXCEPTION_POINTERS* const pExceptionInfo)
    {
        ReportFatal(nullptr, pExceptionInfo);

        return EXCEPTION_EXECUTE_HANDLER; //Do not call Windows Error Reporting.
    }

    //The engine's guard chain catches hardware faults itself and rethrows them as C++ exceptions, which loses the
    //registers and the faulting stack. Recording them first-chance is the only way to still have them at report time.
    LONG WINAPI FirstChanceHandler(EXCEPTION_POINTERS* const pExceptionInfo)
    {
        if(!g_bReporting && pExceptionInfo != nullptr && pExceptionInfo->ExceptionRecord != nullptr && pExceptionInfo->ContextRecord != nullptr
            && (pExceptionInfo->ExceptionRecord->ExceptionCode & 0xF0000000) == 0xC0000000) //Faults only; the unwind chain throws constantly
        {
            g_FaultRecord = *pExceptionInfo->ExceptionRecord;
            g_FaultContext = *pExceptionInfo->ContextRecord;
            g_bHaveFault = true;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    //An exception that escapes the engine's guard chain unwinds past our main loop and lands here instead of in the filter above
    void TerminateHandler()
    {
        ReportFatal(L"Unhandled C++ exception.\r\n", nullptr);
        TerminateProcess(GetCurrentProcess(), 1); //A terminate handler may not return
    }

    //The secure CRT functions kill the process on a bad argument (an overlong wcscpy_s destination, say); at least say so first
    void InvalidParameterHandler(const wchar_t* const pszExpression, const wchar_t* const pszFunction, const wchar_t* const pszFile, const unsigned int iLine, const uintptr_t /*pReserved*/)
    {
        static wchar_t szReason[1024];
        _snwprintf_s(szReason, _TRUNCATE, L"Invalid parameter to '%s' (%s, %s line %u).\r\n",
            pszFunction != nullptr ? pszFunction : L"<unknown function>",
            pszExpression != nullptr ? pszExpression : L"<no expression>",
            pszFile != nullptr ? pszFile : L"<no file>", iLine);

        ReportFatal(szReason, nullptr);
        TerminateProcess(GetCurrentProcess(), 1);
    }
}

const wchar_t* CrashReport::FormatScriptContext(wchar_t* const pszBuffer, const size_t count, const UObject* const pObject, const UStruct* const pFunction)
{
    wchar_t szObject[1024];
    SafeObjectName(szObject, _countof(szObject), pObject);

    //_snwprintf_s/_TRUNCATE, not swprintf_s: two full-length object names don't fit in a caller's buffer, and
    //overflowing swprintf_s kills the process - in a function that only ever runs while reporting a problem.
    if (pFunction)
    {
        wchar_t szFunction[1024];
        _snwprintf_s(pszBuffer, count, _TRUNCATE, L"object '%s' in %s", szObject, SafeObjectName(szFunction, _countof(szFunction), pFunction));
    }
    else
    {
        _snwprintf_s(pszBuffer, count, _TRUNCATE, L"object '%s'", szObject);
    }

    return pszBuffer;
}

void CrashReport::LogFault(const EXCEPTION_POINTERS* const pExceptionInfo)
{
    static bool bLogged = false; //Both the launcher's catch and the unhandled filter can reach this for one crash
    if(bLogged)
    {
        return;
    }
    bLogged = true;

    const bool bWasReporting = g_bReporting; //HandleError() reaches this without going through ReportFatal()
    g_bReporting = true;

    //Snapshots, because walking a corrupt process can fault and the vectored handler writes to the globals
    static EXCEPTION_RECORD Record;
    static CONTEXT Context;
    const EXCEPTION_RECORD* pRecord = nullptr;
    const CONTEXT* pContext = nullptr;
    if(pExceptionInfo != nullptr && pExceptionInfo->ExceptionRecord != nullptr)
    {
        Record = *pExceptionInfo->ExceptionRecord;
        pRecord = &Record;
        if(pExceptionInfo->ContextRecord != nullptr)
        {
            Context = *pExceptionInfo->ContextRecord;
            pContext = &Context;
        }
    }
    else if(g_bHaveFault) //The engine caught the fault itself, so fall back to what the vectored handler saw
    {
        Record = g_FaultRecord;
        Context = g_FaultContext;
        pRecord = &Record;
        pContext = &Context;
    }

    const void* const pFaultAddress = pRecord != nullptr && pRecord->NumberParameters >= 2
        && (pRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || pRecord->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
        ? reinterpret_cast<const void*>(pRecord->ExceptionInformation[1]) : nullptr;

    //GLog is often already unusable this deep into a crash, and the message box still has to appear
    __try
    {
        FObjectScan Scan;
        ScanObjectTable(pFaultAddress, Scan);

        LogDetail(L"--- Crash detail ---");
        LogDetail(L"  Thread %u (%s). Running for %.1f s.", GetCurrentThreadId(),
            GetCurrentThreadId() == g_dwMainThreadId ? L"main thread" : L"not the main thread", SecondsSince(g_iStartTicks));
        LogDetail(L"  Engine state: running=%i, exiting=%i, critical=%i, slow task=%i, in script=%i. %Iu objects, %i names.",
            GIsRunning, GIsRequestingExit, GIsCriticalError, GIsSlowTask, GScriptEntryTag, Scan.iObjects, FName::GetMaxNames());

        if(pFaultAddress != nullptr)
        {
            LogDetail(L"--- Faulting address ---");
            LogFaultAddress(pFaultAddress, Scan);
        }

        if(pContext != nullptr)
        {
            LogDetail(L"--- Registers ---");
            LogRegisters(*pContext);

            LogDetail(L"--- Call stack ---");
            LogCallStack(*pContext);

            LogDetail(L"--- Stack objects ---");
            LogStackObjects(*pContext);
        }
        else
        {
            LogDetail(L"  No fault context recorded: no call stack or registers.");
        }

        LogDetail(L"--- Recent files ---");
        LogRecentFiles();

        LogDetail(L"--- Address space ---");
        LogAddressSpace();

        LogDetail(L"--- Loaded modules ---");
        LogModules();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    g_bReporting = bWasReporting;
}

void CrashReport::SetScriptTrace(const UObject* const pObject, const UStruct* const pFunction)
{
    g_pCrashObject = pObject;
    g_pCrashFunction = pFunction;
}

void CrashReport::SetPhase(const wchar_t* const pszPhase)
{
    wcsncpy_s(g_szCrashPhase, _countof(g_szCrashPhase) - 1, pszPhase != nullptr ? pszPhase : L"", _TRUNCATE);
    g_iCrashPhaseTicks = GetTickCount64();
}

void CrashReport::SetMap(const wchar_t* const pszMap)
{
    wcsncpy_s(g_szCrashMap, _countof(g_szCrashMap) - 1, pszMap != nullptr ? pszMap : L"", _TRUNCATE);
    g_iCrashMapTicks = GetTickCount64();
}

void CrashReport::RecordFileOpen(const wchar_t* const pszPath)
{
    wcsncpy_s(g_szRecentFiles[g_iRecentFileNext], _countof(g_szRecentFiles[0]) - 1, pszPath, _TRUNCATE);
    g_iRecentFileNext = (g_iRecentFileNext + 1) % _countof(g_szRecentFiles);
}

const wchar_t* CrashReport::GetContext(wchar_t* const pszBuffer, const size_t count)
{
    wchar_t szScript[1152];
    if (g_pCrashObject)
    {
        FormatScriptContext(szScript, _countof(szScript), g_pCrashObject, g_pCrashFunction);
    }
    else
    {
        wcsncpy_s(szScript, Misc::IsVerboseLogging() ? L"<no hooked-native context recorded>" : L"<verbose logging disabled>", _TRUNCATE);
    }

    _snwprintf_s(pszBuffer, count, _TRUNCATE,
        L"Map: %s (%.1f s ago)\r\nLast engine stage: %s (%.1f s ago)\r\nLast script context: %s\r\nRunning for %.1f s.",
        g_szCrashMap[0] != 0 ? g_szCrashMap : L"<none>", SecondsSince(g_iCrashMapTicks),
        g_szCrashPhase[0] != 0 ? g_szCrashPhase : L"<none>", SecondsSince(g_iCrashPhaseTicks),
        szScript, SecondsSince(g_iStartTicks));

    return pszBuffer;
}

void CrashReport::Setup()
{
    ULONG ulStackGuarantee = 32 * 1024; //Without a reserve, a stack overflow leaves no room to run the handler at all
    SetThreadStackGuarantee(&ulStackGuarantee);

    g_iStartTicks = GetTickCount64();
    g_dwMainThreadId = GetCurrentThreadId();

    AddVectoredExceptionHandler(1, &FirstChanceHandler); //Before the engine's guard chain gets to swallow the fault
    SetUnhandledExceptionFilter(&CrashHandler);
    std::set_terminate(&TerminateHandler); //Per-thread in the UCRT; the engine and the main loop both run on this one
    _set_invalid_parameter_handler(&InvalidParameterHandler);
}
