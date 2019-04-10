// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Backglass view

#include "stdafx.h"
#include "../Utilities/Config.h"
#include "../Utilities/GraphicsUtil.h"
#include "BackglassView.h"
#include "Resource.h"
#include "D3D.h"
#include "D3DWin.h"
#include "Camera.h"
#include "TextDraw.h"
#include "VersionInfo.h"
#include "Sprite.h"
#include "VideoSprite.h"
#include "GameList.h"
#include "Application.h"


using namespace DirectX;

// -----------------------------------------------------------------------
//
// Backglass View
//

namespace ConfigVars
{
	static const TCHAR *BackglassWinVarPrefix = _T("BackglassWindow");
};

// construction
BackglassView::BackglassView() : BackglassBaseView(IDR_BACKGLASS_CONTEXT_MENU, ConfigVars::BackglassWinVarPrefix)
{
}

// get the background media info
const MediaType *BackglassView::GetBackgroundImageType() const { return &GameListItem::backglassImageType; }
const MediaType *BackglassView::GetBackgroundVideoType() const { return &GameListItem::backglassVideoType; }

// -----------------------------------------------------------------------
//
// Backglass base view - common base class for the backglass and topper
// views
//

BackglassBaseView::BackglassBaseView(int contextMenuId, const TCHAR *configVarPrefix)
	: SecondaryView(contextMenuId, configVarPrefix)
{
}

void BackglassBaseView::UpdateDrawingList()
{
	// do the base class work
	__super::UpdateDrawingList();

	// add the instruction card image
	if (instructionCard != nullptr)
		sprites.push_back(instructionCard);

	// rescale the sprites
	ScaleSprites();
}

void BackglassBaseView::ScaleSprites()
{
	// do the base class work
	__super::ScaleSprites();

	// rescale the instruction card to a 95% span, keeping the 
	// original aspect ratio
	ScaleSprite(instructionCard, .95f, true);
}

bool BackglassBaseView::UpdateAnimation()
{
	// do the base class work
	bool running = __super::UpdateAnimation();

	// check for instruction card fading
	if (instructionCard != nullptr)
	{
		if (instructionCard->IsFadeDone(true))
		{
			// if fading out, remove the instruction card
			if (instructionCard->alpha == 0.0f)
			{
				instructionCard = nullptr;
				UpdateDrawingList();
			}
		}
		else
		{
			// not done
			running = true;
		}
	}

	// return the "still running" result
	return running;
}

void BackglassBaseView::OnShowHideFrameWindow(bool show)
{
	// if hiding, remove the instruction card
	if (!show)
		instructionCard = nullptr;

	// do the base class work
	__super::OnShowHideFrameWindow(show);
}

void BackglassBaseView::ClearMedia()
{
	// remove the instruction card
	instructionCard = nullptr;

	// do the base class work
	__super::ClearMedia();
}

const DWORD instCardFadeTime = 150;
bool BackglassBaseView::ShowInstructionCard(const TCHAR *filename)
{
	// use a fade if we don't already have an instruction card
	bool fade = instructionCard == nullptr;

	// set up a new instruction card
	instructionCard.Attach(PrepInstructionCard(filename));
	if (instructionCard == nullptr)
		return false;

	// start the fade if desired
	if (fade)
	{
		// start the fade, and start the timer to monitor for completion
		instructionCard->StartFade(1, instCardFadeTime);
		SetTimer(hWnd, animTimerID, animTimerInterval, 0);
	}

	// update the drawing list with the card
	UpdateDrawingList();

	// success
	return true;
}

void BackglassBaseView::RemoveInstructionCard()
{
	// if an instruction card is showing, fade it out
	if (instructionCard != nullptr)
	{
		// start the fade-out, and set the timer to monitor for completion
		instructionCard->StartFade(-1, instCardFadeTime);
		SetTimer(hWnd, animTimerID, animTimerInterval, 0);
	}
}

