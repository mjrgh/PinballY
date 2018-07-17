// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Base class for view windows.  A view window is a child window
// of a frame window that's used to display the main content area
// of the parent window.

#pragma once
#include "BaseWin.h"

class ViewWin : public BaseWin
{
public:
	ViewWin(int contextMenuId) : BaseWin(contextMenuId) { }

protected:
	virtual ~ViewWin() { }
};
