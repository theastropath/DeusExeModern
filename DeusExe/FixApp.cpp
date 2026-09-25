#include "stdafx.h"
#include "FixApp.h"
#include "Misc.h"
#include "GUIScalingFix.h"
#include "SubTitleFix.h"
#include "resource.h"

bool CFixApp::Show(const HWND hWndParent)
{
    return DialogBoxParam(GetModuleHandle(0),MAKEINTRESOURCE(IDD_FIXAPP),hWndParent,FixAppDialogProc,reinterpret_cast<LPARAM>(this)) == 1;
}

void CFixApp::ReadSettings()
{
    assert(GConfig);

    //Full-screen
    UBOOL bBorderless = TRUE;
    GConfig->GetBool(PROJECTNAME, L"BorderlessFullscreenWindow", bBorderless);

    UBOOL bFullscreen = FALSE;
    if (!bBorderless)
    {
        GConfig->GetBool(L"WinDrv.WindowsClient", L"StartupFullscreen", bFullscreen);
    }

    //Set each radio explicitly: CheckRadioButton unchecks every control in the ID range, which here spans unrelated ones too
    CheckDlgButton(m_hWnd, RADIO_VPBORDERLESS, bBorderless ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hWnd, RADIO_VPFULLSCREEN, !bBorderless && bFullscreen ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hWnd, RADIO_VPWINDOWED, !bBorderless && !bFullscreen ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(m_hWnd, CHK_BORDERLESSALLMONITORS), bBorderless);

    UBOOL bBorderlessAllMonitors = FALSE;
    GConfig->GetBool(PROJECTNAME, L"BorderlessFullscreenWindowAllMonitors", bBorderlessAllMonitors);
    CheckDlgButton(m_hWnd, CHK_BORDERLESSALLMONITORS, bBorderlessAllMonitors);

    //Resolution
    int iResX = 1024;
    GConfig->GetInt(L"WinDrv.WindowsClient", bFullscreen ? L"FullscreenViewportX" : L"WindowedViewportX", iResX);
    int iResY = 768;
    GConfig->GetInt(L"WinDrv.WindowsClient", bFullscreen ? L"FullscreenViewportY" : L"WindowedViewportY", iResY);

    wchar_t szBuffer[20];
    _snwprintf_s(szBuffer, _TRUNCATE, L"%dx%d", iResX, iResY);
    const int iComboRes = ComboBox_FindStringExact(m_hWndCBResolutions, -1, szBuffer);

    const bool bStandardRes = iComboRes != CB_ERR;

    if(bStandardRes) //Can fail if res not supported
    {
        ComboBox_SetCurSel(m_hWndCBResolutions, iComboRes);
    }
    CheckRadioButton(m_hWnd, RADIO_RESCOMMON, RADIO_RESCUSTOM, bStandardRes ? RADIO_RESCOMMON : RADIO_RESCUSTOM);

    SetDlgItemInt(m_hWnd, TXT_RESX, iResX, FALSE);
    SetDlgItemInt(m_hWnd, TXT_RESY, iResY, FALSE);

    EnableDisableResSettings(!bBorderless, bStandardRes);


    //Bit depth
    int iBitDepth = sm_iBPP_32;
    GConfig->GetInt(L"WinDrv.WindowsClient", L"FullscreenColorBits", iBitDepth);
    CheckRadioButton(m_hWnd, RADIO_16BIT, RADIO_32BIT, iBitDepth == sm_iBPP_16 ? RADIO_16BIT : RADIO_32BIT);

    //FOV
    const float fDefaultFOV = Misc::GetDefaultFOV();
    float fFOV = fDefaultFOV;
    GConfig->GetFloat(L"Engine.PlayerPawn", L"DefaultFOV", fFOV, *static_cast<FConfigCacheIni*>(GConfig)->UserIni);
    UBOOL bUseAutoFOV = TRUE;
    GConfig->GetBool(PROJECTNAME, L"UseAutoFOV", bUseAutoFOV);

    const int iFOVRadio = bUseAutoFOV ? RADIO_FOVAUTO : fFOV == fDefaultFOV ? RADIO_FOVDEFAULT : RADIO_FOVCUSTOM;
    CheckRadioButton(m_hWnd, RADIO_FOVDEFAULT, RADIO_FOVCUSTOM, iFOVRadio);
    EnableWindow(m_hWndTxtFOV, iFOVRadio == RADIO_FOVCUSTOM);
    SetDlgItemInt(m_hWnd, TXT_FOV, static_cast<UINT>(fFOV), FALSE);

    //GUI scaling fix
    int iGuiScale = 0;
    GConfig->GetInt(PROJECTNAME, CGUIScalingFix::sm_pszConfigString, iGuiScale);
    if (iGuiScale > CGUIScalingFix::sm_iMaxScale) //The fix clamps a hand-edited value the same way
    {
        iGuiScale = CGUIScalingFix::sm_iMaxScale;
    }
    CheckDlgButton(m_hWnd, CHK_GUIFIX, iGuiScale > 0);
    EnableWindow(m_hWndCBGUIScales, iGuiScale > 0);
    if (iGuiScale > 0)
    {
        ComboBox_SetCurSel(m_hWndCBGUIScales, iGuiScale - 1);
    }

    //Subtitle fix
    BOOL bSubtitleFix = FALSE; //Same default as CSubtitleFix::Factory, else the box claims the fix is on while it isn't
    GConfig->GetBool(PROJECTNAME, L"SubtitleFix", bSubtitleFix);
    CheckDlgButton(m_hWnd, CHK_SUBTITLEFIX, bSubtitleFix);

    //Renderer
    const wchar_t* pszRenderer = GConfig->GetStr(L"Engine.Engine", L"GameRenderDevice");
    if(pszRenderer[0]=='\0')
    {
        pszRenderer = L"SoftDrv.SoftwareRenderDevice";
    }

    //Case-insensitive: the engine treats ini values that way, so a hand-edited 'd3ddrv.d3drenderdevice' has to match too
    const auto IsSameRenderer = [pszRenderer](const std::wstring& Other) { return _wcsicmp(Other.c_str(), pszRenderer) == 0; };
    const auto renderIt = std::find_if(m_Renderers.cbegin(), m_Renderers.cend(), IsSameRenderer);
    if(renderIt != m_Renderers.cend())
    {
        ComboBox_SetCurSel(m_hWndCBRenderers, static_cast<int>(renderIt - m_Renderers.cbegin()));
    }

    //Detail textures
    BOOL bDetailTextures = TRUE;
    GConfig->GetBool(pszRenderer, L"DetailTextures", bDetailTextures);
    CheckDlgButton(m_hWnd,CHK_DETAILTEX, bDetailTextures);

    //Mouse acceleration
    BOOL bNoMouseAccel = TRUE;
    GConfig->GetBool(PROJECTNAME, L"RawInput", bNoMouseAccel);
    CheckDlgButton(m_hWnd,CHK_NOMOUSEACCEL, bNoMouseAccel);

    //DirectSound
    BOOL bDirectSound = FALSE;
    GConfig->GetBool(L"Galaxy.GalaxyAudioSubsystem", L"UseDirectSound", bDirectSound);
    CheckDlgButton(m_hWnd,CHK_DIRECTSOUND, bDirectSound);

    //Audio latency
    int iSndLatency = 40;
    GConfig->GetInt(L"Galaxy.GalaxyAudioSubsystem", L"Latency", iSndLatency);
    SetDlgItemInt(m_hWnd,TXT_LATENCY,iSndLatency,FALSE);

    //FPS limit
    int iFPSLimit = sm_iDefaultFPSLimit;
    GConfig->GetInt(PROJECTNAME, L"FPSLimit", iFPSLimit);
    SetDlgItemInt(m_hWnd, TXT_FPSLIMIT, iFPSLimit, FALSE);

    //Single CPU
    BOOL bUseSingleCPU = FALSE;
    GConfig->GetBool(PROJECTNAME, L"UseSingleCPU", bUseSingleCPU);
    CheckDlgButton(m_hWnd, CHK_USESINGLECPU, bUseSingleCPU);

    //Verbose logging
    BOOL bVerboseLogging = FALSE;
    GConfig->GetBool(PROJECTNAME, L"VerboseLogging", bVerboseLogging);
    CheckDlgButton(m_hWnd, CHK_VERBOSELOGGING, bVerboseLogging);
}

