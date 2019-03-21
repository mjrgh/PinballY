// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/FileUtil.h"
#include "BaseView.h"
#include "Application.h"
#include "PlayfieldView.h"
#include "VideoSprite.h"
#include "MediaDropTarget.h"
#include "MouseButtons.h"
#include "LogFile.h"

BaseView::~BaseView()
{
}

bool BaseView::Create(HWND parent, const TCHAR *title)
{
	// do the base class creation
	if (!D3DView::Create(parent, title, WS_CHILD | WS_VISIBLE, SW_SHOWNORMAL))
		return false;

	// update the "About <this program>" item with the name of the host application
	MENUITEMINFO mii;
	TCHAR aboutBuf[256];
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_FTYPE | MIIM_STRING;
	mii.cch = countof(aboutBuf);
	mii.dwTypeData = aboutBuf;
	if (GetMenuItemInfo(hContextMenu, ID_ABOUT, FALSE, &mii))
	{
		// The resource string is in the format "About %s", so use this
		// as a sprintf format string to format the final string with
		// the actual host application name.
		TCHAR newAbout[256];
		_stprintf_s(newAbout, aboutBuf, Application::Get()->Title.c_str());
		mii.dwTypeData = newAbout;
		SetMenuItemInfo(hContextMenu, ID_ABOUT, FALSE, &mii);
	}

	// set the context menu's key shortcuts, if the playfield view
	// exists yet
	if (PlayfieldView *pfView = Application::Get()->GetPlayfieldView(); pfView != 0)
		pfView->UpdateMenuKeys(GetSubMenu(hContextMenu, 0));

	// success
	return true;
}

bool BaseView::OnCreate(CREATESTRUCT *cs)
{
	// do the base class work
	bool ret = __super::OnCreate(cs);

	// register our media drop target
	dropTarget.Attach(new MediaDropTarget(this));

	// return the base class result
	return ret;
}

bool BaseView::OnDestroy()
{
	// revoke our system drop target
	if (dropTarget != nullptr)
	{
		dropTarget->OnDestroyWindow();
		dropTarget = nullptr; 
	}

	// do the base class work
	return __super::OnDestroy();
}

bool BaseView::OnKeyEvent(UINT msg, WPARAM wParam, LPARAM lParam)
{
	// hide the cursor on any key input
	Application::HideCursor();

	// run it through the playfield view key handler
	if (PlayfieldView *v = Application::Get()->GetPlayfieldView();
		v != 0 && v->HandleKeyEvent(this, msg, wParam, lParam))
		return true;

	// use the inherited handling
	return __super::OnKeyEvent(msg, wParam, lParam);
}

bool BaseView::OnSysKeyEvent(UINT msg, WPARAM wParam, LPARAM lParam)
{
	// hide the cursor on any key input
	Application::HideCursor();

	// run it through the playfield view key handler
	if (PlayfieldView *v = Application::Get()->GetPlayfieldView();
	    v != 0 && v->HandleSysKeyEvent(this, msg, wParam, lParam))
		return true;

	// not handled there - inherit the default processing
	return __super::OnSysKeyEvent(msg, wParam, lParam);
}

bool BaseView::OnSysChar(WPARAM wParam, LPARAM lParam)
{
	// hide the cursor on any key input
	Application::HideCursor();

	// run it through the playfield view key handler
	if (PlayfieldView *v = Application::Get()->GetPlayfieldView();
	    v != 0 && v->HandleSysCharEvent(this, wParam, lParam))
		return true;

	// not handled - inherit the default processing
	return __super::OnSysChar(wParam, lParam);
}

