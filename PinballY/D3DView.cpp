// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "D3DView.h"
#include "GraphicsUtil.h"
#include "TextureShader.h"
#include "Camera.h"
#include "MouseButtons.h"
#include "Application.h"
#include "AudioManager.h"
#include "Sprite.h"
#include "VideoSprite.h"

using namespace DirectX;

// config variables
namespace ConfigVars
{
	static const TCHAR *Rotation = _T("Rotation");
	static const TCHAR *MirrorHorz = _T("MirrorHorz");
	static const TCHAR *MirrorVert = _T("MirrorVert");
};

// statics
std::list<D3DView*> D3DView::activeD3DViews;
std::list<D3DView::IdleEventSubscriber*> D3DView::idleEventSubscribers;

// construction
D3DView::D3DView(int contextMenuId, const TCHAR *configVarPrefix) 
	: ViewWin(contextMenuId), perfMon(60.0f), configVarPrefix(configVarPrefix)
{
	// clear object pointers
	d3dwin = 0;
	camera = 0;
	textDraw = 0;

	// clear modes
	dragMode = DragModeNone;
	szLayout = szClient;

	// set up config vars
	configVarRotation = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::Rotation);
	configVarMirrorHorz = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::MirrorHorz);
	configVarMirrorVert = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::MirrorVert);
}

// destruction
D3DView::~D3DView()
{
	// delete objects
	if (textDraw != 0) delete textDraw;
	if (camera != 0) delete camera;
	if (d3dwin != 0) delete d3dwin;
}

void D3DView::SetRotation(int rotation)
{
	// set the rotation in the camera
	camera->SetMonitorRotation(rotation);

	// this changes our camera view sizing
	OnResizeCameraView();

	// save the change to the configuration
	ConfigManager::GetInstance()->Set(configVarRotation.c_str(), camera->GetMonitorRotation());
}

void D3DView::SetMirrorHorz(bool f)
{
	// set the mirroring in the camera
	camera->SetMirrorHorz(f);

	// save the change to the configuration
	ConfigManager::GetInstance()->SetBool(configVarMirrorHorz.c_str(), camera->IsMirrorHorz());
}

void D3DView::SetMirrorVert(bool f)
{
	// set the mirroring in the camera
	camera->SetMirrorVert(f);

	// save the change to the configuration
	ConfigManager::GetInstance()->SetBool(configVarMirrorVert.c_str(), camera->IsMirrorVert());
}

