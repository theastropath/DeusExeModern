#pragma once

class UObject;
class UStruct;

/**
Crash reporting: the handlers that catch a fault, and the breadcrumbs that give the resulting report its context.

The engine's own guard chain catches hardware faults and rethrows them as C++ exceptions, which loses the registers
and the faulting stack, so a vectored handler records those first-chance and everything here is written to be usable
afterwards - it validates every pointer it follows rather than trusting an already-corrupt process.

Include after stdafx.h so the Unreal/Win32 types are available.
*/
namespace CrashReport
{
    void Setup(); //!< Installs the exception, terminate and invalid-parameter handlers; call before anything else

    void SetScriptTrace(const UObject* const pObject, const UStruct* const pFunction);

    void SetPhase(const wchar_t* const pszPhase); //!< Engine stage (slow task/status update) a crash would be attributed to

    void SetMap(const wchar_t* const pszMap);

    void RecordFileOpen(const wchar_t* const pszPath); //!< Feeds the ring of recently read files a crash report lists; file opens never reach the log

    const wchar_t* GetContext(wchar_t* const pszBuffer, const size_t count); //!< Map, engine stage and script context; safe to call mid-crash

    const wchar_t* FormatScriptContext(wchar_t* const pszBuffer, const size_t count, const UObject* const pObject, const UStruct* const pFunction);

    void LogFault(const EXCEPTION_POINTERS* const pExceptionInfo); //!< Faulting address, registers, call stack and the objects behind them; pass null to use the recorded fault
};
