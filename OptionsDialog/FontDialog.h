#pragma once
// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"
#include "FontPreviewCombo/FontPreviewCombo.h"

class FontDialog : public OptionsPage
{
	DECLARE_DYNAMIC(FontDialog)

public:
	FontDialog(int dialogId);
	virtual ~FontDialog();

protected:
	// set up the VarMap entries
	virtual void InitVarMap() override;

	// initialize the dialog
	virtual BOOL OnInitDialog() override;

	// font combos
	CFontPreviewCombo cbMenuFont;
};

