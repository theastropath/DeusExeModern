#include "stdafx.h"
#include <timeapi.h> //timeBeginPeriod/timeEndPeriod for frame-limiter Sleep granularity
#include "ConfigCacheDeusExe.h"
#include "Diagnostics.h"
#include "FileManagerDeusExe.h"
#include "Misc.h"
#include "CrashReport.h"
#include "RawInput.h"
#include "LauncherDialog.h"
#include "FixApp.h"
#include "ExecHook.h"
#include "NativeHooks.h"
#include "PluginManager.h"
#include "Launcher.h"

//Do not put before stdafx.h
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib,"winmm.lib")

extern "C" {wchar_t GPackage[64] = L"Launch"; } //Will be set to exe name later

INT WINAPI WinMain(HINSTANCE /*hInInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, INT /*nCmdShow*/)
{
    //Set up the crash handler first, so faults during start-up (including plugin loading) are reported with context.
    CrashReport::Setup();

    //Disable DEP before any DLLs load (needed for Galaxy.dll; also needs the exe linked /NXCOMPAT:NO). Logged after appInit, once GLog exists.
    const bool bDEPDisabled = Misc::SetDEP(0);

    const INITCOMMONCONTROLSEX CommonControlsInfo = { sizeof(INITCOMMONCONTROLSEX), ICC_TREEVIEW_CLASSES | ICC_LINK_CLASS };
    if (InitCommonControlsEx(&CommonControlsInfo) != TRUE)
    {
        return EXIT_FAILURE;
    }

    wcsncpy_s(GPackage, appPackage(), _TRUNCATE); //appStrcpy would overrun GPackage for a long executable name

    //Init core. The Deus Exe devices add timestamps, flush every line and log the engine's load progress and error history.
    FMallocWindows Malloc;
    FOutputDeviceFileDeusExe Log;
    FOutputDeviceErrorDeusExe Error;
    FFeedbackContextDeusExe Warn;

    //A -gamename gives every name its own data directory, so even with -localdata the data no longer lives where the game is installed
    wchar_t szDataDir[MAX_PATH];
    std::unique_ptr<FFileManagerDeusExe> pFileManager(Misc::GetDataDir(szDataDir) ? new FFileManagerDeusExeDataDir(szDataDir) : new FFileManagerDeusExe);

    //Load plugins before appInit so they can hook the entire start-up; must outlive appInit and the launcher. PreAppInit/PostAppInit bracket appInit.
    CPluginManager Plugins(PROJECTNAME);
    Plugins.Dispatch(DeusExePluginEvent_PreAppInit);

    appInit(GPackage, GetCommandLine(), &Malloc, &Log, &Error, &Warn, pFileManager.get(), FConfigCacheDeusExe::Factory, 1);

    UBOOL bVerboseLogging = FALSE; //Only readable now: the log device is already running, so its first lines are always stock
    GConfig->GetBool(PROJECTNAME, L"VerboseLogging", bVerboseLogging);
    Misc::SetVerboseLogging(bVerboseLogging != 0);

    GLog->Logf(L"Deus Exe: version %s.", Misc::GetVersion());
    if (!bDEPDisabled)
    {
        GLog->Log(L"Failed to set process DEP flags.");
    }

    Plugins.Dispatch(DeusExePluginEvent_PostAppInit);

    pFileManager->AfterCoreInit();

    GIsStarted = 1;
    GIsServer = 1;
    GIsClient = !ParseParam(appCmdLine(), L"SERVER");
    GIsEditor = 0;
    GIsScriptable = 1;
    GLazyLoad = !GIsClient;

    {
        CLauncher Launcher(Plugins);
    }

    Plugins.Dispatch(DeusExePluginEvent_Shutdown); //While the DLLs are still loaded; the manager frees them when it goes out of scope

    if (!GIsCriticalError)
    {
        appPreExit();
    }
    appExit();
    GIsStarted = 0;

    return EXIT_SUCCESS;
}

