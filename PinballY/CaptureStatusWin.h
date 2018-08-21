// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "BaseWin.h"

class FrameWin;

class CaptureStatusWin : public BaseWin
{
public:
	CaptureStatusWin();
	~CaptureStatusWin();

	// we don't have any menu commands to update
	virtual void UpdateMenu(HMENU, BaseWin *) override { }

	// Set the current capture status.  This sets the current status
	// text and the estimated time for this operation.
	void SetCaptureStatus(const TCHAR *msg, DWORD time_ms);

	// position the window over the given frame window
	void PositionOver(FrameWin *win);

	// Set the estimated total time for the capture process
	void SetTotalTime(DWORD time_ms);

	// Set the drawing rotation, in degrees
	void SetRotation(float angle);

	// Set the mirroring
	void SetMirrorVert(bool f);
	void SetMirrorHorz(bool f);

protected:
	// width and height
	static const int winWidth = 520;
	static const int winHeight = 360;

	// get the initial window position for creation
	virtual RECT GetCreateWindowPos(int &nCmdShow) override;

	// window creation
	virtual bool OnCreate(CREATESTRUCT *cs) override;

	// since we redraw the entire window on each update, there's
	// no need to erase the background
	virtual bool OnEraseBkgnd(HDC hdc) { return true; }

	// paint
	virtual void OnPaint(HDC) override;

	// timers
	virtual bool OnTimer(WPARAM timer, LPARAM callback) override;
	void OnCountdownTimer();

	// timer IDs
	static const int CountdownTimerId = 1;     // estimated time updater

	// current status message
	TSTRING status;

	// estimated time remaining in current operation, in millseconds
	DWORD curOpTime;

	// estimated total time remaining, in milliseconds
	DWORD totalTime;

	// system tick count at last timer update
	DWORD lastTicks;

	// current drawing rotation
	float rotation;

	// mirroring
	bool mirrorHorz;
	bool mirrorVert;

	// lock for thread access
	CriticalSection lock;
};
