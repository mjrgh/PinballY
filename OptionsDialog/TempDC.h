// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Temporary DC wrapper.  This is used to attach to an existing 
// device context obtained from a caller, such as in a custom
// draw routine.  The wrapper is automatically detached when the
// object goes out of scope.  This is useful for patterns like
// this:
//
// void OnCustomDraw(LPNMCUSTOMDRAW d)
// {
//     TempDC dc(d->hdc);  // attaches to the DC passed in by the caller
//     dc.FillRect(...)    // do some work with the DC
//
//     // DC automatically detaches when 'dc' goes out of scope
//  }
//

#pragma once

#include <afxwin.h>

class TempDC : public CDC
{
public:
	TempDC(HDC dc)
	{
		Attach(dc);
	}

	~TempDC()
	{
		Detach();
	}
};

