# Deus Exe

See also: [http://kentie.net/article/dxguide](http://kentie.net/article/dxguide)

## Installation

Place the contents of the DeusExe zip file in your Deus Ex 'System' directory. Examples of this are `C:\\Deus Ex\\System` and `D:\\Steam\\steamapps\\common\\Deus Ex\\System`.

## New command-line options

* **`-skipdialog`** — Skips the launcher dialog and immediately starts the game.
* **`-localdata`** — Uses the game directory, instead of 'Documents', for configuration data, save games, etc. Please make sure you have write permissions.
* **`-gamename <name>`** — Keeps configuration data, save games, etc. in a subdirectory of that name, so several mods can be played side by side without overwriting each other's settings. For example, `-gamename "GMDXAE"` uses 'Documents\\Deus Ex\\GMDXAE', or the same subdirectory of the game directory when combined with -localdata. Settings are read from and written to '<name>.ini' instead of the game's own ini; the first time a name is used, that file is created from the existing settings.

## Plugins

Deus Exe can load plugin DLLs of your own before the game starts, and notifies them at each step of the start-up, so a plugin can hook the game from the very beginning. This is meant for tools and mods that need to run native code; nothing needs to be installed for the game itself to work.

### Enabling plugins

List the plugins in 'PluginList.ini', in the same 'System' directory as the executable, with one `Plugin` line per DLL. Deus Exe uses its own file for this, so the game's ini (which is redirected to 'Documents') is left untouched.

```ini
\[DeusExe.Plugins]
Plugin=MyDebugPlugin.dll
Plugin=C:\\Tools\\AnotherPlugin.dll
```

Relative paths are resolved against the 'System' directory, absolute paths are used as-is, and the plugins are called in the order they're listed. A plugin's own dependencies are looked up in its folder. Loading is logged to the game log, including the reason a DLL was skipped; at most 32 plugins are loaded. Remove or comment out the line to disable a plugin.

### Writing a plugin

Build a DLL that includes 'DeusExePlugin.h' (in the DeusExe source) and exports a single, undecorated entry point:

```cpp
#include "DeusExePlugin.h"

extern "C" \_\_declspec(dllexport)
void DeusExePluginMain(unsigned int AbiVersion, const DeusExePluginContext\* Context)
{
    if (AbiVersion != DEUSEXE\_PLUGIN\_ABI\_VERSION)
        return; //Built against a different launcher ABI

    if (Context->Event == DeusExePluginEvent\_ViewportCreated)
        MessageBox((HWND)Context->Window, L"Hello", L"Plugin", MB\_OK);
}
```

The entry point is called once per event, with a context holding the command line and, once they exist, the engine, the viewport and the game window. Anything not available yet for a given event is null, and everything in the context is owned by Deus Exe, so don't free it. A DLL that doesn't export `DeusExePluginMain` is still loaded, so it can do its work from `DllMain`, but it receives no events. Always check `AbiVersion` first: it's passed as an argument rather than in the context, so a plugin built for an older Deus Exe can bail out before touching a struct whose layout may have changed.

### Events

* **`DeusExePluginEvent\_PreAppInit`** — The plugins have just been loaded, before the game core is initialized. The earliest point, for low-level hooking; only the command line is available.
* **`DeusExePluginEvent\_PostAppInit`** — The game core (configuration, logging) is up. The engine doesn't exist yet.
* **`DeusExePluginEvent\_PreEngineInit`** — The game engine has been created but not yet initialized. `Engine` is valid.
* **`DeusExePluginEvent\_PostEngineInit`** — The game engine has been initialized. `Engine` is valid.
* **`DeusExePluginEvent\_ViewportCreated`** — The game viewport and window have been created. `Engine`, `Viewport` and `Window` are valid.
* **`DeusExePluginEvent\_PreMainLoop`** — Just before the main game loop starts running.
* **`DeusExePluginEvent\_Shutdown`** — Deus Exe is shutting down and the plugins are about to be unloaded. The last chance to clean up.

## Technical notes

* Deus Exe runs with Data Execution Prevention (DEP) disabled, both in the executable's headers and at start-up. The game's 'Galaxy' audio subsystem executes code from data pages and crashes otherwise. This is logged to the game log.

## Changelog

### Version 9.0.1.0 (september 18, 2026)

* Added an 'Open save folder' link to the launcher, which opens the directory holding the configuration data and save games, following `-localdata` and `-gamename`. Under WINE/Proton it opens the desktop's own file manager rather than WINE's.
* The script package behind the widescreen subtitle fix is now named 'SubtitleFix.u' instead of 'DeusExe.u'. Delete the leftover 'DeusExe.u' from the 'System' directory, it is no longer used.

### Version 9 (august 17, 2026)

* Added the `-gamename <name>` command-line option, so several mods can be played side by side without overwriting each other's configuration data, save games and settings. See 'New command-line options' above.
* `-localdata` and `-gamename` are now remembered when the engine relaunches itself, e.g. after a video mode change, so the same data directory keeps being used.
* Added plugin support: DLLs listed in 'PluginList.ini' are loaded before the game starts and are notified at each step of the start-up. See 'Plugins' above.
* Conversation packages ('DeusExCon\*.u') in a data directory now override the copies in 'System'. The game loads these by bare name and keeps the first copy it reads for the whole session, so previously the 'System' copy could win and a mod's conversations would be ignored.
* Localization (.int) overrides are now applied from the very first file access. The game reads some .int files, notably its own 'DeusEx.int', before the data directory list used to be built, and as it keeps whatever it reads first, a mod's .int file could end up unused.
* Rewrote the frame limiter. The game is now ticked every iteration with the actually elapsed time, and the frame rate is capped by sleeping away the remainder of the frame instead of by skipping ticks. The game's own maximum tick rate (e.g. a server's) is taken into account as well.
* The engine's current tick rate is now updated, so in-game frame rate displays show a value again.
* Alt+Tab and focus fixes:

  * The game is restored when a renderer left it minimized after switching back to it.
  * The click that gives the game focus back is no longer passed on to the game.
  * The mouse cursor is only hidden while the game actually has focus, so it no longer disappears over the game's window when another window is in front of it.
  * The Windows mouse cursor no longer reappears on top of the game after alt+tabbing away and back.
  * In exclusive full-screen mode the cursor is confined to the game's monitor. In a plain window it is only confined during camera control, so the window's border and title bar can still be reached while a menu is open.
  * The cursor is released again whenever the game loses focus or the window closes, so it is never left confined or invisible.
  * When the renderer recreates the game window, which happens on a video mode change, raw input and the cursor handling are re-attached to the new window.
  * Fixed cursor position not accounting for OTP UI Scaling, which makes the cursor not drift to the top-left corner when the game window regains focus.
  * Fixed modifier keys not being released on focus loss (on alt+tab and focus regain, the game missed alt release).
