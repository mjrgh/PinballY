// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Custom Window frame window.  Custom windows are created by user code via
// Javascript.


#pragma once

#include "FrameWin.h"
#include "JavascriptEngine.h"

class TopperView;

// Playfield frame window
class CustomWin : public FrameWin
{
public:
	// construction
	CustomWin(int serialNum, JsValueRef jsobj, const TCHAR *configVarPrefix, const TCHAR *title);

	// get a custom window by serial number
	static CustomWin *GetBySerial(int n);

	// Call a callback for each custom window.  Stops when the callback returns false.
	// Returns the result from the last callback, or true if no callbacks are invoked.
	static bool ForEachCustomWin(std::function<bool(CustomWin*)>);

	// Destroy all of the custom windows
	static void DestroyAll();

protected:
	virtual ~CustomWin() override;

	// create my view window
	virtual BaseView *CreateViewWin() override;

	// hide the window on minimize or close
	virtual bool IsHideable() const override { return true; }

	// serial number of the window, assigned by PlayfieldView at creation
	int serialNum;

	// Javascript object representing the window
	JsValueRef jsobj;

	// configuration variable prefix
	TSTRING configVarPrefix;

	// custom window title
	TSTRING title;

	// Map of all CustomView windows currently open, indexed by serial number.
	// Access to this list is limited to the main UI thread (the thread that
	// processes window messages, which is also the startup thread).
	static std::unordered_map<int, CustomWin*> allCustomWins;
};
