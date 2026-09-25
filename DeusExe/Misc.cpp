#include "stdafx.h"
#include "Misc.h"

namespace
{
    bool g_bVerboseLogging = false;

    //Both -switch and /switch are accepted, matching how the engine parses its own options
    bool IsSwitch(const wchar_t* const pszArg, const wchar_t* const pszSwitch)
    {
        return (pszArg[0] == L'-' || pszArg[0] == L'/') && _wcsicmp(pszArg + 1, pszSwitch) == 0;
    }

    //Windows resolves reserved DOS device names even when they carry an extension, so they can't name a file or directory
    bool IsReservedDeviceName(const wchar_t* const pszName)
    {
        static const wchar_t* const pszReserved[] =
        {
            L"CON", L"PRN", L"AUX", L"NUL",
            L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
            L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9",
        };

        const wchar_t* const pszDot = wcschr(pszName, L'.');
        const size_t iStemLen = pszDot ? static_cast<size_t>(pszDot - pszName) : wcslen(pszName);

        wchar_t szStem[8]; //Longest reserved name is four characters
        if(iStemLen == 0 || iStemLen >= _countof(szStem))
        {
            return false;
        }
        wcsncpy_s(szStem, pszName, iStemLen);

        for(const wchar_t* const pszDevice : pszReserved)
        {
            if(_wcsicmp(szStem, pszDevice) == 0)
            {
                return true;
            }
        }

        return false;
    }
}

bool Misc::SetDEP(const DWORD dwFlags)
{
    const HMODULE hMod = GetModuleHandleW(L"Kernel32.dll");
    if(!hMod)
    {
        return false;
    }

    const auto procSet = reinterpret_cast<BOOL(WINAPI * const)(DWORD)>(GetProcAddress(hMod, "SetProcessDEPPolicy"));
    if(!procSet)
    {
        return false;
    }

    return procSet(dwFlags)!=FALSE;
}

/**
Returns game directory in user documents directory
*/
bool Misc::GetUserDocsDir(wchar_t(&pszBuf)[MAX_PATH])
{
    if(FAILED(SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, NULL, pszBuf)))
    {
        pszBuf[0] = '\0'; //Leave a defined value behind for callers that don't check
        return false;
    }
    return PathAppend(pszBuf, FRIENDLYGAMENAME) != FALSE;
}

/**
Returns System directory in game directory
*/
bool Misc::GetGameSystemDir(wchar_t(&pszBuf)[MAX_PATH])
{
    const DWORD dwLen = GetModuleFileName(NULL, pszBuf, MAX_PATH);
    if(dwLen == 0 || dwLen >= MAX_PATH) //A path that doesn't fit comes back truncated, which would silently point somewhere else
    {
        pszBuf[0] = '\0';
        return false;
    }
    PathRemoveFileSpec(pszBuf);
    return true;
}

/**
Returns the name given with the -gamename command line option, or an empty string if there is none.
It names a subdirectory of the data directory as well as the config file, so anything that can't occur in a file name is stripped.
*/
const wchar_t* Misc::GetGameName()
{
    //Persisted in an environment variable so the name survives an engine relaunch, which rebuilds the command line and drops our options
    static const wchar_t* const pszGameNameEnvVar = L"DeusExeGameName";
    static wchar_t szGameName[64] = L"";
    static bool bDetermined = false;

    if(bDetermined)
    {
        return szGameName;
    }
    bDetermined = true;

    //Can't use appCmdLine()/Parse(); the core isn't up yet and the name is a separate, possibly quoted, argument
    int iNumArgs = 0;
    wchar_t** const ppszArgs = CommandLineToArgvW(GetCommandLine(), &iNumArgs);
    if(ppszArgs)
    {
        for(int i = 1; i < iNumArgs - 1; i++)
        {
            if(IsSwitch(ppszArgs[i], L"gamename") && ppszArgs[i + 1][0] != '-')
            {
                wcsncpy_s(szGameName, ppszArgs[i + 1], _TRUNCATE);
                break;
            }
        }
        LocalFree(ppszArgs);
    }

    if(szGameName[0] == '\0')
    {
        //A value that doesn't fit leaves the buffer's contents undefined, so it can't be used at all
        const DWORD dwLen = GetEnvironmentVariable(pszGameNameEnvVar, szGameName, _countof(szGameName));
        if(dwLen == 0 || dwLen >= _countof(szGameName))
        {
            szGameName[0] = '\0';
        }
    }

    for(wchar_t* pszChar = szGameName; *pszChar != '\0'; pszChar++)
    {
        if(*pszChar < L' ' || wcschr(L"\\/:*?\"<>|", *pszChar) != nullptr)
        {
            *pszChar = L'_';
        }
    }

    //Reduce '.' and '..' to nothing, so they can't escape the data directory
    for(size_t i = wcslen(szGameName); i > 0 && (szGameName[i - 1] == L'.' || szGameName[i - 1] == L' '); i--)
    {
        szGameName[i - 1] = '\0';
    }

    if(IsReservedDeviceName(szGameName))
    {
        szGameName[0] = '\0';
    }

    SetEnvironmentVariable(pszGameNameEnvVar, szGameName[0] != '\0' ? szGameName : nullptr);

    return szGameName;
}

