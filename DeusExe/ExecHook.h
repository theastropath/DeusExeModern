#pragma once

/**
Basically copied from UnEngineWin.h and cleaned up a little
*/

class FExecHook : public FExec, public FNotifyHook
{
public:
    FExecHook() = default;
    ~FExecHook();
private:
    WConfigProperties* m_pPreferences = nullptr; //Cleaned up by engine
    WObjectProperties* m_pEditActor = nullptr; //Reused across EditActor commands; cleaned up by engine

//From FExec
protected:
    /**
    True while one of the property windows we dropped exclusive fullscreen for is still up. Both of them are tracked,
    so closing one while the other is open doesn't restore fullscreen out from under the one that's left.
    */
    bool HasOpenToolWindow() const;

    bool m_bRestoreFullscreenOnToolClose = false; //We dropped exclusive fullscreen to show a tool window; return to it once they're all closed
    UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar ) override;

private:
    UViewport* GetViewport() const; //!< The first viewport, or null on a dedicated server or before one exists
    UViewport* DropFullscreenForToolWindow(); //!< Leaves exclusive fullscreen so an overlapping tool window can be shown; returns the viewport

//From FNotifyHook
private:
    void NotifyDestroy( void* Src ) override;
};