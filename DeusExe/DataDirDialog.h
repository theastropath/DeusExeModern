#pragma once

#include <unordered_set>
#include "FancyTreeView.h"


class CDataDirDialog
{
public:
    bool Show(const HWND hWndParent);

private:
    struct SDirIdentity
    {
        DWORD dwVolumeSerialNumber;
        DWORD dwFileIndexHigh;
        DWORD dwFileIndexLow;

        bool operator==(const SDirIdentity& Other) const
        {
            return dwVolumeSerialNumber == Other.dwVolumeSerialNumber && dwFileIndexHigh == Other.dwFileIndexHigh && dwFileIndexLow == Other.dwFileIndexLow;
        }
    };

    static bool GetDirIdentity(const wchar_t* const pszDir, SDirIdentity& Identity); //!< Identifies the directory a path ends up at, following any symlink or junction

    void ProcessDirFiles(const wchar_t* const pszDir, const wchar_t* const pszRelDir);
    void SearchDirs(const wchar_t* const pszTargetDir, const wchar_t* const pszRootDir, const size_t iDepth = 0);

    static constexpr size_t sm_iMaxSearchDepth = 64; //Long pathname would exhaust the stack

    /**
    Finds the items from the default.ini file
    */
    void FindDefaultItems();

    void AddItemsFromConfig();
    void PopulateList();
    void PopulateConfig() const;

    /*
     * Strips a path into its component parts and adds those to the ini file.
     * We do this instead of tracking the components while iterating the directories, so existing items can be added even if they aren't on disk.
    */
    HTREEITEM AddItemToList(const wchar_t* const pszPath);

    static INT_PTR CALLBACK DataDirDialogProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam);

    static const wchar_t* const sm_pszPaths;

    HWND m_hWnd = NULL;
    CFancyTreeView m_TreeView;

    std::vector<SDirIdentity> m_AncestorDirs; //!< The directories the search is currently inside, so a link leading back into one can be spotted

    // Top-level dirs (Shifter, HDTP) are added to the list first, their contents afterwards in on-disk order,
    // so the order of sub-items doesn't shift depending on when they were added.
    bool m_bAddTopLevelDirsOnly = true;

    class CCaseInsensitiveHash
    {
    public:
        size_t operator()(const std::wstring& str) const
        {
            return appStrihash(str.c_str());
        }
    };

    class CCaseInsensitiveEquals
    {
    public:
        bool operator()(const std::wstring& str1, const std::wstring& str2) const
        {
            return _wcsicmp(str1.c_str(), str2.c_str()) == 0;
        }
    };
    template<class T> using CaseInsensitiveMap = std::unordered_set<T, CCaseInsensitiveHash, CCaseInsensitiveEquals>;


    CaseInsensitiveMap<std::wstring> m_DefaultDataDirs; //!< Data dirs the game has standard, don't allow the user to change these

    static const std::array<const wchar_t*, 7> sm_SupportedExtensions; // Extensions that make sense to show
};