/**
True if the given switch (given without its leading '-') is present as an argument of its own.
Can't use appCmdLine()/ParseParam(): this runs before the core is up.
*/
bool Misc::HasCommandLineSwitch(const wchar_t* const pszSwitch)
{
    assert(pszSwitch);

    int iNumArgs = 0;
    wchar_t** const ppszArgs = CommandLineToArgvW(GetCommandLine(), &iNumArgs);
    if(!ppszArgs)
    {
        return false;
    }

    bool bFound = false;
    for(int i = 1; i < iNumArgs && !bFound; i++)
    {
        bFound = IsSwitch(ppszArgs[i], pszSwitch);
    }
    LocalFree(ppszArgs);

    return bFound;
}

/**
True if -localdata was given, so configuration data, save games etc. stay with the install instead of in 'Documents'.
*/
bool Misc::IsLocalData()
{
    //Persisted in an environment variable so the mode survives an engine relaunch, which rebuilds the command line and drops our options
    static const wchar_t* const pszLocalDataEnvVar = L"DeusExeLocalData";
    static bool bLocalData = false;
    static bool bDetermined = false;

    if(bDetermined)
    {
        return bLocalData;
    }
    bDetermined = true;

    bLocalData = HasCommandLineSwitch(L"localdata") || GetEnvironmentVariable(pszLocalDataEnvVar, nullptr, 0) != 0;
    if(bLocalData)
    {
        SetEnvironmentVariable(pszLocalDataEnvVar, L"1"); //Inherited by the engine's self-relaunches
    }

    return bLocalData;
}

/**
Returns the directory holding configuration data, save games etc.
False if that's the game's own directory, in which case no redirection is needed at all.
*/
bool Misc::GetDataDir(wchar_t(&pszBuf)[MAX_PATH])
{
    const wchar_t* const pszGameName = GetGameName();

    if(IsLocalData())
    {
        if(*pszGameName == '\0')
        {
            return false; //Plain -localdata: everything stays where the game is installed
        }

        wchar_t szSystemDir[MAX_PATH];
        if(!GetGameSystemDir(szSystemDir))
        {
            return false;
        }
        if(!PathCombine(pszBuf, szSystemDir, L"..")) //Move up from system directory; on failure it empties the buffer
        {
            return false;
        }
    }
    else if(!GetUserDocsDir(pszBuf))
    {
        return false; //No usable Documents folder; fall back to the game's own directory
    }

    if(*pszGameName != '\0' && !PathAppend(pszBuf, pszGameName))
    {
        return false;
    }

    return true;
}

const wchar_t* Misc::GetVersion()
{
    static std::wstring Version; //Empty when there's no usable version resource; reported as-is rather than crashing
    static bool bDetermined = false;

    if(!bDetermined)
    {
        bDetermined = true;

        wchar_t szFileName[MAX_PATH];
        const DWORD dwNameLen = GetModuleFileName(0, szFileName, _countof(szFileName));
        DWORD dwHandle = 0; //Doesn't do anything but still needed
        const DWORD dwSize = (dwNameLen > 0 && dwNameLen < _countof(szFileName)) ? GetFileVersionInfoSize(szFileName, &dwHandle) : 0;

        void* pVersion = nullptr;
        UINT iLen = 0;
        std::unique_ptr<char[]> DataPtr;
        if(dwSize > 0)
        {
            DataPtr.reset(new char[dwSize]);
            if(GetFileVersionInfo(szFileName, 0, dwSize, DataPtr.get()))
            {
                VerQueryValue(DataPtr.get(), L"\\StringFileInfo\\040904b0\\ProductVersion", &pVersion, &iLen);
            }
        }

        if(pVersion != nullptr && iLen > 0)
        {
            //Bounded by the length VerQueryValue reported: a resource whose string isn't terminated would otherwise be read past its end
            const wchar_t* const pszVersion = static_cast<const wchar_t*>(pVersion);
            Version.assign(pszVersion, wcsnlen(pszVersion, iLen));
        }
    }

    return Version.c_str();
}

float Misc::GetDefaultFOV()
{
    float fFOV = 75.0f;
    GConfig->GetFloat(L"Engine.PlayerPawn", L"DesiredFOV", fFOV, L"DefUser.ini");
    return fFOV;
}

