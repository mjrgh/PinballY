// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "InstCardDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(InstCardDialog, OptionsPage)

InstCardDialog::InstCardDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

InstCardDialog::~InstCardDialog()
{
}

void InstCardDialog::InitVarMap()
{
	static const TCHAR *instCardLocs[] = { _T("Playfield"), _T("Backglass"), _T("Topper") };
	varMap.emplace_back(new RadioStrMap(
		_T("InstructionCardLocation"), IDC_RB_INST_PF, instCardLocs[1], instCardLocs, countof(instCardLocs)));

	varMap.emplace_back(new CkBoxMap(_T("InstructionCards.EnableFlash"), IDC_CK_ENABLE_SWF, TRUE));
}

