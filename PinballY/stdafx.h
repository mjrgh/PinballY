// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include "MemoryLeakDebugging.h"
#endif

#include "targetver.h"

// Windows Header Files:
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <windowsx.h>
#include <Shlwapi.h>
#include <ObjIdl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Mferror.h>
#include <evr.h>

typedef double DOUBLE;

// Direct Input version
#define DIRECTINPUT_VERSION 0x0800

// C RunTime Header Files
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <memory>
#include <list>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <regex>
#include <unordered_set>


// Widely used internal headers
#include "Resource.h"
#include "../Utilities/UtilResource.h"
#include "../Utilities/InstanceHandle.h"
#include "../Utilities/Util.h"
#include "../Utilities/StringUtil.h"
#include "../Utilities/Pointers.h"
#include "../Utilities/LogError.h"
#include "../Utilities/WinUtil.h"
#include "../Utilities/FileUtil.h"
#include "GraphicsUtil.h"
#include "BaseWin.h"