void CFixApp::PopulateDialog()
{
    //Populate the GUI scale combobox
    for (INT i = 1; i <= CGUIScalingFix::sm_iMaxScale; i++)
    {
        wchar_t szBuf[8];
        swprintf_s(szBuf, L"x%d", i);
        ComboBox_AddString(m_hWndCBGUIScales, szBuf);
    }
    ComboBox_SetCurSel(m_hWndCBGUIScales, 0);

    //Populate the resolution combobox
    DEVMODE dm = {};
    dm.dmSize = sizeof(dm);
    for(DWORD iModeNum = 0; EnumDisplaySettings(NULL, iModeNum, &dm) != FALSE; iModeNum++)
    {
        if(dm.dmBitsPerPel != 32) //Don't actually check if it matches the current color depth/refresh rate
        {
            continue;
        }

        const Resolution r = { static_cast<size_t>(dm.dmPelsWidth), static_cast<size_t>(dm.dmPelsHeight) };
        const auto IsSameRes = [&r](const Resolution& Other) { return Other.iX == r.iX && Other.iY == r.iY; };
        if(std::find_if(m_Resolutions.cbegin(), m_Resolutions.cend(), IsSameRes) != m_Resolutions.cend()) //Each resolution is listed once per refresh rate
        {
            continue;
        }
        m_Resolutions.push_back(r);

        wchar_t szBuffer[20];
        _snwprintf_s(szBuffer, _TRUNCATE, L"%Iux%Iu", r.iX, r.iY);
        const int iIndex = ComboBox_AddString(m_hWndCBResolutions, szBuffer);
        if(iIndex >= 0)
        {
            ComboBox_SetItemData(m_hWndCBResolutions, iIndex, &m_Resolutions.back());
        }
    }
    ComboBox_SetCurSel(m_hWndCBResolutions, 0);


    //Renderers (based on UnEngineWin.h), requires appInit() to have been called
    TArray<FRegistryObjectInfo> Classes;

    UObject::GetRegistryObjects( Classes, UClass::StaticClass(), URenderDevice::StaticClass(), 0 );
    for( TArray<FRegistryObjectInfo>::TIterator It(Classes); It; ++It )
    {
        FString Path = It->Object, Left, Right;
        if( Path.Split(L".",&Left,&Right)  )
        {
            const wchar_t* pszDesc = Localize(*Right,L"ClassCaption",*Left);
            assert(pszDesc);
            if(ComboBox_FindStringExact(m_hWndCBRenderers, -1, pszDesc) == CB_ERR)
            {
                //Only track the class once the entry is really in the combo, else the index no longer maps onto m_Renderers
                if(ComboBox_AddString(m_hWndCBRenderers, pszDesc) >= 0)
                {
                    m_Renderers.emplace_back(static_cast<wchar_t*>(Path.GetCharArray().GetData()));
                }
            }
        }
    }

}

