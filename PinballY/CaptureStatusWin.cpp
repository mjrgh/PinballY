// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "CaptureStatusWin.h"
#include "Application.h"
#include "FrameWin.h"
#include "PlayfieldWin.h"
#include "D3DView.h"
#include "PlayfieldView.h"


CaptureStatusWin::CaptureStatusWin() : 
	BaseWin(0),
	curOpTime(0),
	totalTime(0),
	rotation(0),
	mirrorHorz(false),
	mirrorVert(false)
{
	// set the initial rotation and mirroring to match the playfield
	// window, since we'll be displayed there initially
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
	{
		rotation = (float)pfv->GetRotation();
		mirrorHorz = pfv->IsMirrorHorz();
		mirrorVert = pfv->IsMirrorVert();
	}

	// load the initial status string
	status = LoadStringT(IDS_CAPSTAT_STARTING);
}

CaptureStatusWin::~CaptureStatusWin()
{
}

void CaptureStatusWin::SetCaptureStatus(const TCHAR *msg, DWORD time_ms)
{
	CriticalSectionLocker locker(lock);
	status = msg;
	curOpTime = time_ms;
	InvalidateRect(hWnd, NULL, FALSE);
}

// Set the estimated total time for the capture process
void CaptureStatusWin::SetTotalTime(DWORD time_ms)
{
	CriticalSectionLocker locker(lock);
	totalTime = time_ms;
	InvalidateRect(hWnd, NULL, FALSE);
}

// Set the drawing rotation, in degrees
void CaptureStatusWin::SetRotation(float angle)
{
	CriticalSectionLocker locker(lock);
	rotation = angle;
	InvalidateRect(hWnd, NULL, FALSE);
}

// Set the mirroring
void CaptureStatusWin::SetMirrorHorz(bool f)
{
	CriticalSectionLocker locker(lock);
	mirrorHorz = f;
	InvalidateRect(hWnd, NULL, FALSE);
}

void CaptureStatusWin::SetMirrorVert(bool f)
{
	CriticalSectionLocker locker(lock);
	mirrorVert = f;
	InvalidateRect(hWnd, NULL, FALSE);
}

RECT CaptureStatusWin::GetCreateWindowPos(int &nCmdShow)
{
	// figure the initial rotation based on the playfield view
	rotation = (float)Application::Get()->GetPlayfieldView()->GetRotation();
	int cx = winWidth, cy = winHeight;
	if (rotation == 90 || rotation == 270)
		cx = winHeight, cy = winWidth;

	// initially position it centered over the playfield window
	RECT rc;
	GetWindowRect(Application::Get()->GetPlayfieldWin()->GetHWnd(), &rc);
	int x = (rc.right + rc.left - cx) / 2;
	int y = (rc.bottom + rc.top - cy) / 2;
	return { x, y, x + cx, y + cy };
}

