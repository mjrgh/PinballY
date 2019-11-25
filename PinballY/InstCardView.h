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

class InstCardView : public SecondaryView, public ConfigManager::Subscriber

{
public:
	InstCardView();

protected:
	virtual UINT GetNextWindowSyncCommand() const override { return 0; }

	// Get the background media info
	virtual const MediaType *GetBackgroundImageType() const override;
	virtual const MediaType *GetBackgroundVideoType() const override;
	virtual void GetBackgroundImageMedia(const GameListItem *game, const MediaType *mtype, TSTRING &image) override;
	virtual const TCHAR *GetDefaultBackgroundImage() const override { return _T("Default Instruction Card"); }
	virtual const TCHAR *GetDefaultBackgroundVideo() const override { return _T("Default Instruction Card"); }
	virtual const TCHAR *GetDefaultSystemImage() const override { return _T("Default Images\\No Instruction Card"); }
	virtual const TCHAR *GetDefaultSystemVideo() const override { return _T("Default Videos\\No Instruction Card"); }
	virtual const TCHAR *StartupVideoName() const override { return _T("Startup Video (instcard)"); }

	// "show when running" window ID
	virtual const TCHAR *ShowWhenRunningWindowId() const override { return _T("instcard"); }

	// ConfigManager::Subscriber implementation
	virtual void OnConfigReload() override { OnConfigChange(); }
	void OnConfigChange();

	// are SWF files enabled?
	bool enableFlash = true;
};
