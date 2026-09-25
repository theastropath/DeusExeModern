#pragma once
#include "NativeHooks.h"


class CGUIScalingFix : public CNativeHooks::CFixBaseT<CGUIScalingFix, APlayerPawnExt, EXTENSION_PreRenderWindows>
{
public:
    static const wchar_t* const sm_pszConfigString;
    static const INT sm_iMaxScale = 5; //Matches what the configuration dialog offers

    static void Factory(const wchar_t* const pszIniSection)
    {
        INT i = 0;
        GConfig->GetInt(pszIniSection, sm_pszConfigString, i);
        if (i > 0) //A hand-edited negative value would scale the UI by a negative multiplier
        {
            new CGUIScalingFix(i > sm_iMaxScale ? sm_iMaxScale : i);
        }
    }

    static void ReplacementFunc(const APlayerPawnExt& PlayerPawnThis, CGUIScalingFix& Context, FFrame& Stack, RESULT_DECL);

private:
    explicit CGUIScalingFix(const decltype(XRootWindow::hMultiplier) iScaleAmount);

    const decltype(XRootWindow::hMultiplier) m_iScaleAmount;
    decltype (UCanvas::X) m_iSizeX = 0;
    decltype (UCanvas::Y) m_iSizeY = 0;
};