* The 'EditActor' command now also accepts `Name=<actor name>` besides `Class=`.
* The FPS limit is now also written to 'dxgi.maxFrameRate' and 'd3d9.maxFrameRate' in 'dxvk.conf', if such a file is present in the 'System' directory.
* The checkboxes in the 'Data Directories' dialog are now drawn and handled by Deus Exe itself, so checking a directory also (un)checks its subdirectories when running under WINE/Proton.
* The 'Data Directories' dialog now recognizes localization files regardless of how the extension is capitalized, so a '.INT' entry is no longer treated as a package directory.
* The game log is a lot more useful when something goes wrong. Level changes, queued travel and the engine's loading progress are logged, and a crash report now names the module and offset that faulted, what the faulting instruction was doing, and which map and loading stage the game was in. The engine's own call history, previously only shown in a message box, ends up in the log as well.
* Added an 'Enable verbose logging' option to the configuration dialog. It timestamps every log line and writes it to disk as it happens, so the lines leading up to a crash are no longer lost in a buffer, and it records the object and script function behind the last hooked native call for the crash report. It is off by default, as logging this way costs performance.
* Fixed raw mouse setting not being saved properly to the ini.
* Switched to Visual Studio 2022, make sure to [update your runtimes](https://aka.ms/vs/17/release/vc_redist.x86.exe).

### Version 8.1 (februari 20, 2016)

* Fixed alt+enter behavior in borderless windowed full-screen mode.
* Added option to span borderless windowed full-screen mode over all monitors.

### Version 8 (februari 18, 2016)

* Game window is now shown on the monitor where the launcher window was closed.
* Added borderless full-screen window mode.
* When raw input is enabled, mouse buttons 4/5 work. In the key bindings screen they're shown as 'Unknown05'/'Unknown06'.

### Version 7 (august 29, 2015)

* Resolution shown in configuration dialog upon opening it now matches windowed/fullscreen selection.
* Thanks Sebastian Kaufel for discovering and sharing how to replace native script functions.
* Added free disk space fix that doesn't require detoured.dll.
* Added native UI scaling fix that no longer requires OTP patch. Has a manual scaling setting, and properly deals with resizing of the game window.
* Added option to fix subtitles during cinematics for wider-than-16:9 resolutions.
* Fixed issue with FPS limiter not being applied correctly.
* Changed enable/disable behavior of configuration dialog options.
* Switched to Visual Studio 2015, make sure to [update your runtimes](http://download.microsoft.com/download/9/3/F/93FCF1E7-E6A4-478B-96E7-D4B285925B00/vc_redist.x86.exe).

### Version 6.2 (june 29, 2014)

* The FPS limiter is now stored under the \[Deus Exe] heading in the .ini file. It is also now set to 120 by default.
* Since the first version, Deus Exe has forced the game to run on a single CPU (core). However, being that the game is generally CPU bound, and that OS/driver support for multiple CPUs has improved since then, the game now uses all processors by default. The launcher options still allow the game to be forced to run on a single CPU.
* Fixed raw input mouse locking/unlocking for menus, which tripped up on cutscenes and dialogue sequences.

### Version 6.1 (may 31, 2014)

* Fixed rapidly clicking in full-screen mode with raw input enabled minimizing game.

### Version 6 (may 18, 2014)

* Switched to Visual Studio 2013, make sure to [update your runtimes](http://download.microsoft.com/download/2/E/6/2E61CFA4-993B-4DD4-91DA-3737CD5CD6E3/vcredist_x86.exe).
* **Dropped Windows XP support.**
* Executable marked as High DPI aware.
* The INI file names in the launcher dialog are now clickable to easily edit the files. Note that any changes made will not be picked up by the game until next launch; changes made in the Deus Exe GUI will be visible in the files, though.
* Instead of the various separate FOV settings, the choices are now 'Default', 'Automatic' and 'Custom'.

  * **Default** — Uses game game default (75 for Deus Ex).
  * **Automatic** — Calculates optimal FOV depending on the game's resolution.
  * **Custom** — Custom FOV.

  Note that automatic mode sets the FOV for the selected resolution, it will not change the FOV upon in-game resolution changes.

* The resolution drop-down box is now populated with the resolutions supported by the primary monitor.
* User.ini changes (FOV) now take the `USERINI` command-line option into account.
* When starting the game for the first time, the desktop resolution is chosen for full-screen mode.
* Instead of using a mix of DirectInput and raw input to combat mouse acceleration, raw input is now a dedicated mode. In this mode, standard Windows input is used for the menu mouse cursor, while the player camera uses the raw input API to be acceleration free.
* Duplicate mouse cursor when playing in windowed mode now also removed in standard input mode.
* Added LARGEADDRESSAWARE linker flag and set stack size to 2MB for Revision support.
* Data Directories dialog is now tree-based.
* Localization file override support. Mods such as Shifter and its derivatives replace some game strings by overwriting DeusEx.int. These .int files can now be stored in a mod's subdirectory and will be used if the user adds this directory in the 'Data Directories' dialog.

  Three matters complicate all this:

  1. The user running a localized version of the game, e.g. the language set to something other than 'int', such as 'det' for German. In that case, the game uses 'DeusEx.det' for its strings, with the original 'int' file as a fallback for when a string is not found in the 'det' file.
  2. The ability to rename the game's executable; if DeusEx.exe is renamed to 'bla.exe', the executable will use 'bla.int' for its strings.
  3. The fact that DeusEx.int also contains strings for the 'DeusEx.u' package, such as main menu text items. When DeusEx.exe is renamed, 'DeusEx.int' (or .det, etc.) will still be used for those.

  In the end I settled on the following behavior:

  1. The 'Data Directories' dialog only looks for .int files, so it won't recognize mods that for some odd reason do not include English strings.
  2. The replacement mechanism itself will reroute access to any localization file the game tries to access. This means the 'DeusEx.u' package will still use the 'DeusEx' localization file for its strings no matter the .exe name, just like before.
  3. When the language is something other than 'int', access to both that language's localization file (e.g. 'DeusEx.det') and the '.int' file is rerouted, so localized mods work fine. If a string is not found in the mod's language-specific file, its .int file is used.
  4. Combining the above two rules, if Deus Exe is renamed to 'bla.exe' and the language is 'det', it reroutes access from 'System\\bla.det' to 'Shifter\\bla.det' and from 'System\\bla.int' to 'Shifter\\bla.int'. But explicit access to 'System\\DeusEx.int' by the 'DeusEx.u' package is unchanged.
  5. As a result of the localization file fallback mechanism, with the language set to e.g. 'det', if a mod only includes an .int file, and not a .det file, the .int file will be used for all its strings, which turns the game English but does allow the mod to use its text.
  6. There's NO fallback for individual entries; if 'Shifter\\DeusEx.int' does not contain a string, the original 'System\\DeusEx.int' file is not queried; but then, the new file needs to include all strings anyway to be compatible with the original DeusEx.exe, which has no override mechanism for localization files at all.

### Version 5.3 (august 11, 2013)

* If a custom RootWindow is found (installed by a mod), the configurator's GUI setting is disabled to preserve this.

### Version 5.2 (april 13 2013)

* Fixed the application failing to start when installed to the same drive as the 'Documents' directory (e.g. C:).

### Version 5.1 (march 29, 2013)

* Fixed a directory creation issue if 'Documents\\Deus Ex' did not already exist.

### Version 5 (march 26, 2013)

* Switched to Visual Studio 2012.
* Fixed dedicated server mode / preferences / console.
* Fixed ability to add corrupted item to 'Data Directories' listbox.
* Server max tickrate is now taken into account.
* Added some new prefab resolutions / FOVs.
* Restored 'EditActor' command.
* Completely cleaned up code, so who knows what's broken.

### Version 4.3 (july 28, 2012)

* Fixed data directories window not showing paths, only extensions, on Windows XP.

### Version 4.2 (june 27, 2012)

* Fixed a dumb mistake in the hard disk free space fix code. Negative free space when trying to save should be fixed now. (Thanks Myk)

### Version 4.1 (january 29, 2012)

* Fixed inability to overwrite/delete savegames.
* Fixed data dir dialog not picking up .uax files.

### Version 4 (january 11, 2012)

* Added `-localdata` command-line option to disable usage of the 'My Documents' directory for files.
* Game now uses original (default or existing) ini files instead of custom ones; fixes non-English versions of the game being turned English.
* Added data directory dialog so mods can be more easily enabled/disabled and combined.
* Mod support improved when using `INI`/`USERINI` command line options.
* Fixed 'Preferences' dialog only being able to be summoned once.

### Version 3 (august 2, 2011)

* Added '-skipdialog' command-line option to skip the launcher dialog.
* (Re) added dedicated server support ('-server' command-line option).
* Removed admin-rights fix applier; packaged OTPUIFix dlls seperately as DeusEx.exe now itself uses Detoured.dll.
* Fooled the game into thinking there's a max of 4GB free hard drive space; this prevents negative free space values in the save/load screens when running on an extremely large (TB+ partition). More importantly, this fixes an inability to save with the game compaining about too little free space.
* Got rid of the original Windows mouse cursor in DirectInput (i.e. no mouse acceleration) mode. The two cursors ran at different speeds, making clicking outside of windowed games' areas a frequent occurrence. To get the cursor back, just alt+tab.

### Version 2 (december 13, 2010)

* Made FPS limit user configurable.

### Version 1 (may 25, 2010)

* Initial release.