bool D3DView::InitWin()
{
	// do nothing if I've already been initialized
	if (d3dwin != 0)
		return true;

	// load the menu icons
	LoadMenuIcon(ID_ABOUT, IDB_MNU_ABOUT);
	LoadMenuIcon(ID_HELP, IDB_MNU_HELP);
	LoadMenuIcon(ID_ROTATE_CW, IDB_MNU_ROTATE);
	LoadMenuIcon(ID_EXIT, IDB_MNU_EXIT);
	LoadMenuIcon(ID_FULL_SCREEN, IDB_MNU_FULLSCREEN);
	LoadMenuIcon(ID_WINDOW_BORDERS, IDB_MNU_WINDOW_BORDERS);
	LoadMenuIcon(ID_FPS, IDB_MNU_FPS);
	LoadMenuIcon(ID_OPTIONS, IDB_MNU_OPTIONS);
	LoadMenuIcon(ID_VIEW_BACKGLASS, IDB_MNU_BACKGLASS);
    LoadMenuIcon(ID_VIEW_PLAYFIELD, IDB_MNU_PLAYFIELD);
	LoadMenuIcon(ID_VIEW_DMD, IDB_MNU_DMD);
    LoadMenuIcon(ID_VIEW_TOPPER, IDB_MNU_TOPPER);
    LoadMenuIcon(ID_VIEW_INSTCARD, IDB_MNU_INSTCARD);
	LoadMenuIcon(ID_HIDE, IDB_MNU_HIDE);
	LoadMenuIcon(ID_MIRROR_HORZ, IDB_MNU_MIRROR_HORZ);
	LoadMenuIcon(ID_MIRROR_VERT, IDB_MNU_MIRROR_VERT);
	LoadMenuIcon(ID_REALDMD_MIRROR_HORZ, IDB_MNU_MIRROR_HORZ);
	LoadMenuIcon(ID_REALDMD_MIRROR_VERT, IDB_MNU_MIRROR_VERT);

	// get config items
	int rotation = ConfigManager::GetInstance()->GetInt(configVarRotation.c_str());
	bool mirrorHorz = ConfigManager::GetInstance()->GetBool(configVarMirrorHorz.c_str());
	bool mirrorVert = ConfigManager::GetInstance()->GetBool(configVarMirrorVert.c_str());

	// get the actual window size
	RECT arc;
	GetClientRect(hWnd, &arc);
	int width = arc.right - arc.left;
	int height = arc.bottom - arc.top;

	// initialize D3D
	d3dwin = new D3DWin();
	if (!d3dwin->Init(hWnd))
	{
		DestroyWindow(hWnd);
		return false;
	}

	// create the camera
	camera = new Camera();
	if (!camera->Init(width, height))
		return false;

	// Set the initial camera position.  We're using a simple 2D model
	// in the X-Y plane, at Z=0, viewed with an orthographic projection.
	// This keeps object scaling simple since there's no adjustment for 
	// perspective.  The default camera orientation points square in
	// that direction, but we need to position the camera some arbitrary
	// distance out from the Z=0 plane so that the objects in the plane are
	// within the depth bounds of the view frustum; since we're using an
	// ortho projection, there's no perspective, so the distance makes no
	// difference as long as it's within the depth limits.
	camera->SetPosition(0, 0, -100);

	// set the view rotation and mirroring
	camera->SetMonitorRotation(rotation);
	camera->SetMirrorHorz(mirrorHorz);
	camera->SetMirrorVert(mirrorVert);

	// set the initial ortho projection scale
	SetOrthoScale(width, height);

	// create the text handler
	textDraw = new TextDraw();
	if (!textDraw->Init())
		return false;

	// load our fonts
	TCHAR fontfile[MAX_PATH];
	GetDeployedFilePath(fontfile, _T("assets\\dotfont.dxtkfont"), _T(""));
    Application::InUiErrorHandler eh;
	if ((dmdFont = textDraw->GetFont(fontfile, eh)) == 0)
		return false;

	// add me to the list of active D3D windows, adding a reference on
	// behalf of the list
	AddRef();
	activeD3DViews.push_back(this);

	// success
	return true;
}

bool D3DView::OnNCDestroy()
{
	// remove myself from the active D3D view list, and release the list ref
	activeD3DViews.remove(this);
	Release();

	// return the base class handling
	return __super::OnNCDestroy();
}

// Set the scale for our orthographic projection.  Everything in the UI is
// scaled to the window height.
void D3DView::SetOrthoScale(int width, int height)
{
	if (camera != 0)
	{
		// If the UI is rotated by 90 or 270 degrees, the UI height is
		// actually the window width, and vice versa.
		int rot = camera->GetMonitorRotation();
		int uiWidth = width, uiHeight = height;
		if (rot == 90 || rot == 270)
			uiWidth = height, uiHeight = width;

		// Set the scale factor so that the height is normalized to 1.0f
		// in mesh units.
		camera->SetOrthoScaleFactor(1.0f / uiHeight);
	}
}

void D3DView::SetOrthoScale()
{
	SetOrthoScale(szClient.cx, szClient.cy);
}

// Render a D3D frame.  This is called during normal window message
// loop painting, and also during idle processing.
void D3DView::RenderFrame()
{
	// skip hidden and minimized windows
	if (IsIconic(hWnd) || !IsWindowVisible(hWnd))
		return;

	// count the frame
	perfMon.CountFrame();

	// make sure I'm the active window in D3D
	D3D *d3d = D3D::Get();
	d3d->SetWin(d3dwin);

	// prepare D3D for a new frame
	d3dwin->BeginFrame();

	// turn off the depth stencil
	d3d->SetUseDepthStencil(false);
	
	// If the view is mirroring one axis (but not both), use the mirrored
	// rasterizing state to compensate for the reversed vertex winding
	// order relative.  This isn't necessary when mirroring both axes,
	// since the two coordinate system reversals cancel out with respect
	// to the winding order.
	d3d->SetMirroredRasterizerState(camera->IsMirrorHorz() ^ camera->IsMirrorVert());

	// render the sprite list
	for (auto s : sprites)
		s->Render(camera);

	// draw any text overlay
	textDraw->Render(camera);

	// close out the frame
	d3dwin->EndFrame();
}

