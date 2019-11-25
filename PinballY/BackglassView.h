// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Backglass view window
//
// This is a child window that serves as the D3D drawing surface for
// the backglass window.
//
// The BackglassBaseView is a common subclass for both the backglass
// and topper views.  These are almost identical; the only difference
// is the media sources.

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

// Common base class for backglass and topper views
class BackglassBaseView : public SecondaryView
{
public:
	BackglassBaseView(int contextMenuId, const TCHAR *configVarPrefix);

	// Show an instruction card; returns true on success, false if
	// we couldn't load the card image.
	bool ShowInstructionCard(const TCHAR *filename);

	// Remove any instruction card display
	void RemoveInstructionCard();

	// frame window is being shown/hidden
	virtual void OnShowHideFrameWindow(bool show) override;

	// begin running game mode
	virtual void ClearMedia() override;

protected:
	// update our sprite drawing list
	virtual void UpdateDrawingList() override;

	// update sprite scaling
	virtual void ScaleSprites() override;

	// update the animation
	virtual bool UpdateAnimation() override;

	// Instruction card sprite
	RefPtr<Sprite> instructionCard;
};

// The concrete backglass view class
class BackglassView : public BackglassBaseView
{
public:
	BackglassView();

protected:
	virtual UINT GetNextWindowSyncCommand() const override { return ID_SYNC_DMD; }

	// Get the background media info
	virtual const MediaType *GetBackgroundImageType() const override;
	virtual const MediaType *GetBackgroundVideoType() const override;
	virtual const TCHAR *GetDefaultBackgroundImage() const override { return _T("Default Backglass"); }
	virtual const TCHAR *GetDefaultBackgroundVideo() const override { return _T("Default Backglass"); }
	virtual const TCHAR *GetDefaultSystemImage() const override { return _T("Default Images\\No Back Glass"); }
	virtual const TCHAR *GetDefaultSystemVideo() const override { return _T("Default Videos\\No Back Glass"); }

	virtual const TCHAR *StartupVideoName() const override { return _T("Startup Video (bg)"); }

	// "show when running" window ID
	virtual const TCHAR *ShowWhenRunningWindowId() const override { return _T("bg"); }
};
