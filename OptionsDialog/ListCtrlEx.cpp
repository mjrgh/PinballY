// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "ListCtrlEx.h"

BEGIN_MESSAGE_MAP(CListCtrlEx, CListCtrl)
	ON_WM_LBUTTONDOWN()
	ON_WM_MEASUREITEM_REFLECT()
END_MESSAGE_MAP()

CListCtrlEx::CListCtrlEx()
{
	synthesizeClickNotification = FALSE;
	detailRowHeight = -1;
}

int CListCtrlEx::PointToItem(CPoint pt, int &iSubItem)
{
	// get the first and last visible items in the list
	int iFirstVisible = GetTopIndex();
	int iLastVisible = iFirstVisible + GetCountPerPage();
	if (iLastVisible > GetItemCount())
		iLastVisible = GetItemCount() - 1;

	// scan the visible items
	for (int row = iFirstVisible; row <= iLastVisible; ++row)
	{
		// get this item's bounding rectangle
		CRect r;
		GetItemRect(row, &r, LVIR_BOUNDS);
		if (r.PtInRect(pt))
		{
			// It's in this row.  Now figure out which column it's
			// in.  Start at the left edge, adjusted for scrolling.
			CRect rc;
			GetClientRect(&rc);
			int left = rc.left - GetScrollPos(SB_HORZ);

			// get the column order array
			int nColumns = GetHeaderCtrl()->GetItemCount();
			int *colOrder = new int[nColumns];
			GetColumnOrderArray(colOrder, nColumns);

			// scan the columns
			int col;
			for (col = 0; col < nColumns; ++col)
			{
				// figure the extent of this column
				int right = left + GetColumnWidth(colOrder[col]);

				// see if we're in this zone
				if (pt.x >= left && pt.x < right)
					break;

				// advance to the next column
				left = right;
			}

			// done with the column order array
			delete[] colOrder;

			// return the item index and subindex
			iSubItem = col < nColumns ? col : -1;
			return row;
		}
	}

	// not found
	return -1;
}

// Invalidate on-screen items that match a predicate
void CListCtrlEx::InvalidateRowsIf(std::function<BOOL(int row)> pred)
{
	// get the first and last visible items in the list
	int iFirstVisible = GetTopIndex();
	int iLastVisible = iFirstVisible + GetCountPerPage();
	if (iLastVisible >= GetItemCount())
		iLastVisible = GetItemCount() - 1;

	// scan the visible items
	for (int row = iFirstVisible; row <= iLastVisible; ++row)
	{
		if (pred(row))
		{
			// get this item's bounding rectangle
			CRect r;
			GetItemRect(row, &r, LVIR_BOUNDS);

			// invalidate it
			InvalidateRect(&r);
		}
	}
}

void CListCtrlEx::OnLButtonDown(UINT flags, CPoint pt)
{
	// do the basic work 
	__super::OnLButtonDown(flags, pt);

	// synthesize a click notification if desired
	if (synthesizeClickNotification)
	{
		// synthesize an NM_CLICK
		NMITEMACTIVATE nm;
		nm.hdr.code = NM_CLICK;
		nm.hdr.hwndFrom = GetSafeHwnd();
		nm.hdr.idFrom = GetDlgCtrlID();
		nm.iItem = -1;
		nm.iSubItem = -1;
		nm.lParam = 0;
		nm.ptAction = pt;
		nm.uChanged = 0;
		nm.uNewState = 0;
		nm.uOldState = 0;
		GetParent()->SendMessage(WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
	}
}

void CListCtrlEx::InvalidateRowRect(int row)
{
	CRect rc;
	if (GetItemRect(row, &rc, LVIR_BOUNDS))
		InvalidateRect(rc);
}

void CListCtrlEx::SetDetailRowHeight(int pix)
{
	// set the new height
	detailRowHeight = pix;

	// Force the control to generate a WM_MEASUREITEM by sending
	// it a WM_WINDOWPOSCHANGED message with a resize indicated.
	// We're not actually changing size, so send it the current
	// size in the parameters.
	if (m_hWnd != 0)
	{
		// get the current window size
		CRect rc;
		GetWindowRect(&rc);

		// send a WINDOWPOSCHANGED at the current size
		WINDOWPOS wp;
		wp.hwnd = m_hWnd;
		wp.cx = rc.Width();
		wp.cy = rc.Height();
		wp.flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER;
		SendMessage(WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp);
	}
}

void CListCtrlEx::MeasureItem(LPMEASUREITEMSTRUCT lpmi)
{
	if (detailRowHeight != -1)
		lpmi->itemHeight = detailRowHeight;
}

