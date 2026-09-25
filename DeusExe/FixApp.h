#pragma once

class CFixApp
{
public:
    bool Show(const HWND hWndParent);

    static const int sm_iDefaultFPSLimit = 120;

private:
    void ReadSettings();
    void PopulateDialog();
    void EnableDisableResSettings(const bool bChangesAllowed, const bool bCommon) const;
    void ApplySettings() const;
    void UpdateDXVKConfig() const;

    static INT_PTR CALLBACK FixAppDialogProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam);

    static constexpr unsigned char sm_iBPP_16 = 16;
    static constexpr unsigned char sm_iBPP_32 = 32;
    static constexpr size_t sm_iFallbackResX = 320; //Only used when a field is left empty
    static constexpr size_t sm_iFallbackResY = 200;

    struct Resolution //These are all ints to match what Unreal loads from the ini file
    {
        size_t iX;
        size_t iY;
    };

    HWND m_hWnd = NULL;
    HWND m_hWndCBGUIScales = NULL;
    HWND m_hWndCBRenderers = NULL;
    HWND m_hWndRadioResCommon = NULL;
    HWND m_hWndCBResolutions = NULL;
    HWND m_hWndRadioResCustom = NULL;
    HWND m_hWndTxtResX = NULL;
    HWND m_hWndTxtResY = NULL;
    HWND m_hWndTxtFOV = NULL;
    std::vector<std::wstring> m_Renderers;
    std::deque<Resolution> m_Resolutions; //Don't want push_back to change address
};
