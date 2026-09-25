#include "stdafx.h"
#include "FileManagerDeusExe.h"
#include "Misc.h"
#include "CrashReport.h"

const wchar_t* const FFileManagerDeusExe::sm_pszIntPaths = L"IntPaths";

void FFileManagerDeusExe::OnGameStart()
{
    //Rebuild from the (possibly just edited in the data directories dialog) config
    BuildIntPaths();
    BuildConPaths();
}

void FFileManagerDeusExe::BuildIntPaths()
{
    assert(GConfig);
    //Create the container up front so a reentrant IntOverride() call can't try to build it again
    m_pIntPaths = std::make_unique<std::vector<std::wstring>>();
    TMultiMap<FString, FString>* const pSectionInt = GConfig->GetSectionPrivate(PROJECTNAME, FALSE, FALSE);
    if(pSectionInt)
    {
        TArray<FString> IntPaths;
        pSectionInt->MultiFind(sm_pszIntPaths, IntPaths); //Returns in reverse order
        for(int i = IntPaths.Num() - 1; i >= 0; i--)
        {
            //Convert format like "..\Shifter\*.int" to "..\Shifter\"
            wchar_t szBuf[MAX_PATH];
            wcsncpy_s(szBuf, *IntPaths(i), _TRUNCATE); //Truncate rather than abort on an over-long ini entry
            PathRemoveFileSpec(szBuf);
            PathAddBackslash(szBuf);
            m_pIntPaths->emplace_back(szBuf);
        }
    }
}

bool FFileManagerDeusExe::IntOverride(wchar_t(&szNewName)[MAX_PATH], const wchar_t* const pszOldName)
{
    wchar_t* pszExtension = PathFindExtension(pszOldName);
    assert(pszExtension);
    if(*pszExtension == '\0')
    {
        return false;
    }
    pszExtension++; //After period

    const bool bIntFile = _wcsicmp(pszExtension, L"int") == 0;
    const bool bLocalizedFile = !bIntFile && _wcsicmp(pszExtension, UObject::GetLanguage()) == 0;

    if(!bIntFile && !bLocalizedFile)
    {
        return false;
    }

    //Build the override paths on first use. The game reads some .int files (notably the DeusEx package's own
    //DeusEx.int) before OnGameStart() runs; as the config cache keeps whatever content is read first, without
    //this those early reads would miss the override and a mod's DeusEx.int would never be used.
    if(!m_pIntPaths)
    {
        if(!GConfig) //Core (and thus the config holding the override paths) isn't up yet
        {
            return false;
        }
        BuildIntPaths();
    }

    const auto IsExistingFile = [](const wchar_t* const pszPath)
    {
        const DWORD dwAttrib = GetFileAttributes(pszPath); //PathFileExists also returns true for directories
        return dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
    };

    for(const std::wstring& IntPath : *m_pIntPaths)
    {
        //PathCombine() doens't work with relative paths...
        wcsncpy_s(szNewName, IntPath.c_str(), _TRUNCATE);
        const size_t iPrefixLen = wcslen(szNewName); //Not IntPath's length: the copy above may have truncated
        wcsncpy_s(szNewName + iPrefixLen, _countof(szNewName) - iPrefixLen, pszOldName, _TRUNCATE);
        if(IsExistingFile(szNewName))
        {
            return true;
        }

        if(bLocalizedFile) //If localized, try again with .int
        {
            wchar_t* const pszNewExtension = PathFindExtension(szNewName);
            const size_t iExtCharsLeft = _countof(szNewName) - static_cast<size_t>(pszNewExtension - szNewName);
            if(iExtCharsLeft < _countof(L".int")) //The copy above may have truncated the extension away; wcscpy_s aborts the process rather than overrun
            {
                continue;
            }
            wcscpy_s(pszNewExtension, iExtCharsLeft, L".int");
            if(IsExistingFile(szNewName))
            {
                return true;
            }
        }
    }

    return false;
}

