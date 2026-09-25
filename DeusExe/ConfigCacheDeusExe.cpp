#include "stdafx.h"
#include "ConfigCacheDeusExe.h"
#include "Misc.h"

FConfigCache* FConfigCacheDeusExe::Factory()
{
    return new FConfigCacheDeusExe();
}

void FConfigCacheDeusExe::Init(const wchar_t* InSystem, const wchar_t* InUser, UBOOL RequireConfig)
{
    const wchar_t* const pszGameName = Misc::GetGameName();
    if(*pszGameName == '\0')
    {
        FConfigCacheIni::Init(InSystem, InUser, RequireConfig);
        return;
    }

    wchar_t szSystemIni[MAX_PATH];
    swprintf_s(szSystemIni, L"%s.ini", pszGameName);

    //Seed a game name's first run from the game's own ini so it starts out with working settings. Source paths are
    //made absolute because the current directory isn't necessarily the game's System directory yet at this point.
    assert(GFileManager);
    if(GFileManager->FileSize(szSystemIni) < 0)
    {
        wchar_t szSystemDir[MAX_PATH];
        if(Misc::GetGameSystemDir(szSystemDir))
        {
            wchar_t szSourceIni[MAX_PATH];
            PathCombine(szSourceIni, szSystemDir, InSystem);
            if(!GFileManager->Copy(szSystemIni, szSourceIni))
            {
                PathCombine(szSourceIni, szSystemDir, L"Default.ini"); //The game's ini isn't there either; fall back to what it's generated from
                GFileManager->Copy(szSystemIni, szSourceIni);
            }
        }
    }

    FConfigCacheIni::Init(szSystemIni, InUser, RequireConfig);
}
