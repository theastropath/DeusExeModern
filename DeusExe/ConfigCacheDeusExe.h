#pragma once

/**
Config cache that, if a game name is set with -gamename, uses '<game name>.ini' instead of the game's regular
system ini (DeusEx.ini), so every game name keeps its own settings.
*/
class FConfigCacheDeusExe : public FConfigCacheIni
{
public:
    static FConfigCache* Factory();

    //From FConfigCacheIni
    virtual void Init(const wchar_t* InSystem, const wchar_t* InUser, UBOOL RequireConfig) override;
};