void CFixApp::EnableDisableResSettings(const bool bChangesAllowed, const bool bCommon) const
{
    EnableWindow(m_hWndRadioResCommon, bChangesAllowed);
    EnableWindow(m_hWndRadioResCustom, bChangesAllowed);
    EnableWindow(m_hWndCBResolutions, bChangesAllowed && bCommon);
    EnableWindow(m_hWndTxtResX, bChangesAllowed && !bCommon);
    EnableWindow(m_hWndTxtResY, bChangesAllowed && !bCommon);
}

void CFixApp::ApplySettings() const
{
    assert(GConfig);

    //GUI scaling fix
    GConfig->SetInt(PROJECTNAME, L"GUIScalingFix", IsDlgButtonChecked(m_hWnd, CHK_GUIFIX)==0 ? 0 : ComboBox_GetCurSel(m_hWndCBGUIScales)+1);

    //Subtitle fix
    GConfig->SetBool(PROJECTNAME, L"SubtitleFix", IsDlgButtonChecked(m_hWnd, CHK_SUBTITLEFIX) != 0);

    //Bit depth
    const unsigned char iBitDepth = IsDlgButtonChecked(m_hWnd,RADIO_16BIT) ? sm_iBPP_16 : sm_iBPP_32;   
    GConfig->SetInt(L"WinDrv.WindowsClient",L"FullscreenColorBits",iBitDepth);

    //Resolution
    const Resolution* pRes = nullptr;
    if(IsDlgButtonChecked(m_hWnd,RADIO_RESCOMMON))
    {
        const int i = ComboBox_GetCurSel(m_hWndCBResolutions);
        const LRESULT ItemData = i != CB_ERR ? ComboBox_GetItemData(m_hWndCBResolutions, i) : CB_ERR;
        if(ItemData != CB_ERR)
        {
            pRes = reinterpret_cast<const Resolution*>(ItemData);
        }
    }

    size_t iResX;
    size_t iResY;
    if(pRes)
    {
        iResX = pRes->iX;
        iResY = pRes->iY;
    }
    else //Custom resolution, or the list is empty/has nothing selected
    {
        iResX = GetDlgItemInt(m_hWnd, TXT_RESX, nullptr, FALSE);
        iResY = GetDlgItemInt(m_hWnd, TXT_RESY, nullptr, FALSE);
    }

    //An empty field reads as 0, which would leave the game with no viewport at all. Any other value is the user's to pick.
    if(iResX == 0)
    {
        iResX = sm_iFallbackResX;
    }
    if(iResY == 0)
    {
        iResY = sm_iFallbackResY;
    }

    GConfig->SetInt(L"WinDrv.WindowsClient",L"FullscreenViewportX",iResX);
    GConfig->SetInt(L"WinDrv.WindowsClient", L"WindowedViewportX", iResX);
    GConfig->SetInt(L"WinDrv.WindowsClient",L"FullscreenViewportY",iResY);
    GConfig->SetInt(L"WinDrv.WindowsClient", L"WindowedViewportY", iResY);

    //FOV
    const bool bAutoFOV = IsDlgButtonChecked(m_hWnd, RADIO_FOVAUTO) != 0;
    GConfig->SetBool(PROJECTNAME, L"UseAutoFOV", bAutoFOV);
    if(!bAutoFOV) //With auto FOV the launcher recomputes it from the viewport size instead
    {
        const float fFOV = IsDlgButtonChecked(m_hWnd, RADIO_FOVDEFAULT) ? Misc::GetDefaultFOV() : static_cast<float>(GetDlgItemInt(m_hWnd, TXT_FOV, nullptr, FALSE));

        const wchar_t* const pszUserIni = *static_cast<FConfigCacheIni*>(GConfig)->UserIni;
        GConfig->SetFloat(L"Engine.PlayerPawn", L"DesiredFOV", fFOV, pszUserIni);
        GConfig->SetFloat(L"Engine.PlayerPawn", L"DefaultFOV", fFOV, pszUserIni);
    }
    
    //Disable mouse scaling
    GConfig->SetBool(PROJECTNAME,L"RawInput",IsDlgButtonChecked(m_hWnd,CHK_NOMOUSEACCEL)!=0);
    //DirectSound
    GConfig->SetBool(L"Galaxy.GalaxyAudioSubsystem",L"UseDirectSound",IsDlgButtonChecked(m_hWnd,CHK_DIRECTSOUND)!=0);
    //Audio latency
    GConfig->SetInt(L"Galaxy.GalaxyAudioSubsystem", L"Latency", GetDlgItemInt(m_hWnd, TXT_LATENCY, nullptr, FALSE));
    //Full-screen
    GConfig->SetBool(PROJECTNAME, L"BorderlessFullscreenWindow", IsDlgButtonChecked(m_hWnd, RADIO_VPBORDERLESS) != 0);
    GConfig->SetBool(PROJECTNAME, L"BorderlessFullscreenWindowAllMonitors", IsDlgButtonChecked(m_hWnd, CHK_BORDERLESSALLMONITORS) != 0);
    GConfig->SetBool(L"WinDrv.WindowsClient",L"StartupFullscreen",IsDlgButtonChecked(m_hWnd,RADIO_VPFULLSCREEN)!=0);
    //FPS Limit
    GConfig->SetInt(PROJECTNAME, L"FPSLimit", GetDlgItemInt(m_hWnd, TXT_FPSLIMIT, nullptr, FALSE));
    //Single CPU
    GConfig->SetBool(PROJECTNAME, L"UseSingleCPU", IsDlgButtonChecked(m_hWnd, CHK_USESINGLECPU) != 0);
    //Verbose logging. Applied straight away too: the dialog runs before the engine, but after the setting was read.
    const bool bVerboseLogging = IsDlgButtonChecked(m_hWnd, CHK_VERBOSELOGGING) != 0;
    GConfig->SetBool(PROJECTNAME, L"VerboseLogging", bVerboseLogging);
    Misc::SetVerboseLogging(bVerboseLogging);
    //Renderer
    const int iRendererIndex = ComboBox_GetCurSel(m_hWndCBRenderers);
    if(iRendererIndex != CB_ERR && static_cast<size_t>(iRendererIndex) < m_Renderers.size()) //No selection if the configured renderer isn't registered
    {
        const wchar_t* const pszRenderer = m_Renderers[iRendererIndex].c_str();
        GConfig->SetString(L"Engine.Engine",L"GameRenderDevice",pszRenderer);
        //Detail textures
        GConfig->SetBool(pszRenderer,L"DetailTextures",IsDlgButtonChecked(m_hWnd,CHK_DETAILTEX)!=0);
    }

    //Keep an optional dxvk.conf in sync with the FPS limit
    UpdateDXVKConfig();
}