bool BaseView::OnCommand(int cmd, int source, HWND hwndControl)
{
	switch (cmd)
	{
	case ID_ABOUT:
	case ID_HELP:
	case ID_OPTIONS:
		// forward to the main playfield view
		if (PlayfieldView *v = Application::Get()->GetPlayfieldView(); v != 0)
			v->SendMessage(WM_COMMAND, cmd);
		return true;

	case ID_VIEW_BACKGLASS:
	case ID_VIEW_DMD:
    case ID_VIEW_PLAYFIELD:
    case ID_VIEW_TOPPER:
    case ID_VIEW_INSTCARD:
		// reflect these to our parent frame
		::SendMessage(GetParent(hWnd), WM_COMMAND, cmd, 0);
		return true;
	}

	// not handled - inherit default handling
	return __super::OnCommand(cmd, source, hwndControl);
}

Sprite *BaseView::PrepInstructionCard(const TCHAR *filename)
{
	// get the file dimensions
	ImageFileDesc imageDesc;
	GetImageFileInfo(filename, imageDesc, true);

	// Figure the aspect ratio of the card.  The scale of the sprite 
	// dimensions are arbitrary, because the window will automatically
	// rescale the sprite to fill the width and/or height, but we do
	// need to set the aspect ratio to maintain the correct geometry.
	// Use a normalized height of 1.0, and set the width proportionally.
	float aspect = imageDesc.dispSize.cy == 0 ? 1.0f : float(imageDesc.dispSize.cx) / float(imageDesc.dispSize.cy);
	float ht = 1.0f;
	float wid = ht * aspect;
	POINTF normSize = { wid, ht };

	// Figure an initial pixel size based on our window layout.  This
	// isn't actually important, because we'll revise this in when we
	// scale the image to fit the window layout.
	SIZE pixSize = { (int)(wid * szLayout.cy), (int)(ht * szLayout.cy) };

	// load the image at the calculated size
	CapturingErrorHandler eh;
	RefPtr<Sprite> sprite(new Sprite());
	if (!sprite->Load(filename, normSize, pixSize, eh))
	{
		// Load failed.  If the file is an SWF (Shockwave Flash), handle
		// it with a special error in the main window, to give the user
		// a chance to disable SWF loading in the future.  Otherwise 
		// just show the error normally.
		auto pfv = Application::Get()->GetPlayfieldView();
		if (imageDesc.imageType == ImageFileDesc::SWF)
			pfv->ShowFlashError(eh);
		else
			pfv->ShowError(EIT_Error, nullptr, &eh);

		// return failure
		return nullptr;
	}

	// return the new sprite, adding a reference on behalf of the caller
	sprite->AddRef();
	return sprite;
}

bool BaseView::OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case BVMsgAsyncSpriteLoadDone:
		{
			// get the parameters as pointers
			auto sprite = reinterpret_cast<VideoSprite*>(wParam);
			auto thread = reinterpret_cast<AsyncSpriteLoader::Thread*>(lParam);

			// invoke the Load Done callback on the loader object
			thread->loader->OnAsyncSpriteLoadDone(sprite, thread);
		}
		return true;
	}

	return __super::OnUserMessage(msg, wParam, lParam);
}

bool BaseView::OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case AVPMsgEndOfPresentation:
		// check for the end of the overlay video
		if (videoOverlay != nullptr && videoOverlay->GetVideoPlayerCookie() == wParam)
			OnEndOverlayVideo();
		break;
	}

	return __super::OnAppMessage(msg, wParam, lParam);
}

void BaseView::OnEndOverlayVideo()
{
	// if the startup video is ending, remove it
	if (videoOverlayID == _T("Startup"))
		OnEndStartupVideo();
}

