// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Backglass frame window

#pragma once

#include "stdafx.h"
#include "FrameWin.h"

class BackglassView;

// Playfield frame window
class BackglassWin : public FrameWin
{
public:
	// construction
	BackglassWin();

protected:
	// destruction - called internally when the reference count reaches zero
	~BackglassWin();

	// create my view window
	virtual BaseView *CreateViewWin() override;

	// hide the window on minimize or close
	virtual bool IsHideable() const override { return true; }
};
