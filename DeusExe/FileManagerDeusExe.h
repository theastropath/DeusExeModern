#pragma once

//Pretty much default file manager, but with .int file overrides
class FFileManagerDeusExe : public FFileManagerWindows
{
public:
    static const wchar_t* const sm_pszIntPaths;

    FFileManagerDeusExe() = default;
    virtual ~FFileManagerDeusExe() = default;

    virtual void AfterCoreInit() {}
    void OnGameStart();

    //Used by startup dialog
    virtual bool ToModernFileName(wchar_t(&szNewName)[MAX_PATH], const wchar_t* const pszOldName, const char op = 'r');

    //We new() this class before the Unreal core is started so we can use derived classes. Unreal's global operator new
    //requires the core to be running, hence our own.
    void* operator new(const size_t s)
    {
        return malloc(s);
    }

    void operator delete(void* const p)
    {
        free(p);
    }

protected:
    /**
    For some reason, the game (also tested Unreal 1) crashes when a *.int is added as an override path; so we use our own mechanism.
    
    Returns false of the original file isn't a (localized) .int file, or if no suitable replacement was found.
    */  
    bool IntOverride(wchar_t(&szNewName)[MAX_PATH], const wchar_t* const pszOldName);

    /**
    Redirects opens of DeusExCon*.u conversation packages to the copy in the highest-priority data directory.
    Deus Ex loads these by bare name and never releases them, so the origin/System copy can otherwise win.
    Returns false if the file isn't a conversation package that a data directory provides.
    */
    bool ConOverride(wchar_t(&szNewName)[MAX_PATH], const wchar_t* const pszOldName);

private:
    void BuildIntPaths(); //(Re)reads the .int override folders from the config's IntPaths entries

    std::unique_ptr<std::vector<std::wstring>> m_pIntPaths; //Pointer because we can't allocate on startup

    void BuildConPaths(); //(Re)builds the DeusExCon*.u -> data directory copy redirect map from the search paths
    void ScanConDir(const wchar_t* const pszDir, std::vector<std::wstring>& ScannedDirs); //!< Adds a directory's conversation packages to the map, unless it was scanned already

    std::unique_ptr<std::unordered_map<std::wstring, std::wstring>> m_pConPaths; //Lower-cased package file name -> winning data directory copy

//From FFileManagerWindows
public:
    virtual FArchive* CreateFileReader(const wchar_t* Filename, DWORD Flags, FOutputDevice* Error) override;
    virtual INT FileSize(const wchar_t* Filename) override;
};

//File manager that keeps configuration data, save games etc. in a separate data directory
class FFileManagerDeusExeDataDir: public FFileManagerDeusExe
{
public:
    explicit FFileManagerDeusExeDataDir(const wchar_t* const pszDataDir);
private:
    wchar_t m_szDataDir[MAX_PATH];
    wchar_t m_szUserDataPath[MAX_PATH];
    wchar_t m_szSystemPath[MAX_PATH];
    wchar_t m_szGamePath[MAX_PATH];
    bool m_bHaveGamePath = false; //!< False if the install's own location couldn't be determined, so absolute paths can't be classified

    //From FFileManagerDeusExe
public:
    virtual void AfterCoreInit() override;

    /**
    Converts relative paths to modern (i.e. Documents-based) filename.
    */
    virtual bool ToModernFileName(wchar_t(&szNewName)[MAX_PATH], const wchar_t* const pszOldName, const char op = 'r') override;

    //From FFileManagerWindows
public:
    virtual FArchive* CreateFileReader(const wchar_t* Filename, DWORD Flags, FOutputDevice* Error) override;
    virtual FArchive* CreateFileWriter(const wchar_t* Filename, DWORD Flags, FOutputDevice* Error) override;
    virtual INT FileSize(const wchar_t* Filename) override;
    virtual UBOOL Copy(const wchar_t* DestFile, const wchar_t* SrcFile, UBOOL ReplaceExisting, UBOOL EvenIfReadOnly, UBOOL Attributes, void(*Progress)(FLOAT Fraction)) override;
    virtual UBOOL Delete(const wchar_t* Filename, UBOOL RequireExists = 0, UBOOL EvenReadOnly = 0) override;
    virtual UBOOL Move(const wchar_t* Dest, const wchar_t* Src, UBOOL Replace = 1, UBOOL EvenIfReadOnly = 0, UBOOL Attributes = 0) override;
    virtual UBOOL MakeDirectory(const wchar_t* Path, UBOOL Tree = 0) override;
    virtual UBOOL DeleteDirectory(const wchar_t* Path, UBOOL RequireExists = 0, UBOOL Tree = 0) override;
    virtual TArray<FString> FindFiles(const wchar_t* Filename, UBOOL Files, UBOOL Directories) override;
};