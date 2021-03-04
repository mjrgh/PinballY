// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Custom Window frame window.  Custom windows are created by user Javascript code.

#include "stdafx.h"
#include "../Utilities/Config.h"
#include "Resource.h"
#include "Application.h"
#include "CustomView.h"
#include "CustomWin.h"
#include "PlayfieldView.h"
#include "JavascriptEngine.h"

// map of all active custom windows, by serial number
std::unordered_map<int, CustomWin*> CustomWin::allCustomWins;


// construction
CustomWin::CustomWin(int serial, JsValueRef jsobj, const TCHAR *configVarPrefix, const TCHAR *title) : 
	FrameWin(configVarPrefix, title, IDI_MAINICON, IDI_MAINICON_GRAY),
	serialNum(serial),
	configVarPrefix(configVarPrefix),
	title(title)
{
	// remember the Javascript object, keeping a reference
	JsAddRef(this->jsobj = jsobj, nullptr);

	// Add me to the custom window map
	allCustomWins.emplace(serial, this);
}

CustomWin::~CustomWin()
{
	// release our reference on the Javascript object
	JsRelease(jsobj, nullptr);
	jsobj = JS_INVALID_REFERENCE;

	// remove me from the list of active custom windows
	allCustomWins.erase(serialNum);
}

BaseView *CustomWin::CreateViewWin()
{
	// create our view
	CustomView *view = new CustomView(jsobj, configVarPrefix.c_str());
	if (!view->Create(hWnd, title.c_str()))
	{
		view->Release();
		return 0;
	}

	// return the window
	return view;
}

void CustomWin::DestroyAll()
{
	// Make a list of the frame windows.  We create a private local list before
	// destroying any windows, so that we don't have to worry about the allCustomViews
	// list getting updated as windows are closed.  That could make the iteration
	// unstable if we iterated through allCustomViews directly while doing the
	// window destruction.
	std::list<HWND> frames;
	for (auto cv : allCustomWins)
		frames.push_back(cv.second->GetHWnd());

	// destroy all of the frame windows
	for (auto frame : frames)
		DestroyWindow(frame);
}

CustomWin *CustomWin::GetBySerial(int n)
{
	auto it = allCustomWins.find(n); 
	return it != allCustomWins.end() ? it->second : nullptr;
}

// call a callback for each custom window
bool CustomWin::ForEachCustomWin(std::function<bool(CustomWin*)> f)
{
	for (auto it : allCustomWins)
	{
		if (!f(it.second))
			return false;
	}
	return true;
}