CLauncher::CLauncher(CPluginManager& Plugins)
{
    if (QueryPerformanceFrequency(&m_iPerfCounterFreq) == FALSE)
    {
        GError->Log(L"Failed to query performance counter.");
    }

    int iFirstRun = 0;
    GConfig->GetInt(L"FirstRun", L"FirstRun", iFirstRun);
    const bool bFirstRun = iFirstRun < ENGINE_VERSION;
    if (bFirstRun) //Select better default options
    {
        GConfig->SetString(L"Engine.Engine", L"GameRenderDevice", L"D3DDrv.D3DRenderDevice");
        GConfig->SetString(L"WinDrv.WindowsClient", L"FullscreenColorBits", L"32");
        wchar_t szTemp[1024];
        _itow_s(GetSystemMetrics(SM_CXSCREEN), szTemp, 10);
        GConfig->SetString(L"WinDrv.WindowsClient", L"FullscreenViewportX", szTemp);
        _itow_s(GetSystemMetrics(SM_CYSCREEN), szTemp, 10);
        GConfig->SetString(L"WinDrv.WindowsClient", L"FullscreenViewportY", szTemp);
    }

    if (ParseParam(appCmdLine(), L"changevideo") || bFirstRun)
    {
        CFixApp FixApp;
        FixApp.Show(NULL);
        if (bFirstRun)
        {
            GConfig->SetInt(L"FirstRun", L"FirstRun", ENGINE_VERSION);
        }
    }

    HMONITOR hMonitor = NULL;

    const auto DoLauncherDialog = [&hMonitor]
    {
        CLauncherDialog LD;
        const auto bRet = LD.Show(NULL);
        hMonitor = LD.GetChildWindowMonitor();
        return bRet;
    };

    if (!GIsClient || ParseParam(appCmdLine(), TEXT("skipdialog")) || DoLauncherDialog()) //Here the game actually starts
    {
        LoadSettings();

        static_cast<FFileManagerDeusExe*>(GFileManager)->OnGameStart();

        if (m_bUseSingleCPU)
        {
            if (SetProcessAffinityMask(GetCurrentProcess(), 0x1) == FALSE)
            {
                GLog->Log(L"Failed to set process affinity.");
            }
        }

        GConfig->SetBool(L"WinDrv.WindowsClient", L"UseDirectInput", m_bRawInput ? FALSE : TRUE);

        if (m_bBorderlessFullscreenWindow)
        {
            GConfig->SetBool(L"WinDrv.WindowsClient", L"StartupFullscreen", FALSE);
        }

        InitWindowing();

        const std::unique_ptr<WLog> LogWindowPtr = std::make_unique<WLog>(static_cast<FOutputDeviceFile*>(GLog)->Filename, static_cast<FOutputDeviceFile*>(GLog)->LogAr, L"GameLog");
        GLogWindow = LogWindowPtr.get();
        GLogWindow->OpenWindow(!GIsClient, 0);
        GLogWindow->Log(NAME_Title, LocalizeGeneral("Start"));

        GExec = this;


        GIsGuarded = 1;
        try
        {
            UClass* const pEngineClass = LoadClass<UGameEngine>(nullptr, L"ini:Engine.Engine.GameEngine", nullptr, LOAD_NoFail, nullptr);
            assert(pEngineClass);
            UEngine* const pEngine = ConstructObject<UEngine>(pEngineClass);
            assert(pEngine);
            if (!pEngine)
            {
                GError->Log(L"Engine initialization failed.");
            }

            Plugins.Dispatch(DeusExePluginEvent_PreEngineInit, pEngine);
            pEngine->Init();
            Plugins.Dispatch(DeusExePluginEvent_PostEngineInit, pEngine);

            GLogWindow->SetExec(pEngine); //If we directly set GExec, only our custom commands work
            GLogWindow->Log(NAME_Title, LocalizeGeneral("Run"));

            if (GIsClient)
            {
                if (pEngine->Client && pEngine->Client->Viewports.Num() > 0)
                {
                    m_pViewPort = pEngine->Client->Viewports(0);
                    m_hWnd = static_cast<const HWND>(m_pViewPort->GetWindow());
                }
                else
                {
                    GLog->Log(L"Unable to get viewport.");
                }
            }

            Plugins.Dispatch(DeusExePluginEvent_ViewportCreated, pEngine, m_pViewPort, m_hWnd);

            //Follow the launcher dialog onto whichever monitor it was closed on
            if (hMonitor != NULL && m_hWnd)
            {
                Misc::CenterWindowOnMonitor(m_hWnd, hMonitor);
            }

            if (m_bBorderlessFullscreenWindow)
            {
                ToggleBorderlessWindowedFullscreen();
            }

            if (m_bRawInput && m_hWnd)
            {
                if (!RegisterRawInput(m_hWnd))
                {
                    GError->Log(L"Raw input: Failed to register device.");
                }
            }

            AttachViewportSubclass(); //Deferred while Direct3D owns the window procedure; the main loop keeps retrying

            //Seed from the viewport size, which is what the main loop tracks; the client rect can differ in borderless mode
            if (GIsClient && m_bAutoFov && m_pViewPort && m_pViewPort->SizeX > 0 && m_pViewPort->SizeY > 0)
            {
                ApplyAutoFOV(static_cast<size_t>(m_pViewPort->SizeX), static_cast<size_t>(m_pViewPort->SizeY));
            }

            CNativeHooks NativeHooks(PROJECTNAME);

            GIsRunning = 1;
            if (!GIsRequestingExit)
            {
                Plugins.Dispatch(DeusExePluginEvent_PreMainLoop, pEngine, m_pViewPort, m_hWnd);
                MainLoop(pEngine);
            }
            GIsRunning = 0;

            GIsGuarded = 0;
        }
        catch(...)
        {
            GIsGuarded = 0;
            ReleaseCursor(); //MainLoop didn't get to do it, and the error message box needs a visible, unclipped cursor
            GError->HandleError(); //GErrorHist now holds the unwound call chain; this logs it and shows it
        }

        GLogWindow->Log(NAME_Title, LocalizeGeneral("Exit"));

        //appPreExit()/appExit() still run after this scope, so don't leave them pointing at destroyed objects
        GExec = nullptr;
        GLogWindow = nullptr;
    }

}

