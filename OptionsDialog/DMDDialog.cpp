// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "DMDDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(DMDDialog, OptionsPage)

DMDDialog::DMDDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

DMDDialog::~DMDDialog()
{
}

void DMDDialog::InitVarMap()
{
	static const TCHAR *dmdOpts[] = { _T("Auto"), _T("On"), _T("Off") };
	varMap.emplace_back(new RadioStrMap(_T("RealDMD"), IDC_RB_DMD_AUTO, _T("Auto"), dmdOpts, countof(dmdOpts)));
	varMap.emplace_back(new CkBoxMap(_T("RealDMD.MirrorHorz"), IDC_CK_DMD_MIRROR_HORZ, false));
	varMap.emplace_back(new CkBoxMap(_T("RealDMD.MirrorVert"), IDC_CK_DMD_FLIP_VERT, false));
}
