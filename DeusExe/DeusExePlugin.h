#pragma once

/**
Deus Exe launcher plugin SDK.

The launcher loads plugin DLLs before appInit() and notifies them at key points
during start-up, so a plugin can hook the game from the very beginning.

To write a plugin, build a DLL that exports a single, undecorated entry point:

    #include "DeusExePlugin.h"

    extern "C" __declspec(dllexport)
    void DeusExePluginMain(unsigned int AbiVersion, const DeusExePluginContext* Context)
    {
        if (AbiVersion != DEUSEXE_PLUGIN_ABI_VERSION)
            return; // Built against a different launcher ABI

        switch (Context->Event)
        {
        case DeusExePluginEvent_PreAppInit:      // Earliest point, before core init
            break;
        case DeusExePluginEvent_PostEngineInit:  // Context->Engine is now valid
            break;
        case DeusExePluginEvent_ViewportCreated: // Context->Viewport / Context->Window are valid
            break;
        }
    }

List the plugins in PluginList.ini, in the executable's (System) folder, one
"Plugin" line per DLL:

    [DeusExe.Plugins]
    Plugin=MyDebugPlugin.dll
    Plugin=C:\Tools\AnotherPlugin.dll

Relative paths are resolved against the System directory; the entry point is
called once per event, in the order the plugins are listed. A DLL that doesn't
export DeusExePluginMain is still loaded (so it can work from DllMain) but
receives no events.
*/

#ifdef __cplusplus
extern "C" {
#endif

/** Bumped whenever the entry-point signature, DeusExePluginContext layout, or event semantics change. */
#define DEUSEXE_PLUGIN_ABI_VERSION 1u

/** Undecorated name of the entry point every plugin should export (see DeusExePluginMainFunc). */
#define DEUSEXE_PLUGIN_ENTRYPOINT_NAME "DeusExePluginMain"

/** Points in the launcher's start-up (and shutdown) at which plugins are notified. */
typedef enum DeusExePluginEvent
{
    DeusExePluginEvent_PreAppInit      = 0, /**< Plugins just loaded, before appInit(). Earliest point; for low-level hooking. Only CmdLine is available. */
    DeusExePluginEvent_PostAppInit     = 1, /**< Immediately after appInit(); the game core (config, logging) is up. Engine not created yet. */
    DeusExePluginEvent_PreEngineInit   = 2, /**< Immediately before the game engine is initialized; Engine is valid but not yet initialized. */
    DeusExePluginEvent_PostEngineInit  = 3, /**< Immediately after the game engine is initialized; Engine is valid. */
    DeusExePluginEvent_ViewportCreated = 4, /**< Game viewport/window created; Engine, Viewport and Window are valid. */
    DeusExePluginEvent_PreMainLoop     = 5, /**< Just before the main game loop starts running. */
    DeusExePluginEvent_Shutdown        = 6  /**< Launcher is shutting down; plugins are about to be unloaded. */
} DeusExePluginEvent;

/**
Passed to the plugin entry point on every event. Fields that are not yet
available for a given event are null, so always check the event (or the pointer)
before using Engine/Viewport/Window. Every pointer is owned by the launcher and
must not be freed by the plugin.
*/
typedef struct DeusExePluginContext
{
    unsigned int   StructSize;   /**< sizeof(DeusExePluginContext); lets the ABI grow compatibly. */
    unsigned int   Event;        /**< The DeusExePluginEvent being delivered. */
    const wchar_t* CmdLine;      /**< The game's command line (never null). */
    void*          Engine;       /**< Unreal UGameEngine* (UEngine*); null before DeusExePluginEvent_PreEngineInit. */
    void*          Viewport;     /**< Unreal UViewport*; null before DeusExePluginEvent_ViewportCreated. */
    void*          Window;       /**< Game window HWND; null before DeusExePluginEvent_ViewportCreated. */
} DeusExePluginContext;

/**
Signature of the plugin entry point, exported undecorated under the name
DEUSEXE_PLUGIN_ENTRYPOINT_NAME with the default (__cdecl) convention:

    extern "C" __declspec(dllexport)
    void DeusExePluginMain(unsigned int AbiVersion, const DeusExePluginContext* Context);

AbiVersion is the launcher's DEUSEXE_PLUGIN_ABI_VERSION, passed as an argument
(not in the context) so a plugin can check it without dereferencing a struct
whose layout it may not share.
*/
typedef void (*DeusExePluginMainFunc)(unsigned int AbiVersion, const DeusExePluginContext* Context);

#ifdef __cplusplus
} // extern "C"
#endif
