#include "stdafx.h"
#include "NativeHooks.h"

#include "FreeSpaceFix.h"
#include "SubTitleFix.h"
#include "GUIScalingFix.h"

CNativeHooks* CNativeHooks::sm_pSingleton;

CNativeHooks::CNativeHooks(const wchar_t* const pszIniSection)
{
    assert(!sm_pSingleton);
    sm_pSingleton = this;

    //Could create a fancy system to register factory functions, but this will suffice for now
    for (const auto Factory : { &CFreeSpaceFix::Factory, &CSubtitleFix::Factory, &CGUIScalingFix::Factory })
    {
        Factory(pszIniSection);
    }
}

CNativeHooks::~CNativeHooks()
{
    m_ActiveHooks.clear(); //The hooks restore GNatives as they go; do it while the singleton is still reachable
    sm_pSingleton = nullptr;
}
