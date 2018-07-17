// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Topper frame window

#pragma once

#include "stdafx.h"
#include "FrameWin.h"

class TopperView;

// Playfield frame window
class TopperWin : public FrameWin
{
public:
	// construction
	TopperWin();

protected:
	// create my view window
	virtual BaseView *CreateViewWin() override;

	// hide the window on minimize or close
	virtual bool IsHideable() const override { return true; }
};