void BaseView::AsyncSpriteLoader::AsyncLoad(
	bool sta,
	std::function<void(VideoSprite*)> load,
	std::function<void(VideoSprite*)> done)
{
#if 1
	// The asynchronous loading doesn't actually seem to make the
	// UI any more responsive, so let's keep things simple and just
    // do it synchronously for now - it might complicate things
    // with the video renderers and their D3D usage to load them
    // on a background thread.
	//
	// Fortunately, the async loader interface adapts trivially to
	// synchronous operation: we simply call the 'load' and 'done'
	// callbacks in sequence.  This lets us leave the async design
	// in place in the callers in case we ever want to reinstate 
	// actual async loading.  In principal, async loading should
	// give us smoother rendering while the load is happening, since
	// our UI thread's message loop shouldn't be interrupted by the
    // video loader or Flash loader.  In practice it doesn't seem
    // to matter, especially with libvlc, which does all of its
    // video decoding in separate threads anyway.  (It would have
	// been more important if we were using DirectShow or Windows
	// Media Foundation instead of libvlc, since those do more of 
	// the initial file loading work in the foreground thread.  WMF
	// in particular does a LOT of work in the calling thread on 
	// the initial video load, often about 100ms worth.  libvlc, in
	// contrast, just seems to spin up some threads and then sits
	// back and lets the threads do the heavy lifting.  In other
	// words, it already does what our async version does, so it
	// doesn't improve matters further to add our own async-ness.)

	// Create a sprite
	RefPtr<VideoSprite> sprite(new VideoSprite());

	// load it
	load(sprite);

	// complete the loading
	done(sprite);

#else
	// create the new async loader, replacing any previous one
	thread.Attach(new Thread(sta, this, load, done));

	// add a reference to the loader on behalf of its thread
	thread->AddRef();

	// launch the thread - use suspended mode so that we can
	// adjust the priority before starting it
	HANDLE hThread = CreateThread(NULL, 0, &Thread::ThreadMain, thread.Get(), 
		CREATE_SUSPENDED, NULL);
	if (hThread != NULL)
	{
		// success - keep the handle in our thread object
		thread->hThread = hThread;

		// bump down the priority so that the loading process doesn't 
		// glitch the UI too much, then let the thread run
		SetThreadPriority(hThread, THREAD_PRIORITY_BELOW_NORMAL);
		ResumeThread(hThread);
	}
	else
	{
		// The thread launch failed.   The thread won't be needing its reference, and the 
		// object is now defunct
		thread->Release();
		thread = nullptr;
	}
#endif
}

#if 0
BaseView::AsyncSpriteLoader::Thread::Thread(
	bool sta,
	AsyncSpriteLoader *loader,
	std::function<void(VideoSprite*)> load,
	std::function<void(VideoSprite*)> done)
	: sta(sta), loader(loader), load(load), done(done)
{
	// Explicitly initialize our reference on the containing view,
	// so that we count the added reference.  DON'T use the 
	// RefPtr constructor initializer form here, as that's for a 
	// newly constructed object that already counts our reference.
	// In this case we explicitly want to add an extra reference.
	view = loader->view;
}


DWORD BaseView::AsyncSpriteLoader::Thread::Main()
{
	// enter background work mode
	SetThreadPriority(hThread, THREAD_MODE_BACKGROUND_BEGIN);

	// Initialize OLE or COM on this thread, as needed
	if (sta)
		OleInitialize(NULL);
	else
		CoInitializeEx(NULL, COINIT_MULTITHREADED);

	// Create a sprite
	RefPtr<VideoSprite> sprite(new VideoSprite());

	// call the loader callback
	load(sprite);

	// Send a message to our window to tell it that we've finished
	// loading.  This is our thread synchronization mechanism: the
	// window message will be handled on the main UI thread, which 
	// is the same thread that initiated the load, so this completes
	// the load back on the main thread.  That means the completion
	// callback doesn't need to do anything about special for thread
	// safety or synchronization.
	//
	// Note that we have a counted reference on the window object,
	// so the window object will still be valid even if the UI window
	// has been closed.  So check the window handle to make sure it's
	// valid.  If it's not, we can simply abandon the load.
	HWND hWnd = view->hWnd;
	if (hWnd != NULL && IsWindow(hWnd))
	{
		// Send the message.  Do this synchronously, so that our
		// counted reference on the sprite remains in effect until
		// the completion callback returns.  If we posted the message
		// asynchronously, we'd have to add a reference on behalf of
		// the message itself to keep the sprite in memory until the
		// window got the message and added its own reference.  But
		// we can't gauarantee delivery of a posted message (the 
		// window could close after we call PostMessage() but before
		// the message is received), so we can't guarantee that the
		// added reference would be removed - thus we could leak the
		// sprite.  Doing it synchronously guarantees that we'll both
		// keep the sprite in memory until the window takes ownership
		// and release our reference when done.
		::SendMessage(hWnd, BVMsgAsyncSpriteLoadDone,
			reinterpret_cast<WPARAM>(sprite.Get()),
			reinterpret_cast<LPARAM>(this));
	}

	// remove the reference on self for the thread
	Release();

	// shut down OLE/COM on the thread
	if (sta)
		OleUninitialize();
	else
		CoUninitialize();

	// done
	return 0;
}
#endif