void FFileManagerDeusExe::BuildConPaths()
{
    //Deus Ex loads DeusExCon*.u conversation packages by bare name and never releases them, so the first copy read
    //stays resident for the session and the origin/System copy can win. Map each such package a data directory
    //provides to its highest-priority (first-listed) copy; ConOverride() then redirects every open of it there.
    m_pConPaths = std::make_unique<std::unordered_map<std::wstring, std::wstring>>();

    if(!GSys)
    {
        return;
    }

    wchar_t szSystemDir[MAX_PATH];
    if(!Misc::GetGameSystemDir(szSystemDir))
    {
        return;
    }

    std::vector<std::wstring> ScannedDirs; //A directory appears once per extension; only scan it once.

    const INT iPathCount = GSys->Paths.Num();
    for(INT i = 0; i < iPathCount; i++) //Top-to-bottom: the first (highest-priority) data directory copy wins
    {
        wchar_t szRelDir[MAX_PATH];
        wcsncpy_s(szRelDir, *GSys->Paths(i), _TRUNCATE);
        PathRemoveFileSpec(szRelDir);

        wchar_t szDir[MAX_PATH];
        if(!PathCombine(szDir, szSystemDir, szRelDir)) //Leaves the buffer empty, which would search the current directory instead
        {
            continue;
        }

        //A redirected data directory has its own copy of the folder, which outranks the install's for this same entry
        wchar_t szRedirectedDir[MAX_PATH];
        if(ToModernFileName(szRedirectedDir, szDir) && _wcsicmp(szRedirectedDir, szDir) != 0)
        {
            ScanConDir(szRedirectedDir, ScannedDirs);
        }

        //Skip the origin/System folder; data directories override it.
        if(_wcsicmp(szDir, szSystemDir) != 0)
        {
            ScanConDir(szDir, ScannedDirs);
        }
    }
}