float Misc::CalcFOV(const size_t iResX, const size_t iResY)
{
    constexpr float fDeg2Rad = static_cast<float>(M_PI) / 180.0f;
    constexpr float fDefaultAspect = 4.0f / 3.0f;

    if(iResX == 0 || iResY == 0) //An empty viewport, which a mode change can briefly report, has no aspect ratio
    {
        return GetDefaultFOV();
    }

    const float fAspect = static_cast<float>(iResX) / iResY;

    const float fFov = atanf(tanf(0.5f*GetDefaultFOV()*fDeg2Rad)*(fAspect / fDefaultAspect)) / fDeg2Rad*2.0f;
    return fFov;
}

/**
True when the process runs under WINE/Proton rather than on Windows itself.
Probes ntdll for wine_get_version, the export WINE documents for exactly this purpose.
*/
bool Misc::IsRunningUnderWine()
{
    const HMODULE hNtdll = GetModuleHandle(L"ntdll.dll"); //Always loaded, and GetModuleHandle takes no reference that would have to be released
    return hNtdll != NULL && GetProcAddress(hNtdll, "wine_get_version") != nullptr;
}

/**
Opens a directory in the file manager.

Handing the path to the shell under WINE would open WINE's own file manager, which is of little use for getting at the
files from the desktop. winebrowser.exe is WINE's bridge to the native handlers: it converts the path to a Unix one and
runs xdg-open (or whatever HKCU\Software\Wine\WineBrowser lists), so the desktop's own file manager opens instead.
*/
bool Misc::OpenFolder(const HWND hWndParent, const wchar_t* const pszPath)
{
    assert(pszPath);

    if(IsRunningUnderWine())
    {
        //Quoted: winebrowser tokenizes its command line like any other program, so a path with spaces would arrive split up
        wchar_t szArgs[MAX_PATH + 3];
        if(_snwprintf_s(szArgs, _TRUNCATE, L"\"%s\"", pszPath) < 0)
        {
            return false;
        }
        return reinterpret_cast<INT_PTR>(ShellExecute(hWndParent, L"open", L"winebrowser.exe", szArgs, nullptr, SW_SHOWNORMAL)) > 32;
    }

    return reinterpret_cast<INT_PTR>(ShellExecute(hWndParent, L"open", pszPath, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

void Misc::CenterWindowOnMonitor(const HWND hWnd, const HMONITOR hMonitor)
{
    assert(hWnd);
    assert(hMonitor);

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    RECT r;
    if(!GetMonitorInfo(hMonitor, &mi) || !GetWindowRect(hWnd, &r))
    {
        return;
    }

    const int iW = r.right - r.left;
    const int iH = r.bottom - r.top;

    const int iX = (mi.rcMonitor.left + mi.rcMonitor.right - iW) / 2;
    const int iY = (mi.rcMonitor.top + mi.rcMonitor.bottom - iH) / 2;
    MoveWindow(hWnd, iX, iY, iW, iH, FALSE);
#ifdef _DEBUG
    RECT r2;
    GetWindowRect(hWnd, &r2);
    assert(r.right - r.left == r2.right - r2.left);
    assert(r.bottom - r.top == r2.bottom - r2.top);
#endif
}

void Misc::SetBorderlessFullscreen(const HWND hWnd, const BorderlessFullscreenMode Mode)
{
    assert(hWnd);

    LONG_PTR Style = GetWindowLongPtr(hWnd, GWL_STYLE);

    if (Mode != BorderlessFullscreenMode::NONE)
    {
        Style &= ~(WS_CAPTION | WS_THICKFRAME);
        SetWindowLongPtr(hWnd, GWL_STYLE, Style);

        int iX;
        int iY;
        int iW;
        int iH;

        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (Mode == BorderlessFullscreenMode::CURRENT_MONITOR && GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi))
        {
            iX = mi.rcMonitor.left;
            iY = mi.rcMonitor.top;
            iW = mi.rcMonitor.right - mi.rcMonitor.left;
            iH = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }
        else //Span everything, which is also the best guess when there's no usable monitor info
        {
            iX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            iY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            iW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            iH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        }

        SetWindowPos(hWnd, NULL, iX, iY, iW, iH, SWP_FRAMECHANGED);
    }
    else
    {
        Style |= (WS_CAPTION | WS_THICKFRAME);
        SetWindowLongPtr(hWnd, GWL_STYLE, Style);
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_FRAMECHANGED);
    }
}

void Misc::SetVerboseLogging(const bool bEnabled)
{
    g_bVerboseLogging = bEnabled;
}

bool Misc::IsVerboseLogging()
{
    return g_bVerboseLogging;
}
