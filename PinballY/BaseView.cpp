// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "BaseView.h"
#include "Application.h"
#include "PlayfieldView.h"
#include "VideoSprite.h"

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

bool BaseView::OnKeyEvent(UINT msg, WPARAM wParam, LPARAM lParam)
{
	// hide the cursor on any key input
	SetCursor(0);

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
	SetCursor(0);

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
	SetCursor(0);

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
		if (PlayfieldView *v = Application::Get()->GetPlayfieldView(); v != 0)
			v->SendMessage(WM_COMMAND, ID_ABOUT);
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
	GetImageFileInfo(filename, imageDesc);

	// Figure the aspect ratio of the card.  The scale of the sprite 
	// dimensions are arbitrary, because the window will automatically
	// rescale the sprite to fill the width and/or height, but we do
	// need to set the aspect ratio to maintain the correct geometry.
	// Use a normalized height of 1.0, and set the width proportionally.
	float aspect = imageDesc.size.cy == 0 ? 1.0f : float(imageDesc.size.cx) / float(imageDesc.size.cy);
	float ht = 1.0f;
	float wid = ht * aspect;
	POINTF normSize = { wid, ht };

	// Figure an initial pixel size based on our window layout.  This
	// isn't actually important, because we'll revise this in when we
	// scale the image to fit the window layout.
	SIZE pixSize = { (int)(wid * szLayout.cy), (int)(ht * szLayout.cy) };

	// load the image at the calculated size
	Application::InUiErrorHandler eh;
	RefPtr<Sprite> sprite(new Sprite());
	if (!sprite->Load(filename, normSize, pixSize, eh))
		return nullptr;

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
    // video decoding in separate threads anyway.

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

