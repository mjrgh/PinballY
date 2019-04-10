// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class GameLaunchDialog : public OptionsPage
{
	DECLARE_DYNAMIC(GameLaunchDialog)

public:
	GameLaunchDialog(int dialogId);
	virtual ~GameLaunchDialog();

	virtual void InitVarMap() override;
	
	virtual BOOL OnApply() override;
};

