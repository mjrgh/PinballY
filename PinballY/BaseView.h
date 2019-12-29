// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Basic view class.  This implements common functionality for our
// view windows, which are the child windows that fill the content
// areas of our top-level windows (playfield, backglass, DMD).

#pragma once

#include "D3DView.h"
#include "PrivateWindowMessages.h"
#include "Sprite.h"
#include "VideoSprite.h"
#include "MediaDropTarget.h"
#include "JavascriptEngine.h"

class BaseView : public D3DView
{
	friend class PlayfieldView;

public:
	BaseView(int contextMenuId, const TCHAR *winConfigVarPrefix) :
		D3DView(contextMenuId, winConfigVarPrefix),
		activeDropArea(nullptr)
	{ }

	// window creation
	bool Create(HWND parent, const TCHAR *title);

	// enclosing frame window is being shown/hidden
	virtual void OnShowHideFrameWindow(bool show) = 0;

	// Drag-and-drop mouse event handlers.  These provide live
	// feedback during the drag process for dropping different
	// types of media.  The POINT is in local coordinates,
	// adjusted for the window rotation and reflection.
	virtual void ShowDropTargets(const MediaDropTarget::FileDrop &fd, POINT pt, DWORD *pdwEffect);
	virtual void UpdateDropTargets(const MediaDropTarget::FileDrop &fd, POINT pt, DWORD *pdwEffect);
	virtual void DoMediaDrop(const MediaDropTarget::FileDrop &fd, POINT pt, DWORD *pdwEffect);
	virtual void RemoveDropTargets();

	// Media information for the main background image/video
	virtual const MediaType *GetBackgroundImageType() const = 0;
	virtual const MediaType *GetBackgroundVideoType() const = 0;

	// load/play our startup video
	bool LoadStartupVideo();
	bool PlayStartupVideo();
	void EndStartupVideo();
	void FadeStartupVideo(float amount);
	bool IsStartupVideoPlaying() const;

	// Startup video name for this window.  This is the base filename,
	// with no path or extension.
	virtual const TCHAR *StartupVideoName() const = 0;

	// Javascript drawing layer access
	JsValueRef JsCreateDrawingLayer(int zIndex);
	void JsRemoveDrawingLayer(JavascriptEngine::JsObj obj);

	// get the layout size
	SIZE GetLayoutSize() const { return szLayout; }

	// Figure the pixel width of the window layout in terms of the normalized
	// height of 1920 pixels.
	int NormalizedWidth()
	{
		if (szLayout.cy == 0)
			return 1080;
		else
			return static_cast<int>(1920.0f *
			(static_cast<float>(szLayout.cx) / static_cast<float>(szLayout.cy)));
	}

protected:
	~BaseView();

	// Is the parent in borderless window mode?  This returns true if
	// the parent is in borderless mode and not in full-screen mode.
	// (Full-screen mode is also technically borderless, but it's not
	// "window mode" in the sense of being sizeable and movable.)
	bool IsBorderlessWindowMode(HWND parent);

	// Instruction card display setup.  Returns a new sprite on
	// success, null on failure.
	Sprite *PrepInstructionCard(const TCHAR *filename);

	// Scale sprites
	virtual void ScaleSprites() override;

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

	// create the window
	virtual bool OnCreate(CREATESTRUCT *cs) override;

	// destroy the window
	virtual bool OnDestroy() override;

	// mouse move handler
	virtual bool OnMouseMove(POINT pt) override;

	// non-client hit testing
	virtual bool OnNCHitTest(POINT pt, UINT &hit) override;

	// Handle a Windows keyboard event
	virtual bool OnKeyEvent(UINT msg, WPARAM wParam, LPARAM lParam) override;
	virtual bool OnSysKeyEvent(UINT msg, WPARAM wParam, LPARAM lParam) override;
	virtual bool OnSysChar(WPARAM wParam, LPARAM lParam) override;

	// command handling
	virtual bool OnCommand(int cmd, int source, HWND hwndControl) override;

	// user message handling
	virtual bool OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// app message handling
	virtual bool OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// overlay video ending
	void OnEndOverlayVideo();
	void OnEndStartupVideo();

