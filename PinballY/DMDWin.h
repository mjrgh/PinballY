// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD frame window

#pragma once

#include "stdafx.h"
#include "FrameWin.h"

class DMDView;

// DMD frame window
class DMDWin : public FrameWin
{
public:
	// construction
	DMDWin();

protected:
	// create my view window
	virtual BaseView *CreateViewWin() override;

	// use borderless mode for the DMD
	virtual bool IsBorderless() const override { return true; }

	// hide the window on minimize or close
	virtual bool IsHideable() const override { return true; }
};