void D3DView::ScaleSprite(Sprite *sprite, float span, bool maintainAspect)
{
	// do nothing with a null sprite
	if (sprite == nullptr)
		return;

	// Figure the window's width in terms of its height.  The
	// window's height in normalized sprite units is fixed at 1.0,
	// so this is the same as figuring the width in normalized
	// units.
	float y = 1.0f;
	float x = float(szLayout.cx) / float(szLayout.cy);

	// get the load size
	float xLoad0 = sprite->loadSize.x;
	float yLoad0 = sprite->loadSize.y;

	// adjust for the sprite's rotation
	FLOAT theta = sprite->rotation.z;
	float sinTh = sinf(-theta);
	float cosTh = cosf(-theta);
	float xLoad = fabsf(xLoad0*cosTh - yLoad0*sinTh);
	float yLoad = fabsf(yLoad0*cosTh + xLoad0*sinTh);

	// Figure the scaling factor for each dimension that makes the
	// sprite exactly fill the requested span in that dimension.
	float xScale0 = span * x / xLoad;
	float yScale0 = span / yLoad;

	// rotate back to sprite space
	sinTh = sinf(theta);
	cosTh = cosf(theta);
	float xScale = fabsf(xScale0*cosTh - yScale0*sinTh);
	float yScale = fabsf(yScale0*cosTh + xScale0*sinTh);

	// are we maintaining the aspect ratio?
	if (maintainAspect)
	{
		// We're maintaining the aspect ratio.  Figure the scale that
		// makes the sprite exactly fill the span in each dimension,
		// then pick the smaller of the two and use it for both 
		// scaling dimensions.  This will make the sprite fill the
		// span in one dimension without overflowing the other.
		sprite->scale.x = sprite->scale.y = fminf(xScale, yScale);
	}
	else
	{
		// We're stretching the sprite to exactly fill the requested
		// span in both dimensions.  Simply figure the scale for each
		// axis independently.
		sprite->scale.x = xScale;
		sprite->scale.y = yScale;
	}

	// update the sprite's world matrix for the new scaling
	sprite->UpdateWorld();

	// Update the pixel layout, for vector graphics types
	sprite->AdviseWindowSize(szLayout);
}

void D3DView::ForDrawingList(std::function<void(Sprite*)> callback)
{
	for (auto s : sprites)
		callback(s);
}

bool D3DView::OnActivate(int waCode, int minimized, HWND hWndOther)
{
	// see what to do based on the activation state
	switch (waCode)
	{
	case WA_INACTIVE:
		// exit drag modes on inactivation
		dragMode = DragModeNone;
		break;
	}

	// use the default handling
	return __super::OnActivate(waCode, minimized, hWndOther);
}

void D3DView::ToggleFrameCounter()
{
	if (!fpsDisplay)
	{
		// start the timer
		SetTimer(hWnd, fpsTimerID, 250, 0);
		fpsDisplay = true;

		// get the current statistics
		perfMon.GetCurFPS(fpsCur, 1.0f);
		fpsAvg = perfMon.GetRollingFPS();
	}
	else
	{
		// stop the timer
		KillTimer(hWnd, fpsTimerID);
		fpsDisplay = false;
	}

	// update the text display
	UpdateText();
}

bool D3DView::OnCommand(int cmd, int source, HWND hwndControl)
{
	// run it by our command handler
	if (HandleCommand(cmd))
		return true;

	// not handled - use the base class handling
	return __super::OnCommand(cmd, source, hwndControl);
}

bool D3DView::OnSysCommand(WPARAM wParam, LPARAM lParam)
{
	// run it by our command handler
	if (HandleCommand(LOWORD(wParam)))
		return true;

	// not handled - use the base class handling
	return __super::OnSysCommand(wParam, lParam);
}

bool D3DView::HandleCommand(int cmd)
{
	switch (cmd)
	{
	case ID_FPS:
		// toggle the performance overlay
		ToggleFrameCounter();
		return true;

	case ID_FULL_SCREEN:
	case ID_HIDE:
	case ID_WINDOW_BORDERS:
		// forward these commands to our parent
		::SendMessage(GetParent(hWnd), WM_COMMAND, cmd, 0);
		return true;

	case ID_ROTATE_CW:
		// rotate clockwise 90 degrees
		SetRotation(Wrap(GetRotation() + 90, 360));
		return true;

	case ID_ROTATE_CCW:
		// rotate counter-clockwise 90 degrees
		SetRotation(Wrap(GetRotation() - 90, 360));
		return true;

	case ID_MIRROR_HORZ:
		// mirror horizontally
		SetMirrorHorz(!IsMirrorHorz());
		return true;

	case ID_MIRROR_VERT:
		// mirror vertically
		SetMirrorVert(!IsMirrorVert());
		return true;
	}

	// not handled
	return false;
}