	// Drop area.  This describes a target area within the window where
	// a media file can be dropped to install it as a particular media
	// type.  For example, in the main playfield window, a PNG file 
	// could be used as the playfield background image or the wheel 
	// logo image for the game, so we set up drop areas for each type
	// to let the user indicate how the file will be installed.
	struct MediaDropArea
	{
		MediaDropArea(const TCHAR *label) :
			mediaType(nullptr), label(label), hilite(false)
		{
			SetRectEmpty(&rc);
		}

		MediaDropArea(const MediaType *mediaType, bool hilite = false) :
			mediaType(mediaType), hilite(hilite)
		{
			SetRectEmpty(&rc);
		}

		MediaDropArea(const RECT &rc, const MediaType *mediaType) :
			rc(rc), mediaType(mediaType), hilite(true)
		{
		}

		MediaDropArea(const RECT &rc, const MediaType *mediaType, const TCHAR *label, bool hilite) :
			rc(rc), mediaType(mediaType), label(label), hilite(hilite)
		{
		}

		// drop area, in client coordinates
		RECT rc;

		// media type for this area
		const MediaType *mediaType;

		// label text
		TSTRING label;

		// hilite this area when over it?
		bool hilite;

	};
	std::list<MediaDropArea> dropAreas;
	
	// the drop area we're currently hovering over
	MediaDropArea *activeDropArea;

	// Get the drop area list for a given media file.  Populates
	// the drop target area list with a suitable list of drop areas
	// for the given type.
	virtual bool BuildDropAreaList(const TCHAR *filename);

	// Find the drop button containing the given point (in local
	// window coordinates, adjusted for monitor rotation)
	MediaDropArea *FindDropAreaHit(POINT pt);

	// draw the drop area list
	void DrawDropAreaList(POINT pt);

	// current drop target feedback overlay
	RefPtr<Sprite> dropTargetSprite;

	// media drop target object
	RefPtr<MediaDropTarget> dropTarget;

	// Video overlay sprite.  This is the surface where we draw
	// the startup video.
	RefPtr<VideoSprite> videoOverlay;

	// video overlay ID
	TSTRING videoOverlayID;

	// Javascript custom drawing layers.  Javascript code can create
	// sprite layers for its own use.  These are layered in front of
	// the system sprites, in a sorting order specified by the script
	// code.
	struct JsDrawingLayer
	{
		JsDrawingLayer(double id, int zIndex);

		// ID for Javascript.  We store this ID in a property of the
		// associated Javascript object, so that we can connect the
		// Javascript 'this' to the C++ drawing layer object.
		double id;

		// Z index, specified by the scripting code.  Higher Z is
		// drawn in front of lower Z.
		int zIndex;

		// Sprite.  Note that this can be any sprite subclass, including
		// video sprites and DMD sprites.
		RefPtr<Sprite> sprite;

		// Positioning.  For x, -.5 is the left edge, +.5 is the
		// right edge, and 0 is the horizontal center.  For y,
		// -.5 is the bottom edge, +.5 is the top edge, and 0
		// is the horizontal center.
		struct
		{
			int xAlign = 0;  // -1 = left, 0 = center, 1 = right
			int yAlign = 0;  // -1 = bottom, 0 = center, 1 = top
			float x = 0.0f;
			float y = 0.0f;
		} pos;

		// Auto scaling options.  This tells us how to scale the
		// sprite when the window size changes.
		struct
		{
			// X and Y span.  These specify the fraction of the
			// window width and height that the image covers, with
			// 1.0 meaning that it fills that dimension exactly.
			// Zero means that this is a free dimension.
			//
			// If both X and Y spans are specified, the sprite size
			// is always pegged to the window size in both dimensions.
			//
			// If X is specified and Y is 0 (free), we'll size the
			// image to match the specified portion of the window
			// width, and set the height to maintain the previous 
			// aspect ratio.
			//
			// If Y is specified and X is 0 (free), we'll size the
			// image to match the specified portion of the window
			// height, and set the width to maintain the previous
			// aspect ratio.
			//
			// If both are 0, the combined scaling applies.
			float xSpan = 1.0f;
			float ySpan = 1.0f;

