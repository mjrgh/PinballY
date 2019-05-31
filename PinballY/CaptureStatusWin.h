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

	// Set manual start mode.  In this mode, we're paused waiting for
	// the user to press a button to start the next capture.  We show
	// a prompt to this effect to let the user know what to do.
	void SetManualStartMode(bool f);

	// Set manual stop mode.  In this mode, a capture is running, and
	// will continue running until the user presses a button sequence.
	// We display a prompt to let the user know that they must press
	// a button to stop the capture.
	void SetManualStopMode(bool f);

	// position the window over the given frame window
	void PositionOver(FrameWin *win);

	// Set the estimated total time for the capture process.  For
	// a batch capture, this represents the time for the current
	// game only.
	void SetTotalTime(DWORD time_ms);

	// Set the batch capture information, if applicable.  Times are
	// in milliseconds.
	void SetBatchInfo(int nCurGame, int nGames, int remainingTime_ms, int totalTime_ms);

	// Set the drawing rotation, in degrees
	void SetRotation(float angle);

	// Set mirroring
	void SetMirrorVert(bool f);
	void SetMirrorHorz(bool f);

	// Show/hide the batch capture cancellation prompt
	void BatchCaptureCancelPrompt(bool show);

	// Show a "cancellation in progress" message
	void ShowCaptureCancel();

protected:
	// width and height
	static const int winWidth = 640;
	static const int winHeight = 480;

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
	static const int BlinkTimerId = 2;         // blinking text timer

	// current status message
	TSTRING status;

	// is the batch capture prompt showing?
	bool batchCancelPrompt = false;

	// capture has been cancelled
	bool cancelled = false;

	// manual start/stop mode
	bool manualStartMode = false;
	bool manualStopMode = false;

	// For manual start/stop mode, the resource ID for the trigger buttons
	int manualGoResId = IDS_CAPSTAT_BTN_FLIPPERS;

	// Update blinking modes.  Call this after changing one of
	// the modes that involves blinking prompt text.
	void UpdateBlinkMode();

	// blink timer event
	void OnBlinkTimer();

	// Blinking text on/off state.  This is used for blinking text
	// in the capture prompts when user action is required (Manual
	// Start, Manual Stop)
	bool blinkState = 1;

	// Time progress.  All times are in milliseconds.
	struct OpTime
	{
		OpTime() : total(0), rem(0) { }

		// estimated total time for this operation
		DWORD total;

		// remaining time
		DWORD rem;

		// Figure the progress as the fraction of the total time
		// completed so far.
		float Progress() const
		{
			// we can't do the division if the total time is zero
			if (total == 0)
				return 0.0f;

			// figure the fraction completed
			return fmaxf(float(total - rem), 0.0f) / float(total);
		}
	};

	// Is this a batch capture?  You might think that we could
	// simply use "nGames > 1" to mean "batch", but that doesn't
	// quite work, because it's perfectly okay to have a batch
	// consisting of a single game.  Starting a capture via the
	// batch UI still makes it count as a batch even if it only
	// includes one game, since we use a slightly different UI
	// for a batch to reflect the overall batch progress.
	bool isBatch = false;

	// Number of games in the overall batch.  For a single-game
	// capture, nGames is 1 and nCurGame is 1; that's the same for
	// a batch capture with a single game, but we can distinguish
	// it via isBatch.
	int nGames = 1;
	int nCurGame = 1;

	// Time for the whole operation.  For a batch, this is the total
	// time for all games in the batch.  For a single game, this is
	// the same as the current game time.
	OpTime batchTime;

	// time for the current game
	OpTime gameTime;

	// time for the current individual operation, which might be a
	// capture or a pause
	OpTime curOpTime;

	// system tick count at last timer update
	DWORD lastTicks;

	// current drawing rotation
	float rotation = 0;

	// mirroring
	bool mirrorHorz = false;
	bool mirrorVert = false;

	// lock for thread access
	CriticalSection lock;
};
