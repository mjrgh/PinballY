// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class MenuDialog : public OptionsPage
{
	DECLARE_DYNAMIC(MenuDialog)

public:
	MenuDialog(int dialogId);
	virtual ~MenuDialog();

	virtual void InitVarMap() override;
};

