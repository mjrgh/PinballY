// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Playfield window

#pragma once

#include "stdafx.h"
#include "FrameWin.h"

class PlayfieldView;

// Playfield frame window
class PlayfieldWin : public FrameWin
{
public:
	// construction
	PlayfieldWin();

	// application foreground/background changes
	virtual void OnAppActivationChange(bool activating) override;

protected:
	// destruction - called internally when the reference count reaches zero
	~PlayfieldWin();

	// create my view window
	virtual BaseView *CreateViewWin();

	// handle raw input
	virtual void OnRawInput(UINT rawInputCode, HRAWINPUT hRawInput) override;

	// Process a raw input device change event (WM_INPUT_DEVICE_CHANGED)
	virtual void OnRawInputDeviceChange(USHORT what, HANDLE hDevice) override;

	// terminate the application on closing the main window
	virtual bool OnNCDestroy() override;

	// App deactivation while in full-screen mode.  The base class handler
	// sends the window to the bottom of the stack, which is appropriate for
	// the secondary windows, since these windows are usually in monitor
	// areas used for auxiliary windows in the game (backglass, DMD) that
	// might not actually be owned by the game process and thus might not
	// automatically switch to the foreground on an app switch.  The
	// playfield window usually covers the same monitor area as the main
	// game program, though, so it should always come up in front of our
	// window as a natural consequence of Windows context switching.  And
	// it can be undesirable to do the explicit send-to-back when we don't
	// have to, since it can bring forward other windows from other 
	// unrelated apps that AREN'T being activated, which looks clunky.
	virtual void DeactivateFullScreen() override { }
};
