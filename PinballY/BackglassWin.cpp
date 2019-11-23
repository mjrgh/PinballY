// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/Config.h"
#include "Resource.h"
#include "Application.h"
#include "BackglassView.h"
#include "BackglassWin.h"
#include "PlayfieldView.h"

namespace ConfigVars
{
	const TCHAR *BackglassWinVarPrefix = _T("BackglassWindow");
}

// construction
BackglassWin::BackglassWin() : FrameWin(ConfigVars::BackglassWinVarPrefix, _T("Backglass"), IDI_MAINICON, IDI_MAINICON_GRAY)
{
}

// destruction
BackglassWin::~BackglassWin()
{
}

BaseView *BackglassWin::CreateViewWin()
{
	// create our view
	BackglassView *bgView = new BackglassView();
	if (!bgView->Create(hWnd, _T("Backglass")))
	{
		bgView->Release();
		return 0;
	}

	// return the window
	return bgView;
}