bool D3DView::OnTimer(WPARAM timer, LPARAM callback)
{
	switch (timer)
	{
	case fpsTimerID:
		// get the current statistics - update only if we have a new 
		// instantaneous counter
		if (perfMon.GetCurFPS(fpsCur, .5f))
		{
			fpsAvg = perfMon.GetRollingFPS();
			UpdateText();
		}

		// timer handled
		return true;
	}

	// use the default handling
	return __super::OnTimer(timer, callback);
}

// Handle a mouse button down event.  The caller provides the Action code
// mapped for the physical button.
bool D3DView::OnMouseButtonDown(int button, POINT pt)
{
	// begin a mouse drag operation
	MouseDragBegin(button, pt);
	return true;
}

bool D3DView::OnMouseButtonUp(int button, POINT pt)
{
	// if the button doesn't match the button that started the
	// drag operation, ignore it
	if (button != dragButton)
		return 0;

	// check for a right-click
	if (dragButton == MouseButton::mbRight)
		ShowContextMenu(pt);

	// end the drag operation
	dragButton = 0;

	// end mouse capture
	ReleaseCapture();

	// processed
	return true;
}

bool D3DView::OnMouseMove(POINT pt)
{
	// Get the delta from the last position
	POINT delta;
	delta.x = pt.x - dragPos.x;
	delta.y = pt.y - dragPos.y;

	// if we took any action during a drag, it would go here

	// update the last position
	dragPos = pt;

	// handled
	return true;
}

void D3DView::MouseDragBegin(int button, POINT pt)
{
	// remember the button and where we started
	dragButton = button;
	dragPos = pt;

	// capture the mouse throughout the drag so that we still get events
	// if the mouse leaves the window
	SetCapture(hWnd);
}

// Update the text display
void D3DView::UpdateText()
{
	// clear old text
	textDraw->Clear();

	// starting x and y offset
	float x = 10;
	float y = 10;
	float lineHeight = dmdFont->GetLineHeight();

	// add the FPS display
	if (fpsDisplay)
	{
		XMFLOAT4 color = { 1.0f, 0.6f, 0.0f, 1.0f };

		// format the text
		TCHAR buf[256];
		_stprintf_s(buf, _T("FPS Cur %.2f, Avg %.2f"), fpsCur, fpsAvg);

		// add it
		textDraw->Add(buf, dmdFont, color, x, y, 0);
		y += lineHeight;

		// add the cpu display
		PerfMon::CPUMetrics cpuMetrics;
		if (perfMon.GetCPUMetrics(cpuMetrics))
		{
			TCHAR *p = buf + _stprintf_s(
				buf, _T("CPU: %3d%% | Cores: "), cpuMetrics.cpuLoad);
			for (int i = 0; i < cpuMetrics.nCpus; ++i)
			{
				p += _stprintf_s(p, buf + countof(buf) - p, _T("%3d%%  "),
					cpuMetrics.coreLoad[i]);
			}
			textDraw->Add(buf, dmdFont, color, x, y, 0);
			y += lineHeight;
		}
	}
}