CLauncher::~CLauncher()
{
    DetachViewportSubclass(); //The viewport window outlives us: engine shutdown only runs after this
}

void CLauncher::ApplyAutoFOV(const size_t iSizeX, const size_t iSizeY)
{
    assert(m_iSizeX != iSizeX || m_iSizeY != iSizeY);
    assert(m_pViewPort);
    const float fFOV = Misc::CalcFOV(iSizeX, iSizeY);
    wchar_t szCmd[32];
    _snwprintf_s(szCmd, _TRUNCATE, L"fov %6.3f", fFOV);

    m_pViewPort->Exec(szCmd);
    m_iSizeX = iSizeX;
    m_iSizeY = iSizeY;
}

namespace
{
    //The menu cursor lives in root window coordinates, which are the client area divided by the GUI scale the game
    //picks for the resolution. hMultiplier is protected, so reach it through a derived class like the GUI scaling fix does.
    INT GetGUIScale(XRootWindow& Root)
    {
        class RootHack : public XRootWindow
        {
        public:
            INT GetScale() { return hMultiplier > 0 ? hMultiplier : 1; } //Zero until the root has been sized to the canvas once
        };
        return static_cast<RootHack&>(Root).GetScale();
    }

    /**
    Paces the main loop, and owns whatever it had to set up to do so, so an engine exception unwinding out of the
    loop can't leave the process behind with the system timer resolution still raised.

    Sleep() is the only wait with a usable resolution available across the supported Windows versions, and even at a
    1ms timer period it overshoots, so the last few milliseconds of a frame are spun out instead of slept away.
    */
    class CFrameLimiter
    {
    public:
        CFrameLimiter()
        {
            //Gives Sleep() ~1ms granularity instead of the scheduler's default ~15.6ms, which is far too coarse to pace a frame with
            m_bTimerPeriodSet = timeBeginPeriod(sm_uTimerPeriodMs) == TIMERR_NOERROR;
        }

        ~CFrameLimiter()
        {
            if(m_bTimerPeriodSet)
            {
                timeEndPeriod(sm_uTimerPeriodMs);
            }
        }

        CFrameLimiter(const CFrameLimiter&) = delete;
        CFrameLimiter& operator=(const CFrameLimiter&) = delete;

        //Waits until the performance counter reaches iDeadlineTicks, yielding the CPU for all but the final approach
        void WaitUntil(const LONGLONG iDeadlineTicks, const LONGLONG iFrequency) const
        {
            //Spun rather than slept, so the loop doesn't oversleep the deadline: Sleep(1) can return several
            //milliseconds late, and how late depends on whether raising the timer period above was allowed at all.
            const LONGLONG iSpinTicks = iFrequency * 6 / 1000; //~6ms worth of counter ticks

            for(;;)
            {
                LARGE_INTEGER iNow;
                QueryPerformanceCounter(&iNow);
                const LONGLONG iRemaining = iDeadlineTicks - iNow.QuadPart;
                if(iRemaining <= 0)
                {
                    return;
                }

                if(iRemaining > iSpinTicks) //More than ~6ms to go: yield instead of busy-spinning
                {
                    Sleep(1);
                }
                else
                {
                    YieldProcessor(); //Hint to the CPU that this is a spin-wait, so the core isn't hammered
                }
            }
        }

    private:
        static const UINT sm_uTimerPeriodMs = 1;

        bool m_bTimerPeriodSet = false;
    };
}

