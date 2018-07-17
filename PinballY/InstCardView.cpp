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
};

// construction
InstCardView::InstCardView() : BorderlessSecondaryView(IDR_INSTCARD_CONTEXT_MENU, ConfigVars::InstCardWinVarPrefix)
{
}

// get the background media info
const MediaType *InstCardView::GetBackgroundImageType() const { return &GameListItem::instructionCardImageType; }
const MediaType *InstCardView::GetBackgroundVideoType() const { return nullptr; }
const TCHAR *InstCardView::GetDefaultBackgroundImage() const { return _T("assets\\DefaultInstCard.png"); }
