// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Custom Window view, for windows created by user Javascript code

#include "stdafx.h"
#include "../Utilities/Config.h"
#include "../Utilities/GraphicsUtil.h"
#include "CustomView.h"
#include "CustomWin.h"
#include "PlayfieldView.h"
#include "Resource.h"
#include "D3D.h"
#include "D3DWin.h"
#include "Camera.h"
#include "TextDraw.h"
#include "VersionInfo.h"
#include "VideoSprite.h"
#include "GameList.h"
#include "Application.h"
#include "MouseButtons.h"
#include "JavascriptEngine.h"

using namespace DirectX;

// Get the nth custom view from the list
CustomView *CustomView::GetBySerial(int n)
{
	auto frame = CustomWin::GetBySerial(n);
	return frame != nullptr ? dynamic_cast<CustomView*>(frame->GetView()) : nullptr;
}

// call a callback for each custom view
bool CustomView::ForEachCustomView(std::function<bool(CustomView*)> f)
{
	return CustomWin::ForEachCustomWin([&f](CustomWin *win) {
		if (auto view = dynamic_cast<CustomView*>(win->GetView()); view != nullptr)
			return f(view);
		return true;
	});
}


// construction
CustomView::CustomView(JsValueRef jsobj, const TCHAR *configVarPrefix) : 
	SecondaryView(IDR_CUSTOMVIEW_CONTEXT_MENU, configVarPrefix)
{
	// keep a reference to the Javascript object
	JsAddRef(this->jsobj = jsobj, nullptr);
}

CustomView::~CustomView()
{
	// release our Javascript object
	JsRelease(jsobj, nullptr);
}

JsValueRef CustomView::JsGetShowMediaWhenRunningFlag() const
{
	auto js = JavascriptEngine::Get();
	return showMediaWhenRunningFlag == SM_UNDEF ? js->GetUndefVal() :
		showMediaWhenRunningFlag == SM_SHOW ? js->GetTrueVal() :
		js->GetFalseVal();
}

void CustomView::JsSetShowMediaWhenRunningFlag(JsValueRef f)
{
	auto js = JavascriptEngine::Get();
	showMediaWhenRunningFlag = (f == js->GetUndefVal() ? SM_UNDEF :
		js->IsFalsy(f) ? SM_NOSHOW : SM_SHOW);
}

bool CustomView::ShowMediaWhenRunning(GameListItem *game, GameSystem *system) const
{
	switch (showMediaWhenRunningFlag)
	{
	case SM_SHOW:
		return true;

	case SM_NOSHOW:
		return false;

	default:
		// Undefined - use the normal ID key mechanism.  This only applies
		// if we actually have an ID key, though!  If not, don't show media.
		return showMediaWhenRunningId.size() != 0 ? __super::ShowMediaWhenRunning(game, system) : false;
	}
}

void CustomView::OnBeginMediaSync()
{
	// clear the media sync flag in all of our windows
	ForEachCustomView([](CustomView *cv) { cv->mediaSyncFlag = false; return true; });
}


void CustomView::SyncNextCustomView()
{
	// if we're not in simultaneous sync mode, sync the next window
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr && !pfv->IsSimultaneousSync())
	{
		// scan for the next custom view that hasn't been sync'ed on this round
		ForEachCustomView([](CustomView *cv)
		{
			// if this window hasn't been synchronized yet, do so now
			if (!cv->mediaSyncFlag)
			{
				// kick off the media load for this window
				cv->SyncCurrentGame();

				// mark this window as synchronized for this pass
				cv->mediaSyncFlag = true;

				// Stop looking - only sync one window on each command cycle, in
				// keeping with our sequential media loading in the standard windows.
				// (This is to avoid overloading the CPU with simultaneous media
				// load operations - opening several videos at the same time can
				// stall video playback on anything but a high-end machine.)
				return false;
			}

			// keep scanning
			return true;
		});
	}
}

void CustomView::SyncAllCustomViews()
{
	// make a private list of custom views, so that we don't run any risk of
	// the rendering code changing the list while we're iterating over it
	std::list<RefPtr<CustomView>> allViews;
	ForEachCustomView([&allViews](CustomView *cv) { allViews.emplace_back(cv, RefCounted::DoAddRef); return true; });

	// synchronize each game in our private list
	for (auto &cv : allViews)
		cv->SyncCurrentGame();
}

// Process WM_INITMENUPOPUP
void CustomView::UpdateMenu(HMENU hMenu, BaseWin *fromWin)
{
	// Update the "Hide <this window>" item with the window title.  If
	// this is the first time we've done this, the menu will still contain
	// template text with a "%s" substitution parameter where the window
	// title goes, and our private copy of the template will be blank.
	MENUITEMINFO mii;
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_FTYPE | MIIM_STRING;
	if (origHideThisWindowMenuLabel.size() == 0)
	{
		TCHAR buf[256];
		mii.cch = countof(buf);
		mii.dwTypeData = buf;
		if (GetMenuItemInfo(hMenu, ID_HIDE, FALSE, &mii))
			origHideThisWindowMenuLabel = buf;
	}

	// Now substitute the window title into the HIDE item text
	TCHAR title[256], newLabel[256];
	GetWindowText(hWnd, title, countof(title));
	_stprintf_s(newLabel, origHideThisWindowMenuLabel.c_str(), title);
	mii.dwTypeData = newLabel;
	SetMenuItemInfo(hMenu, ID_HIDE, FALSE, &mii);

	// continue with base class handing
	__super::UpdateMenu(hMenu, fromWin);
}