void CLauncher::MainLoop(UEngine* const pEngine)
{
    assert(pEngine);

    if(m_iPerfCounterFreq.QuadPart <= 0) //QueryPerformanceFrequency failed in the constructor; can't pace frames
    {
        GLog->Log(L"Main loop aborted: no usable performance counter.");
        return;
    }

    LARGE_INTEGER iOldTime;
    if(!QueryPerformanceCounter(&iOldTime)) //Initial time
    {
        return;
    }

    const CFrameLimiter FrameLimiter; //Owns the timer resources; a destructor, so an engine exception can't leak them

    LARGE_INTEGER iSecondStart = iOldTime;
    int iTickCount = 0;
    CLevelWatcher LevelWatcher;

    while (GIsRunning && !GIsRequestingExit)
    {
        LARGE_INTEGER iTime;
        QueryPerformanceCounter(&iTime);

        //Clamped: a stall (a level load, a debugger break, a resume from sleep) would otherwise hand the actors
        //seconds of movement in a single tick, which sends them through walls. Better to briefly run slow instead.
        constexpr float fMaxDeltaTime = 0.2f;
        float fDeltaTime = (iTime.QuadPart - iOldTime.QuadPart) / static_cast<float>(m_iPerfCounterFreq.QuadPart);
        fDeltaTime = std::min(std::max(fDeltaTime, 0.0f), fMaxDeltaTime); //Also covers a counter that jumped backwards
        iOldTime = iTime;

        //Tick (and render) every iteration with the real elapsed time, like the stock UnEngineWin.h loop; the frame rate is capped by sleeping at the bottom of the loop.
        pEngine->Tick(fDeltaTime);
        if(GWindowManager)
        {
            GWindowManager->Tick(fDeltaTime);
        }

        LevelWatcher.Update(pEngine); //Map transitions happen inside Tick(), so this reports them right after

        iTickCount++;
        const float fSinceSecondStart = (iTime.QuadPart - iSecondStart.QuadPart) / static_cast<float>(m_iPerfCounterFreq.QuadPart);
        if(fSinceSecondStart > 1.0f)
        {
            pEngine->CurrentTickRate = static_cast<float>(iTickCount) / fSinceSecondStart;
            iSecondStart = iTime;
            iTickCount = 0;
        }

        //Re-fetch the viewport each frame: a video-mode change can swap the viewport object.
        if(pEngine->Client && pEngine->Client->Viewports.Num() > 0)
        {
            m_pViewPort = pEngine->Client->Viewports(0);
        }
        else
        {
            m_pViewPort = nullptr;
        }

        //WinDrv recreates the viewport window on a video-mode change, so re-attach (focus tracking, raw input, subclass) when the handle changes.
        if(m_pViewPort)
        {
            const HWND hViewportWindow = static_cast<HWND>(m_pViewPort->GetWindow());
            if(hViewportWindow != NULL && hViewportWindow != m_hWnd)
            {
                DetachViewportSubclass();
                m_hWnd = hViewportWindow;
                if(m_bRawInput)
                {
                    RegisterRawInput(m_hWnd);
                }
            }
            AttachViewportSubclass(); //Not necessarily possible right away, so retry until it is
        }

        //GetCursorPos fails while another desktop is active (UAC prompt, locked workstation), leaving the point unset
        POINT CursorPos = {};
        const bool bHaveCursorPos = GetCursorPos(&CursorPos) != FALSE;
        const bool bMouseOverWindow = bHaveCursorPos && WindowFromPoint(CursorPos) == m_hWnd;
        const HWND hForeground = GetForegroundWindow();
        const bool bHasFocus = m_hWnd != NULL && hForeground == m_hWnd;

        //A key held while focus goes away never gets its release message, so the engine keeps it down: alt+tab alone
        //leaves alt stuck. Clear the input state on both edges, so nothing is held while away and nothing is stuck on return.
        if(bHasFocus != m_bPrevHasFocus)
        {
            m_bPrevHasFocus = bHasFocus;
            if(m_pViewPort && m_pViewPort->Input)
            {
                m_pViewPort->Input->ResetInput();
            }
        }

        //A renderer can leave the game stuck minimized after alt+tabbing back into fullscreen. Restore when our process
        //owns the foreground but the viewport is still iconic, so we don't fight an intentional alt+tab away.
        if(m_hWnd != NULL && IsIconic(m_hWnd))
        {
            DWORD dwForegroundPid = 0;
            GetWindowThreadProcessId(hForeground, &dwForegroundPid);
            if(dwForegroundPid == GetCurrentProcessId())
            {
                ShowWindow(m_hWnd, SW_RESTORE);
            }
        }

        RECT rClientArea;
        if(m_pViewPort && m_hWnd != NULL && GetClientRect(m_hWnd, &rClientArea)) //Fails once the window is gone, leaving the rect unset
        {
            std::array<POINT, 2> ClientPoints = { { {rClientArea.left, rClientArea.top}, {rClientArea.right, rClientArea.bottom} } };
            MapWindowPoints(m_hWnd, NULL, ClientPoints.data(), ClientPoints.size());
            const RECT rClientScreen = { ClientPoints[0].x, ClientPoints[0].y, ClientPoints[1].x, ClientPoints[1].y };

            //Actor, and its root window, are briefly absent during level transitions and viewport re-creation
            const APlayerPawnExt* const pPlayer = static_cast<APlayerPawnExt*>(m_pViewPort->Actor);
            XRootWindow* const pRoot = pPlayer ? static_cast<XRootWindow*>(pPlayer->rootWindow) : nullptr;

            //PeekMessage() doesn't get WM_SIZE
            //Default/desired FOV check is so we don't change FOV while zoomed in
            //A mode change can briefly report an empty viewport; calculating an FOV for it would divide by zero
            if (m_bAutoFov && pPlayer && pPlayer->DesiredFOV == pPlayer->DefaultFOV && m_pViewPort->SizeX > 0 && m_pViewPort->SizeY > 0)
            {
                const size_t iSizeX = static_cast<size_t>(m_pViewPort->SizeX);
                const size_t iSizeY = static_cast<size_t>(m_pViewPort->SizeY);

                if(m_iSizeX != iSizeX  || m_iSizeY != iSizeY)
                {
                    ApplyAutoFOV(iSizeX, iSizeY);
                }
            }
            
            //Raw input means WM_MOUSEMOVE is swallowed below, so the engine's own cursor centering
            //(SetMouseCapture) never happens; the menu cursor is driven with SetCursorPos()/ClipCursor() here
            //instead. Menu input itself uses relative deltas (MouseDelta, not MousePosition), so it doesn't care.

            const bool bInMenu = pRoot && pRoot->IsMouseGrabbed() != 0;

            //Confine the OS cursor to the game's client area while we own the mouse.
            if (m_bRawInput && bHasFocus)
            {
                if (m_pViewPort->IsFullscreen() && bInMenu && !m_bPrevInMenu) //Fixes that in fullscreen mode, windows mouse cursor pos isn't matched to DX menu cursor
                {
                    float fRootX, fRootY;
                    pRoot->GetRootCursorPos(&fRootX, &fRootY);

                    POINT p;
                    if (fRootX <= 0.0f && fRootY <= 0.0f)
                    {
                        //A level switch gives the player a new root window, whose cursor sits at the origin until the game
                        //moves it. Following it would park the mouse in the top-left corner, so center both cursors instead.
                        p.x = (rClientArea.left + rClientArea.right) / 2;
                        p.y = (rClientArea.top + rClientArea.bottom) / 2;
                        pEngine->MousePosition(m_pViewPort, 0, static_cast<float>(p.x), static_cast<float>(p.y));
                    }
                    else
                    {
                        const INT iGUIScale = GetGUIScale(*pRoot);
                        p.x = static_cast<int>(fRootX * iGUIScale);
                        p.y = static_cast<int>(fRootY * iGUIScale);
                    }
                    ClientToScreen(m_hWnd, &p);
                    SetCursorPos(p.x, p.y);
                }

                if (m_pViewPort->IsFullscreen())
                {
                    //Exclusive fullscreen: confine to the whole monitor (the client-rect mapping is unreliable here).
                    MONITORINFO MonitorInfo = { sizeof(MonitorInfo) };
                    if (GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &MonitorInfo))
                    {
                        ClipCursor(&MonitorInfo.rcMonitor);
                    }
                    else
                    {
                        ClipCursor(&rClientScreen);
                    }
                    m_bCursorClipped = true;
                }
                //Borderless also covers the whole monitor, so confine in menu mode too. A plain window only confines
                //during camera control, so the user can still reach the border/title bar while a menu is open.
                else if (m_bInBorderlessFullscreenWindow || !bInMenu)
                {
                    ClipCursor(&rClientScreen);
                    m_bCursorClipped = true;
                }
                else
                {
                    ClipCursor(NULL);
                    m_bCursorClipped = false;
                }
            }
            else if (m_bCursorClipped) //Don't leave the cursor confined behind whichever window took focus
            {
                ClipCursor(NULL);
                m_bCursorClipped = false;
            }
            m_bPrevInMenu = bInMenu;

            const bool bMouseInClientRect = bHaveCursorPos && PtInRect(&rClientScreen, CursorPos)!=0; //This makes sure resize cursor isn't hidden
            const bool bCaptured = GetCapture() == m_hWnd;
            //Only hide the cursor while the game owns focus, else it vanishes over the game area behind a focused tool window
            SetCursorHidden(bHasFocus && bMouseInClientRect && (bMouseOverWindow || bCaptured));
        }

        MSG Msg;
        while (PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            bool bSkipMessage = false;

            switch (Msg.message)
            {
            case WM_QUIT:
                GIsRequestingExit = 1;
                break;

            case WM_MOUSEMOVE:
                if (m_pViewPort && m_bRawInput)
                {
                    if (bMouseOverWindow) //Because preferences window defers mousemove calls to us, somehow
                    {
                        const int iXPos = GET_X_LPARAM(Msg.lParam);
                        const int iYPos = GET_Y_LPARAM(Msg.lParam);
                        pEngine->MousePosition(m_pViewPort, 0, static_cast<float>(iXPos), static_cast<float>(iYPos));
                    }
                    bSkipMessage = true;
                }
                break;

            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
                //After alt+tab
                if (Msg.hwnd == m_hWnd && !bHasFocus)
                {
                    bSkipMessage = true;
                }
                break;

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                //Only for the game window: alt+enter in the log window shouldn't restyle the viewport
                if (m_bBorderlessFullscreenWindow && Msg.hwnd == m_hWnd && Msg.wParam == VK_RETURN && (HIWORD(Msg.lParam) & KF_ALTDOWN)) //User hits alt+enter
                {
                    ToggleBorderlessWindowedFullscreen();
                    bSkipMessage = true;
                }
                break;


            case WM_INPUT:
            {
                if (m_pViewPort && bHasFocus)
                {
                    RAWINPUT raw;
                    UINT rawSize = sizeof(raw);
                    //On failure this returns (UINT)-1 and leaves 'raw' untouched, so the deltas below would be stack garbage
                    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(Msg.lParam), RID_INPUT, &raw, &rawSize, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) || raw.header.dwType != RIM_TYPEMOUSE)
                    {
                        break;
                    }

                    const float fDeltaX = static_cast<float>(raw.data.mouse.lLastX);
                    const float fDeltaY = static_cast<float>(raw.data.mouse.lLastY);
                    if(fDeltaX != 0.0f)
                    {
                        pEngine->InputEvent(m_pViewPort, EInputKey::IK_MouseX, EInputAction::IST_Axis, fDeltaX);
                    }
                    if(fDeltaY != 0.0f)
                    {
                        pEngine->InputEvent(m_pViewPort, EInputKey::IK_MouseY, EInputAction::IST_Axis, -fDeltaY);
                    }

                    if (raw.data.mouse.ulButtons & RI_MOUSE_BUTTON_4_DOWN)
                    {
                        pEngine->InputEvent(m_pViewPort, EInputKey::IK_Unknown05, EInputAction::IST_Press);
                    }
                    if (raw.data.mouse.ulButtons & RI_MOUSE_BUTTON_4_UP)
                    {
                        pEngine->InputEvent(m_pViewPort, EInputKey::IK_Unknown05, EInputAction::IST_Release);
                    }

                    if (raw.data.mouse.ulButtons & RI_MOUSE_BUTTON_5_DOWN)
                    {
                        pEngine->InputEvent(m_pViewPort, EInputKey::IK_Unknown06, EInputAction::IST_Press);
                    }
                    if (raw.data.mouse.ulButtons & RI_MOUSE_BUTTON_5_UP)
                    {
                        pEngine->InputEvent(m_pViewPort, EInputKey::IK_Unknown06, EInputAction::IST_Release);
                    }

                    bSkipMessage = true;
                }
            }
                break;
            }

            if(!bSkipMessage)
            {
                TranslateMessage(&Msg);
                DispatchMessage(&Msg);
            }
        }

        if(m_hWnd != NULL && !IsWindow(m_hWnd)) //Force window close handling
        {
            ReleaseCursor();
            m_pViewPort = nullptr;
            m_hWnd = NULL;
            m_bViewportSubclassed = false; //Went away with the window (our subclass proc drops it on WM_NCDESTROY)
        }

        //When the last property window closes, re-enter the fullscreen we dropped for it: that forces WinDrv's full
        //input re-init, the only thing that reliably restores keyboard/mouse routing after it stole focus mid-switch.
        const bool bToolWindowOpen = HasOpenToolWindow();
        if(m_bPrevToolWindowOpen && !bToolWindowOpen && m_hWnd != NULL)
        {
            SetForegroundWindow(m_hWnd);
        }
        m_bPrevToolWindowOpen = bToolWindowOpen;

        //Not tied to the frame the window closed on: a mode change can leave the viewport briefly absent, and losing
        //the switch to that would strand the game in windowed mode for the rest of the session.
        if(!bToolWindowOpen && m_bRestoreFullscreenOnToolClose && m_hWnd != NULL && m_pViewPort)
        {
            if(!m_pViewPort->IsFullscreen()) //The user can have gone back to fullscreen themselves in the meantime
            {
                m_pViewPort->Exec(TEXT("ToggleFullscreen"));
            }
            m_bRestoreFullscreenOnToolClose = false;
        }

        //Cap the frame rate by waiting out the rest of the frame's period. Cap = min(user FPSLimit, engine
        //GetMaxTickRate()); 0 = unlimited. Paced from the frame start (iTime) so the next delta lands on one period, no drift.
        if(!GIsRequestingExit)
        {
            const float fEngineMaxTickRate = pEngine->GetMaxTickRate();
            float fMaxFPS = m_fFPSLimit; //Already clamped to >= 0 in LoadSettings
            if(fEngineMaxTickRate > 0.0f && (fMaxFPS == 0.0f || fEngineMaxTickRate < fMaxFPS))
            {
                fMaxFPS = fEngineMaxTickRate;
            }
            if(fMaxFPS > 0.0f)
            {
                const LONGLONG iPeriodTicks = static_cast<LONGLONG>(m_iPerfCounterFreq.QuadPart / fMaxFPS);
                FrameLimiter.WaitUntil(iTime.QuadPart + iPeriodTicks, m_iPerfCounterFreq.QuadPart);
            }
        }
    }

    ReleaseCursor();
}

