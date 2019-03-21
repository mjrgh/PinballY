// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Instruction Card view

#include "stdafx.h"
#include "../Utilities/Config.h"
#include "InstCardView.h"
#include "Resource.h"
#include "D3D.h"
#include "D3DWin.h"
#include "GraphicsUtil.h"
#include "Camera.h"
#include "TextDraw.h"
#include "VersionInfo.h"
#include "VideoSprite.h"
#include "GameList.h"
#include "Application.h"
#include "MouseButtons.h"

using namespace DirectX;

namespace ConfigVars
{
	static const TCHAR *InstCardWinVarPrefix = _T("InstCardWindow");
	static const TCHAR *EnableFlash = _T("InstructionCards.EnableFlash");
};

// construction
InstCardView::InstCardView() : SecondaryView(IDR_INSTCARD_CONTEXT_MENU, ConfigVars::InstCardWinVarPrefix)
{
	// subscribe for configuration change events
	ConfigManager::GetInstance()->Subscribe(this);
	OnConfigChange();
}

void InstCardView::OnConfigChange()
{
	enableFlash = ConfigManager::GetInstance()->GetBool(ConfigVars::EnableFlash, true);
}

// get the background image, respecting the Flash Enabled option
void InstCardView::GetBackgroundImageMedia(const GameListItem *game, const MediaType *mtype, TSTRING &image)
{
	game->GetMediaItem(image, *mtype, false, enableFlash);
}

// get the background media info
const MediaType *InstCardView::GetBackgroundImageType() const { return &GameListItem::instructionCardImageType; }
const MediaType *InstCardView::GetBackgroundVideoType() const { return nullptr; }
