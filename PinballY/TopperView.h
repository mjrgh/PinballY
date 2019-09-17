// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Topper view window
//
// This is a child window that serves as the D3D drawing surface for
// the Topper window.  The topper is a very lightly tweaked subclass
// of the backglass window, since it serves almost exactly the same
// purpose.  The only difference is that the topper takes its media
// from the topper files, obviously, instead of the backglass files.

#pragma once

#include "../Utilities/Config.h"
#include "D3D.h"
#include "D3DWin.h"
#include "D3DView.h"
#include "Camera.h"
#include "TextDraw.h"
#include "PerfMon.h"
#include "BaseView.h"
#include "SecondaryView.h"
#include "BackglassView.h"

class Sprite;
class VideoSprite;
class TextureShader;
class GameListItem;

class TopperView : public BackglassBaseView
{
public:
	TopperView();

protected:
	virtual UINT GetNextWindowSyncCommand() const override { return ID_SYNC_INSTCARD; }

	// Get the background media info
	virtual const MediaType *GetBackgroundImageType() const override;
	virtual const MediaType *GetBackgroundVideoType() const override;
	virtual const TCHAR *GetDefaultBackgroundImage() const override { return _T("Default Topper"); }
	virtual const TCHAR *GetDefaultBackgroundVideo() const override { return _T("Default Topper"); }
	virtual const TCHAR *GetDefaultSystemImage() const override { return _T("Default Images\\No Topper"); }
	virtual const TCHAR *GetDefaultSystemVideo() const override { return _T("Default Videos\\No Topper"); }
	virtual const TCHAR *StartupVideoName() const override { return _T("Startup Video (topper)"); }

	// "show when running" window ID
	virtual const TCHAR *ShowWhenRunningWindowId() const override { return _T("topper"); }
};

