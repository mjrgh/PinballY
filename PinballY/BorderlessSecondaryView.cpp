// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "BorderlessSecondaryView.h"
#include "MouseButtons.h"

bool BorderlessSecondaryView::OnNCHitTest(POINT pt, UINT &hit)
{
	// If it's within the sizing border with of an edge, let 
	// the parent window handle it.  This gives us an invisible
	// sizing border that acts like a normal sizing border, but
	// is covered out to the edge by the DMD contents.
	if (HWND parent = GetParent(hWnd); parent != 0)
	{
		// figure the sizing border area of the parent, based on its
		// window style, but excluding the caption area
		RECT rcFrame = { 0, 0, 0, 0 };
		DWORD dwStyle = GetWindowLong(parent, GWL_STYLE);
		DWORD dwExStyle = GetWindowLong(parent, GWL_EXSTYLE);
		AdjustWindowRectEx(&rcFrame, dwStyle & ~WS_CAPTION, FALSE, dwExStyle);

		// get my window rect 
		RECT rcWindow;
		GetWindowRect(hWnd, &rcWindow);

		// check if we're in the sizing border
		if ((pt.x >= rcWindow.left && pt.x < rcWindow.left - rcFrame.left)
			|| (pt.x < rcWindow.right && pt.x >= rcWindow.right - rcFrame.right)
			|| (pt.y >= rcWindow.top && pt.y < rcWindow.top - rcFrame.top)
			|| (pt.y < rcWindow.bottom && pt.y >= rcWindow.bottom - rcFrame.bottom))
		{
			// it's in the sizing border - let the parent window handle it
			hit = HTTRANSPARENT;
			return true;
		}
	}

	// not handled
	return FALSE;
}

bool BorderlessSecondaryView::OnMouseMove(POINT pt)
{
	if (dragButton == MouseButton::mbLeft)
	{
		// Get the delta from the last position
		POINT delta;
		delta.x = pt.x - dragPos.x;
		delta.y = pt.y - dragPos.y;

		// move the parent window by the drag position
		HWND par = GetParent(hWnd);
		RECT rc;
		GetWindowRect(par, &rc);
		SetWindowPos(par, 0, rc.left + delta.x, rc.top + delta.y, -1, -1, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);

		// Note that we don't need to update the drag position, as it's
		// relative to the client area, and we're moving the client area
		// in sync with each mouse position change.  That means that the
		// relative mouse never changes.
	}

	// handled
	return true;
}
