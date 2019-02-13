// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Topper view

#include "stdafx.h"
#include "../Utilities/Config.h"
#include "TopperView.h"
#include "Resource.h"
#include "D3D.h"
#include "D3DWin.h"
#include "GraphicsUtil.h"
#include "Camera.h"
#include "TextDraw.h"
#include "VideoSprite.h"
#include "GameList.h"
#include "Application.h"
#include "MouseButtons.h"

using namespace DirectX;

namespace ConfigVars
{
	static const TCHAR *TopperWinVarPrefix = _T("TopperWindow");
};

// construction
TopperView::TopperView() : BackglassBaseView(IDR_TOPPER_CONTEXT_MENU, ConfigVars::TopperWinVarPrefix)
{
}

// get the background media info
const MediaType *TopperView::GetBackgroundImageType() const { return &GameListItem::topperImageType; }
const MediaType *TopperView::GetBackgroundVideoType() const { return &GameListItem::topperVideoType; }
