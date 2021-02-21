// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD view window
// This is a child window that serves as the D3D drawing surface for
// the DMD window.

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
#include "FontPref.h"

class Sprite;
class VideoSprite;
class GameListItem;
class DMDFont;

// DMD sprite.  This is a simple subclass of the basic sprite
// that uses the special DMD shader, which renders a simulation
// of the visible pixel structure of a physical DMD.
class DMDSprite : public Sprite
{
public:
	DMDSprite(RGBQUAD bgColor, BYTE bgAlpha) : bgColor(bgColor), bgAlpha(bgAlpha) { }

protected:
	virtual void Render(Camera *camera) override;
	virtual Shader *GetShader() const override;

	RGBQUAD bgColor;
	BYTE bgAlpha;
};

class DMDView : public SecondaryView, public ConfigManager::Subscriber
{
	friend struct HighScoreGraphicsGenThread;

public:
	DMDView();

	// clear media
	virtual void ClearMedia() override;

	// receive a high score update
	void OnUpdateHighScores(GameListItem *game);

	// pick a font for a generated high score screen
	static const DMDFont *PickHighScoreFont(const std::list<TSTRING> &group);
	static const DMDFont *PickHighScoreFont(const std::list<const TSTRING*> &group);

	// wait for high score image generator threads to exit
	void WaitForHighScoreThreads(DWORD timeout = INFINITE);

	// enter/exit running game mode
	virtual void BeginRunningGameMode(GameListItem *game, GameSystem *system, bool &hasVideo) override;
	virtual void EndRunningGameMode() override;

	// Alphanumeric display options
	struct AlphanumOptions
	{
		AlphanumOptions() 
		{
			// set a default color scheme based on an approximation of
			// the characteristic amber of the original plasma displays
			// in the 1980s machines
			InitFromSegColor(0xff, 0x58, 0x20); 
		}

		// Initialize with default layer settings based on a given
		// color for the foreground segments.
		void InitFromSegColor(BYTE r, BYTE g, BYTE b);
		void InitFromSegColor(const RGBQUAD &rgb) { InitFromSegColor(rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue); }

		// slant angle in degrees
		int slant = 10;

		// layer options
		struct Layer
		{
			// color for this segment (with alpha)
			Gdiplus::Color color;

			// dilation x and y radius (0 = no dilation)
			int dilationx, dilationy;

			// gaussian blur radius (0 = no blur)
			int blur;
		};

		// Lit segments layer.  Displays the main lighting of the lit 
		// segments.
		Layer lit;

		// Upper glow layer.  Intended for a bright, narrow halo around 
		// the lit segments.
		Layer glow1;

		// Lower glow layer.  Intended for a larger and more diffuse 
		// halo around the lit segments.
		Layer glow2;

		// Unlit segments.  Displays a faint image of the unlit segments,
		// to simulate the visible structure of a physical segmented
		// dsiplay device.
		Layer unlit;
	};

	// Generate a DMD-style image slide.  This can be used to generate
	// this graphics style for use in any window.
	//
	// The request runs asynchronously in a thread.  On completion, we
	// send a BVMsgDMDImageReady to the specified view window with the
	// results.  The message has parameters WPARAM = seqence number,
	// LPARAM = (std::list<HighScoreImage>*).
	//
	// Returns the sequence number of the request, which can be used 
	// to associate the result back to the requester in the completion
	// message handler.
	DWORD GenerateDMDImage(
		BaseView *view, std::list<TSTRING> &messages,
		const TCHAR *style = nullptr, const TCHAR *font = nullptr,
		RGBQUAD *txtColor = nullptr, RGBQUAD *bgColor = nullptr, BYTE bgAlpha = 255,
		AlphanumOptions *alphanumOptions = nullptr);

	// high-score graphics list
	struct HighScoreImage
	{
		// sprite type, for deferred sprite creation from a bitmap
		enum SpriteType
		{
			NoSpriteType,
			NormalSpriteType,
			DMDSpriteType
		};

		HighScoreImage(SpriteType spriteType, DWORD t) :
			spriteType(spriteType), displayTime(t)
		{ }

		HighScoreImage(Sprite *sprite, DWORD t) :
			spriteType(NoSpriteType), sprite(sprite), displayTime(t)
		{ }

		HighScoreImage(SpriteType spriteType, const BITMAPINFO &bmi, BYTE *dibits,
			DWORD t, RGBQUAD bgColor, BYTE bgAlpha) :
			spriteType(spriteType), dibits(dibits), displayTime(t), 
			bgColor(bgColor), bgAlpha(bgAlpha)
		{
			memcpy(&this->bmi, &bmi, sizeof(this->bmi));
		}

		HighScoreImage(SpriteType spriteType, HBITMAP hbmp, const BITMAPINFO &bmi, const void *dibits, DWORD t) :
			spriteType(spriteType), hbmp(hbmp), dibits(dibits), displayTime(t)
		{
			memcpy(&this->bmi, &bmi, sizeof(this->bmi));
		}

		// transfer ownership of resources from another HighScoreImage object
		HighScoreImage(HighScoreImage &i) :
			spriteType(i.spriteType),
			sprite(i.sprite.Detach()),
			hbmp(i.hbmp.Detach()),
			dibits(i.dibits),
			displayTime(i.displayTime),
			bgColor(i.bgColor),
			bgAlpha(i.bgAlpha)
		{
			// copy the bitmap info
			memcpy(&this->bmi, &i.bmi, sizeof(this->bmi));

			// we've taken ownership of the DIbits - clear it in the source
			i.dibits = nullptr;
		}

