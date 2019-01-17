// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "../Utilities/Config.h"
#include "FontDialog.h"

IMPLEMENT_DYNAMIC(FontDialog, OptionsPage)

FontDialog::FontDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

FontDialog::~FontDialog()
{
}

void FontDialog::InitVarMap()
{
}

// initialize the dialog
BOOL FontDialog::OnInitDialog()
{
	// do the base class work
	BOOL ret = __super::OnInitDialog();

	// initialize the font combos
	auto InitCombo = [this](int id, CFontPreviewCombo &combo)
	{
		combo.SubclassDlgItem(id, this);
		combo.SetPreviewStyle(CFontPreviewCombo::PreviewStyle::NAME_ONLY, false);
		combo.Init();
	};

	InitCombo(IDC_CB_MENU_FONT, cbMenuFont);

	// return the result from the base class
	return ret;
}


