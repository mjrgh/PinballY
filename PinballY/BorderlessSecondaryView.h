// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Base class for borderless secondary view windows

#pragma once
#include "SecondaryView.h"

class BorderlessSecondaryView : public SecondaryView
{
public:
	BorderlessSecondaryView(int contextMenuId, const TCHAR *configVarPrefix)
		: SecondaryView(contextMenuId, configVarPrefix) { }

	// move/resize the window on mouse events
	virtual bool OnMouseMove(POINT pt) override;

	// pass non-client hit testing to the parent window
	virtual bool OnNCHitTest(POINT pt, UINT &hit) override;
};