void CLauncher::SetCursorHidden(const bool bHide)
{
    //ShowCursor keeps a display counter, so only step it on an actual change: it stays at -1 (hidden) or 0 (shown)
    const int iTarget = bHide ? -1 : 0;
    constexpr int iMaxSteps = 32;
    int iCount = ShowCursor(bHide ? FALSE : TRUE);
    for(int i = 0; iCount != iTarget && i < iMaxSteps; i++)
    {
        iCount = ShowCursor(iCount < iTarget ? TRUE : FALSE);
    }
}

void CLauncher::ReleaseCursor()
{
    if(m_bCursorClipped)
    {
        ClipCursor(NULL);
        m_bCursorClipped = false;
    }
    SetCursorHidden(false);
}

void CLauncher::LoadSettings()
{
    assert(GConfig);
    int iFPSLimit = static_cast<int>(m_fFPSLimit);
    GConfig->GetInt(PROJECTNAME, L"FPSLimit", iFPSLimit);
    if(iFPSLimit < 0) //A negative/malformed ini value would otherwise disable the limit entirely
    {
        iFPSLimit = 0;
    }
    m_fFPSLimit = static_cast<float>(iFPSLimit);

    GConfig->GetBool(PROJECTNAME, L"RawInput", m_bRawInput);
    GConfig->GetBool(PROJECTNAME, L"UseAutoFOV", m_bAutoFov);
    GConfig->GetBool(PROJECTNAME, L"BorderlessFullscreenWindow", m_bBorderlessFullscreenWindow);
    GConfig->GetBool(PROJECTNAME, L"BorderlessFullscreenWindowAllMonitors", m_bBorderlessFullscreenWindowUseAllMonitors);
    GConfig->GetBool(PROJECTNAME, L"UseSingleCPU", m_bUseSingleCPU);

    //Galaxy defaults DirectSound to on when the ini has no entry, so write an explicit 'off' instead
    UBOOL bUseDirectSound = FALSE;
    if(!GConfig->GetBool(L"Galaxy.GalaxyAudioSubsystem", L"UseDirectSound", bUseDirectSound))
    {
        GConfig->SetBool(L"Galaxy.GalaxyAudioSubsystem", L"UseDirectSound", FALSE);
    }
}

