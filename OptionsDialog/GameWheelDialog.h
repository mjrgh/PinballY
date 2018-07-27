// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class GameWheelDialog : public OptionsPage
{
	DECLARE_DYNAMIC(GameWheelDialog)

public:
	GameWheelDialog(int dialogId);
	virtual ~GameWheelDialog();

	virtual void InitVarMap() override;
};