		~HighScoreImage()
		{
			// If we have a dibits array but no bitmap handle, the 
			// dibits array was separately allocated and we're
			// responsible for cleaning it up.  If there's a bitmap
			// handle, the dibits are owned by the bitmap and don't
			// need to be separately deleted.
			if (hbmp == NULL && dibits != nullptr)
				delete[] dibits;
		}

		// for deferred sprite creation, the type of Sprite object to create
		SpriteType spriteType;

		// image for this item
		RefPtr<Sprite> sprite;

		// Create and load the sprite
		void CreateSprite()
		{
			// if there's no sprite, create one
			if (sprite == nullptr)
			{
				// create a sprite of the appropriate type
				switch (spriteType)
				{
				case HighScoreImage::DMDSpriteType:
					sprite.Attach(new DMDSprite(bgColor, bgAlpha));
					break;

				default:
					sprite.Attach(new Sprite());
					break;
				}

				// load the image
				if (!sprite->Load(bmi, dibits, SilentErrorHandler(), _T("high score slide")))
					sprite = nullptr;
			}
		}

		// We create the images in a background thread, staging them
		// initially to a DIB for later conversion to a D3D image in
		// the main thread.  The DIB information is saved here until
		// the renderer needs to display the image, at which point
		// it's converted into a sprite.
		HBITMAPHolder hbmp;
		BITMAPINFO bmi;
		const void *dibits = nullptr;

		// time in milliseconds to display this item
		DWORD displayTime = 3500;

		// Background color and alpha for the DMD shader.   This has
		// to be stored separately from the image, since we have to 
		// pass it to the shader through a constant buffer.
		RGBQUAD bgColor = { 0, 0, 0, 0 };
		BYTE bgAlpha = 255;
	};
	std::list<HighScoreImage> highScoreImages;

protected:
	// ConfigManager::Subscriber implementation
	virtual void OnConfigReload() override { OnConfigChange(); }

	// update internal variables for a config change
	void OnConfigChange();

	// private application message (WM_APP to 0xBFFF)
	virtual bool OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// private window mesage
	virtual bool OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// timers
	virtual bool OnTimer(WPARAM timer, LPARAM callback) override;

	// timer IDs
	static const int StartHighScoreTimerID = 200;  // start the high score slide show
	static const int NextHighScoreTimerID = 201;   // advance to next high score image

	// get the next window for game syncing
	virtual UINT GetNextWindowSyncCommand() const override { return ID_SYNC_TOPPER; }

	// Get the background media info
	virtual const MediaType *GetBackgroundImageType() const override;
	virtual const MediaType *GetBackgroundVideoType() const override;
	virtual const TCHAR *GetDefaultBackgroundImage() const override { return _T("Default DMD"); }
	virtual const TCHAR *GetDefaultBackgroundVideo() const override { return _T("Default DMD"); }
	virtual const TCHAR *GetDefaultSystemImage() const override { return _T("Default Images\\No DMD"); }
	virtual const TCHAR *GetDefaultSystemVideo() const override { return _T("Default Videos\\No DMD"); }
	virtual const TCHAR *StartupVideoName() const override { return _T("Startup Video (dmd)"); }

	// "show when running" window ID
	virtual const TCHAR *ShowWhenRunningWindowId() const override { return _T("dmd"); }

	// handle a change of background image
	virtual void OnChangeBackgroundImage() override;

	// add the main background image to the drawing list
	virtual void AddBackgroundToDrawingList();

	// scale sprites
	virtual void ScaleSprites() override;

	// generate high score images for the current game, or for custom
	// Javascript messages
	void GenerateHighScoreImages();

	// clear out the high score images
	void ClearHighScoreImages();

	// get the "auto" high score style for the current game
	const TCHAR *GetCurGameHighScoreStyle();

	// DMD color palette holder.  This holds a 16-shade monochrome color
	// ramp to use for generated text on the DMD, derived from the VPinMAME
	// per-game color settings.
	struct DMDPalette
	{
		// Color ramp - color[0] is the color for fully OFF pixels,
		// color[15] is the color for 100% brightness pixels, and the
		// values in between are a linear ramp of grayscale brightness.
		RGBQUAD color[16];
	};

	// Get the DMD dot color for high score displays for the current game.  If
	// the text and background colors are explicitly specified, we'll construct
	// a custom color shade ramp based on those endpoints.  Otherwise, if the
	// currently selected game has custom VPinMAME color settings, we'll use
	// those.  If not, we'll use the VPinMAME default color settings, or a
	// default amber approximating the original 1990s plasma displays if no
	// VPM default is available.
	void GetCurGameHighScoreColor(DMDPalette &pal, RGBQUAD *txtColor = nullptr, RGBQUAD *bgColor = nullptr);

	// start the high score slideshow
	void StartHighScorePlayback();

	// Current display position in high score image list.  This
	// is ignored when the high score image list is empty.  When
	// it's non-empty, this points to the current image being
	// displayed.
	decltype(highScoreImages)::iterator highScorePos;

	// Font preference parser for the typewriter-style high score font
	FontPref ttHighScoreFont{ 0, _T("Courier New") };
	COLORREF ttHighScoreTextColor;

	// Set the high score image list.  When we switch to a new game, we kick
	// off a thread to generate the high score images.  We use a thread rather
	// than generating them on the main thread, because it can take long enough
	// to cause a noticeable delay in the UI if done on the main thread.  The
	// sequence number lets us determine if this is the most recent request;
	// we'll simply discard results from an older request.
	void SetHighScoreImages(DWORD seqno, std::list<HighScoreImage> *images);

	// Next available image request sequence number
	DWORD nextImageRequestSeqNo = 1;

	// Sequence number of current outstanding image request
	// in this window
	DWORD pendingImageRequestSeqNo = 0;

	// Number of outstanding high score image generator threads
	volatile DWORD nHighScoreThreads = 0;
};
