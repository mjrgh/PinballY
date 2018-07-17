// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class DMDDialog : public OptionsPage
{
	DECLARE_DYNAMIC(DMDDialog)

public:
	DMDDialog(int dialogId);
	virtual ~DMDDialog();

	virtual void InitVarMap() override;
};

