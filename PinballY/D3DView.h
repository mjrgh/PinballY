// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// D3D view.  This is a common base class for our D3D drawing surface
// windows.  This is implemented as a child window, to be embedded in 
// a parent frame window.

#pragma once

class Sprite;

#include <unordered_map>
#include <vector>
#include <list>
#include <memory>
#include "stdafx.h"
#include <Uxtheme.h>
#include "../Utilities/Config.h"
#include "D3D.h"
#include "D3DWin.h"
#include "Camera.h"
#include "TextDraw.h"
#include "PerfMon.h"
#include "BaseWin.h"
#include "ViewWin.h"

class Sprite;
class VideoSprite;

class D3DView : public ViewWin
{
public:
	D3DView(int contextMenuId, const TCHAR *winConfigVarPrefix);

	// render a frame
	void RenderFrame();

	// get/set monitor rotation in degrees
	int GetRotation() const { return camera->GetMonitorRotation(); }
	void SetRotation(int rotation);

	// get/set mirroring
	bool IsMirrorHorz() const { return camera->IsMirrorHorz(); }
	bool IsMirrorVert() const { return camera->IsMirrorVert(); }
	void SetMirrorHorz(bool f);
	void SetMirrorVert(bool f);

	// Windows message loop.  This can be used to process messages
	// when D3D windows are displayed.  This does D3D rendering to
	// all D3D windows whenever the message loop is idle.
	static int MessageLoop();

	// Render all D3D windows.  This can be explicitly called in nested
	// message loops (e.g., WM_ENTERIDLE) to continue rendering if
	// desired.  If this isn't called, D3D views will freeze at the 
	// last frame before the nested loop was entered.
	static void RenderAll();

	// Toggle the frame counter display
	void ToggleFrameCounter();

	// Idle event subscriber
	class IdleEventSubscriber
	{
	public:
		virtual ~IdleEventSubscriber() { D3DView::UnsubscribeIdleEvents(this); }
		virtual void OnIdleEvent() = 0;
	};

	static void SubscribeIdleEvents(IdleEventSubscriber *sub) 
		{ idleEventSubscribers.push_back(sub); }
	static void UnsubscribeIdleEvents(IdleEventSubscriber *sub) 
		{ idleEventSubscribers.remove(sub); }

	// Apply a callback to all active sprites in the drawing list
	void ForDrawingList(std::function<void(Sprite*)> callback);

	// update menu command status
	virtual void UpdateMenu(HMENU hMenu, BaseWin *fromWin) override;

	// Handle a change in the global "videos enabled" status.  Reloads
	// any video-capable sprites to reflect the new status.
	virtual void OnEnableVideos(bool enable) = 0;

protected:
	// ref-counted -> protected destructor (destructor is only called 
	// from self via Release())
	virtual ~D3DView();

	// initialize the window
	virtual bool InitWin() override;

	// Update the sprite drawing list.  Subclasses must override to populate
	// 'sprites' and 'videos' with the current list of drawing items.  Subclasses
	// must call this whenever a new sprite needs to be added to the list or 
	// removed from it.
	virtual void UpdateDrawingList() = 0;

	// Rescale sprites to match the window layout.  This should update any 
	// sprites in the drawing list that scale according to the window size.
	virtual void ScaleSprites() = 0;

	// update the text overlay
	void UpdateText();

	// Scale a sprite according to the window size.  'span' is the fraction
	// of the window's width and/or height to fill, where 1.0 means we scale
	// the sprite to exactly fill the width or height.  
	// 
	// If 'maintainAspect' is true, we'll maintain the original aspect ratio
	// of the sprite.  We'll figure the scaling that makes the sprite fill
	// the requested in span in whichever dimension makes the image smaller,
	// so that it doesn't overflow the span in the other dimension.
	//
	// If 'maintainAspect' is false, we'll scale the image anisotropically
	// such that it exactly fills the span in both dimensions.
	//
	void ScaleSprite(Sprite *sprite, float span, bool maintainAspect);

	// activate
	virtual bool OnActivate(int waCode, int minimized, HWND hWndOther) override;

	// destroy
	virtual bool OnNCDestroy() override;

	// handle window size changes
	virtual void OnResize(int width, int height) override;

	// Handle a camera view size change.  We call this when the window
	// size itself changes, or when the camera rotation changes.
	void OnResizeCameraView();

	// paint the window
	virtual void OnPaint(HDC) override { RenderFrame(); }

	// do nothing on background erase - D3D rendering covers the window
	virtual bool OnEraseBkgnd(HDC) override { return 0; }

	// private application message (WM_APP to 0xBFFF)
	virtual bool OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// set the scale of the ortho projection based on the given layout
	void SetOrthoScale(int width, int height);

	// set the ortho projection scale according to the current window height
	void SetOrthoScale();

	// mouse events
	virtual bool OnMouseButtonDown(int button, POINT pt) override;
	virtual bool OnMouseButtonUp(int button, POINT pt) override;
	virtual bool OnMouseMove(POINT pt) override;

	// timer events
	virtual bool OnTimer(WPARAM timer, LPARAM callback) override;

	// Begin a mouse drag process
	virtual void MouseDragBegin(int button, POINT pt);

	// Command handler
	virtual bool OnCommand(int cmd, int source, HWND hwndControl) override;
	virtual bool OnSysCommand(WPARAM wParam, LPARAM lParam) override;

	// common command handler for regular and system commands
	bool HandleCommand(int cmd);

	// drag modes
	enum
	{
		DragModeNone,	// no drag mode
		DragModePan,	// panning - move camera relative to view direction
		DragModeOrbit	// 
	}
	dragMode;

	// last drag mode coordinates
	POINT dragModePos;

	// Mouse drag operation in progress
	int dragButton;						// initiating mouse button (a MouseButton::mbXxx code)
	POINT dragPos;						// mouse position at last event

	// Timer IDs
	static const int fpsTimerID = 1;		// performance overlay timer

	// Window layout area.  This is the client area, rotated as needed to
	// match the camera orientation.  So if we're rotated 90 or 270 degrees,
	// the layout width and height are swapped vs the window width and 
	// height.
	SIZE szLayout;

	// Direct3D window interface
	D3DWin *d3dwin;

	// Freeze background rendering.  When a game is running, and this 
	// window is showing a blank background or a static image, we can
	// freeze updates when we're in the background to minimize the
	// performance impact on the running game.  We can't do this when
	// a video is running, as we need to continue to update the video
	// frames as usual.
	bool freezeBackgroundRendering = false;

	// D3D camera
	Camera *camera;

	// text handler and font
	TextDraw *textDraw;
	TextDrawFont *dmdFont;

	// Sprite list in drawing order
	std::list<Sprite*> sprites;

	// performance monitor for this window
	PerfMon perfMon;

	// display the FPS counters?
	bool fpsDisplay;

	// latest FPS statistics
	float fpsCur, fpsAvg;

	// config variable prefix for this window's variables
	TSTRING configVarPrefix;
	TSTRING configVarRotation;
	TSTRING configVarMirrorHorz;
	TSTRING configVarMirrorVert;

	// global list of active D3D windows
	static std::list<D3DView*> activeD3DViews;

	// global list idle event subscribers
	static std::list<IdleEventSubscriber*> idleEventSubscribers;
};
