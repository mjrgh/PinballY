// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class StatuslineDialog : public OptionsPage
{
	DECLARE_DYNAMIC(StatuslineDialog)

public:
	StatuslineDialog(int dialogId);
	virtual ~StatuslineDialog();

	virtual void InitVarMap() override;
};

