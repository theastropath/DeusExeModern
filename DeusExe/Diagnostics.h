#pragma once

//Diagnostic replacements for Core's stock log/error/feedback devices, aimed at crashes inside the engine
//(map transitions in particular). The stock log archive buffers 4 KB before it touches disk, so the lines
//leading up to a crash are exactly the ones that get lost.

//Timestamps every line and flushes it to disk immediately.
class FOutputDeviceFileDeusExe : public FOutputDeviceFile
{
public:
    void Serialize(const TCHAR* Data, EName Event) override;

private:
    bool m_bInSerialize = false; //The base class logs its own "log file open" line from inside our Serialize
};

//Writes the engine's unwound call history to the log; stock only puts it in a message box.
class FOutputDeviceErrorDeusExe : public FOutputDeviceWindowsError
{
public:
    void Serialize(const TCHAR* Msg, EName Event) override;
    void HandleError() override;
};

//Logs the engine's slow-task progress, which is how far a map load got before it died.
class FFeedbackContextDeusExe : public FFeedbackContextWindows
{
public:
    void Serialize(const TCHAR* V, EName Event) override;
    void BeginSlowTask(const TCHAR* Task, UBOOL StatusWindow, UBOOL Cancelable) override;
    void EndSlowTask() override;
    UBOOL VARARGS StatusUpdatef(INT Numerator, INT Denominator, const TCHAR* Fmt, ...) override;

private:
    wchar_t m_szLastStatus[256] = {};
};

//Logs map/level transitions. The transition itself happens inside a single Tick(), so this only sees before
//and after; the queued-travel and level-action lines are what show up while the switch is still pending.
class CLevelWatcher
{
public:
    void Update(UEngine* const pEngine);

private:
    const void* m_pLevel = nullptr; //Identity only: the level can be garbage collected, so this is never dereferenced
    ULONGLONG m_iLastChangeTicks = 0;
    wchar_t m_szMap[128] = {};
    wchar_t m_szNextURL[256] = {};
    BYTE m_LevelAction = LEVACT_None;
};
