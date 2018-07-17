// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Extended version of CListCtrl

#pragma once

class CListCtrlEx : public CListCtrl
{
public:
	CListCtrlEx();

	// Explicitly set the row height in the detail view.  This
	// only works for owner-drawn list controls (i.e., with
	// style flag LVS_OWNERDRAWFIXED).
	void SetDetailRowHeight(int ht);

	// Turn on synthetic click notifications.  If this is set,
	// we'll generate an NM_CLICK notification on each LButtonDown
	// event.  This can be useful if the control isn't in 
	// LVS_EX_ONECLICKACTIVATE (one-click activation) mode, since
	// that mode treats an initial click as an activation, not
	// a click in the control's contents.
	void SynthesizeClickNotification(BOOL f) { synthesizeClickNotification = f; }

	// Enhanced hit test to find the item containing a click.
	// The base class HitTest() only maps a point to an item if
	// the point is within the main item.  In a detail view with
	// subitems, it won't find hits in subitems.  This version
	// finds the hit anywhere in an item row.  Returns the item
	// index, or -1 if the click wasn't in an item.
	int PointToItem(CPoint pt, int &iSubItem);

	// invalidate the on-screen area for the given row
	void InvalidateRowRect(int row);

	// Invalidate on-screen items that match a predicate
	void InvalidateRowsIf(std::function<BOOL(int row)> pred);

protected:
	// item height in the detail view
	int detailRowHeight;

	// should we synthesize a click notification on LButtonDown?
	BOOL synthesizeClickNotification;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnLButtonDown(UINT flags, CPoint pt);
	afx_msg void MeasureItem(LPMEASUREITEMSTRUCT lpmi);
};