void CFixApp::UpdateDXVKConfig() const
{
    wchar_t szConfPath[MAX_PATH];
    if(!Misc::GetGameSystemDir(szConfPath) || !PathAppend(szConfPath, L"dxvk.conf") || !PathFileExists(szConfPath))
    {
        return;
    }

    std::ifstream in(szConfPath, std::ios::binary);
    if(!in)
    {
        return;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    const std::string value = std::to_string(GetDlgItemInt(m_hWnd, TXT_FPSLIMIT, nullptr, FALSE));
    const char* const pszKeys[] = { "dxgi.maxFrameRate", "d3d9.maxFrameRate" };
    bool bFound[ARRAYSIZE(pszKeys)] = {};

    //Match the file's dominant line ending so any appended keys blend in
    const char* const pszNewLine = content.find("\r\n") != std::string::npos ? "\r\n" : "\n";

    //Returns the index of the target key a line assigns, even if it's commented out, else -1
    const auto MatchKey = [&pszKeys](const std::string& line) -> int
    {
        size_t i = line.find_first_not_of(" \t");
        if(i == std::string::npos)
        {
            return -1;
        }
        if(line[i] == '#') //Activate commented-out defaults as well
        {
            i = line.find_first_not_of(" \t", i + 1);
            if(i == std::string::npos)
            {
                return -1;
            }
        }
        for(int k = 0; k < static_cast<int>(ARRAYSIZE(pszKeys)); k++)
        {
            const size_t len = strlen(pszKeys[k]);
            if(line.compare(i, len, pszKeys[k]) == 0)
            {
                const size_t eq = line.find_first_not_of(" \t", i + len);
                if(eq != std::string::npos && line[eq] == '=')
                {
                    return k;
                }
            }
        }
        return -1;
    };

    std::string result;
    result.reserve(content.size() + 64);

    for(size_t pos = 0; pos < content.size();)
    {
        const size_t nl = content.find('\n', pos);
        size_t bodyEnd = (nl == std::string::npos) ? content.size() : nl;
        const char* pszTerm = "";
        if(nl != std::string::npos)
        {
            if(bodyEnd > pos && content[bodyEnd - 1] == '\r')
            {
                bodyEnd--;
                pszTerm = "\r\n";
            }
            else
            {
                pszTerm = "\n";
            }
        }

        const std::string body = content.substr(pos, bodyEnd - pos);
        const int k = MatchKey(body);
        if(k >= 0)
        {
            result += pszKeys[k];
            result += " = ";
            result += value;
            result += pszTerm;
            bFound[k] = true;
        }
        else
        {
            result += body;
            result += pszTerm;
        }

        pos = (nl == std::string::npos) ? content.size() : nl + 1;
    }

    //Add any keys that weren't already present so the limit is actually applied
    for(int k = 0; k < static_cast<int>(ARRAYSIZE(pszKeys)); k++)
    {
        if(!bFound[k])
        {
            if(!result.empty() && result.back() != '\n')
            {
                result += pszNewLine;
            }
            result += pszKeys[k];
            result += " = ";
            result += value;
            result += pszNewLine;
        }
    }

    std::ofstream out(szConfPath, std::ios::binary | std::ios::trunc);
    if(out)
    {
        out.write(result.data(), result.size());
        out.close(); //Flush here, else a write error would only surface in the destructor, after the check below
    }

    if(!out) //Opening with trunc already emptied the file, so a failure here can't go unreported
    {
        assert(GLog);
        GLog->Logf(L"Deus Exe: failed to write '%s'.", szConfPath);
    }
}

INT_PTR CALLBACK CFixApp::FixAppDialogProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam)
{   
    CFixApp* pThis = reinterpret_cast<CFixApp*>(GetProp(hwndDlg,L"this"));

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            SetProp(hwndDlg,L"this",reinterpret_cast<HANDLE>(lParam));
            pThis =  reinterpret_cast<CFixApp*>(lParam);
            pThis->m_hWnd = hwndDlg;
            pThis->m_hWndCBGUIScales = GetDlgItem(hwndDlg, COMBO_GUISCALING);
            pThis->m_hWndCBRenderers = GetDlgItem(hwndDlg, COMBO_RENDERER);
            pThis->m_hWndRadioResCommon = GetDlgItem(hwndDlg, RADIO_RESCOMMON);
            pThis->m_hWndCBResolutions = GetDlgItem(hwndDlg, COMBO_RESOLUTION);
            pThis->m_hWndRadioResCustom = GetDlgItem(hwndDlg, RADIO_RESCUSTOM);
            pThis->m_hWndTxtResX = GetDlgItem(hwndDlg, TXT_RESX);
            pThis->m_hWndTxtResY = GetDlgItem(hwndDlg, TXT_RESY);
            pThis->m_hWndTxtFOV = GetDlgItem(hwndDlg, TXT_FOV);
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIcon(reinterpret_cast<HINSTANCE>(GetWindowLongPtr(hwndDlg, GWLP_HINSTANCE)), MAKEINTRESOURCE(IDI_ICON))));

            pThis->PopulateDialog();
            pThis->ReadSettings();
        }
    return TRUE;
    
    case WM_COMMAND:
        switch (HIWORD(wParam))
        {
        case BN_CLICKED:
            switch (LOWORD(wParam))
            {
            case RADIO_RESCOMMON:
            case RADIO_RESCUSTOM:
            {
                const bool bCommon = LOWORD(wParam) == RADIO_RESCOMMON;
                pThis->EnableDisableResSettings(IsDlgButtonChecked(pThis->m_hWnd, RADIO_VPBORDERLESS)==0, bCommon);
            }
            return TRUE;

            case RADIO_FOVAUTO:
            case RADIO_FOVDEFAULT:
            case RADIO_FOVCUSTOM:
            {
                const bool bCustom = LOWORD(wParam) == RADIO_FOVCUSTOM;
                EnableWindow(pThis->m_hWndTxtFOV, bCustom);
            }
            return TRUE;

            case RADIO_VPFULLSCREEN:
            case RADIO_VPWINDOWED:
            case RADIO_VPBORDERLESS:
            {
                const bool bBorderless = LOWORD(wParam) == RADIO_VPBORDERLESS;
                pThis->EnableDisableResSettings(!bBorderless, IsDlgButtonChecked(pThis->m_hWnd, RADIO_RESCOMMON)!=0);
                EnableWindow(GetDlgItem(pThis->m_hWnd, CHK_BORDERLESSALLMONITORS), bBorderless);
            }
            return TRUE;

            case CHK_GUIFIX:
                EnableWindow(pThis->m_hWndCBGUIScales, IsDlgButtonChecked(pThis->m_hWnd, CHK_GUIFIX));
                return TRUE;

            case IDOK:
                pThis->ApplySettings();
                EndDialog(hwndDlg, 1);
                return TRUE;

            case IDCANCEL:
                EndDialog(hwndDlg, 0);
                return TRUE;

            }
            break;
        }
        break;

    case WM_CLOSE:
        EndDialog(hwndDlg,0);
        return TRUE;

    case WM_NCDESTROY:
        RemoveProp(hwndDlg, L"this");
        break;

    }

    return FALSE;
}