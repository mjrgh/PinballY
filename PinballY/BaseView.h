// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Basic view class.  This implements common functionality for our
// view windows, which are the child windows that fill the content
// areas of our top-level windows (playfield, backglass, DMD).

#pragma once

#include "D3DView.h"
#include "PrivateWindowMessages.h"

class Sprite;

class BaseView : public D3DView
{
public:
	BaseView(int contextMenuId, const TCHAR *winConfigVarPrefix)
		: D3DView(contextMenuId, winConfigVarPrefix) { }

	// window creation
	bool Create(HWND parent, const TCHAR *title);

	// enclosing frame window is being shown/hidden
	virtual void OnShowHideFrameWindow(bool show) = 0;

protected:
	// Instruction card display setup.  Returns a new sprite on
	// success, null on failure.
	Sprite *PrepInstructionCard(const TCHAR *filename);

	// Asynchronous sprite loader.  This interface allow for loading
	// a sprite in the background, on a separate thread.  This is
	// designed to minimize time that the UI is blocked.
	//
	// The current implementation is actually fully synchronous,
	// but we're providing the interface anyway to make it easy to
	// write client code that can be switched to async mode in the
	// future, should that become desirable.  I wrote this class
	// originally when I was using a Media Foundation implementation
	// of the Audio Video Player, because MF video loads can take
	// around 200ms, which is noticeable in the UI.  However, the
	// system now uses a libvlc media player instead of MF, and
	// libvlc itself loads videos asynchronously (in its own worker
	// threads), which makes this redundant.  And even with MF, we
	// can't actually do the loading in the background without some
	// (indeterminate) extra work anyway, because of complications 
	// with the multithreaded access to the D3D11 device context.
	//
	// This class is designed to be aggregated into a BaseView as a 
	// member variable, to provide async loading of one type of sprite
	// used by the window (e.g., the playfield background).  Multiple
	// member variables can be created if multiple sprites are used.
	//
	class AsyncSpriteLoader
	{
		friend class BaseView;

	public:
		AsyncSpriteLoader(BaseView *view) : view(view) { }

		// Load a sprite asynchronously.  This starts a new thread that
		// creates a new VideoSprite object and invoke the 'load' callback.
		// If a previous sprite load is already in progress, we abandon it:
		// the old sprite is harmlessly discarded once it finished loading.
		// 
		// The load callback must be thread-safe, as it runs in a separate
		// background thread.  The easiest way to accomplish this is to keep
		// it self-contained, so that it only accesses its own locals, and
		// not any object class members, globals, or static variables.
		//
		// After the loader returns, we call the 'done' callback.  This is
		// called on the main UI thread, via a SendMessage() from the thread
		// to the window.  That means the 'done' callback DOESN'T need to be
		// thread-safe; it can directly access anything the main UI thread 
		// can.
		//
		// IMPORTANT:  Because the 'load' and 'done' callbacks might
		// operate in separate threads, don't use reference captures to
		// any local variables in the enclosing scope if you're using
		// lambdas as the callbacks.  Captured references to locals become
		// invalid when the local scope exits, which it surely will in the
		// case of threaded calls to these callbacks.  C++ is no Javascript
		// on this count.
		//
		// 'sta' is true if the sprite requires OLE "single-threaded
		// apartment" initialization on the thread.  If this is false, we 
		// initialize COM in multi-threaded mode instead.  OLE is needed if
		// the thread might create an OLE object, such as a Flash player.
		// Sprites that will only load video or static image media should
		// set 'sta' to false.
		//
		void AsyncLoad(
			bool sta,
			std::function<void(VideoSprite*)> load,
			std::function<void(VideoSprite*)> done);

	protected:
		// containing view
		BaseView *view;

		// async loader thread
		class Thread : public RefCounted
		{
		public:
			Thread(
				bool sta,
				AsyncSpriteLoader *loader,
				std::function<void(VideoSprite*)> load,
				std::function<void(VideoSprite*)> done);

			// static thread entrypoint
			static DWORD CALLBACK ThreadMain(LPVOID param)
			{
				return reinterpret_cast<Thread*>(param)->Main();
			}

			// method main entrypoint
			DWORD Main();

			// Do we need OLE STA initialization?
			bool sta;

			// Owner window.  We keep a counted reference on the window,
			// to guarantee that the object won't be deleted before we're
			// finished with it.  This in turn keeps the AsyncSpriteLoader
			// object around, since that's meant to be a member variable
			// of the view.
			RefPtr<BaseView> view;

			// The loader object.  This should always be a member variable
			// of our BaseView, so it's guaranteed to stay around as long
			// as the BaseView exists, which we ensure via our counted
			// reference on the view.
			AsyncSpriteLoader *loader;

			// my thread handle
			HandleHolder hThread;

			// loader callback function
			std::function<void(VideoSprite*)> load;

			// 'done' callback function
			std::function<void(VideoSprite*)> done;
		};

		// current loader thread
		RefPtr<Thread> thread;

		// completion message handler (BVMsgAsyncSpriteLoadDone)
		void OnAsyncSpriteLoadDone(VideoSprite *sprite, Thread *thread);
	};

	// Handle a Windows keyboard event
	virtual bool OnKeyEvent(UINT msg, WPARAM wParam, LPARAM lParam) override;
	virtual bool OnSysKeyEvent(UINT msg, WPARAM wParam, LPARAM lParam) override;
	virtual bool OnSysChar(WPARAM wParam, LPARAM lParam) override;

	// command handling
	virtual bool OnCommand(int cmd, int source, HWND hwndControl) override;

	// user message handling
	virtual bool OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;
};
