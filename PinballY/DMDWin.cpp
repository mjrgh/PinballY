// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/Config.h"
#include "Resource.h"
#include "Application.h"
#include "DMDView.h"
#include "DMDWin.h"
#include "PlayfieldView.h"

namespace ConfigVars
{
	const TCHAR *DMDWinVarPrefix = _T("DMDWindow");
}

// construction
DMDWin::DMDWin() : FrameWin(ConfigVars::DMDWinVarPrefix, _T("DMD"), IDI_MAINICON, IDI_MAINICON_GRAY)
{
}

BaseView *DMDWin::CreateViewWin()
{
	// create our view
	DMDView *dmdView = new DMDView();
	if (!dmdView->Create(hWnd, _T("DMD")))
	{
		dmdView->Release();
		return 0;
	}

	// return the window
	return dmdView;
}