void BaseView::AsyncSpriteLoader::OnAsyncSpriteLoadDone(VideoSprite *sprite, Thread *thread)
{
	// If the completing thread is our active thread, invoke the complation
	// callback.  If we've switched to a new active thread, the one that's
	// finishing has been abandoned, so its sprite is no longer needed.  We
	// can simply skip the completion callback in this case.  The thread's
	// reference on the sprite will be the only one outstanding, so when the
	// thread exits, the abandoned sprite will be automatically deleted.
	if (this->thread == thread)
	{
		// this thread is still active - invoke its callback
		thread->done(sprite);

		// we're now done tracking this thread - forget it
		thread = nullptr;
	}
}

void BaseView::DrawDropAreaList(POINT pt)
{
	// set up the drop feedback sprite
	int width = szLayout.cx, height = szLayout.cy;
	dropTargetSprite.Attach(new Sprite());
	dropTargetSprite->Load(width, height, [this, pt, width, height](Gdiplus::Graphics &g)
	{
		// fill the window
		Gdiplus::SolidBrush bkg(Gdiplus::Color(128, 0, 0, 0));
		Gdiplus::SolidBrush hibr(Gdiplus::Color(128, 0, 0, 255));
		Gdiplus::Pen pen(Gdiplus::Color(128, 255, 255, 255), 2);
		g.FillRectangle(&bkg, 0, 0, width, height);

		// set up for text drawing
		Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
		fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
		fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		Gdiplus::SolidBrush txtbr(Gdiplus::Color(255, 255, 255, 255));
		std::unique_ptr<Gdiplus::Font> font(CreateGPFont(_T("Tahoma"), 36, 400));

		// Find the drop area at the current mouse location
		activeDropArea = FindDropAreaHit(pt);

		// Figure the top location of the uppermost button with a specific
		// button area.  We'll draw the background button caption above 
		// this area to make sure it doesn't overlap any of the buttons.
		int topBtn = height;
		for (auto const &a : dropAreas)
		{
			if (!IsRectEmpty(&a.rc) && a.rc.top < topBtn)
				topBtn = a.rc.top;
		}

		// draw each area, from back to front
		for (auto const &a : dropAreas)
		{
			// Get the area.  An empty rect means to use the whole window.
			Gdiplus::RectF rc;
			if (IsRectEmpty(&a.rc))
				rc = { 0.0f, 0.0f, (float)width, (float)height };
			else
				rc = { (float)a.rc.left, (float)a.rc.top, (float)(a.rc.right - a.rc.left), (float)(a.rc.bottom - a.rc.top) };

			// fill the background
			g.FillRectangle(&a == activeDropArea && a.hilite ? &hibr : &bkg, rc);

			// draw an outline around this area
			g.DrawRectangle(&pen, rc);

			// Figure the label.  If a string was provided directly, use that.
			// Otherwise, if a media type was provided, say "Drop <media type> here".
			const TCHAR *label = nullptr;
			TSTRINGEx typeLabel;
			if (a.label.length() != 0)
			{
				// use the provided literal label text
				label = a.label.c_str();
			}
			else if (a.mediaType != nullptr)
			{
				// generate a label from the media type name
				typeLabel.Format(LoadStringT(IDS_MEDIA_DROP_TYPE_HERE), LoadStringT(a.mediaType->nameStrId).c_str());
				label = typeLabel.c_str();
			}

			// draw the caption
			if (label != nullptr)
			{
				// start with the button rect
				Gdiplus::RectF rcTxt(rc);

				// if this is the background button, draw above the top button
				if (IsRectEmpty(&a.rc))
					rcTxt.Height = (float)topBtn;

				// insert a bit for a margin
				rcTxt.Inflate(-16.0f, -16.0f);

				// draw it
				g.DrawString(label, -1, font.get(), rcTxt, &fmt, &txtbr);
			}

		}

	}, Application::InUiErrorHandler(), _T("drop target sprite"));

	// update the drawing list with the new sprite
	UpdateDrawingList();
}