void CLauncher::ToggleBorderlessWindowedFullscreen()
{
    if(m_hWnd == NULL) //No viewport window (dedicated server, or it's already gone): nothing to restyle, and the mode flag must not flip out of sync
    {
        return;
    }
    Misc::SetBorderlessFullscreen(m_hWnd, m_bInBorderlessFullscreenWindow ? Misc::BorderlessFullscreenMode::NONE : m_bBorderlessFullscreenWindowUseAllMonitors ? Misc::BorderlessFullscreenMode::ALL_MONITORS : Misc::BorderlessFullscreenMode::CURRENT_MONITOR);
    m_bInBorderlessFullscreenWindow = !m_bInBorderlessFullscreenWindow;
}

namespace
{
    bool IsAddressInDirect3DModule(const LONG_PTR pfnAddress)
    {
        if(pfnAddress == 0)
        {
            return false;
        }

        HMODULE hModule = NULL;
        if(GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<const wchar_t*>(pfnAddress), &hModule) == FALSE)
        {
            return false;
        }

        wchar_t szModulePath[MAX_PATH];
        if(GetModuleFileName(hModule, szModulePath, ARRAYSIZE(szModulePath)) == 0)
        {
            return false;
        }

        //Covers the wrappers renderers ship as well, as those take over the same file names
        const wchar_t* const pszModuleName = PathFindFileName(szModulePath);
        return _wcsnicmp(pszModuleName, L"d3d", 3) == 0 || _wcsicmp(pszModuleName, L"ddraw.dll") == 0 || _wcsicmp(pszModuleName, L"dxgi.dll") == 0;
    }

    //Direct3D subclasses the focus window of a full-screen device with its own window procedure, and drops that hook
    //again when the device is destroyed. The hook remembers the procedure it found at device creation, so anything
    //that subclasses on top of it survives the device: our chain then still calls the hook, which dereferences the
    //freed device and faults inside d3d9.dll on the first message after a mode change. Never subclass over such a
    //hook; Direct3D layering itself on top of us afterwards is fine, as it unhooks cleanly.
    bool IsWindowProcOwnedByDirect3D(const HWND hWnd)
    {
        //Whichever of the two doesn't match how the window's class was registered returns an internal handle rather
        //than an address, which simply resolves to no module at all
        return IsAddressInDirect3DModule(GetWindowLongPtrW(hWnd, GWLP_WNDPROC)) || IsAddressInDirect3DModule(GetWindowLongPtrA(hWnd, GWLP_WNDPROC));
    }
}

