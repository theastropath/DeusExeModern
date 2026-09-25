#pragma once

class CPluginManager;

class CLauncher : private FExecHook
{
public:
    explicit CLauncher(CPluginManager& Plugins);
    ~CLauncher();
    CLauncher(const CLauncher&) = delete;
    CLauncher& operator=(const CLauncher&) = delete;

private:
    void ApplyAutoFOV(const size_t iSizeX, const size_t iSizeY);
    void MainLoop(UEngine * const pEngine);
    void LoadSettings();
    void ToggleBorderlessWindowedFullscreen();
    void SetCursorHidden(const bool bHide);
    void ReleaseCursor(); //!< Undoes any clipping/hiding we applied, so the cursor is never left confined or invisible
    void AttachViewportSubclass(); //!< No-op while Direct3D's window hook owns the viewport's window procedure: subclassing over it outlives the render device and crashes on the next mode change
    void DetachViewportSubclass();
    static LRESULT CALLBACK ViewportSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    HWND m_hWnd = NULL;

    LARGE_INTEGER m_iPerfCounterFreq = {};
    size_t m_iSizeX = 0;
    size_t m_iSizeY = 0;
    UViewport* m_pViewPort = nullptr; //If user closes window, viewport disappears before we get WM_QUIT
    bool m_bPrevInMenu = false;
    bool m_bPrevHasFocus = true; //No focus change on start
    bool m_bInBorderlessFullscreenWindow = false;
    bool m_bCursorClipped = false;
    bool m_bViewportSubclassed = false;
    bool m_bPrevToolWindowOpen = false; //!< A property window was open last frame; fullscreen is restored on the closing edge

    //Settings
    float m_fFPSLimit = 120.0f; //Because GetMaxTickRate() is float
    UBOOL m_bRawInput = TRUE;
    UBOOL m_bAutoFov = TRUE;
    UBOOL m_bBorderlessFullscreenWindow = TRUE;
    UBOOL m_bBorderlessFullscreenWindowUseAllMonitors = FALSE;
    UBOOL m_bUseSingleCPU = FALSE;

//From FExec
private:
    UBOOL Exec(const TCHAR* Cmd, FOutputDevice& Ar) override;

};
