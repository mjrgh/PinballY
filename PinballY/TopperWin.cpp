// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/Config.h"
#include "Resource.h"
#include "Application.h"
#include "TopperView.h"
#include "TopperWin.h"
#include "PlayfieldView.h"

namespace ConfigVars
{
	const TCHAR *TopperWinVarPrefix = _T("TopperWindow");
}

// construction
TopperWin::TopperWin() : FrameWin(ConfigVars::TopperWinVarPrefix, IDI_MAINICON, IDI_MAINICON_GRAY)
{
}

BaseView *TopperWin::CreateViewWin()
{
	// create our view
	TopperView *topperView = new TopperView();
	if (!topperView->Create(hWnd, _T("Topper")))
	{
		topperView->Release();
		return 0;
	}

	// return the window
	return topperView;
}
