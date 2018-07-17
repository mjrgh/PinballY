// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Real DMD interface

#pragma once
#include "VLCAudioVideoPlayer.h"

class ErrorHandler;
class GameListItem;
class VLCAudioVideoPlayer;

class RealDMD : public VLCAudioVideoPlayer::DMD
{
public:
	RealDMD();
	~RealDMD();

	// Locate the DLL.  Returns true if the DLL is found, false if not
	bool FindDLL();

	// initialize: load the device interface DLL and open the device
	bool Init(ErrorHandler &eh);

	// shut down
	void Shutdown();

	// Update the display to match the current game list selection
	void UpdateGame();

	// Clear media.  This removes any playing video and clears the
	// last game record.  This should be called before launching a
	// game program, to make sure we're not trying to send updates
	// to the display while the game might be using it.
	void ClearMedia();

	// Receive notification that new high scores have been received
	// for a given game.
	void OnUpdateHighScores(GameListItem *game);

	// Loop Needed notification.  The main window calls this when it
	// gets an AVPMsgLoopNeeded message from the video player.  If
	// the cookie matches our current video's cookie, we'll replay
	// our video.
	void VideoLoopNeeded(WPARAM cookie);

	// 
	// video player callback interface
	// 

	// present a frame
	virtual void PresentVideoFrame(int width, int height,
		const BYTE *y, const BYTE *u, const BYTE *v) override;

	// do we support RGB?
	virtual bool SupportsRGBDisplay() const override;

	// get/set mirroring
	bool IsMirrorHorz() const { return mirrorHorz; }
	bool IsMirrorVert() const { return mirrorVert; }
	void SetMirrorHorz(bool b);
	void SetMirrorVert(bool b);

protected:
	// singleton instance
	static RealDMD *inst;

	// DLL location - set by FindDLL()
	TSTRING dllPath;

	// load the DLL
	bool LoadDLL(ErrorHandler &eh);

	// Have we attempted to load the DLL yet?
	static bool dllLoaded;

	// DLL module handle
	static HMODULE hmodDll;

	// vertical/horizontal mirroring
	bool mirrorHorz;
	bool mirrorVert;

	// Monochrome base color for the current game, from the 
	// VPinMAME settings for the game's ROM.
	COLORREF baseColor;

	// Color space for a stored image.  This specifies the type of
	// pixel data stored in the image, and the render function we 
	// use to display it.
	enum ColorSpace
	{
		DMD_COLOR_MONO4,		// 4-shade grayscale
		DMD_COLOR_MONO16,		// 16-shade grayscale
		DMD_COLOR_RGB			// 24-bit RGB
	};

	// current game selection
	GameListItem *curGame;

	// Force a reload of the current game's media
	void ReloadGame();

	// video player
	RefPtr<VLCAudioVideoPlayer> videoPlayer;

	// color space for the video
	ColorSpace videoColorSpace;

	// Generate high score graphics for the current game
	void GenerateHighScoreGraphics();

	// The "slide show".  This is a series of still images that
	// we display on the DMD.  If there's a video, we display
	// these in alternation with the video.
	//
	// If there's a still image for the game (from the "Real DMD
	// Images" media folder), it's the first slide in the list.
	// Note that we only use one media type for each game - still
	// image or video, not both
	// 
	// If we have high score graphics, they appear in this list.
	// High score graphics (unlike the still image from the media
	// folder) can be used in combination with a video. 
	struct Slide
	{
		// Slide type
		enum SlideType
		{
			MediaSlide,      // still image from the game's media folder
			HighScoreSlide   // generated high score screen
		} slideType;

		Slide(ColorSpace colorSpace, BYTE *pix, DWORD displayTime, SlideType slideType) :
			colorSpace(colorSpace),
			pix(pix),
			displayTime(displayTime),
			slideType(slideType)
		{
		}

		// The image's color type - this selects the render
		// function that we use to display it
		ColorSpace colorSpace;

		// Pixel array for the image
		std::unique_ptr<BYTE> pix;

		// display time for this image, in milliseconds
		DWORD displayTime;
	};
	std::list<Slide> slideShow;

	// start slide show playback
	void StartSlideShow();

	// advance to the next slide
	void NextSlide();

	// render the current slide
	void RenderSlide();

	// current slide show position
	decltype(slideShow)::const_iterator slideShowPos;

	// slide show timer ID
	UINT_PTR slideShowTimerID;

	// timer event handler
	static VOID CALLBACK SlideTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
};
