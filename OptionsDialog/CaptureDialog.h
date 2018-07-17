// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class CaptureDialog : public OptionsPage
{
	DECLARE_DYNAMIC(CaptureDialog)

public:
	CaptureDialog(int dialogId);
	virtual ~CaptureDialog();

	virtual void InitVarMap() override;
};

