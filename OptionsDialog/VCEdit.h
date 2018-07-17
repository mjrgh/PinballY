// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// CVCEdit - vertically centered edit control
//
// Usage note:  if you're moving the control around dynamically,
// use SetWindowPos(..., SWP_FRAMECHANGED) for the moves whenever
// the control is resized.  This will ensure that we recalculate
// the internal layout properly after a resize.

#pragma once
#include <afx.h>
#include <afxctl.h>

class CVCEdit : public CEdit
{
public:
	CVCEdit();
	virtual ~CVCEdit();

protected:
	// original control's border sizes
	bool bordersInited;
	int cxBorder;
	int cyBorder;

	// margin areas at the top and bottom of the non-client area
	CRect rectNCBottom;
	CRect rectNCTop;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS FAR* lpncsp);
	afx_msg void OnNcPaint();
	afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);

};
