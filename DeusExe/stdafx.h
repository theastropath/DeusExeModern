#pragma once
#define _WIN32_WINNT _WIN32_WINNT_VISTA
//Can't use WIN32_LEAN_AND_MEAN

#pragma warning(push,0)

//MS
#include <Windows.h>
#include <Windowsx.h>
#include <Shlwapi.h>
#include <CommCtrl.h>
#include <ShlObj.h>
#include <Uxtheme.h>
#include <vsstyle.h> //BP_CHECKBOX / CBS_* constants for custom-drawn checkbox images
//#include <hidusage.h> //Not included in Platform SDK

//C/C++
#include <algorithm>
#include <vector>
#include <deque>
#include <cmath>
#include <string>
#include <limits>
#include <array>
#include <memory>
#include <cassert>
#include <cstring>
#include <fstream>
#include <unordered_map>

//Unreal
#include <Engine.h>
#include <Window.h>
#include <FMallocWindows.h>
#include <FOutputDeviceFile.h>
#include <FOutputDeviceWindowsError.h>
#include <FFeedbackContextWindows.h>
#include <UnRender.h>
#include <FConfigCacheIni.h>
#include <FFileManagerWindows.h>

#include <Extension.h>
#include <DeusEx.h>

#pragma warning(pop)
