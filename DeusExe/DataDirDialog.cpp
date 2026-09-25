#include "stdafx.h"
#include "DataDirDialog.h"
#include "Misc.h"
#include "FileManagerDeusExe.h"
#include "resource.h"

const wchar_t* const CDataDirDialog::sm_pszPaths = L"Paths";

decltype(CDataDirDialog::sm_SupportedExtensions) CDataDirDialog::sm_SupportedExtensions =
{
    L"*.int", L"*.u", L"*.utx", L"*.umx", L"*.dx", L"*.unr", L"*.uax",
};

bool CDataDirDialog::Show(const HWND hWndParent)
{
    return DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_DATADIRS), hWndParent, DataDirDialogProc, reinterpret_cast<LPARAM>(this)) == 1;
}

void CDataDirDialog::ProcessDirFiles(const wchar_t* const pszDir, const wchar_t* const pszRelDir)
{
    assert(pszDir);
    assert(pszRelDir);

    wchar_t szRelativePath[MAX_PATH];
    wcsncpy_s(szRelativePath, pszRelDir, _TRUNCATE);
    wchar_t* const pszRelFileName = szRelativePath + wcslen(szRelativePath);
    const size_t iCharsLeft = _countof(szRelativePath) - (pszRelFileName - szRelativePath);

    //For each extension, check if a file exists
    for(const wchar_t* const pszExt : sm_SupportedExtensions)
    {
        wchar_t szFileSpec[MAX_PATH];
        if(!PathCombine(szFileSpec, pszDir, pszExt)) //Leaves the buffer empty, which would search the current directory instead
        {
            continue;
        }
        WIN32_FIND_DATA FindData;
        const HANDLE hDir = FindFirstFile(szFileSpec, &FindData);
        if(hDir != INVALID_HANDLE_VALUE)
        {
            do
            {
                if(!(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    wcsncpy_s(pszRelFileName, iCharsLeft, pszExt, _TRUNCATE);
                    AddItemToList(szRelativePath);
                    break;
                }

            } while(FindNextFile(hDir, &FindData) != FALSE);
            FindClose(hDir);
        }
    }
}

bool CDataDirDialog::GetDirIdentity(const wchar_t* const pszDir, SDirIdentity& Identity)
{
    assert(pszDir);

    //FILE_FLAG_BACKUP_SEMANTICS is needed to open a directory. Reparse points are followed, so a symlink or junction identifies its target.
    const HANDLE hDir = CreateFile(pszDir, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if(hDir == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    BY_HANDLE_FILE_INFORMATION FileInfo;
    const bool bSucceeded = GetFileInformationByHandle(hDir, &FileInfo) != FALSE;
    CloseHandle(hDir);

    if(bSucceeded)
    {
        Identity = { FileInfo.dwVolumeSerialNumber, FileInfo.nFileIndexHigh, FileInfo.nFileIndexLow };
    }
    return bSucceeded;
}

void CDataDirDialog::SearchDirs(const wchar_t* const pszTargetDir, const wchar_t* const pszRootDir, const size_t iDepth)
{
    assert(pszTargetDir);
    assert(pszRootDir);

    if(iDepth >= sm_iMaxSearchDepth)
    {
        return;
    }

    wchar_t szTargetPath[MAX_PATH];
    if(!PathCombine(szTargetPath, pszTargetDir, L"*."))
    {
        return;
    }

    //Symlinks and junctions are followed, so stop when one leads back into a directory we're already inside: that would recurse forever.
    //Filesystems that don't report a usable identity aren't tracked; the depth limit still bounds the search.
    SDirIdentity Identity;
    const bool bTrackIdentity = GetDirIdentity(pszTargetDir, Identity);
    if(bTrackIdentity)
    {
        if(std::find(m_AncestorDirs.cbegin(), m_AncestorDirs.cend(), Identity) != m_AncestorDirs.cend())
        {
            return;
        }
        m_AncestorDirs.push_back(Identity);
    }

    WIN32_FIND_DATA FindData;
    const HANDLE hDir = FindFirstFile(szTargetPath, &FindData);

    if(hDir != INVALID_HANDLE_VALUE)
    {
        do
        {
            //Symlinked and junctioned directories are included: users link data directories in from elsewhere. Loops are handled by the identity check above.
            if((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(FindData.cFileName, L"..") != 0 && wcscmp(FindData.cFileName, L".") != 0)
            {
                wchar_t szChildPath[MAX_PATH];
                if(!PathCombine(szChildPath, pszTargetDir, FindData.cFileName))
                {
                    continue;
                }

                wchar_t szRelativePath[MAX_PATH];
                if(!PathRelativePathTo(szRelativePath, pszRootDir, FILE_ATTRIBUTE_DIRECTORY, szChildPath, FILE_ATTRIBUTE_DIRECTORY)) //Leaves the buffer undefined on failure
                {
                    continue;
                }

                //Extra check for "." as we're relative to System, and DefaultDataDirs is not
                if(wcscmp(szRelativePath, L".") == 0) 
                {
                    continue;
                }
                //Standard directories aren't the user's to change, so don't process them any further
                if(m_DefaultDataDirs.find(szRelativePath) != m_DefaultDataDirs.cend())
                {
                    continue;
                }

                SearchDirs(szChildPath, pszRootDir, iDepth + 1);

                PathAddBackslash(szRelativePath);
                ProcessDirFiles(szChildPath, szRelativePath);
                
            }
        } while(FindNextFile(hDir, &FindData) != FALSE);

        FindClose(hDir);
    }

    if(bTrackIdentity)
    {
        m_AncestorDirs.pop_back();
    }
}

void CDataDirDialog::FindDefaultItems()
{
    TMultiMap<FString, FString>* const pSection = GConfig->GetSectionPrivate(L"Core.System", FALSE, TRUE, L"default.ini");
    if(pSection) //Absent in an install without a default.ini
    {
        TArray<FString> Defaults;
        pSection->MultiFind(sm_pszPaths, Defaults);
        for(int i = 0; i < Defaults.Num(); i++)
        {
            //Convert format like "..\Music\*.umx" to "..\Music"
            wchar_t szBuf[MAX_PATH];
            wcsncpy_s(szBuf, *Defaults(i), _TRUNCATE); //Truncate rather than abort on an over-long ini entry
            PathRemoveFileSpec(szBuf);

            m_DefaultDataDirs.insert(szBuf);
        }
    }

    const wchar_t* const pszCDPath = GConfig->GetStr(L"Engine.Engine", L"CdPath");
    assert(pszCDPath);
    if(pszCDPath[0]!='\0')
    {
        m_DefaultDataDirs.insert(pszCDPath);
    }
}

void CDataDirDialog::AddItemsFromConfig()
{
    //An entry with no directory part adds nothing and returns the root; checking that would tick the whole tree
    const auto AddCheckedItem = [this](const wchar_t* const pszPath)
    {
        const HTREEITEM hItem = AddItemToList(pszPath);
        if(hItem != m_TreeView.GetRoot())
        {
            m_TreeView.ApplyCheckState(hItem, CFancyTreeView::EItemState::CHECKED);
        }
    };

    assert(GSys);
    for(int i = 0; i < GSys->Paths.Num(); i++)
    {
        //Convert format like "..\Music\*.umx" to "..\Music"
        wchar_t szBuf[MAX_PATH];
        wcsncpy_s(szBuf, *GSys->Paths(i), _TRUNCATE);
        PathRemoveFileSpec(szBuf);

        if(m_DefaultDataDirs.find(szBuf) == m_DefaultDataDirs.cend()) //Don't show the default entries in the list
        {
            AddCheckedItem(*GSys->Paths(i));
        }
    }

    //Add int overrides
    TMultiMap<FString, FString>* const pSectionInt = GConfig->GetSectionPrivate(PROJECTNAME, FALSE, TRUE); //Int files have their own section as we use our own override mechanism
    if(pSectionInt)
    {
        TArray<FString> IntPaths;
        pSectionInt->MultiFind(FFileManagerDeusExe::sm_pszIntPaths, IntPaths); //Returns in reverse order
        for(int i = IntPaths.Num() - 1; i >= 0; i--)
        {
            //Never in a default data directory as we don't give users a way to add .int files there
            AddCheckedItem(*IntPaths(i));
        }
    }
}

void CDataDirDialog::PopulateList()
{   
    m_bAddTopLevelDirsOnly = true; //Add top level dirs using .ini file priority
    AddItemsFromConfig();
    m_bAddTopLevelDirsOnly = false;

    //Search directories. Paths in the tree are relative to System, matching the ini's own entries.
    wchar_t szSystemDir[MAX_PATH];
    wchar_t szDirUp[MAX_PATH];
    if(Misc::GetGameSystemDir(szSystemDir) && PathCombine(szDirUp, szSystemDir, L"..")) //PathCombine empties the buffer on failure, which would scan the current directory instead
    {
        SearchDirs(szDirUp, szSystemDir);
    }

    AddItemsFromConfig(); //Add subdirs using disk priority
}

void CDataDirDialog::PopulateConfig() const
{
    assert(GSys);

    TMultiMap<FString, FString>* const pSection = GConfig->GetSectionPrivate(L"Core.System", TRUE, FALSE);
    assert(pSection);
    TMultiMap<FString, FString>* const pSectionInt = GConfig->GetSectionPrivate(PROJECTNAME, TRUE, FALSE); //Int files have their own section as we use our own override mechanism
    assert(pSectionInt);

    pSection->Remove(sm_pszPaths);
    pSectionInt->Remove(FFileManagerDeusExe::sm_pszIntPaths);

    for(HTREEITEM hTreeItem = m_TreeView.FindNextLeaf(m_TreeView.GetRoot()); hTreeItem; hTreeItem = m_TreeView.FindNextLeaf(hTreeItem))
    {
        if(m_TreeView.GetItemState(hTreeItem) == CFancyTreeView::EItemState::CHECKED)
        {
            wchar_t szTreeText[MAX_PATH];
            m_TreeView.GetItemText(hTreeItem, szTreeText, _countof(szTreeText));

            if(_wcsicmp(PathFindExtension(szTreeText), L".int") == 0)
            {
                pSectionInt->Add(FFileManagerDeusExe::sm_pszIntPaths, szTreeText);
            }
            else
            {
                pSection->Add(sm_pszPaths, szTreeText);
            }
        }
        
    }
    
    //Re-add default items
    TMultiMap<FString, FString>* const pDefSection = GConfig->GetSectionPrivate(L"Core.System", FALSE, FALSE, L"default.ini");
    if(pDefSection) //Absent in an install without a default.ini
    {
        TArray<FString> Defaults;
        pDefSection->MultiFind(sm_pszPaths, Defaults);
        for(int i = 0; i < Defaults.Num(); i++)
        {
            pSection->Add(sm_pszPaths, *Defaults(i));
        }
    }

    GSys->LoadConfig();
}


HTREEITEM CDataDirDialog::AddItemToList(const wchar_t* const pszPath)
{
    assert(pszPath);

    const wchar_t* const pszFileName = PathFindFileName(pszPath);
    assert(pszFileName);

    HTREEITEM hTreeItem = m_TreeView.GetRoot();

    const wchar_t* pszPathComponent = PathFindNextComponent(pszPath); //Skip ".."
    for(const wchar_t* pszNextComponent = PathFindNextComponent(pszPathComponent); pszNextComponent; pszPathComponent = pszNextComponent, pszNextComponent = PathFindNextComponent(pszPathComponent))
    {
        if(pszPathComponent == pszFileName) //Leaf item should be the whole path, so it matches the complete .ini file entry (easier for both users and me)
        {
            hTreeItem = m_TreeView.InsertItemUnique(pszPath, hTreeItem); //Always unique
        }
        else
        {
            wchar_t szBuf[MAX_PATH];
            const size_t iComponentLen = static_cast<size_t>(pszNextComponent - pszPathComponent) - 1; // -1 to skip backslash
            wcsncpy_s(szBuf, pszPathComponent, std::min(iComponentLen, _countof(szBuf) - 1));
            hTreeItem = m_TreeView.InsertItemUnique(szBuf, hTreeItem);
        }

        if(m_bAddTopLevelDirsOnly)
        {
            break;
        }

    }
    return hTreeItem;
}

INT_PTR CALLBACK CDataDirDialog::DataDirDialogProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
    CDataDirDialog* pThis = reinterpret_cast<CDataDirDialog*>(GetProp(hwndDlg,L"this"));

    switch (uMsg)
    {

    case WM_INITDIALOG:
        {
            SetProp(hwndDlg,L"this",reinterpret_cast<HANDLE>(lParam));
            pThis =  reinterpret_cast<CDataDirDialog*>(lParam);
            pThis->m_hWnd = hwndDlg;
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG,reinterpret_cast<LPARAM>(LoadIcon(reinterpret_cast<HINSTANCE>(GetWindowLongPtr(hwndDlg,GWLP_HINSTANCE)), MAKEINTRESOURCE(IDI_ICON))));

            pThis->m_TreeView.Init(GetDlgItem(hwndDlg, IDC_DIRTREE), GetDlgItem(pThis->m_hWnd, IDC_UP), GetDlgItem(pThis->m_hWnd, IDC_DOWN));

            pThis->FindDefaultItems();
            pThis->PopulateList();
            
            pThis->m_TreeView.SelectFirstItem();
        }
        return TRUE;

    case WM_COMMAND:
        switch (HIWORD(wParam))
        {
        case BN_CLICKED:
            switch (LOWORD(wParam))
            {

            case IDOK:
                pThis->PopulateConfig(); //Reads the tree-view, so it has to run before the dialog goes away
                EndDialog(hwndDlg, 1);
                return TRUE;

            case IDCANCEL:
                EndDialog(hwndDlg, 0);
                return TRUE;

            case IDC_UP:
            {
                pThis->m_TreeView.MoveItemUp();
            }
            return TRUE;

            case IDC_DOWN:
            {
                pThis->m_TreeView.MoveItemDown();
            }
            return TRUE;

            }
            break;
        }
        break;

    case WM_NOTIFY:
    {
        const NMHDR* const pNMH = reinterpret_cast<NMHDR*>(lParam);
        assert(pNMH);
        if(pThis && pNMH->hwndFrom == pThis->m_TreeView.GetHWnd())
        {
            const BOOL bResult = pThis->m_TreeView.HandleNotify(pNMH);
            SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, bResult);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(hwndDlg,0);
        return TRUE;

    case WM_NCDESTROY:
        RemoveProp(hwndDlg, L"this");
        break;
    }

    return FALSE;
}