void BaseView::ShowDropTargets(const MediaDropTarget::FileDrop &fd, POINT pt, DWORD *pdwEffect)
{
	// presume that we won't accept the drop
	*pdwEffect = DROPEFFECT_NONE;

	// clear out any old drop area list
	dropAreas.clear();
	activeDropArea = nullptr;

	// by default, drops are processed through the playfield view, so we
	// can't proceed unless that's available
	auto pfv = Application::Get()->GetPlayfieldView();
	if (pfv == nullptr)
		return;

	// If more than one file is being dropped, reject it
	if (fd.GetNumFiles() > 1)
	{
		dropAreas.emplace_back(LoadStringT(IDS_MEDIA_DROP_ONE_AT_A_TIME).c_str());
		DrawDropAreaList(pt);
		return;
	}

	// Process the file (we already know there's only one)
	fd.EnumFiles([this, pdwEffect, pt](const TCHAR *fname)
	{
		// If it's an archive file of a type we can unpack, assume it's 
		// acceptable.  Don't scan it now, since this test is done on the 
		// initial drag entry and thus needs to be quick.  If the user ends
		// up dropping the file and it doesn't contain anything useful, 
		// we'll report an error at that point.  Make the initial judgment
		// based simply on the filename extension.
		if (tstriEndsWith(fname, _T(".zip"))
			|| tstriEndsWith(fname, _T(".rar"))
			|| tstriEndsWith(fname, _T(".7z")))
		{
			// It's an archive file.  Assume it's a media pack.
			dropAreas.emplace_back(LoadStringT(IDS_MEDIA_DROP_MEDIA_PACK).c_str());
			DrawDropAreaList(pt);
			*pdwEffect = DROPEFFECT_COPY;
			return;
		}

		// It's not an archive type.  Try building a drop area list for
		// the specific file type.
		if (BuildDropAreaList(fname))
		{
			// success - draw the sprite
			DrawDropAreaList(pt);
			*pdwEffect = DROPEFFECT_COPY;
			return;
		}
	});
}

bool BaseView::BuildDropAreaList(const TCHAR *filename)
{
	// Check for image or video files matching the background media 
	// types used for the target window, again based purely on the
	// filename extension.
	const MediaType *mt = nullptr;
	if (((mt = GetBackgroundImageType()) != nullptr && mt->MatchExt(filename))
		|| ((mt = GetBackgroundVideoType()) != nullptr && mt->MatchExt(filename)))
	{
		dropAreas.emplace_back(mt);
		return true;
	}

	// no match
	return false;
}

void BaseView::UpdateDropTargets(const MediaDropTarget::FileDrop &fd, POINT pt, DWORD *pdwEffect)
{
	// if there's a drop area list, find the active area under the mouse
	if (dropAreas.size() != 0)
	{
		// find the drop area we're over
		if (auto a = FindDropAreaHit(pt); a != activeDropArea)
			DrawDropAreaList(pt);
	}
}