void FFileManagerDeusExe::ScanConDir(const wchar_t* const pszDir, std::vector<std::wstring>& ScannedDirs)
{
    wchar_t szDirKey[MAX_PATH];
    wcsncpy_s(szDirKey, pszDir, _TRUNCATE);
    _wcslwr_s(szDirKey, _countof(szDirKey));
    if(std::find(ScannedDirs.cbegin(), ScannedDirs.cend(), szDirKey) != ScannedDirs.cend())
    {
        return;
    }
    ScannedDirs.emplace_back(szDirKey);

    wchar_t szSearchSpec[MAX_PATH];
    if(!PathCombine(szSearchSpec, pszDir, L"DeusExCon*.u"))
    {
        return;
    }

    WIN32_FIND_DATA FindData;
    const HANDLE hFind = FindFirstFile(szSearchSpec, &FindData);
    if(hFind == INVALID_HANDLE_VALUE)
    {
        return;
    }
    do
    {
        //FindFirstFile's "*.u" can also match names like "*.u3d" through 8.3 short names, so verify the extension.
        if((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || _wcsicmp(PathFindExtension(FindData.cFileName), L".u") != 0)
        {
            continue;
        }

        wchar_t szNameKey[MAX_PATH];
        wcsncpy_s(szNameKey, FindData.cFileName, _TRUNCATE);
        _wcslwr_s(szNameKey, _countof(szNameKey));

        //Keep the first (highest-priority) data directory copy of each package.
        if(m_pConPaths->find(szNameKey) == m_pConPaths->cend())
        {
            wchar_t szFullPath[MAX_PATH];
            if(PathCombine(szFullPath, pszDir, FindData.cFileName))
            {
                m_pConPaths->emplace(szNameKey, szFullPath);
            }
        }
    } while(FindNextFile(hFind, &FindData) != FALSE);
    FindClose(hFind);
}

bool FFileManagerDeusExe::ConOverride(wchar_t(&szNewName)[MAX_PATH], const wchar_t* const pszOldName)
{
    if(!m_pConPaths || m_pConPaths->empty())
    {
        return false;
    }

    //Only conversation packages (.u) are ever in the map; skip the lookup for anything else.
    const wchar_t* const pszFileName = PathFindFileName(pszOldName);
    if(_wcsicmp(PathFindExtension(pszFileName), L".u") != 0)
    {
        return false;
    }

    wchar_t szNameKey[MAX_PATH];
    wcsncpy_s(szNameKey, pszFileName, _TRUNCATE);
    _wcslwr_s(szNameKey, _countof(szNameKey));

    const auto it = m_pConPaths->find(szNameKey);
    if(it == m_pConPaths->cend())
    {
        return false;
    }

    wcsncpy_s(szNewName, it->second.c_str(), _TRUNCATE);
    return true;
}

FArchive* FFileManagerDeusExe::CreateFileReader(const wchar_t* Filename, DWORD Flags, FOutputDevice* Error)
{
    wchar_t szFilename[MAX_PATH];
    const wchar_t* const pszResolved = ConOverride(szFilename, Filename) || IntOverride(szFilename, Filename) ? szFilename : Filename;
    FArchive* const pReader = FFileManagerWindows::CreateFileReader(pszResolved, Flags, Error);
    if(pReader != nullptr) //Only successful opens: package resolution probes every search path and mostly misses
    {
        CrashReport::RecordFileOpen(pszResolved);
    }
    return pReader;
}

INT FFileManagerDeusExe::FileSize(const wchar_t* Filename)
{
    //Apply the same .int/.u overrides as CreateFileReader(), so the config cache's existence check
    //(FConfigCacheIni::Find uses FileSize()>=0) and the engine's package search resolve to the same file.
    wchar_t szFilename[MAX_PATH];
    return FFileManagerWindows::FileSize(ConOverride(szFilename, Filename) || IntOverride(szFilename, Filename) ? szFilename : Filename);
}

bool FFileManagerDeusExe::ToModernFileName(wchar_t(&szNewName)[MAX_PATH], const wchar_t* const pszOldName, const char /*op*/ /*= 'r'*/)
{
    wcsncpy_s(szNewName, pszOldName, _TRUNCATE);
    return true;
}

FFileManagerDeusExeDataDir::FFileManagerDeusExeDataDir(const wchar_t* const pszDataDir)
{
    wcsncpy_s(m_szDataDir, pszDataDir, _TRUNCATE);

    wcsncpy_s(m_szUserDataPath, m_szDataDir, _TRUNCATE);
    PathAppend(m_szUserDataPath, L"System"); //The games use paths relative to system, so we're doing that too.

    m_szSystemPath[0] = '\0';
    m_szGamePath[0] = '\0';
    m_bHaveGamePath = Misc::GetGameSystemDir(m_szSystemPath) && PathCombine(m_szGamePath, m_szSystemPath, L"..") != nullptr; //Move up from system directory
}

void FFileManagerDeusExeDataDir::AfterCoreInit()
{
    assert(GLog);
    GLog->Logf(L"Deus Exe: Using data directory '%s'.", m_szDataDir);
    if(!m_bHaveGamePath)
    {
        GLog->Log(L"Deus Exe: No game directory; absolute paths are not redirected.");
    }
    FFileManagerDeusExe::AfterCoreInit();
}

bool FFileManagerDeusExeDataDir::ToModernFileName(wchar_t(&szNewName)[MAX_PATH], const wchar_t* const pszOldName, const char op)
{
    assert(pszOldName);
    assert(szNewName != pszOldName);

    //Make all paths relative to game directory
    if(PathIsRelative(pszOldName))
    {
        wcsncpy_s(szNewName, pszOldName, _TRUNCATE);
    }
    else if(!m_bHaveGamePath) //Without the install's location there's no way to tell which absolute paths belong to it
    {
        return false;
    }
    else
    {
        wchar_t szCommonPrefix[MAX_PATH];
        if(!PathCanonicalize(szNewName, pszOldName)) //Fails on an over-long path, leaving the buffer with no usable content
        {
            return false;
        }

        //Already converted. Needed because with a game name the data directory lies inside the game directory, so the check below wouldn't catch it
        PathCommonPrefix(szNewName,m_szDataDir,szCommonPrefix);
        if(_wcsicmp(szCommonPrefix,m_szDataDir)==0)
        {
            return false;
        }

        //If not in game directory, abort. This facilitates how MakeDirectory() recursively creates directories
        PathCommonPrefix(szNewName,m_szGamePath,szCommonPrefix);
        if(_wcsicmp(szCommonPrefix,m_szGamePath)!=0)
        {
            return false;
        }

        //Make path relative to System, like the games' own paths
        if(!PathRelativePathTo(szNewName,m_szSystemPath,FILE_ATTRIBUTE_DIRECTORY,pszOldName,0))
        {
            return false; //Nothing usable to rebase; leave the caller with the original path
        }
    }

    if(!PathCombine(szNewName,m_szUserDataPath,szNewName)) //On failure it empties the buffer, which would then be used as the file name
    {
        return false;
    }
    
    if((op == 'r' || op == 'd') && !PathFileExists(szNewName) && PathFileExists(pszOldName)) //If opening a file that already exists, return original.
    {
        return false;
    }

    if(op=='w' && !PathIsDirectory(pszOldName)) //PathIsDirectory needed as PathRemoveFileSpec would strip stuff like 'Save040' to just 'Save'
    {
        wchar_t* pszFileSpec = PathFindFileName(szNewName);
        PathRemoveFileSpec(szNewName);
        if(!PathFileExists(szNewName))
        {
            MakeDirectory(szNewName,1);
        }
        PathAppend(szNewName,pszFileSpec);
    }
    return true;
}

FArchive* FFileManagerDeusExeDataDir::CreateFileReader(const wchar_t* Filename, DWORD Flags, FOutputDevice* Error)
{
    assert(Filename);
    wchar_t szNewFilename[MAX_PATH];
    const wchar_t* const pszResolved = ConOverride(szNewFilename, Filename) || IntOverride(szNewFilename, Filename) ? szNewFilename : ToModernFileName(szNewFilename, Filename) ? szNewFilename : Filename;
    FArchive* const pReader = FFileManagerWindows::CreateFileReader(pszResolved, Flags, Error);
    if(pReader != nullptr)
    {
        CrashReport::RecordFileOpen(pszResolved);
    }
    return pReader;
}

FArchive* FFileManagerDeusExeDataDir::CreateFileWriter(const wchar_t* Filename, DWORD Flags, FOutputDevice* Error)
{
    assert(Filename);
    wchar_t szNewFilename[MAX_PATH];
    return FFileManagerWindows::CreateFileWriter(ToModernFileName(szNewFilename, Filename, 'w') ? szNewFilename : Filename, Flags, Error);
}

INT FFileManagerDeusExeDataDir::FileSize(const wchar_t* Filename)
{
    assert(Filename);
    wchar_t szNewFilename[MAX_PATH];
    return FFileManagerWindows::FileSize(ConOverride(szNewFilename, Filename) || IntOverride(szNewFilename, Filename) ? szNewFilename : ToModernFileName(szNewFilename, Filename) ? szNewFilename : Filename);
}

UBOOL FFileManagerDeusExeDataDir::Copy(const wchar_t* DestFile, const wchar_t* SrcFile, UBOOL ReplaceExisting, UBOOL EvenIfReadOnly, UBOOL Attributes, void(*Progress)(FLOAT Fraction))
{
    assert(DestFile);
    assert(SrcFile);
    wchar_t szNewDestFile[MAX_PATH];
    wchar_t szNewSrcFile[MAX_PATH];
    return FFileManagerWindows::Copy(ToModernFileName(szNewDestFile, DestFile, 'w') ? szNewDestFile : DestFile, ToModernFileName(szNewSrcFile, SrcFile)  ? szNewSrcFile : SrcFile, ReplaceExisting, EvenIfReadOnly, Attributes, Progress);
}

UBOOL FFileManagerDeusExeDataDir::Delete(const wchar_t* Filename, UBOOL RequireExists, UBOOL EvenReadOnly)
{
    assert(Filename);
    wchar_t szNewFilename[MAX_PATH];
    return FFileManagerWindows::Delete(ToModernFileName(szNewFilename, Filename, 'd') ? szNewFilename : Filename, RequireExists, EvenReadOnly);
}

UBOOL FFileManagerDeusExeDataDir::Move(const wchar_t* Dest, const wchar_t* Src, UBOOL Replace, UBOOL EvenIfReadOnly, UBOOL Attributes)
{
    assert(Dest);
    assert(Src);
    wchar_t szNewDest[MAX_PATH];
    wchar_t szNewSrc[MAX_PATH];
    return FFileManagerWindows::Move(ToModernFileName(szNewDest, Dest, 'w') ? szNewDest : Dest, ToModernFileName(szNewSrc, Src, 'd') ? szNewSrc : Src, Replace, EvenIfReadOnly, Attributes);
}
    
UBOOL FFileManagerDeusExeDataDir::MakeDirectory(const wchar_t* Path, UBOOL Tree)
{
    assert(Path);
    wchar_t szNewPath[MAX_PATH];
    return FFileManagerWindows::MakeDirectory(ToModernFileName(szNewPath, Path, 'w')  ? szNewPath : Path, Tree);
}

UBOOL FFileManagerDeusExeDataDir::DeleteDirectory(const wchar_t* Path, UBOOL RequireExists, UBOOL Tree)
{
    assert(Path);
    wchar_t szNewPath[MAX_PATH];
    return FFileManagerWindows::DeleteDirectory(ToModernFileName(szNewPath, Path, 'd') ? szNewPath : Path, RequireExists, Tree);
}

TArray<FString> FFileManagerDeusExeDataDir::FindFiles(const wchar_t* Filename, UBOOL Files, UBOOL Directories)
{
    assert(Filename);
    
    //Look for old style filenames
    auto Result = FFileManagerWindows::FindFiles(Filename,Files,Directories);

    wchar_t szConvertedFilename[MAX_PATH];
    if(!ToModernFileName(szConvertedFilename, Filename)) //Already started out with a new style file
    {
        return Result;
    }

    //Look for new style filenames
    WIN32_FIND_DATAW Data;
    const HANDLE hHandle = FindFirstFileW(szConvertedFilename, &Data);
    if(hHandle != INVALID_HANDLE_VALUE)
    {
        do
        {
            if
                (appStricmp(Data.cFileName, L".")
                && appStricmp(Data.cFileName, L"..")
                && ((Data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ? Directories : Files)
                && Result.FindItemIndex(Data.cFileName) == INDEX_NONE) //Don't list things twice if the game won't be able to tell them apart (i.e. a directory like 'Save001' with no further path will result in the new version being listed twice)
            {
                #pragma warning(push)
                #pragma warning(disable:4291) //no matching operator delete found; memory will not be freed if initialization throws an exception
                new(Result)FString(Data.cFileName);
                #pragma warning(pop)
            }
        } while(FindNextFileW(hHandle, &Data));

        FindClose(hHandle);
    }

    return Result;
}