void CaptureStatusWin::PositionOver(FrameWin *win)
{
	CriticalSectionLocker locker(lock);

	// if the desired window isn't visible, hide the status box entirely
	if (!IsWindowVisible(win->GetHWnd()))
	{
		ShowWindow(hWnd, SW_HIDE);
		return;
	}

	// get the window rect
	RECT overrc;
	GetWindowRect(win->GetHWnd(), &overrc);

	// set our rotation to match the window we're over
	bool inval = false;
	auto view = dynamic_cast<D3DView*>(win->GetView());
	float newRot = (float)view->GetRotation();
	if (newRot != rotation)
		rotation = newRot, inval = true;

	// set our mirroring to match the window we're over
	if (auto mv = view->IsMirrorVert(); mv != mirrorVert)
		mirrorVert = mv, inval = true;
	if (auto mh = view->IsMirrorHorz(); mh != mirrorHorz)
		mirrorHorz = mh, inval = true;

	// invalidate the drawing area if we changed anything
	if (inval)
		InvalidateRect(hWnd, NULL, FALSE);

	// figure the window width and height for the rotation
	int width = winWidth, height = winHeight;
	if (newRot == 90 || newRot == 270)
		width = winHeight, height = winWidth;

	// center it over the new window
	int x = (overrc.right + overrc.left - width) / 2;
	int y = (overrc.bottom + overrc.top - height) / 2;

	// reposition it
	SetWindowPos(hWnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

bool CaptureStatusWin::OnCreate(CREATESTRUCT *lpcs)
{
	// do the base class work
	bool ret = __super::OnCreate(lpcs);

	// make the window topmost
	SetWindowPos(hWnd, HWND_TOPMOST, -1, -1, -1, -1, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	// remember the initial tick time
	lastTicks = GetTickCount();

	// set the time countdown update timer
	SetTimer(hWnd, CountdownTimerId, 250, 0);

	// return the base class result
	return ret;
}

void CaptureStatusWin::OnPaint(HDC hdc)
{
	// get the window layout
	RECT rcCli;
	GetClientRect(hWnd, &rcCli);
	int cx = winWidth, cy = winHeight;
	if (rotation == 90 || rotation == 270)
		cx = winHeight, cy = winWidth;

	DrawOffScreen(cx, cy, [this, hdc, &rcCli, cx, cy](HDC hdcmem, HBITMAP hbmp, const void *, const BITMAPINFO &)
	{
		// set up a gdiplus context on the off-screen DC
		Gdiplus::Graphics g(hdcmem);

		// erase the background
		Gdiplus::SolidBrush bkgbr(Gdiplus::Color(128, 0, 128));
		Gdiplus::Pen framepen(Gdiplus::Color(0, 0, 0), 2.0f);
		Gdiplus::SolidBrush txtbr(Gdiplus::Color(255, 255, 255));
		g.FillRectangle(&bkgbr, rcCli.left, rcCli.top, cx, cy);
		g.DrawRectangle(&framepen, 1, 1, cx - 2, cy - 2);

		// set up the gdiplus transform to put the origin in the
		// center of the window and set the rotation.
		g.RotateTransform(-rotation);
		g.TranslateTransform(float(cx/2), float(cy/2), Gdiplus::MatrixOrder::MatrixOrderAppend);

		// apply mirroring transforms as needed
		if (mirrorHorz)
		{
			g.ScaleTransform(-1, 1, Gdiplus::MatrixOrderAppend);
			g.TranslateTransform(float(cx), 0, Gdiplus::MatrixOrder::MatrixOrderAppend);
		}
		if (mirrorVert)
		{
			g.ScaleTransform(1, -1, Gdiplus::MatrixOrderAppend);
			g.TranslateTransform(0, float(cy), Gdiplus::MatrixOrder::MatrixOrderAppend);
		}

		// set up a centering text formatter
		Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
		format.SetFormatFlags(format.GetFormatFlags() & ~Gdiplus::StringFormatFlagsLineLimit);
		format.SetAlignment(Gdiplus::StringAlignmentCenter);
		format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

		// generate the status message
		MsgFmt txt(IDS_CAPSTAT_TITLE, status.c_str(), int(curOpTime / 1000), int(totalTime / 1000));
		std::unique_ptr<Gdiplus::Font> font(CreateGPFont(_T("Tahoma"), 22, 400));
		Gdiplus::RectF rcLayout(float(-cx/2), float(-cy/2), float(cx), float(cy));
		g.DrawString(txt.Get(), -1, font.get(), rcLayout, &format, &txtbr);

		// flush the gdiplus drawing operations to the DC
		g.Flush();

		// copy the off-screen bitmap into the window
		BitBlt(hdc, 0, 0, cx, cy, hdcmem, 0, 0, SRCCOPY);
	});
}

bool CaptureStatusWin::OnTimer(WPARAM timer, LPARAM callback)
{
	switch (timer)
	{
	case CountdownTimerId:
		OnCountdownTimer();
		return true;
	}

	return __super::OnTimer(timer, callback);
}

void CaptureStatusWin::OnCountdownTimer()
{
	// figure the elapsed time since the last update
	DWORD now = GetTickCount();
	DWORD dt = now - lastTicks;
	lastTicks = now;

	// Deduct the elapsed time from the running counters
	bool redraw = false;
	auto Update = [this, &redraw, dt](DWORD &t)
	{
		CriticalSectionLocker locker(lock);

		// figure the new time, stopping when we reach zero
		DWORD tOld = t;
		t = dt <= t ? t - dt : 0;

		// note if this is a change in whole seconds
		if (t/1000 != tOld/1000)
			redraw = true;
	};
	Update(curOpTime);
	Update(totalTime);

	// if anything changed, redraw the window
	if (redraw)
		InvalidateRect(hWnd, NULL, FALSE);
}