void D3DView::UpdateMenu(HMENU hMenu, BaseWin *fromWin)
{
	// update FPS display
	CheckMenuItem(hMenu, ID_FPS, MF_BYCOMMAND | (fpsDisplay ? MF_CHECKED : MF_UNCHECKED));

	// update the mirror options
	CheckMenuItem(hMenu, ID_MIRROR_HORZ, MF_BYCOMMAND | (IsMirrorHorz() ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(hMenu, ID_MIRROR_VERT, MF_BYCOMMAND | (IsMirrorVert() ? MF_CHECKED : MF_UNCHECKED));
}

void D3DView::OnResize(int width, int height)
{
	// do the base class work
	__super::OnResize(width, height);

	// update D3D resources with the new size
	if (d3dwin != 0)
		d3dwin->ResizeWindow(width, height);

	// resize the camera view to match
	OnResizeCameraView();
}

// Change the size of the camera view
void D3DView::OnResizeCameraView()
{
	// remember the new layout
	switch (camera != 0 ? camera->GetMonitorRotation() : 0)
	{
	case 90:
	case 270:
		szLayout = { szClient.cy, szClient.cx };
		break;

	default:
		szLayout = szClient;
		break;
	}

	// update the camera
	if (camera != 0)
	{
		// update the view size
		camera->SetViewSize(szClient.cx, szClient.cy);

		// update the ortho projection scale
		SetOrthoScale(szClient.cx, szClient.cy);
	}

	// Update the drawing list, to account for any changes in scaling
	// for the new layout
	ScaleSprites();
}

void D3DView::RenderAll()
{
	for (auto it : activeD3DViews)
		it->RenderFrame();
}

int D3DView::MessageLoop()
{
	// stash the audio manager instance in a stack local for quicker reference
	AudioManager *audioManager = AudioManager::Get();

	// idle processing
	DWORD lastIdleTime = GetTickCount();
	int curRenderWinIndex = 0;
	auto DoIdle = [&lastIdleTime, audioManager, &curRenderWinIndex]()
	{
		// Do graphics rendering in one D3D view when the message queue is idle.
		// We work through the windows round-robin on each idle pass.  We only
		// render one window per idle pass so that we can get right back to the
		// event loop, to minimize event processing latency.  We don't want key
		// inputs to feel laggy by forcing them to wait for every window to
		// render.
		int n = 0;
		for (auto it : activeD3DViews)
		{
			if (n++ == curRenderWinIndex)
			{
				it->RenderFrame();
				break;
			}
		}

		// advance to the next render window for the next pass
		if (++curRenderWinIndex >= (int)activeD3DViews.size())
			curRenderWinIndex = 0;

		// call idle event subscribers
		for (auto it = idleEventSubscribers.begin(); it != idleEventSubscribers.end(); )
		{
			// 'it' will become invalid if the current subscriber unsubscribes
			// in the course of this event callback.  We want to allow that, so
			// to make the loop safe, we have to get the next element ahead of
			// the call.
			auto nxt = it;
			++nxt;

			// call the subscriber
			(*it)->OnIdleEvent();

			// move on to the next one
			it = nxt;
		}

		// Update the audio engine
		audioManager->Update();

		// reset the idle timer
		lastIdleTime = GetTickCount();
	};

	// loop until we get an application Quit message or the window closes
	for (;;)
	{
		// Force a render pass if it's been too long
		if (GetTickCount() > lastIdleTime + 100)
			DoIdle();

		// Check for Windows messages.  If the application is in the foreground,
		// use PeekMessage() so that we do D3D rendering on idle.  If not, we can
		// wait for messages with GetMessage() so that we don't use a lot of CPU
		// while in the background.
		MSG msg;
		if (Application::IsInForeground())
		{
			// we're in the foreground - use the non-blocking PeekMessage,
			// so that we can immediately do another D3D rendering update
			// if no messages are available
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				// Got a message - dispatch it.  Note that most Windows application
				// message loops would call TranslateAccelerator() at this point,
				// before doing the dispatch call.  But player mode uses the game
				// style of keyboard handling instead of the usual productivity
				// app style, so we don't use normal menu accelerators.  We thus
				// bypass accelerator translation and instead dispatch the raw
				// keyboard messages to the window proc for interpretation as
				// game control inputs.
				TranslateMessage(&msg);
				DispatchMessage(&msg);

				// If we received the WM_QUIT message, the application is terminating.
				// Return the process exit code from the message to the caller.
				if (msg.message == WM_QUIT)
					return (int)msg.wParam;
			}
			else
			{
				// Do idle processing
				DoIdle();
			}
		}
		else
		{
			// if an event isn't immediately available, do idle processing
			if (!PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE))
				DoIdle();

			// We're in the background - wait for a message.  This will
			// freeze D3D updates, which is fine when we're in the background,
			// and minimizes our CPU usage.
			if (!GetMessage(&msg, nullptr, 0, 0))
				return 0;

			// translate and dispatch the message
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

bool D3DView::OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case AVPMsgLoopNeeded:
		// Loop needed in a video sprite.  Search for a matching
		// video sprite in our drawing list.
		for (auto s : sprites)
		{
			// if this is a video sprite, match it against the cookie
			if (auto vs = dynamic_cast<VideoSprite*>(s); vs != nullptr && vs->GetVideoPlayerCookie() == wParam)
			{
				// restart playback
				vs->GetVideoPlayer()->Replay(SilentErrorHandler());

				// no need to keep looking
				break;
			}
		}
		break;
	}

	// use the default handling
	return __super::OnAppMessage(msg, wParam, lParam);
}
