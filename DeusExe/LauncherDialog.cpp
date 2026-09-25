#include "stdafx.h"
#include "LauncherDialog.h"
#include "DataDirDialog.h"
#include "FixApp.h"
#include "Misc.h"
#include "FileManagerDeusExe.h"
#include "resource.h"

namespace
{
    /**
    Resolves the directory that holds the configuration data and save games, honouring -localdata and -gamename.
    GetDataDir() reports that no redirection is in effect at all (plain -localdata, or no usable 'Documents'), in which
    case that data lives in the install itself, one level up from 'System'.
    */
    bool GetDataDirToOpen(wchar_t(&szFolder)[MAX_PATH])
    {
        if(Misc::GetDataDir(szFolder))
        {
            return true;
        }

        wchar_t szSystemDir[MAX_PATH];
        return Misc::GetGameSystemDir(szSystemDir) && PathCombine(szFolder, szSystemDir, L"..") != nullptr;
    }

    void RightAlignLinks(const HWND hWndDlg)
    {
        assert(hWndDlg);

        static const int s_iLinkIDs[] = { IDC_SAVEFOLDER, IDC_INIFILES1, IDC_INIFILES2 };

        for(const int iLinkID : s_iLinkIDs)
        {
            const HWND hWndLink = GetDlgItem(hWndDlg, iLinkID);
            RECT rLink;
            if(GetWindowRect(hWndLink, &rLink) == FALSE)
            {
                continue;
            }
            MapWindowPoints(NULL, hWndDlg, reinterpret_cast<POINT*>(&rLink), 2);

            SIZE Ideal = { rLink.right - rLink.left, 0 };
            SendMessage(hWndLink, LM_GETIDEALSIZE, static_cast<WPARAM>(rLink.right), reinterpret_cast<LPARAM>(&Ideal));
            MoveWindow(hWndLink, rLink.right - Ideal.cx, rLink.top, Ideal.cx, rLink.bottom - rLink.top, TRUE);
        }
    }
}

bool CLauncherDialog::Show(const HWND hWndParent)
{
    return DialogBoxParam(GetModuleHandle(0),MAKEINTRESOURCE(IDD_DIALOG1),hWndParent,LauncherDialogProc,reinterpret_cast<LPARAM>(this)) == 1;
}

void CLauncherDialog::FillLinkControl(const HWND hWndLinkControl, const wchar_t* const pszIniFilePath)
{
    wchar_t szIni[MAX_PATH];
    wchar_t szLink[2 * MAX_PATH];

    //On failure the buffer holds a path that isn't there, so link to the original instead
    const wchar_t* const pszTarget = static_cast<FFileManagerDeusExe*>(GFileManager)->ToModernFileName(szIni, pszIniFilePath) ? szIni : pszIniFilePath;

    _snwprintf_s(szLink, _TRUNCATE, L"<a href=\"%s\">%s</a>", pszTarget, PathFindFileName(pszIniFilePath));
    SetWindowText(hWndLinkControl, szLink);
}

INT_PTR CALLBACK CLauncherDialog::LauncherDialogProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
    CLauncherDialog* pThis = reinterpret_cast<CLauncherDialog*>(GetProp(hwndDlg, L"this"));
    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            SetProp(hwndDlg, L"this", reinterpret_cast<HANDLE>(lParam));
            pThis = reinterpret_cast<CLauncherDialog*>(lParam);

            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIcon(reinterpret_cast<HINSTANCE>(GetWindowLongPtr(hwndDlg,GWLP_HINSTANCE)), MAKEINTRESOURCE(IDI_ICON))));

            pThis->m_hWndWebsite = GetDlgItem(hwndDlg, IDC_WEBSITE);
            pThis->m_hWndSaveFolder = GetDlgItem(hwndDlg, IDC_SAVEFOLDER);

            wchar_t szVersion[64];
            _snwprintf_s(szVersion, _TRUNCATE, L"Version %s", Misc::GetVersion());
            SetDlgItemText(hwndDlg,IDC_VERSION,szVersion);

            //Show ini files
            pThis->m_hWndIniFile1 = GetDlgItem(hwndDlg, IDC_INIFILES1);
            pThis->m_hWndIniFile2 = GetDlgItem(hwndDlg, IDC_INIFILES2);

            assert(GConfig);
            FConfigCacheIni* pCI = static_cast<FConfigCacheIni*>(GConfig);

            pThis->FillLinkControl(pThis->m_hWndIniFile1, *pCI->SystemIni);
            pThis->FillLinkControl(pThis->m_hWndIniFile2, *pCI->UserIni);

            RightAlignLinks(hwndDlg);
        }

        return TRUE;


    case WM_COMMAND:
        switch (HIWORD(wParam))
        {
        case BN_CLICKED:
            switch (LOWORD(wParam))
            {
            case IDC_PLAY:
                pThis->m_hMonitor = MonitorFromWindow(hwndDlg, MONITOR_DEFAULTTONEAREST); //Track on which monitor we were closed, so we can move game to there
                EndDialog(hwndDlg, 1);
                return TRUE;
            case IDC_EXIT:
                EndDialog(hwndDlg, 0);
                return TRUE;
            case IDC_DATADIRS:
            {
                CDataDirDialog DataDirDialog;
                DataDirDialog.Show(hwndDlg);
            }
            return TRUE;
            case IDC_CONFIG:
            {
                CFixApp FixApp;
                FixApp.Show(hwndDlg);
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
        switch(pNMH->code)
        {
        case NM_CLICK:
        {
            if(pThis && pNMH->hwndFrom == pThis->m_hWndSaveFolder)
            {
                GConfig->Flush(FALSE); //Writes out the current settings, which is also what creates the directory on a first run

                wchar_t szFolder[MAX_PATH];
                if(!GetDataDirToOpen(szFolder))
                {
                    GLog->Log(L"Deus Exe: Could not determine the save folder.");
                }
                else if(!Misc::OpenFolder(hwndDlg, szFolder))
                {
                    GLog->Logf(L"Deus Exe: Failed to open the save folder '%s'.", szFolder);
                }
                return TRUE;
            }

            if(pThis && (pNMH->hwndFrom == pThis->m_hWndWebsite || pNMH->hwndFrom == pThis->m_hWndIniFile1 || pNMH->hwndFrom == pThis->m_hWndIniFile2))
            {
                GConfig->Flush(FALSE);
                const NMLINK* const pLink = reinterpret_cast<NMLINK*>(lParam);
                assert(pLink);
                ShellExecute(hwndDlg, L"open", pLink->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
        }
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
