#include "stdafx.h"
#include "FreeSpaceFix.h"
#include "FileManagerDeusExe.h"
#include "Misc.h"
#include "CrashReport.h"

CFreeSpaceFix::CFreeSpaceFix()
:CFixBaseT(L"Free save space fix")
{

}

void CFreeSpaceFix::ReplacementFunc(UObject& UObjectThis, CFreeSpaceFix& /*FixObjectThis*/, FFrame& Stack, RESULT_DECL)
{
    P_FINISH;

    const wchar_t* const pszSave = L"..\\Save";
    wchar_t szSaveDirNew[MAX_PATH];
    const wchar_t* const pszSaveDir = static_cast<FFileManagerDeusExe*>(GFileManager)->ToModernFileName(szSaveDirNew, pszSave) ? szSaveDirNew : pszSave;

    ULARGE_INTEGER BytesAvailable = {};

    if(!GetDiskFreeSpaceEx(pszSaveDir, &BytesAvailable, nullptr, nullptr)) //The save directory doesn't have to exist yet
    {
        wchar_t szContext[1024];
        GLog->Logf(L"FreeSpaceFix: GetDiskFreeSpaceEx failed for '%s' (error %u); reporting 0 free for %s.",
            szSaveDirNew, GetLastError(),
            CrashReport::FormatScriptContext(szContext, _countof(szContext), &UObjectThis, Stack.Node));
        GetDiskFreeSpaceEx(nullptr, &BytesAvailable, nullptr, nullptr); //Fall back to the current directory's volume
    }

    const int iResult = static_cast<int>(std::min<ULONGLONG>(BytesAvailable.QuadPart / 1024, std::numeric_limits<int>::max()));
    *static_cast<int*>(Result) = iResult;
}
