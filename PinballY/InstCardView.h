// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

// Instruction Card view window
// This is a child window that serves as the D3D drawing surface for
// the Instruction Card window.

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

class Sprite;
class VideoSprite;
class TextureShader;
class GameListItem;

class InstCardView : public SecondaryView
{
public:
	InstCardView();

protected:
	virtual UINT GetNextWindowSyncCommand() const override { return 0; }

	// Get the background media info
	virtual const MediaType *GetBackgroundImageType() const override;
	virtual const MediaType *GetBackgroundVideoType() const override;
	virtual const TCHAR *GetDefaultBackgroundImage() const override;
	virtual const TCHAR *StartupVideoName() const override { return _T("Startup Video (instcard)"); }

	// "show when running" window ID
	virtual const TCHAR *ShowWhenRunningWindowId() const override { return _T("instcard"); }
};