BaseView::MediaDropArea *BaseView::FindDropAreaHit(POINT pt)
{
	// Scan for a hit starting at the end of the list.  The list is in
	// Z order from background to foreground, so we want to work backwards
	// through the list to find the frontmost object containing the point.
	for (auto it = dropAreas.rbegin(); it != dropAreas.rend(); ++it)
	{
		// If it's an empty rect, it represents the background object
		// that covers the whole window, so it's always a hit.  Otherwise,
		// it's a hit if the point is within the button wrectangle.
		if (IsRectEmpty(&it->rc) || PtInRect(&it->rc, pt))
			return &*it;
	}

	// no hit
	return nullptr;
}

void BaseView::RemoveDropTargets()
{
	// remove the drop target feedback sprite
	dropTargetSprite = nullptr;
	UpdateDrawingList();

	// clear the drop area list
	dropAreas.clear();
	activeDropArea = nullptr;
}

void BaseView::DoMediaDrop(const MediaDropTarget::FileDrop &fd, POINT pt, DWORD *pdwEffect)
{
	// no files processed yet
	int nFilesProcessed = 0;
	*pdwEffect = DROPEFFECT_NONE;

	// all drops are processed through the playfield view, so we
	// can't proceed unless that's available
	auto pfv = Application::Get()->GetPlayfieldView();
	if (pfv == nullptr)
		return;

	// get the current drop area
	MediaDropArea *area = FindDropAreaHit(pt);

	// begin the file drop operation
	pfv->BeginFileDrop();

	// process the dropped files
	fd.EnumFiles([this, pfv, &nFilesProcessed, area](const TCHAR *fname)
	{
		// try processing it through the playfield view
		if (pfv->DropFile(fname, dropTarget, area != nullptr ? area->mediaType : nullptr))
			++nFilesProcessed;
	});

	// end the drop operation
	pfv->EndFileDrop();

	// if we dropped any files, set the drop effect to 'copy' for the
	// caller's benefit, in case they want to show different feedback
	// for different results
	if (nFilesProcessed != 0)
		*pdwEffect = DROPEFFECT_COPY;
}

bool BaseView::IsBorderlessWindowMode(HWND parent)
{
	return parent != NULL
		&& ::SendMessage(parent, PWM_ISBORDERLESS, 0, 0) != 0
		&& ::SendMessage(parent, PWM_ISFULLSCREEN, 0, 0) == 0;
}

bool BaseView::OnNCHitTest(POINT pt, UINT &hit)
{
	// If the parent is borderless and not in full-screen mode, simulate
	// sizing borders around the perimeter of the client area.
	if (HWND parent = GetParent(hWnd); IsBorderlessWindowMode(parent))
	{
		// If it's within the sizing border with of an edge, let 
		// the parent window handle it.  This gives us an invisible
		// sizing border that acts like a normal sizing border, but
		// is covered out to the edge by the DMD contents.

		// figure the sizing border area of the parent, based on its
		// window style, but excluding the caption area
		RECT rcFrame = { 0, 0, 0, 0 };
		DWORD dwStyle = GetWindowLong(parent, GWL_STYLE);
		DWORD dwExStyle = GetWindowLong(parent, GWL_EXSTYLE);
		AdjustWindowRectEx(&rcFrame, dwStyle & ~WS_CAPTION, FALSE, dwExStyle);

		// get my window rect 
		RECT rcWindow;
		GetWindowRect(hWnd, &rcWindow);

		// check if we're in the sizing border
		if ((pt.x >= rcWindow.left && pt.x < rcWindow.left - rcFrame.left)
			|| (pt.x < rcWindow.right && pt.x >= rcWindow.right - rcFrame.right)
			|| (pt.y >= rcWindow.top && pt.y < rcWindow.top - rcFrame.top)
			|| (pt.y < rcWindow.bottom && pt.y >= rcWindow.bottom - rcFrame.bottom))
		{
			// it's in the sizing border - let the parent window handle it
			hit = HTTRANSPARENT;
			return true;
		}
	}

	// inherit the default handling
	return __super::OnNCHitTest(pt, hit);
}

