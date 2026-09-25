#include "stdafx.h"
#include "ExecHook.h"

FExecHook::~FExecHook()
{
    //The engine owns these windows and they can outlive us, so don't leave them calling back into freed memory
    if( m_pPreferences )
    {
        m_pPreferences->SetNotifyHook( nullptr );
    }
    if( m_pEditActor )
    {
        m_pEditActor->SetNotifyHook( nullptr );
    }
}

void FExecHook::NotifyDestroy( void* Src )
{
    if( Src==m_pPreferences )
    {
        m_pPreferences = nullptr;
    }
    else if( Src==m_pEditActor )
    {
        m_pEditActor = nullptr;
    }
}

bool FExecHook::HasOpenToolWindow() const
{
    //The window is destroyed as soon as it's closed, while NotifyDestroy only arrives once the engine gets around to
    //deleting the object, so the handle is what says whether the window is actually still on screen.
    return ( m_pPreferences && IsWindow( m_pPreferences->hWnd ) ) || ( m_pEditActor && IsWindow( m_pEditActor->hWnd ) );
}

UViewport* FExecHook::GetViewport() const
{
    TObjectIterator<UEngine> EngineIt;
    return EngineIt && EngineIt->Client && EngineIt->Client->Viewports.Num() ? EngineIt->Client->Viewports(0) : nullptr;
}

UViewport* FExecHook::DropFullscreenForToolWindow()
{
    //Exclusive fullscreen can't show an overlapping tool window (DirectDraw minimizes the game on focus loss), so drop to windowed first.
    UViewport* const Viewport = GetViewport();
    if( Viewport && Viewport->IsFullscreen() )
    {
        Viewport->Exec( TEXT("ToggleFullscreen") );
        m_bRestoreFullscreenOnToolClose = true; //Only ever latched here; the main loop clears it once the windows are closed
    }
    return Viewport;
}

UBOOL FExecHook::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
    if( ParseCommand(&Cmd,TEXT("ShowLog")) )
    {
        if( GLogWindow )
        {
            GLogWindow->Show(1);
            SetFocus( *GLogWindow );
            GLogWindow->Display.ScrollCaret();
        }
        return TRUE;
    }
    else if( ParseCommand(&Cmd,TEXT("TakeFocus")) )
    {
        TObjectIterator<UEngine> EngineIt;
        if( EngineIt && EngineIt->Client && EngineIt->Client->Viewports.Num() )
        {
            SetForegroundWindow( (HWND)EngineIt->Client->Viewports(0)->GetWindow() );
        }
        return TRUE;
    }
    else if( ParseCommand(&Cmd,TEXT("EditActor")) )
    {
        UClass* Class;
        FName ActorName;
        TObjectIterator<UEngine> EngineIt;
        const AActor* Found = NULL;
        if( EngineIt && ParseObject<UClass>( Cmd, TEXT("Class="), Class, ANY_PACKAGE ) )
        {
            const AActor* Player  = EngineIt->Client && EngineIt->Client->Viewports.Num() ? EngineIt->Client->Viewports(0)->Actor : NULL;
            FLOAT   MinDist = 999999.0f;
            for( TObjectIterator<AActor> It; It; ++It )
            {
                FLOAT Dist = Player ? FDist(It->Location,Player->Location) : 0.0f;
                if( (!Player || It->GetLevel()==Player->GetLevel()) &&  (!It->bDeleteMe) && (It->IsA( Class) ) && (Dist<MinDist) )
                {
                    MinDist = Dist;
                    Found   = *It;
                }
            }
        }
        else if( EngineIt && Parse( Cmd, TEXT("Name="), ActorName ) )
        {
            for( TObjectIterator<AActor> It; It; ++It )
            {
                if( !It->bDeleteMe && It->GetName()==*ActorName )
                {
                    Found = *It;
                    break;
                }
            }
        }
        if( Found )
        {
            UViewport* const Viewport = DropFullscreenForToolWindow();
            if( !m_pEditActor ) //Reuse it: a second window would orphan the first and take over the tool window tracking below
            {
                m_pEditActor = new WObjectProperties( TEXT("EditActor"), 0, TEXT(""), NULL, 1 );
                m_pEditActor->SetNotifyHook( this );
                m_pEditActor->OpenWindow( Viewport ? (HWND)Viewport->GetWindow() : NULL );
            }
            m_pEditActor->Root.SetObjects( (UObject**)&Found, 1 );
            m_pEditActor->Show(1);
        }
        else
        {
            Ar.Logf( TEXT("Bad or missing class or name") );
        }
        return TRUE;
    }
    else if( ParseCommand(&Cmd,TEXT("HideLog")) )
    {
        if( GLogWindow )
        {
            GLogWindow->Show(0);
        }
        return TRUE;
    }
    else if( ParseCommand(&Cmd,TEXT("Preferences")) )
    {
        //Stock only allowed this on a dedicated server, as an overlapping window can't be shown over exclusive
        //fullscreen; with the same drop-to-windowed treatment EditActor gets, it works in the client too.
        UViewport* const Viewport = DropFullscreenForToolWindow();
        if( !m_pPreferences )
        {
            m_pPreferences = new WConfigProperties( TEXT("Preferences"), LocalizeGeneral("AdvancedOptionsTitle",TEXT("Window")) );
            m_pPreferences->SetNotifyHook( this );
            m_pPreferences->OpenWindow( Viewport ? (HWND)Viewport->GetWindow() : GLogWindow ? GLogWindow->hWnd : NULL );
            m_pPreferences->ForceRefresh();
        }
        assert(m_pPreferences);
        m_pPreferences->Show(TRUE);
        SetFocus( *m_pPreferences );
        return TRUE;
    }
    return FALSE;
}