#pragma once

#include "DeusExePlugin.h"

/**
Loads the plugin DLLs listed in PluginList.ini and forwards the launcher's start-up
events to them. See DeusExePlugin.h for the plugin-side SDK and the ini format.

Plugins load before appInit(), so the list is read from PluginList.ini (in the System
folder) with the Win32 profile API, separate from the game's DeusEx.ini.

Include after stdafx.h so the Unreal/Win32 types are available.
*/
class CPluginManager
{
public:
    /**
    Loads every DLL listed under "[<pszBaseIniSection>.Plugins]" in PluginList.ini (one "Plugin"
    entry per DLL; relative paths resolved against the System directory). Must be called before
    appInit(); call Dispatch() afterwards to notify the plugins.
    */
    explicit CPluginManager(const wchar_t* const pszBaseIniSection);
    ~CPluginManager();

    CPluginManager(const CPluginManager&) = delete;
    CPluginManager& operator=(const CPluginManager&) = delete;

    /** Notifies every loaded plugin of an event, passing whatever context is available so far. */
    void Dispatch(const DeusExePluginEvent Event, UEngine* const pEngine = nullptr, UViewport* const pViewport = nullptr, const HWND hWnd = nullptr) const;

private:
    struct SPlugin
    {
        HMODULE               hModule;
        DeusExePluginMainFunc pfnMain; //May be null: the DLL loaded (its DllMain ran) but takes no events
    };

    //Fixed storage, no heap: constructed before appInit(), when GMalloc (which operator new routes through) isn't set up.
    static const size_t sm_iMaxPlugins = 32;
    SPlugin m_Plugins[sm_iMaxPlugins] = {};
    size_t  m_iPluginCount = 0;
};