bool BaseView::OnMouseMove(POINT pt)
{
	// check if we're in a left-button drag operation
	if (dragButton == MouseButton::mbLeft)
	{
		// If the parent is borderless and not in full-screen mode, simulate
		// the sizing border drag operation.
		if (HWND parent = GetParent(hWnd); parent != NULL && IsBorderlessWindowMode(parent))
		{
			// Get the delta from the initial position
			POINT delta;
			delta.x = pt.x - dragPos.x;
			delta.y = pt.y - dragPos.y;

			// move the parent window by the drag position
			RECT rc;
			GetWindowRect(parent, &rc);
			SetWindowPos(parent, 0, rc.left + delta.x, rc.top + delta.y, -1, -1, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);

			// Note that we don't need to update the drag position, as it's
			// relative to the client area, and we're moving the client area
			// in sync with each mouse position change.  That means that the
			// relative mouse never changes.
			return true;
		}
	}

	// use the default handling 
	return __super::OnMouseMove(pt);
}

// Load our startup video
bool BaseView::LoadStartupVideo()
{
	// look for the startup video for this window type
	bool found = false;
	TCHAR startupVideo[MAX_PATH];
	auto gl = GameList::Get();
	if (gl != nullptr && gl->FindGlobalVideoFile(startupVideo, _T("Startup Videos"), StartupVideoName()))
	{
		// Try loading the video in the overlay video sprite.  Don't start
		// playing it yet; we want to get the loading going in any other
		// windows that also have videos so that we can start them all at
		// the same time, for closer synchronization in case we're showing
		// a coordinated multi-screen "experience".
		videoOverlay.Attach(new VideoSprite());
		videoOverlay->alpha = 1.0f;
		POINTF pos = { static_cast<float>(szLayout.cx)/static_cast<float>(szLayout.cy), 1.0f };
		if (videoOverlay->LoadVideo(startupVideo, hWnd, pos, LogFileErrorHandler(), _T("Loading startup video"), false))
		{
			// success
			found = true;
			videoOverlayID = _T("Startup");

			// the video sprite loops by default; we only want to play once
			videoOverlay->GetVideoPlayer()->SetLooping(false);

			// udpate the drawing list to include the video overlay sprite
			UpdateDrawingList();
		}
	}

	// tell the caller whether or not we loaded a startup video
	return found;
}

bool BaseView::PlayStartupVideo()
{
	// If there's no startup video, simply return success.  This counts
	// as success because an error condition means that we cancel startup
	// videos in all windows, and it's perfectly fine (thus not an error
	// condition) if we simply don't have a video to show.  We'll simply
	// sit out the startup video interval with a blank window.
	if (videoOverlay == nullptr 
		|| videoOverlay->GetVideoPlayer() == nullptr 
		|| videoOverlayID != _T("Startup"))
		return true;

	// start the video
	return videoOverlay->GetVideoPlayer()->Play(LogFileErrorHandler());
}

void BaseView::EndStartupVideo()
{
	// if a startup video is playing, stop it
	if (videoOverlay != nullptr && videoOverlayID == _T("Startup"))
	{
		if (auto player = videoOverlay->GetVideoPlayer(); player != nullptr)
		{
			// stop the video
			player->Stop(LogFileErrorHandler());

			// clean up
			OnEndStartupVideo();
		}
	}
}

void BaseView::OnEndStartupVideo()
{
	// make sure we haven't already cleaned up
	if (videoOverlay != nullptr && videoOverlayID == _T("Startup"))
	{
		// shut down the video player
		if (auto player = videoOverlay->GetVideoPlayer(); player != nullptr)
			player->Shutdown();

		// forget the overlay
		videoOverlay = nullptr;
		videoOverlayID = _T("");
		UpdateDrawingList();

		// notify the playfield window
		if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
			pfv->OnEndExtStartupVideo();
	}
}

bool BaseView::IsStartupVideoPlaying() const
{
	return videoOverlay != nullptr
		&& videoOverlayID == _T("Startup")
		&& videoOverlay->GetVideoPlayer() != nullptr
		&& videoOverlay->GetVideoPlayer()->IsPlaying();
}

