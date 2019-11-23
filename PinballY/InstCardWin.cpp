// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"

#include "stdafx.h"
#include <Dwmapi.h>
#include <vssym32.h>
#include "../Utilities/Config.h"
#include "Resource.h"
#include "Application.h"
#include "InstCardView.h"
#include "InstCardWin.h"
#include "PlayfieldView.h"

namespace ConfigVars
{
	const TCHAR *InstCardWinVarPrefix = _T("InstCardWindow");
}

// construction
InstCardWin::InstCardWin() : FrameWin(ConfigVars::InstCardWinVarPrefix, _T("Instruction Card"), IDI_MAINICON, IDI_MAINICON_GRAY)
{
}

BaseView *InstCardWin::CreateViewWin()
{
	// create our view
	InstCardView *icView = new InstCardView();
	if (!icView->Create(hWnd, _T("Instruction Card")))
	{
		icView->Release();
		return 0;
	}

	// return the window
	return icView;
}
