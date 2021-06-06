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
#include "DMDView.h"
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

	default:
		// reflect ID_VIEW_CUSTOM_xxx commands to the parent frame
		if (cmd >= ID_VIEW_CUSTOM_FIRST && cmd <= ID_VIEW_CUSTOM_LAST)
		{
			::SendMessage(GetParent(hWnd), WM_COMMAND, cmd, 0);
			return true;
		}
		break;
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
	if (!sprite->Load(filename, normSize, pixSize, hWnd, eh))
	{
		// Load failed.  If the file is an SWF (Shockwave Flash), handle
		// it with a special error in the main window, to give the user
		// a chance to disable SWF loading in the future.  Otherwise 
		// just show the error normally.
		auto pfv = Application::Get()->GetPlayfieldView();
		if (imageDesc.imageType == ImageFileDesc::ImageType::SWF)
			pfv->ShowFlashError(eh);
		else
			pfv->ShowError(ErrorIconType::EIT_Error, nullptr, &eh);

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
	case BVMsgDMDImageReady:
		DMDImageReady(wParam, lParam);
		return true;

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
	case AVPMsgSetFormat:
		// Video frame format detection/change.  Search for a javascript
		// drawing layer playing this video and set its load size to match,
		// now that we know the true load size.
		for (auto &l : jsDrawingLayers)
		{
			if (l.sprite.Get() != nullptr && l.sprite->GetMediaCookie() == wParam)
			{
				// Update the sprite's load size to match the new aspect ratio
				auto desc = reinterpret_cast<const AudioVideoPlayer::FormatDesc*>(lParam);
				if (desc->width != 0)
				{
					l.sprite->loadSize.y = 1.0f;
					l.sprite->loadSize.x = static_cast<float>(desc->width) / static_cast<float>(desc->height);

					// re-create the mesh at the new aspect ratio
					l.sprite->ReCreateMesh();

					// rescale the sprite
					ScaleDrawingLayerSprite(l);
				}

				// no need to keep searching
				break;
			}
		}
		break;


	case AVPMsgEndOfPresentation:
		// check for the end of the overlay video
		if (videoOverlay != nullptr && videoOverlay->GetMediaCookie() == wParam)
			OnEndOverlayVideo();

		// also check for DrawingLayer end-of-video notifications
		DrawingLayerEndVideoEvent(msg, wParam);
		break;

	case AVPMsgLoopNeeded:
		// check for DraingLayer end-of-video notifications
		DrawingLayerEndVideoEvent(msg, wParam);
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

void BaseView::DrawingLayerEndVideoEvent(UINT msg, WPARAM cookie)
{
	// only proceed if Javascript is running
	if (auto js = JavascriptEngine::Get(); js != nullptr)
	{
		// look for a drawing layer playing this video
		for (auto &l : jsDrawingLayers)
		{
			// check for a video sprite matching the event cookie
			if (l.sprite != nullptr && l.sprite->GetMediaCookie() == cookie)
			{
				// dispatch a Javascript "videoend" notification through
				// the playfield window
				if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
					pfv->FireVideoEndEvent(l.jsObj, msg == AVPMsgLoopNeeded);

				// Stop searching on a successful match.  There can be only
				// one match, since the cookies are unique; and even if there
				// could be more than one match, it's not safe to continue
				// the search, since the Javascript event processor might
				// have deleted the drawing layer.
				break;
			}
		}
	}
}

void BaseView::AsyncSpriteLoader::AsyncLoad(
	bool sta,
	std::function<bool(BaseView*, VideoSprite*)> load,
	std::function<void(BaseView*, VideoSprite*, bool)> done)
{
#if 1
	// The asynchronous loading doesn't actually seem to make the
	// UI any more responsive, so let's keep things simple and just
    // do it synchronously for now - it might complicate things
    // with the video renderers and their D3D usage to load them
    // on a background thread.
	//
	// Fortunately, the async loader interface degenerates trivially
	// to synchronous operation: we simply call the 'load' and 'done'
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
	loadResult = load(view, sprite);

	// complete the loading
	done(view, sprite, loadResult);

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
	load(view, sprite);

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
		thread->done(view, sprite, loadResult);

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
		std::unique_ptr<Gdiplus::Font> font(CreateGPFont(_T("Tahoma"), 36, 400, false));

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
				typeLabel.Format(LoadStringT(IDS_MEDIA_DROP_TYPE_HERE), a.mediaType->nameStr.c_str());
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

void BaseView::ShowDropTargets(MediaDropTarget::FileDrop &fd, POINT pt, DWORD *pdwEffect)
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
	fd.EnumFiles([this, pdwEffect, pt](const TCHAR *fname, IStream *)
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

void BaseView::UpdateDropTargets(MediaDropTarget::FileDrop &fd, POINT pt, DWORD *pdwEffect)
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

void BaseView::DoMediaDrop(MediaDropTarget::FileDrop &fd, POINT pt, DWORD *pdwEffect)
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
	fd.EnumFiles([this, pfv, &nFilesProcessed, area](const TCHAR *fname, IStream *stream)
	{
		// try processing it through the playfield view
		if (pfv->DropFile(fname, stream, dropTarget, area != nullptr ? area->mediaType : nullptr))
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
			videoOverlay->SetLooping(false);

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

void BaseView::FadeStartupVideo(float amount)
{
	if (videoOverlay != nullptr && videoOverlayID == _T("Startup"))
	{
		// adjust the alpha
		videoOverlay->alpha = max(videoOverlay->alpha - amount, 0.0f);

		// adjust the audio volume
		if (auto player = videoOverlay->GetVideoPlayer(); player != nullptr)
		{
			int v = player->GetVolume() - static_cast<int>(amount * 100.0f);
			player->SetVolume(max(v, 0));
		}

		// if we've reached zero opacity, end the video
		if (videoOverlay->alpha <= 0.0f)
			EndStartupVideo();
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

void BaseView::ScaleSprites()
{
	// scale the Javascript drawing layer sprites
	for (auto &it : jsDrawingLayers)
		ScaleDrawingLayerSprite(it);
}

void BaseView::ScaleDrawingLayerSprite(JsDrawingLayer &l)
{
	if (auto s = l.sprite.Get(); s != nullptr)
	{
		// Figure the window's width in terms of its height.  The
		// window's height in normalized sprite units is fixed at 1.0,
		// so this is the same as figuring the width in normalized
		// units.
		const float y = 1.0f;
		const float x = float(szLayout.cx) / float(szLayout.cy);

		// get the load size
		const float xLoad0 = s->loadSize.x;
		const float yLoad0 = s->loadSize.y;

		// adjust for the sprite's rotation
		FLOAT theta = s->rotation.z;
		float sinTh = sinf(-theta);
		float cosTh = cosf(-theta);
		float xLoad = fabsf(xLoad0*cosTh - yLoad0 * sinTh);
		float yLoad = fabsf(yLoad0*cosTh + xLoad0 * sinTh);

		// Figure the scaling factor for each dimension that makes the
		// sprite exactly fill the window in that dimension.
		float xScale0 = x / xLoad;
		float yScale0 = 1.0f / yLoad;

		// rotate back to sprite space
		sinTh = sinf(theta);
		cosTh = cosf(theta);
		float xScale = fabsf(xScale0*cosTh - yScale0 * sinTh);
		float yScale = fabsf(yScale0*cosTh + xScale0 * sinTh);

		// now scale the sprite according to its scaling options
		if (l.scaling.xSpan > 0.0f && l.scaling.ySpan > 0.0f)
		{
			// Both dimensions are constrained.  Adjust each dimension
			// according to the specified span for that dimension.
			s->scale.x = xScale * l.scaling.xSpan;
			s->scale.y = yScale * l.scaling.ySpan;
		}
		else if (l.scaling.xSpan > 0)
		{
			// The width is constrained.  Use the width scale on both axes.
			s->scale.x = s->scale.y = xScale * l.scaling.xSpan;
		}
		else if (l.scaling.ySpan > 0)
		{
			// The height is constrained.  Use the height scale on both axes.
			s->scale.x = s->scale.y = yScale * l.scaling.ySpan;
		}
		else if (l.scaling.span > 0)
		{
			// We have a combined scale.  Use the smaller of the two scaling
			// factors.
			s->scale.x = s->scale.y = fminf(xScale * l.scaling.span, yScale * l.scaling.span);
		}

		// Figure the new position.  Sprite coordinates are relative to the
		// window's height, which is the same scale used for pos.y.  But pos.x
		// is in terms of the window's width, so we need to rescale it for
		// sprite coordinates.
		s->offset.x = (l.pos.x * x) + (l.pos.xAlign * 0.5f * (x - s->loadSize.x * s->scale.x));
		s->offset.y = (l.pos.y) + (l.pos.yAlign * 0.5f * (1.0f - s->loadSize.y * s->scale.y));

		// update the sprite's world matrix for any changes we just made
		s->UpdateWorld();
	}
}

BaseView::JsDrawingLayer::JsDrawingLayer(double id, int zIndex) : 
	id(id),
	zIndex(zIndex)
{
	// create a new sprite
	sprite.Attach(new VideoSprite());
}

JsValueRef BaseView::JsCreateDrawingLayer(int zIndex)
{
	// Find the insertion point in the list.  We want to keep the
	// list ordered in rendering order, meaning back to front, so
	// we want to insert the new item before the next item in front
	// if it, meaning the first item with a higher Z index.
	auto it = jsDrawingLayers.begin();
	while (it != jsDrawingLayers.end() && it->zIndex <= zIndex)
		++it;

	// Insert before this item, or at the end of the list
	auto id = jsDrawingLayerNextID++;
	auto &drawingLayer = jsDrawingLayers.emplace(it, id, zIndex);

	// update the drawing list to incorporate the new sprite
	UpdateDrawingList();

	// Create the Javascript object to represent the new layer object.
	// This is an object with our drawing layer prototype object as its
	// prototype, and the "id" property set to our new layer's ID.
	auto js = JavascriptEngine::Get();
	try
	{
		// create the object
		JavascriptEngine::JsObj obj;
		JsValueRef args[] = { jsDrawingLayerClass };
		if (auto err = JsConstructObject(jsDrawingLayerClass, args, 1, &obj.jsobj); err != JsNoError)
			return js->Throw(err, _T("<window>.createDrawingLayer()"));

		// set the ID, so that we can find the C++ object from the js object
		obj.Set("id", id);

		// And remember the js object from the C++ object.  Note that we have
		// to add an explicit reference, so that the js object won't be collected
		// as long as the C++ object is around.
		drawingLayer->jsObj = obj.jsobj;
		JsAddRef(obj.jsobj, nullptr);

		// success
		return obj.jsobj;
	}
	catch (JavascriptEngine::CallException exc)
	{
		exc.Log(_T("<window>.createDrawingLayer()"));
		return js->Throw(exc.jsErrorCode, _T("<window>.createDrawingLayer()"));
	}
}

BaseView::JsDrawingLayer::~JsDrawingLayer()
{
	// remove our reference on the js object
	if (jsObj != JS_INVALID_REFERENCE)
		JsRelease(jsObj, nullptr);
}

void BaseView::JsRemoveDrawingLayer(JavascriptEngine::JsObj obj)
{
	auto js = JavascriptEngine::Get();
	try
	{
		// get the layer ID
		double id = obj.Get<double>("id");

		// search for a match
		for (auto it = jsDrawingLayers.begin() ; it != jsDrawingLayers.end(); ++it)
		{
			if (it->id == id)
			{
				// got it - remove it from the list
				jsDrawingLayers.erase(it);

				// update the drawing list for the change
				UpdateDrawingList();

				// if we're frozen in the background, force a refresh
				if (freezeBackgroundRendering && !Application::IsInForeground())
					InvalidateRect(hWnd, NULL, FALSE);

				// drawing layers are unique, so there won't be another match
				break;
			}
		}
	}
	catch (JavascriptEngine::CallException exc)
	{
		exc.Log(_T("<window>.removeDrawingLayer()"));
		js->Throw(exc.jsErrorCode, _T("<window>.createDrawingLayer()"));
	}
}

// -----------------------------------------------------------------------
//
// Drawing layers
//

void BaseView::JsDrawingLayerClear(JsValueRef self, JsValueRef argb)
{
	// revert to a standard video sprite
	DrawingLayerConvertSpriteType<VideoSprite>(self);

	// get the layer from the 'self' object
	if (auto sprite = JsThisToDrawingLayerSprite(self); sprite != nullptr)
	{
		// clear the background
		DrawingLayerClear(sprite, JsToGPColor(argb, 0x00));

		// adjust the scaling
		if (auto layer = JsThisToDrawingLayer(self); layer != nullptr)
			ScaleDrawingLayerSprite(*layer);

		// if we're frozen in the background, force a refresh
		if (freezeBackgroundRendering && !Application::IsInForeground())
			InvalidateRect(hWnd, NULL, FALSE);
	}
}

void BaseView::DrawingLayerClear(Sprite *sprite, Gdiplus::Color argb)
{
	// clear any prior video
	sprite->Clear();

	// Draw a blank background with the desired color.  Since we're using
	// a fixed color for every pixel, the scaling is irrelevant, so use
	// a small fixed size to minimize memory consumption.  (We could even
	// just make this 1x1, but that somehow seems wrong.  It's probably
	// an irrational instinct, but at the least I think it might push our
	// luck with probing edge conditions in buggy D3D drivers if we go
	// too far on the small side here.  I expect that modern hardware is
	// so optimized for bigness that anything below a certain threshold
	// will all use the same amount of memory and CPU anyway.)
	const int width = 32, height = 32;
	Application::InUiErrorHandler eh;
	sprite->Load(width, height, [argb, width, height, this](Gdiplus::Graphics &g)
	{
		Gdiplus::SolidBrush bkg(argb);
		g.FillRectangle(&bkg, 0, 0, width, height);
	}, eh, _T("Launch overlay - default background"));

	// reset the scale for full screen
	sprite->loadSize = { 1.0f, 1.0f };
	sprite->ReCreateMesh();
}

// Convert the sprite type in a drawing layer to the given type
template<class SpriteType>
void BaseView::DrawingLayerConvertSpriteType(JsValueRef self)
{
	if (auto layer = JsThisToDrawingLayer(self); layer != nullptr && dynamic_cast<SpriteType*>(layer->sprite.Get()) == nullptr)
	{
		layer->sprite.Attach(new SpriteType());
		UpdateDrawingList();
	}
}

void BaseView::JsDrawingLayerDraw(JsValueRef self, JsValueRef drawFunc, JsValueRef widthArg, JsValueRef heightArg)
{
	// make sure we have a standard video sprite
	DrawingLayerConvertSpriteType<VideoSprite>(self);

	// get the layer from the 'self' object
	if (auto sprite = JsThisToDrawingLayerSprite(self); sprite != nullptr)
	{
		// clear previous resources in the sprite
		sprite->Clear();

		auto js = JavascriptEngine::Get();
		try
		{
			// If the width  height weren't specified, default to the window's
			// actual layout size.
			int width = JavascriptEngine::IsUndefinedOrNull(widthArg) ? szLayout.cx : JavascriptEngine::JsToNative<int>(widthArg);
			int height = JavascriptEngine::IsUndefinedOrNull(heightArg) ? szLayout.cy : JavascriptEngine::JsToNative<int>(heightArg);

			// the playfield view manages the Javascript drawing context - have
			// it do the drawing
			if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
				pfv->JsDraw(sprite, width, height, drawFunc);

			// if we're frozen in the background, force a refresh
			if (freezeBackgroundRendering && !Application::IsInForeground())
				InvalidateRect(hWnd, NULL, FALSE);

			// rescale sprites, in case it changed shape during the draw
			ScaleSprites();
		}
		catch (JavascriptEngine::CallException exc)
		{
			exc.Log(_T("DrawingLayer.draw()"));
			js->Throw(exc.jsErrorCode, _T("DrawingLayer.draw()"));
		}
	}
}

bool BaseView::JsDrawingLayerLoadImage(JsValueRef self, WSTRING filename)
{
	// presume failure
	bool ok = false;

	// make sure we have a standard video sprite
	DrawingLayerConvertSpriteType<VideoSprite>(self);

	// get the layer from the 'self' object
	if (auto sprite = JsThisToDrawingLayerSprite(self); sprite != nullptr)
	{
		// clear old resources from the sprite
		sprite->Clear();

		// if we can't get the image size, load at the window size
		SIZE sz { NormalizedWidth(), 1920 };

		// Get the image size, so that we can load it at its native aspect ratio.
		ImageFileDesc desc;
		if (GetImageFileInfo(WSTRINGToTSTRING(filename).c_str(), desc, true))
		{
			// get the native image size from the file
			sz = desc.size;

			// For SWF, load at the actual window size instead.  SWF is a vector
			// format, so the frame size in the file is usually only advisory;
			// the content will usually scale better if we rasterize it at the
			// target size in the first place, rather than rasterize it at the
			// advisory size and then rescale the pixels.
			if (desc.imageType == ImageFileDesc::ImageType::SWF)
				sz = GetLayoutSize();
		}

		// load the image
		ok = sprite->Load(WSTRINGToTSTRING(filename).c_str(),
			POINTF{ static_cast<float>(sz.cx)/1920.f, static_cast<float>(sz.cy)/1920.f }, sz, hWnd,
			LogFileErrorHandler(_T("Javascript call to mainWindow.launchOverlay.loadImage failed: ")));

		// rescale the drawing layer for the new image
		if (auto layer = JsThisToDrawingLayer(self); layer != nullptr)
			ScaleDrawingLayerSprite(*layer);

		// if we're frozen in the background, force a refresh
		if (freezeBackgroundRendering && !Application::IsInForeground())
			InvalidateRect(hWnd, NULL, FALSE);
	}

	// return the result
	return ok;
}

bool BaseView::JsDrawingLayerLoadVideo(JsValueRef self, WSTRING filename, JavascriptEngine::JsObj options)
{
	// presume failure
	bool ok = false;

	// get options
	auto js = JavascriptEngine::Get();
	bool loop = true;
	bool mute = false;
	int vol = 100;
	bool play = true;
	try
	{
		if (!options.IsNull())
		{
			if (options.Has("loop"))
				loop = options.Get<bool>("loop");
			if (options.Has("mute"))
				mute = options.Get<bool>("mute");
			if (options.Has("volume"))
				vol = options.Get<int>("volume");
			if (options.Has("play"))
				play = options.Get<bool>("play");
		}
	}
	catch (JavascriptEngine::CallException exc)
	{
		exc.Log(_T("DrawingLayer.loadVideo()"));
		js->Throw(exc.jsErrorCode, _T("DrawingLayer.loadVideo()"));
	}

	// make sure we have a standard video sprite
	DrawingLayerConvertSpriteType<VideoSprite>(self);

	// Get the sprite from the 'self' object.  The sprite has to be
	// a video sprite to use this function.
	if (auto sprite = dynamic_cast<VideoSprite*>(JsThisToDrawingLayerSprite(self)); sprite != nullptr)
	{
		// by default, scale the video to the window size
		const float width = static_cast<float>(NormalizedWidth() / 1920.0f);
		POINTF normSize = { width, 1.0f };

		// Check the type.  If it's an animated GIF, we have to figure the normalized
		// size based on the GIF size, rather than waiting for the video player to call
		// us back with the size from the stream, which won't happen with a GIF since
		// we're not actually using the video player.
		ImageFileDesc desc;
		if (GetImageFileInfo(filename.c_str(), desc, true) && desc.imageType == ImageFileDesc::ImageType::GIF)
			normSize = { static_cast<float>(desc.size.cx) / 1920.f, static_cast<float>(desc.size.cy) / 1920.0f };

		// load the video - don't play it yet, so that we can set options first
		LogFileErrorHandler eh;
		ok = sprite->LoadVideo(WSTRINGToTSTRING(filename).c_str(), hWnd, normSize,
			eh, _T("Javascript call to mainWindow.launchOverlay.loadVideo failed"), false, vol);

		if (ok)
		{
			// set options
			sprite->SetLooping(loop);
			if (auto player = sprite->GetVideoPlayer(); player != nullptr)
				player->Mute(mute);

			// if it's a GIF, we won't get a callback from the video player to set the
			// size, so scale the sprite explicitly now
			if (desc.imageType == ImageFileDesc::ImageType::GIF)
			{
				if (auto layer = JsThisToDrawingLayer(self); layer != nullptr)
					ScaleDrawingLayerSprite(*layer);
			}


			// if desired, start playback
			if (play)
				sprite->Play(eh);
		}
	}

	// return the result
	return ok;
}

void BaseView::JsDrawingLayerLoadDMDText(JsValueRef self, WSTRING text, JavascriptEngine::JsObj options)
{
	// we need access to the drawing layer and DMD view to proceed
	auto layer = JsThisToDrawingLayer(self);
	auto dmdview = Application::Get()->GetDMDView();
	if (layer != nullptr && dmdview != nullptr)
	{
		auto js = JavascriptEngine::Get();
		try
		{
			// retrieve options
			TSTRING style, font;
			const TCHAR *pStyle = nullptr, *pFont = nullptr;
			RGBQUAD txtColor, *pTxtColor = nullptr;
			RGBQUAD bgColor, *pBgColor = nullptr;
			BYTE bgAlpha = 255;
			if (!options.IsNull())
			{
				if (options.Has("style"))
				{
					style = options.Get<TSTRING>("style");
					pStyle = style.c_str();
				}
				if (options.Has("font"))
				{
					font = options.Get<TSTRING>("font");
					pFont = font.c_str();
				}
				if (options.Has("color"))
				{
					Gdiplus::Color c = JsToGPColor(options.Get<JsValueRef>("color"), 0xFF);
					txtColor = { c.GetBlue(), c.GetGreen(), c.GetRed() };
					pTxtColor = &txtColor;
				}
				if (options.Has("bgColor"))
				{
					Gdiplus::Color c = JsToGPColor(options.Get<JsValueRef>("bgColor"), 0xFF);
					bgColor = { c.GetBlue(), c.GetGreen(), c.GetRed() };
					pBgColor = &bgColor;
					bgAlpha = c.GetAlpha();
				}
			}

			// Break the string up into a list at newlines
			auto messages = StrSplit<TSTRING>(WSTRINGToTSTRING(text).c_str(), '\n');

			// Clear any existing sprite.  Since we have to generate the image
			// asynchronously, we could potentially get a new Javascript request
			// to load a new image or video before the DMD request completes.
			// The empty sprite will let us know that hasn't happened, since
			// another drawing request in the meantime will create a new sprite.
			layer->sprite = nullptr;
			UpdateDrawingList();

			// Kick off the image generation
			layer->dmdRequestSeqNo = dmdview->GenerateDMDImage(
				this, messages, pStyle, pFont, pTxtColor, pBgColor, bgAlpha);
		}
		catch (JavascriptEngine::CallException exc)
		{
			exc.Log(_T("DrawingLayer.loadVideo()"));
			js->Throw(exc.jsErrorCode, _T("DrawingLayer.loadVideo()"));
		}
	}
}

void BaseView::DMDImageReady(WPARAM seqno, LPARAM lParam)
{
	// Look for a drawing layer with an outstanding request matching 
	// the sequence number.  Only consider layers with null sprites,
	// as Javascript could have done more drawing in the time since
	// we started the request, in which case this request is moot.
	for (auto &layer : jsDrawingLayers)
	{
		if (layer.dmdRequestSeqNo == seqno && layer.sprite == nullptr)
		{
			// get the image list
			auto list = reinterpret_cast<std::list<DMDView::HighScoreImage>*>(lParam);
			if (list->size() > 0)
			{
				// The generator can create a whole series of slides, for
				// a rotating high score display.  For Javascript purposes,
				// we'll only ever generate one slide.
				auto slide = list->begin();

				// create the sprite and grab a reference
				slide->CreateSprite();
				layer.sprite = slide->sprite;

				// if we successfully created the sprite, update the drawing list
				if (layer.sprite != nullptr)
					UpdateDrawingList();
			}

			// no need to keep looking
			break;
		}
	}
}

float BaseView::JsDrawingLayerGetAlpha(JsValueRef self) const
{
	if (auto sprite = JsThisToDrawingLayerSprite(self); sprite != nullptr)
		return sprite->alpha;
	else
		return 0.0;
}

void BaseView::JsDrawingLayerSetAlpha(JsValueRef self, float alpha)
{
	if (auto sprite = JsThisToDrawingLayerSprite(self); sprite != nullptr)
		sprite->alpha = fmaxf(0.0f, fminf(alpha, 1.0f));
}

void BaseView::JsDrawingLayerSetScale(JsValueRef self, JavascriptEngine::JsObj scale)
{
	if (auto l = JsThisToDrawingLayer(self); l != nullptr)
	{
		auto js = JavascriptEngine::Get();
		try
		{
			// set the default options first, in case the options object
			// doesn't include any properties
			l->scaling.xSpan = l->scaling.ySpan = l->scaling.span = 1.0f;

			// set the new scaling properties from the options
			if (!scale.IsNull())
			{
				l->scaling.xSpan = scale.Get<float>("xSpan");
				l->scaling.ySpan = scale.Get<float>("ySpan");
				l->scaling.span = scale.Get<float>("span");
			}

			// update the scaling in the sprite
			ScaleDrawingLayerSprite(*l);
		}
		catch (JavascriptEngine::CallException exc)
		{
			exc.Log(_T("DrawingLayer.setScale()"));
			js->Throw(exc.jsErrorCode, _T("DrawingLayer.setScale()"));
		}
	}
}

void BaseView::JsDrawingLayerSetPos(JsValueRef self, float x, float y, WSTRING align)
{
	if (auto l = JsThisToDrawingLayer(self); l != nullptr)
	{
		// set the new position
		l->pos.x = x;
		l->pos.y = y;

		// set the new alignment point
		l->pos.xAlign = l->pos.yAlign = 0;
		std::wregex pat(L"\\s*(top|middle|bottom)?\\b\\s*(left|center|right)?\\s*", std::regex_constants::icase);
		std::match_results<WSTRING::const_iterator> m;
		if (std::regex_match(align, m, pat))
		{
			if (m[1].matched)
			{
				l->pos.yAlign = _wcsicmp(m[1].str().c_str(), L"bottom") == 0 ? -1 :
					_wcsicmp(m[1].str().c_str(), L"top") == 0 ? 1 : 0;
			}
			if (m[2].matched)
			{
				l->pos.xAlign = _wcsicmp(m[2].str().c_str(), L"left") == 0 ? -1 : 
					_wcsicmp(m[2].str().c_str(), L"right") == 0 ? 1 : 0;
			}
		}

		// update the sprite layout
		ScaleDrawingLayerSprite(*l);
	}
}

void BaseView::JsDrawingLayerPlay(JsValueRef self)
{
	if (auto l = JsThisToDrawingLayer(self); l != nullptr && l->sprite != nullptr)
		l->sprite->Play(SilentErrorHandler());
}

void BaseView::JsDrawingLayerPause(JsValueRef self)
{
	if (auto l = JsThisToDrawingLayer(self); l != nullptr && l->sprite != nullptr)
		l->sprite->Stop(SilentErrorHandler());
}

int BaseView::JsDrawingLayerGetVol(JsValueRef self) const
{
	if (auto vs = dynamic_cast<VideoSprite*>(JsThisToDrawingLayerSprite(self)); vs != nullptr && vs->GetVideoPlayer() != nullptr)
		return vs->GetVideoPlayer()->GetVolume();
	else
		return 100;
}

void BaseView::JsDrawingLayerSetVol(JsValueRef self, int vol)
{
	if (auto vs = dynamic_cast<VideoSprite*>(JsThisToDrawingLayerSprite(self)); vs != nullptr && vs->GetVideoPlayer() != nullptr)
		return vs->GetVideoPlayer()->SetVolume(vol < 0 ? 0 : vol > 100 ? 100 : vol);
}

bool BaseView::JsDrawingLayerGetMute(JsValueRef self) const
{
	if (auto vs = dynamic_cast<VideoSprite*>(JsThisToDrawingLayerSprite(self)); vs != nullptr && vs->GetVideoPlayer() != nullptr)
		return vs->GetVideoPlayer()->IsMute();
	else
		return false;
}

void BaseView::JsDrawingLayerSetMute(JsValueRef self, bool mute)
{
	if (auto vs = dynamic_cast<VideoSprite*>(JsThisToDrawingLayerSprite(self)); vs != nullptr && vs->GetVideoPlayer() != nullptr)
		vs->GetVideoPlayer()->Mute(mute);
}


Sprite *BaseView::JsThisToDrawingLayerSprite(JsValueRef self) const
{
	// get the ID from the object
	auto js = JavascriptEngine::Get();
	double id;
	const TCHAR *where;
	if (js->GetProp(id, self, "id", where) != JsNoError)
	{
		js->LogAndClearException();
		return nullptr;
	}

	// search the drawing layer list for a match to the ID
	for (auto &it : jsDrawingLayers)
	{
		if (it.id == id)
			return it.sprite;
	}

	// not found
	return nullptr;
}

BaseView::JsDrawingLayer *BaseView::JsThisToDrawingLayer(JsValueRef self) 
{
	// get the ID from the object
	auto js = JavascriptEngine::Get();
	double id;
	const TCHAR *where;
	if (js->GetProp(id, self, "id", where) != JsNoError)
	{
		js->LogAndClearException();
		return nullptr;
	}

	// search the drawing layer list for a match to the ID
	for (auto &it : jsDrawingLayers)
	{
		if (it.id == id)
			return &it;
	}

	// not found
	return nullptr;
}

Gdiplus::Color BaseView::JsToGPColor(JsValueRef val, BYTE defaultAlpha)
{
	JsValueType type = JsUndefined;
	JsGetValueType(val, &type);
	if (type == JsNumber)
	{
		// Interpret as 0xAARRGGBB
		int argb;
		JsNumberToInt(val, &argb);
		BYTE a = static_cast<BYTE>((argb >> 24) & 0xff);
		BYTE r = static_cast<BYTE>((argb >> 16) & 0xff);
		BYTE g = static_cast<BYTE>((argb >> 8) & 0xff);
		BYTE b = static_cast<BYTE>(argb & 0xff);

		// Apply the default alpha if the alpha in the number is zero
		if (a == 0)
			a = defaultAlpha;

		// convert to a GDI+ color value
		return Gdiplus::Color(a, r, g, b);
	}
	else if (type == JsString)
	{
		// get the string
		const WCHAR *ptr;
		size_t len;
		JsStringToPointer(val, &ptr, &len);

		// convert to something easier to work with
		WSTRING s(ptr, len);

		// try it once in raw form, then again as an expanded config variable
		for (int pass = 1; pass <= 2; ++pass)
		{
			// try parsing as an HTML-style #RGB value
			static const std::wregex hex3(L"#?([a-f0-9])([a-f0-9])([a-f0-9])", std::regex_constants::icase);
			std::match_results<WSTRING::const_iterator> m;
			if (std::regex_match(s, m, hex3))
			{
				return Gdiplus::Color(
					0xFF,
					static_cast<BYTE>(_tcstol(m[1].str().c_str(), nullptr, 16) * 0x11),
					static_cast<BYTE>(_tcstol(m[2].str().c_str(), nullptr, 16) * 0x11),
					static_cast<BYTE>(_tcstol(m[3].str().c_str(), nullptr, 16) * 0x11));
			}

			// try an HTML-style #RRGGBB six-digit hex value
			static const std::wregex hex6(L"#?([a-f0-9]{2})([a-f0-9]{2})([a-f0-9]{2})", std::regex_constants::icase);
			if (std::regex_match(s, m, hex6))
			{
				return Gdiplus::Color(
					0xFF,
					static_cast<BYTE>(_tcstol(m[1].str().c_str(), nullptr, 16)),
					static_cast<BYTE>(_tcstol(m[2].str().c_str(), nullptr, 16)),
					static_cast<BYTE>(_tcstol(m[3].str().c_str(), nullptr, 16)));
			}

			// try an HTML-style #AARRGGBB value
			static const std::wregex hex8(L"#?([a-f0-9]{2})([a-f0-9]{2})([a-f0-9]{2})([a-f0-9]{2})", std::regex_constants::icase);
			if (std::regex_match(s, m, hex8))
			{
				return Gdiplus::Color(
					static_cast<BYTE>(_tcstol(m[1].str().c_str(), nullptr, 16)),
					static_cast<BYTE>(_tcstol(m[2].str().c_str(), nullptr, 16)),
					static_cast<BYTE>(_tcstol(m[3].str().c_str(), nullptr, 16)),
					static_cast<BYTE>(_tcstol(m[4].str().c_str(), nullptr, 16)));
			}

			// don't re-expand configuration variables on the second pass
			if (pass > 1)
				break;

			// try looking it up as a config variable
			if (auto cv = ConfigManager::GetInstance()->Get(s.c_str()); cv != nullptr)
			{
				// expand to the config variable string and try again
				s = cv;
			}
			else
			{
				// no match - stop
				break;
			}
		}
	}

	// on failure, return solid black as a default
	return Gdiplus::Color(0xFF, 0, 0, 0);
}

