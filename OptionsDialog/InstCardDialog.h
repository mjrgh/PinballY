// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class InstCardDialog : public OptionsPage
{
	DECLARE_DYNAMIC(InstCardDialog)

public:
	InstCardDialog(int dialogId);
	virtual ~InstCardDialog();

	virtual void InitVarMap() override;
};

