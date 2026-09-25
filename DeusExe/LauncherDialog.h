#pragma once

class CLauncherDialog
{
public:
    bool Show(const HWND hWndParent);
	HMONITOR GetChildWindowMonitor() const { return m_hMonitor; }

private:
    void FillLinkControl(const HWND hWndLinkControl, const wchar_t* const pszIniFilePath);

    static INT_PTR CALLBACK LauncherDialogProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam);

    HWND m_hWndIniFile1 = NULL; //!< System.ini
    HWND m_hWndIniFile2 = NULL; //!< User.ini
    HWND m_hWndWebsite = NULL;
    HWND m_hWndSaveFolder = NULL;
    HMONITOR m_hMonitor = NULL;
};


