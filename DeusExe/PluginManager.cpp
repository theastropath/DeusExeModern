#include "stdafx.h"
#include <cstdarg>
#include "PluginManager.h"
#include "Misc.h"

namespace
{
    //Runs before appInit() when GLog isn't up yet: log to GLog if it exists, else to an attached debugger.
    void PluginLog(const wchar_t* const pszFormat, ...)
    {
        wchar_t szMessage[1024];
        va_list Args;
        va_start(Args, pszFormat);
        _vsnwprintf_s(szMessage, _countof(szMessage), _TRUNCATE, pszFormat, Args);
        va_end(Args);

        if (GLog)
        {
            GLog->Log(szMessage);
        }
        else
        {
            OutputDebugString(szMessage);
            OutputDebugString(L"\n");
        }
    }
}

CPluginManager::CPluginManager(const wchar_t* const pszBaseIniSection)
{
    assert(pszBaseIniSection);

    //Runs before appInit(), so the engine's config/file manager don't exist yet: read the plugin list with the
    //Win32 ini API, relative to the executable's (System) directory.
    wchar_t szSystemDir[MAX_PATH];
    if (!Misc::GetGameSystemDir(szSystemDir))
    {
        PluginLog(L"Plugin: no System directory; none loaded.");
        return;
    }

    //Its own PluginList.ini next to the executable, so the game's DeusEx.ini (redirected to Documents) is left untouched.
    wchar_t szIniPath[MAX_PATH];
    if (!PathCombine(szIniPath, szSystemDir, L"PluginList.ini"))
    {
        PluginLog(L"Plugin: no ini path; none loaded.");
        return;
    }

    //Plugins live in their own "[<section>.Plugins]" ini section, one "Plugin=" entry per DLL.
    wchar_t szSection[128];
    swprintf_s(szSection, L"%s.Plugins", pszBaseIniSection);

    //Fixed STACK buffer, no heap (GMalloc, which the global operator new routes through, isn't up before appInit).
    //GetPrivateProfileSection returns every line (repeated "Plugin=" keys preserved, in file order) as a run of
    //null-terminated "key=value" strings ending in an extra null. A plugin list is tiny; an over-long section is truncated.
    wchar_t szSectionBuffer[4096] = {};
    GetPrivateProfileSection(szSection, szSectionBuffer, _countof(szSectionBuffer), szIniPath);

    for (const wchar_t* pszLine = szSectionBuffer; *pszLine != 0; pszLine += wcslen(pszLine) + 1)
    {
        const wchar_t* const pszEquals = wcschr(pszLine, L'=');
        if (!pszEquals)
        {
            continue;
        }

        //Accept only the "Plugin" key (case-insensitive), ignoring spaces before the '='.
        size_t iKeyLen = static_cast<size_t>(pszEquals - pszLine);
        while (iKeyLen > 0 && pszLine[iKeyLen - 1] == L' ')
        {
            iKeyLen--;
        }
        if (iKeyLen != 6 || _wcsnicmp(pszLine, L"Plugin", 6) != 0)
        {
            continue;
        }

        const wchar_t* pszValue = pszEquals + 1;
        while (*pszValue == L' ')
        {
            pszValue++;
        }
        if (*pszValue == 0)
        {
            continue;
        }

        //PathCombine keeps an absolute entry as-is, otherwise roots it in the System dir
        wchar_t szFullPath[MAX_PATH];
        if (!PathCombine(szFullPath, szSystemDir, pszValue))
        {
            PluginLog(L"Plugin: '%s' path too long.", pszValue);
            continue;
        }

        //Checked before loading: a module we can't track is one we can't free again
        if (m_iPluginCount >= sm_iMaxPlugins)
        {
            PluginLog(L"Plugin: max %u plugins; skipped '%s'.", static_cast<unsigned>(sm_iMaxPlugins), szFullPath);
            continue;
        }

        //LOAD_WITH_ALTERED_SEARCH_PATH so the plugin's own dependencies resolve from its folder.
        const HMODULE hModule = LoadLibraryEx(szFullPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!hModule)
        {
            PluginLog(L"Plugin: '%s' failed (error %u).", szFullPath, GetLastError());
            continue;
        }

        //A DLL without the entry point is still kept loaded (a pure-injection DLL works from DllMain); it just gets no events.
        const auto pfnMain = reinterpret_cast<DeusExePluginMainFunc>(GetProcAddress(hModule, DEUSEXE_PLUGIN_ENTRYPOINT_NAME));
        if (pfnMain)
        {
            PluginLog(L"Plugin: loaded '%s'.", szFullPath);
        }
        else
        {
            PluginLog(L"Plugin: loaded '%s' (no '%S' entry; no events).", szFullPath, DEUSEXE_PLUGIN_ENTRYPOINT_NAME);
        }

        m_Plugins[m_iPluginCount].hModule = hModule;
        m_Plugins[m_iPluginCount].pfnMain = pfnMain;
        m_iPluginCount++;
    }
}

CPluginManager::~CPluginManager()
{
    for (size_t i = 0; i < m_iPluginCount; i++)
    {
        FreeLibrary(m_Plugins[i].hModule);
    }
}

void CPluginManager::Dispatch(const DeusExePluginEvent Event, UEngine* const pEngine, UViewport* const pViewport, const HWND hWnd) const
{
    if (m_iPluginCount == 0)
    {
        return;
    }

    DeusExePluginContext Context;
    Context.StructSize = sizeof(Context);
    Context.Event      = static_cast<unsigned int>(Event);
    Context.CmdLine    = appCmdLine();
    if (!Context.CmdLine || Context.CmdLine[0] == 0)
    {
        Context.CmdLine = GetCommandLine(); //appCmdLine() isn't populated until appInit(); fall back before then
    }
    Context.Engine     = pEngine;
    Context.Viewport   = pViewport;
    Context.Window     = hWnd;

    //ABI version passed as an argument (not in the context) so a plugin can reject a mismatch before dereferencing the struct.
    for (size_t i = 0; i < m_iPluginCount; i++)
    {
        if (m_Plugins[i].pfnMain)
        {
            m_Plugins[i].pfnMain(DEUSEXE_PLUGIN_ABI_VERSION, &Context);
        }
    }
}
