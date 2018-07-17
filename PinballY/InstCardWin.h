// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Instruction Card frame window

#pragma once

#include "stdafx.h"
#include "FrameWin.h"

class InstCardView;

// Instruction Card frame window
class InstCardWin : public FrameWin
{
public:
	// construction
	InstCardWin();

protected:
	// create my view window
	virtual BaseView *CreateViewWin() override;

	// use borderless mode
	virtual bool IsBorderless() const override { return true; }

	// hide the window on minimize or close
	virtual bool IsHideable() const override { return true; }
};