			// Combined scaling.  If the X and Y spans are both 0
			// (meaning that they're both free dimensions), we fall
			// back on this combined scaling factor.  In this case,
			// we size the sprite so that it either fills the width
			// or height of the window while maintaining the previous
			// aspect ratio in the other dimension.  We choose the
			// dimension that makes the image larger while still
			// fitting in the other dimension.
			float span = 1.0f;
		} scaling;

		// DMD-style message graphics are generated asynchronously
		// in a background thread.  When we have a threaded request
		// outstanding, we remember the sequence number of the request
		// here.  This lets us determine if the request is stale when
		// it finishes.
		DWORD dmdRequestSeqNo = 0;
	};
	std::list<JsDrawingLayer> jsDrawingLayers;

	// Adjust a drawing layer sprite's scale
	void ScaleDrawingLayerSprite(JsDrawingLayer &layer);

	// Next available drawing layer ID.  These are arbitrary identifiers,
	// so we just hand them out as serial numbers when creating the
	// drawing layer objects.
	double jsDrawingLayerNextID = 1.0;

	// Javascript access to drawing layers
	JsValueRef jsDrawingLayerProto = JS_INVALID_REFERENCE;
	bool JsDrawingLayerLoadImage(JsValueRef self, WSTRING filename);
	bool JsDrawingLayerLoadVideo(JsValueRef self, WSTRING filename, JavascriptEngine::JsObj options);
	void JsDrawingLayerLoadDMDText(JsValueRef self, WSTRING text, JavascriptEngine::JsObj options);
	void JsDrawingLayerDraw(JsValueRef self, JsValueRef drawFunc, JsValueRef width, JsValueRef height);
	void JsDrawingLayerClear(JsValueRef self, JsValueRef argb);
	float JsDrawingLayerGetAlpha(JsValueRef self) const;
	void JsDrawingLayerSetAlpha(JsValueRef self, float alpha);
	void JsDrawingLayerSetScale(JsValueRef self, JavascriptEngine::JsObj scale);
	void JsDrawingLayerSetPos(JsValueRef self, float x, float y, WSTRING align);

	// handle a DMD image completion message (BVMsgDMDImageReady)
	void DMDImageReady(WPARAM seqno, LPARAM imageList);

	// Convert a JsValueRef to a Gdiplus color.  This parses arguments
	// to DrawingLayer and Custom Drawing Context methods:
	//
	// - An integer is treated as an 0xAARRGGBB value, with the default
	//   alpha value applied if the encoded alpha component is zero
	//
	// - A string in format #RGB is parsed as an HTML 3-digit color,
	//   with opaque alpha (FF)
	//
	// - A string in format #RRGGBB is parsed as an HTML 6-digit color,
	//   with opaque alpha (FF)
	//
	// - A string in format #AARRGGBB is parsed as an HTML 6-digit color
	//   with explicit alpha
	//
	// - A string without a "#" is checked to see if it matches a config
	//   variable, and if so, the config variable is parsed as a color
	//   value, with opaque alpha (FF) applied
	//
	Gdiplus::Color JsToGPColor(JsValueRef val, BYTE defaultAlpha);

	// clear a drawing layer with the specified color
	void DrawingLayerClear(Sprite *sprite, Gdiplus::Color argb);

	// convert a drawing layer to the given sprite type
	template<class SpriteType> void DrawingLayerConvertSpriteType(JsValueRef self);

	// For the JsDrawingLayerXxx methods, get the VideoSprite object
	// corresponding to the layer in the Javscript 'this' parameter.
	// Note that this can return null if the layer has been deleted,
	// or if a valid 'this' isn't present.
	//
	// The base class implementation searches the drawing layer list.
	// Subclasses can use this to give Javascript access to other
	// sprites through the same drawing interface.
	virtual Sprite *JsThisToDrawingLayerSprite(JsValueRef self) const;

	// Get the JsDrawingLayer control object for the Javascript 'this'
	// parameter to a JsDrawingLayerXxx method.
	JsDrawingLayer *JsThisToDrawingLayer(JsValueRef self);
};