void CLauncher::AttachViewportSubclass()
{
    if(m_bViewportSubclassed || m_hWnd == NULL)
    {
        return;
    }

    if(IsWindowProcOwnedByDirect3D(m_hWnd)) //Would outlive the render device and crash the game on the next mode change
    {
        return;
    }

    m_bViewportSubclassed = SetWindowSubclass(m_hWnd, &CLauncher::ViewportSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
}

void CLauncher::DetachViewportSubclass()
{
    if(m_bViewportSubclassed && m_hWnd != NULL && IsWindow(m_hWnd))
    {
        RemoveWindowSubclass(m_hWnd, &CLauncher::ViewportSubclassProc, 0);
    }
    m_bViewportSubclassed = false;
}

//Subclass proc for the WinDrv viewport window. Runs ahead of WinDrv's own proc, which we chain to via DefSubclassProc.
LRESULT CALLBACK CLauncher::ViewportSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_MOUSEACTIVATE:
        //Do not fire when regaining focus
        if (LOWORD(lParam) == HTCLIENT)
        {
            return MA_ACTIVATEANDEAT;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && IsIconic(hWnd))
        {
            ShowWindow(hWnd, SW_RESTORE);
        }
        break;

    case WM_NCDESTROY:
        //The launcher detaches in its destructor, so it is still around to be told the subclass is gone with the window
        if (CLauncher* const pThis = reinterpret_cast<CLauncher*>(dwRefData))
        {
            pThis->m_bViewportSubclassed = false;
        }
        RemoveWindowSubclass(hWnd, &CLauncher::ViewportSubclassProc, uIdSubclass);
        break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

UBOOL CLauncher::Exec(const TCHAR * Cmd, FOutputDevice & Ar)
{
    if (ParseCommand(&Cmd, TEXT("ToggleFullScreen")))
    {
        assert(m_pViewPort);
        if (m_bBorderlessFullscreenWindow) //In borderless mode, prevent switch to 'real' fullscreen
        {
            ToggleBorderlessWindowedFullscreen();

            return TRUE;
        }

        return FALSE;
    }
    else if (ParseCommand(&Cmd, TEXT("SetRes")))
    {
        if (m_bInBorderlessFullscreenWindow) //Block resolution changes in borderless fullscreen mode
        {
            return TRUE;
        }
        return FALSE;
    }
    else
    {
        return FExecHook::Exec(Cmd, Ar);
    }
}
