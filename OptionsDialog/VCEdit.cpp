// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// CVCEdit - vertically centered edit control
//

#include "stdafx.h"
#include "VCEdit.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


CVCEdit::CVCEdit()
	: rectNCBottom(0, 0, 0, 0), rectNCTop(0, 0, 0, 0), bordersInited(false)
{
}

CVCEdit::~CVCEdit()
{
}


BEGIN_MESSAGE_MAP(CVCEdit, CEdit)
	ON_WM_NCCALCSIZE()
	ON_WM_NCPAINT()
	ON_WM_CTLCOLOR_REFLECT()
END_MESSAGE_MAP()


void CVCEdit::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS FAR* lpncsp)
{
	// get the DC
	CDC *pDC = GetDC();

	// select our font
	CFont *pFont = GetFont();
	CFont *pOld = pDC->SelectObject(pFont);

	// Calculate the height needed for this font, by doing a CALCRECT with
	// the some sample text.  Use a capital plus a minuscule with a descender
	// to make sure we get the full vertical span.
	CRect rectText;
	rectText.SetRectEmpty();
	pDC->DrawText(_T("Ky"), rectText, DT_CALCRECT | DT_LEFT);
	UINT uiVClientHeight = rectText.Height();

	// restore and release the DC
	pDC->SelectObject(pOld);
	ReleaseDC(pDC);

	// Get the first rectangle from the params - this is the
	// proposed new window rect on input, and the new client
	// rect on output.  This is in parent-relative coordinates
	// for a child window.
	RECT &r0 = lpncsp->rgrc[0];
	int cxWin = r0.right - r0.left;
	int cyWin = r0.bottom - r0.top;

	// if we haven't initialized the borders yet, do so now
	if (!bordersInited)
	{
		// Get the old window rect and client, both in screen
		// coordinates.
		CRect rectWnd, rectClient;
		GetWindowRect(rectWnd);
		GetClientRect(rectClient);
		ClientToScreen(rectClient);

		// Figure the original borders from the inset of the
		// client rect within the window rect.  Regular edit
		// controls have uniform borders.
		cxBorder = (rectWnd.Width() - rectClient.Width()) / 2;
		cyBorder = (rectWnd.Height() - rectClient.Height()) / 2;
		bordersInited = true;
	}

	// figure the vertical padding - figure the total, and divide it
	// between top and bottom
	int cyPaddingTotal = cyWin - (2 * cyBorder) - uiVClientHeight;
	int cyPaddingBottom = cyPaddingTotal / 2;
	int cyPaddingTop = cyPaddingTotal - cyPaddingBottom;

	// figure the size of the margin areas at the top and buttom
	rectNCTop.SetRect(cxBorder, cyBorder, cxWin - cxBorder, cyBorder + cyPaddingTop);
	rectNCBottom.SetRect(cxBorder, cyWin - cyBorder - cyPaddingBottom, cxWin - cxBorder, cyWin - cyBorder);

	// set the NC results
	r0.top += cyBorder + cyPaddingTop;
	r0.bottom -= cyBorder + cyPaddingBottom;
	r0.left += cxBorder;
	r0.right -= cxBorder;
}

void CVCEdit::OnNcPaint()
{
	// do the standard handling
	__super::OnNcPaint();

	// fill the top and bottom margin areas with the background color
	CWindowDC dc(this);
	CBrush Brush(GetSysColor(COLOR_WINDOW));
	dc.FillRect(rectNCBottom, &Brush);
	dc.FillRect(rectNCTop, &Brush);
}

// We don't actually need to do any special coloring, but we handle WM_CTLCOLOR
// anyway, because it's a convenient place to be sure we've calculated the NC
// area before doing any painting.  We need to do this somewhere, because Windows
// doesn't always fire WM_NCCALCSIZE by itself for a new window, and this is "as
// good a place as any".  Actually, it's a particularly good place - even though
// it's obviously a hack, it happens to be a widely-used hack (that is, lots of
// other programs use the same hack for the same purpose).  And widely-used means
// reliable, since MSFT is always pretty diligent about maintaining bug-for-bug 
// compatibility with widely used idioms, even the crappy ones like this.
HBRUSH CVCEdit::CtlColor(CDC* pDC, UINT nCtlColor)
{
	// If our internal NC-top rect is empty, it means we haven't calculated
	// the NC size yet.  Force a WM_NCCALCSIZE via SetWindowPos with a 
	// "frame changed" flag.
	if (rectNCTop.IsRectEmpty())
		SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_FRAMECHANGED);

	// we don't actually need to do any special coloring
	return NULL;
}
