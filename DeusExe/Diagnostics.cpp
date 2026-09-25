#include "stdafx.h"
#include "Misc.h"
#include "CrashReport.h"
#include "Diagnostics.h"

namespace
{
    const wchar_t* DescribeLevelAction(const BYTE Action)
    {
        switch(Action)
        {
        case LEVACT_None:       return L"None";
        case LEVACT_Loading:    return L"Loading";
        case LEVACT_Saving:     return L"Saving";
        case LEVACT_Connecting: return L"Connecting";
        case LEVACT_Precaching: return L"Precaching";
        default:                return L"Unknown";
        }
    }

    //Both entry points into the error device reach this, and either can be the first: HandleError() runs on its own
    //when the launcher catches an unwind that no appError() started. GIsCriticalError is still zero the first time.
    void LogFatalErrorContext()
    {
        if(GLog == nullptr || GIsCriticalError)
        {
            return;
        }

        wchar_t szContext[2048];
        GLog->Log(NAME_Critical, PROJECTNAME L": fatal error."); //NAME_Critical so the log device flushes it; GIsCriticalError isn't set yet
        GLog->Log(NAME_Critical, CrashReport::GetContext(szContext, _countof(szContext)));
    }
}

void FOutputDeviceFileDeusExe::Serialize(const TCHAR* const Data, const EName Event)
{
    if(!Misc::IsVerboseLogging()) //Stock buffered logging: a timestamp and a disk write per line cost too much to always pay for
    {
        FOutputDeviceFile::Serialize(Data, Event);

        //A crash report still has to reach disk. Nothing closes the log archive once appRequestExit(1) or the
        //unhandled-exception filter ends the process, so a line left in the writer's buffer is simply lost.
        if(LogAr != nullptr && (GIsCriticalError != 0 || Event == NAME_Critical || Event == NAME_Error || Event == NAME_Exit))
        {
            LogAr->Flush();
        }
        return;
    }

    const TCHAR* pszLine = Data;

    wchar_t szTimestamped[8192];
    if(!m_bInSerialize && Data != nullptr && !FName::SafeSuppressed(Event)) //Don't format a line the base class is going to drop
    {
        SYSTEMTIME Time;
        GetLocalTime(&Time);

        //A negative return means it didn't fit: log the line unprefixed rather than truncate a crash report
        if(_snwprintf_s(szTimestamped, _TRUNCATE, L"[%02u:%02u:%02u.%03u] %s", Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds, Data) >= 0)
        {
            pszLine = szTimestamped;
        }
    }

    m_bInSerialize = true;
    FOutputDeviceFile::Serialize(pszLine, Event);
    m_bInSerialize = false;

    if(LogAr != nullptr)
    {
        LogAr->Flush(); //Stock buffers 4 KB, i.e. it drops precisely the lines that explain a crash
    }
}

void FOutputDeviceErrorDeusExe::Serialize(const TCHAR* const Msg, const EName Event)
{
    //Log what we know before the base class shuts the object system down and the context becomes unreadable
    LogFatalErrorContext();

    FOutputDeviceWindowsError::Serialize(Msg, Event);
}

void FOutputDeviceErrorDeusExe::HandleError()
{
    LogFatalErrorContext();

    //The unwound engine call history is the most useful part of a crash and stock only shows it in a message box
    if(GLog != nullptr && GErrorHist[0] != 0)
    {
        GLog->Log(NAME_Critical, PROJECTNAME L": engine error history follows.");
        GLog->Log(NAME_Critical, GErrorHist); //Not Logf: the history can fill Core's 4096 character format buffer on its own
    }

    CrashReport::LogFault(nullptr); //Names the objects the history couldn't, from the fault the guard chain swallowed

    FOutputDeviceWindowsError::HandleError();
}

void FFeedbackContextDeusExe::Serialize(const TCHAR* const V, const EName Event)
{
    if(Event == NAME_UserPrompt && GLog != nullptr) //Stock only puts these in a message box
    {
        GLog->Logf(PROJECTNAME L": user prompt: %s", V);
    }

    FFeedbackContextWindows::Serialize(V, Event);
}

