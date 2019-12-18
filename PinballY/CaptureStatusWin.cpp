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


CaptureStatusWin::CaptureStatusWin() : BaseWin(0)
{
	// set the initial rotation and mirroring to match the playfield
	// window, since we'll be displayed there initially
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
	{
		// get the playfield view layout
		rotation = (float)pfv->GetRotation();
		mirrorHorz = pfv->IsMirrorHorz();
		mirrorVert = pfv->IsMirrorVert();

		// while we're at it, note the manual capture gesture name
		manualGoResId = pfv->GetCaptureManualGoButtonNameResId();
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
	curOpTime.total = curOpTime.rem = time_ms;
	if (hWnd != NULL)
		InvalidateRect(hWnd, NULL, FALSE);
}

// Set the estimated total time for the capture process
void CaptureStatusWin::SetTotalTime(DWORD time_ms)
{
	CriticalSectionLocker locker(lock);
	gameTime.total = gameTime.rem = time_ms;
	if (hWnd != NULL)
		InvalidateRect(hWnd, NULL, FALSE);
}

void CaptureStatusWin::BatchCaptureCancelPrompt(bool show)
{
	CriticalSectionLocker locker(lock);
	batchCancelPrompt = show;
	if (hWnd != NULL)
		InvalidateRect(hWnd, NULL, FALSE);
}

void CaptureStatusWin::ShowCaptureCancel()
{
	CriticalSectionLocker locker(lock);
	cancelled = true;
	if (hWnd != NULL)
		InvalidateRect(hWnd, NULL, FALSE);
}

void CaptureStatusWin::SetManualStartMode(bool f)
{
	CriticalSectionLocker locker(lock);
	manualStartMode = f;
	UpdateBlinkMode();
}

void CaptureStatusWin::SetManualStopMode(bool f)
{
	CriticalSectionLocker locker(lock);
	manualStopMode = f;
	UpdateBlinkMode();
}

static const DWORD BlinkOnTime = 850;
static const DWORD BlinkOffTime = 850;

void CaptureStatusWin::UpdateBlinkMode()
{
	if (manualStartMode || manualStopMode)
	{
		// We're in a blinking mode.  Start the blink timer.
		SetTimer(hWnd, BlinkTimerId, BlinkOnTime, NULL);
	}
	else
	{
		// We're not in a blinking mode.  Kill any blink timer.
		KillTimer(hWnd, BlinkTimerId);
	}

	// start in blink 'on' mode (especially if we're *not* blinking!)
	blinkState = true;

	// in any case, make sure we redraw for the mode change
	InvalidateRect(hWnd, NULL, FALSE);
}

// Set the batch time estimates
void CaptureStatusWin::SetBatchInfo(int nCurGame, int nGames, int remainingTime_ms, int totalTime_ms)
{
	CriticalSectionLocker locker(lock);
	isBatch = nGames > 0;
	this->nCurGame = nCurGame;
	this->nGames = nGames;
	batchTime.rem = remainingTime_ms;
	batchTime.total = totalTime_ms;
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

	// now make sure it's in front of any other topmost windows by also
	// bringing it to the TOP; it's already in the TOPMOST layer, but
	// there seem to be times when other TOPMOST windows can stay ahead
	// of it on that step
	SetWindowPos(hWnd, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

		// figure the basic color scheme based on the mode
		Gdiplus::Color bkColor, textColor, frameColor, blinkOffColor;
		if (batchCancelPrompt || cancelled)
		{
			// cancel prompt or cancellation in progress - white text on red, with a red frame
			textColor = Gdiplus::Color(255, 255, 255);
			bkColor = Gdiplus::Color(255, 0, 0);
			frameColor = Gdiplus::Color(128, 0, 0);
			blinkOffColor = Gdiplus::Color(128, 64, 64);
		}
		else
		{
			// normal mode - black text on a white background
			bkColor = Gdiplus::Color(255, 255, 255);
			textColor = Gdiplus::Color(0, 0, 0);

			// use a blue frame for single captures, purple for batches
			frameColor = isBatch ? Gdiplus::Color(128, 0, 128) : Gdiplus::Color(0, 0, 192);
			blinkOffColor = isBatch ? Gdiplus::Color(192, 144, 192) : Gdiplus::Color(144, 144, 192);
		}

		// set up brushes and pens
		int frameWidth = 6;
		Gdiplus::SolidBrush bkgbr(bkColor);
		Gdiplus::SolidBrush textBr(textColor); 
		Gdiplus::Pen framePen(frameColor, (float)frameWidth);
		Gdiplus::SolidBrush frameBrush(frameColor);
		Gdiplus::SolidBrush titleTextBr(Gdiplus::Color(255, 255, 255));
		Gdiplus::SolidBrush ctlBr(Gdiplus::Color(238, 238, 238));
		Gdiplus::SolidBrush ctlTextBr(blinkState ? frameColor : blinkOffColor);

		// draw the background and window frame
		g.FillRectangle(&bkgbr, rcCli.left, rcCli.top, cx, cy);
		g.DrawRectangle(&framePen, frameWidth / 2, frameWidth / 2, cx - frameWidth, cy - frameWidth);

		// Set up the gdiplus transform to put the origin in the center of
		// the window, and set the rotation to our drawing to match the
		// current background area rotation.  We center the origin to make
		// the rotation easier to think about, as placing the origin at the
		// center means that it stays at the center after any rotation.
		// (We could have placed it at the upper left like a normal window,
		// but that's harder to think about with the rotation because the
		// rotation effectively moves the origin to one of the other corners,
		// so we'd need a compensating translation.  Which is perfectly
		// doable; it's just harder to think about.)
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

		// set up the text layout area, taking into account that the Gdiplus
		// origin is at the center of the window
		Gdiplus::RectF rcLayout(float(-winWidth / 2), float(-winHeight / 2), float(winWidth), float(winHeight));

		// set up a centering text formatter
		Gdiplus::StringFormat cformat(Gdiplus::StringFormat::GenericTypographic());
		cformat.SetFormatFlags(cformat.GetFormatFlags() & ~Gdiplus::StringFormatFlagsLineLimit);
		cformat.SetAlignment(Gdiplus::StringAlignmentCenter);
		cformat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

		// set up a right-aligned formatter
		Gdiplus::StringFormat rformat(Gdiplus::StringFormat::GenericTypographic());
		rformat.SetFormatFlags(rformat.GetFormatFlags() & ~Gdiplus::StringFormatFlagsLineLimit);
		rformat.SetAlignment(Gdiplus::StringAlignmentFar);

		// set up a regular typographic formatter
		Gdiplus::StringFormat tformat(Gdiplus::StringFormat::GenericTypographic());
		tformat.SetFormatFlags(tformat.GetFormatFlags() & ~Gdiplus::StringFormatFlagsLineLimit);

		// check the message mode
		if (cancelled)
		{
			std::unique_ptr<Gdiplus::Font> textFont(CreateGPFont(_T("Tahoma"), 24, 400, false));
			g.DrawString(LoadStringT(IDS_CAPSTAT_CANCELLED), -1, textFont.get(), rcLayout, &cformat, &textBr);
		}
		else if (batchCancelPrompt)
		{
			std::unique_ptr<Gdiplus::Font> textFont(CreateGPFont(_T("Tahoma"), 24, 400, false));
			g.DrawString(LoadStringT(IDS_CAPSTAT_BATCH_CONFIRM_CXL), -1, textFont.get(), rcLayout, &cformat, &textBr);
		}
		else
		{
			//
			// No special modes - show the normal status screen, with the
			// current operation message and the countdown timers.
			//

			// measure the text for the top title area
			TSTRINGEx title;
			std::unique_ptr<Gdiplus::Font> titleFont(CreateGPFont(_T("Tahoma"), 22, 400, false));
			title.Load(isBatch ? IDS_CAPSTAT_BATCH_TITLE : IDS_CAPSTAT_TITLE);
			Gdiplus::RectF bbox;
			g.MeasureString(title.c_str(), -1, titleFont.get(), rcLayout, &cformat, &bbox);

			// fill the title bar area and draw the title text
			Gdiplus::RectF rcTitleBar = rcLayout;
			rcTitleBar.X += frameWidth;
			rcTitleBar.Height = bbox.Height + 16;
			g.FillRectangle(&frameBrush, rcTitleBar);
			g.DrawString(title.c_str(), -1, titleFont.get(), rcTitleBar, &cformat, &titleTextBr);

			// format a time value
			auto FmtTime = [](DWORD ms)
			{
				int s = ms / 1000;
				int hh = s / 3600;
				int mm = (s % 3600) / 60;
				int ss = (s % 60);

				if (hh > 0)
					return MsgFmt(_T("%d:%02d:%02d"), hh, mm, ss);
				else
					return MsgFmt(_T("%d:%02d"), mm, ss);
			};

			// get the text for the bottom control area
			TSTRINGEx ctls;
			std::unique_ptr<Gdiplus::Font> ctlFont(CreateGPFont(_T("Tahoma"), 16, 700, false));
			if (manualStartMode || manualStopMode)
			{
				// figure the main prompt
				int prompt = manualStartMode ? IDS_CAPSTAT_MANUAL_START_PROMPT : IDS_CAPSTAT_MANUAL_STOP_PROMPT;

				// get the string for the button gesture
				TSTRINGEx gesture;
				gesture.Load(manualGoResId);

				// format the prompt message
				ctls.Format(LoadStringT(prompt), gesture.c_str());
			}
			else if (manualStopMode)
			{
				// show the manual stop prompt
				ctls.Load(IDS_CAPSTAT_MANUAL_STOP_PROMPT);
			}
			else
			{
				// regular mode - show "press exit to cancel"
				ctls.Load(IDS_CAPSTAT_EXIT_KEY);
			}

			// fill the control area and draw the text
			Gdiplus::RectF rcCtlBar = rcLayout;
			g.MeasureString(_T("X"), -1, ctlFont.get(), rcLayout, &cformat, &bbox);
			rcCtlBar.Height = bbox.Height*3 + 20;
			rcCtlBar.Y += rcLayout.Height - rcCtlBar.Height - frameWidth;
			rcCtlBar.X += frameWidth;
			rcCtlBar.Width -= frameWidth * 2;
			g.FillRectangle(&ctlBr, rcCtlBar);
			g.DrawString(ctls.c_str(), -1, ctlFont.get(), rcCtlBar, &cformat, &ctlTextBr);

			// draw the progress bar
			Gdiplus::RectF rcProgBar = rcCtlBar;
			rcProgBar.Height = bbox.Height * 1.25f;
			rcProgBar.Y -= rcProgBar.Height;
			Gdiplus::SolidBrush progBkgBr(Gdiplus::Color(192, 220, 192));
			Gdiplus::SolidBrush progBarBr(Gdiplus::Color(0, 192, 0));
			g.FillRectangle(&progBkgBr, rcProgBar);
			rcProgBar.Width *= isBatch ? batchTime.Progress() : gameTime.Progress();
			g.FillRectangle(&progBarBr, rcProgBar);

			// generate the main status text
			TSTRINGEx statusTxt, timeLabel, timeVal;
			if (isBatch)
			{
				// Batch mode
				statusTxt.Format(LoadStringT(IDS_CAPSTAT_BATCH_GAME), nCurGame, nGames);
				statusTxt += _T("\n\n") + status;

				timeLabel.Load(IDS_CAPSTAT_BATCH_TIMES);
				timeVal.Format(_T("\n%s\n%s\n%s"), FmtTime(curOpTime.rem), FmtTime(gameTime.rem), FmtTime(batchTime.rem));
			}
			else
			{
				// Single game capture mode
				statusTxt = status;
				timeLabel.Load(IDS_CAPSTAT_TIMES);
				timeVal.Format(_T("\n%s\n%s"), FmtTime(curOpTime.rem), FmtTime(gameTime.rem));
			}

			// figure the text area
			std::unique_ptr<Gdiplus::Font> txtFont(CreateGPFont(_T("Tahoma"), 14, 400, false));
			Gdiplus::RectF rcTxt = rcLayout;
			int txtMargin = 30;
			rcTxt.Y += rcTitleBar.Height + txtMargin;
			rcTxt.X += txtMargin + frameWidth;
			rcTxt.Height -= rcTitleBar.Height + rcCtlBar.Height + rcProgBar.Height - txtMargin * 2;
			rcTxt.Width -= txtMargin * 2 + frameWidth * 2;

			// draw the status text
			g.DrawString(statusTxt.c_str(), -1, txtFont.get(), rcTxt, &tformat, &textBr);
			g.MeasureString(statusTxt.c_str(), -1, txtFont.get(), rcTxt, &tformat, &bbox);
			rcTxt.Y += bbox.Height + 24;

			// draw the 'time remaining' labels
			g.DrawString(timeLabel.c_str(), -1, txtFont.get(), rcTxt, &tformat, &textBr);
			g.MeasureString(timeLabel.c_str(), -1, txtFont.get(), rcTxt, &tformat, &bbox);

			// draw the time values, right-justified with a bit of padding to the left
			Gdiplus::RectF tvbox;
			g.MeasureString(timeVal.c_str(), -1, txtFont.get(), rcTxt, &rformat, &tvbox);
			rcTxt.X += bbox.Width + 10 + tvbox.Width;
			rcTxt.Width = tvbox.Width;
			g.DrawString(timeVal.c_str(), -1, txtFont.get(), rcTxt, &rformat, &textBr);
		}

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

	case BlinkTimerId:
		OnBlinkTimer();
		return true;
	}

	return __super::OnTimer(timer, callback);
}

void CaptureStatusWin::OnBlinkTimer()
{
	// invert the blink state
	blinkState = !blinkState;

	// redraw
	InvalidateRect(hWnd, NULL, FALSE);

	// set the next timer
	SetTimer(hWnd, BlinkTimerId, blinkState ? BlinkOnTime : BlinkOffTime, NULL);
}

void CaptureStatusWin::OnCountdownTimer()
{
	// figure the elapsed time since the last update
	DWORD now = GetTickCount();
	DWORD dt = now - lastTicks;
	lastTicks = now;

	// Don't update any of the timers in Manual Start mode.
	// We're just waiting for the user in this mode, so none
	// of the progress clocks are running.
	if (manualStartMode)
		return;

	// Deduct the elapsed time from the running counters
	bool redraw = false;
	auto Update = [this, &redraw, dt](OpTime &t)
	{
		CriticalSectionLocker locker(lock);

		// figure the new time, stopping when we reach zero
		DWORD tOld = t.rem;
		t.rem = dt <= t.rem ? t.rem - dt : 0;

		// note if this is a change in whole seconds
		if (t.rem/1000 != tOld/1000)
			redraw = true;
	};
	Update(curOpTime);
	Update(gameTime);
	Update(batchTime);

	// if anything changed, redraw the window
	if (redraw)
		InvalidateRect(hWnd, NULL, FALSE);
}
