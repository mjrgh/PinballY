// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class AudioVideoDialog : public OptionsPage
{
	DECLARE_DYNAMIC(AudioVideoDialog)

public:
	AudioVideoDialog(int dialogId);
	virtual ~AudioVideoDialog();

protected:
	// set up the VarMap entries
	virtual void InitVarMap() override;
};

