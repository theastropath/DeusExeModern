#pragma once

namespace Misc
{
    bool SetDEP(const DWORD dwFlags);

    bool GetUserDocsDir(wchar_t(&pszBuf)[MAX_PATH]);

    bool GetGameSystemDir(wchar_t(&pszBuf)[MAX_PATH]);

    const wchar_t* GetGameName();

    bool HasCommandLineSwitch(const wchar_t* const pszSwitch);

    bool IsLocalData();

    bool GetDataDir(wchar_t(&pszBuf)[MAX_PATH]);

    const wchar_t* GetVersion();

    float GetDefaultFOV();

    float CalcFOV(const size_t iResX, const size_t iResY);

    bool IsRunningUnderWine();

    bool OpenFolder(const HWND hWndParent, const wchar_t* const pszPath);

    void CenterWindowOnMonitor(const HWND hWnd, const HMONITOR hMonitor);

    enum class BorderlessFullscreenMode { NONE, CURRENT_MONITOR, ALL_MONITORS };
    void SetBorderlessFullscreen(const HWND hWnd, const BorderlessFullscreenMode Mode);

    void SetVerboseLogging(const bool bEnabled); //!< Gates the diagnostics that cost time every frame: the live (timestamped, flushed) log and the script trace

    bool IsVerboseLogging();
};