void FFeedbackContextDeusExe::BeginSlowTask(const TCHAR* const Task, const UBOOL StatusWindow, const UBOOL Cancelable)
{
    if(GLog != nullptr)
    {
        GLog->Logf(PROJECTNAME L": task started: %s", Task != nullptr ? Task : L"<unnamed>");
    }
    CrashReport::SetPhase(Task);
    m_szLastStatus[0] = 0;

    FFeedbackContextWindows::BeginSlowTask(Task, StatusWindow, Cancelable);
}

void FFeedbackContextDeusExe::EndSlowTask()
{
    if(GLog != nullptr)
    {
        GLog->Log(PROJECTNAME L": task finished.");
    }
    CrashReport::SetPhase(nullptr);

    FFeedbackContextWindows::EndSlowTask();
}

UBOOL FFeedbackContextDeusExe::StatusUpdatef(const INT Numerator, const INT Denominator, const TCHAR* Fmt, ...)
{
    TCHAR szText[4096];
    GET_VARARGS(szText, ARRAY_COUNT(szText), Fmt);

    //Called once per serialized object, so only a change of text marks progress worth a line
    if(wcsncmp(szText, m_szLastStatus, _countof(m_szLastStatus) - 1) != 0)
    {
        wcsncpy_s(m_szLastStatus, szText, _TRUNCATE);
        if(GLog != nullptr)
        {
            GLog->Logf(PROJECTNAME L": task progress: %s", szText);
        }
        CrashReport::SetPhase(szText);
    }

    return FFeedbackContextWindows::StatusUpdatef(Numerator, Denominator, L"%s", szText);
}

void CLevelWatcher::Update(UEngine* const pEngine)
{
    UGameEngine* const pGameEngine = Cast<UGameEngine>(pEngine);
    ULevel* const pLevel = pGameEngine != nullptr ? pGameEngine->GLevel : nullptr;

    if(pLevel != m_pLevel)
    {
        const ULONGLONG iNow = GetTickCount64();

        wchar_t szMap[_countof(m_szMap)] = L"<none>";
        if(pLevel != nullptr)
        {
            wcsncpy_s(szMap, *pLevel->URL.Map, _TRUNCATE);
        }

        if(GLog != nullptr)
        {
            GLog->Logf(PROJECTNAME L": level changed from '%s' to '%s' (%i actors, %.2f s since the last).",
                m_szMap[0] != 0 ? m_szMap : L"<none>", szMap,
                pLevel != nullptr ? pLevel->Actors.Num() : 0,
                m_iLastChangeTicks != 0 ? (iNow - m_iLastChangeTicks) / 1000.0 : 0.0);
        }

        wcsncpy_s(m_szMap, szMap, _TRUNCATE);
        m_pLevel = pLevel;
        m_iLastChangeTicks = iNow;
        CrashReport::SetMap(szMap);
    }

    //Actors(0) is the level info; it's absent while a level is being built
    ALevelInfo* const pLevelInfo = (pLevel != nullptr && pLevel->Actors.Num() > 0) ? Cast<ALevelInfo>(pLevel->Actors(0)) : nullptr;
    if(pLevelInfo == nullptr)
    {
        return;
    }

    if(pLevelInfo->LevelAction != m_LevelAction)
    {
        if(GLog != nullptr)
        {
            GLog->Logf(PROJECTNAME L": level action %s -> %s.", DescribeLevelAction(m_LevelAction), DescribeLevelAction(pLevelInfo->LevelAction));
        }
        m_LevelAction = pLevelInfo->LevelAction;
    }

    //Set a frame or more before the engine actually browses, so this is the last thing logged before a transition
    if(wcsncmp(*pLevelInfo->NextURL, m_szNextURL, _countof(m_szNextURL) - 1) != 0)
    {
        wcsncpy_s(m_szNextURL, *pLevelInfo->NextURL, _TRUNCATE);
        if(GLog != nullptr && m_szNextURL[0] != 0)
        {
            GLog->Logf(PROJECTNAME L": travel queued to '%s' (carry items: %i, in %.2f s).",
                m_szNextURL, pLevelInfo->bNextItems != 0 ? 1 : 0, pLevelInfo->NextSwitchCountdown);
        }
    }
}
