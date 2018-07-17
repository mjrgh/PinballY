// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class AttractModeDialog : public OptionsPage
{
	DECLARE_DYNAMIC(AttractModeDialog)

public:
	AttractModeDialog(int dialogId);
	virtual ~AttractModeDialog();

protected:
	// set up the VarMap entries
	virtual void InitVarMap() override;
};

