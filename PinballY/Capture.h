// This file is part of PinballY
// Copyright 2024 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Capture definitions
// Defines some types related to media capture.

#pragma once

#include "../Utilities/StringUtil.h"
#include "../Utilities/Pointers.h"
#include "GameList.h"
#include "CaptureStatusWin.h"

// Media item capture information.  This stores information on a single
// item during the capture process.
struct CaptureItemDesc
{
	CaptureItemDesc(const MediaType &mediaType, bool enableAudio) :
		mediaType(mediaType),
		enableAudio(enableAudio),
		windowRotation(0),
		windowMirrorVert(false),
		windowMirrorHorz(false),
		mediaRotation(0),
		captureTime(0),
		manualStart(false),
		manualStop(false)
	{ }

	// media type
	const MediaType &mediaType;

	// for a video item, is audio capture enabled?
	bool enableAudio;

	// filename with path of this item
	TSTRING filename;

	// screen area to capture, in screen coordinates
	RECT rc{ 0 };

	// DXGI output index of monitor containing capture area
	int dxgiOutputIndex = 0;

	// desktop coordinates of DXGI output monitor containing capture area
	RECT rcMonitor;

	// Current display rotation for this window, in degrees
	// clockwise, relative to the nominal desktop layout.  In
	// most cases, only the playfield window is rotated, and 
	// the typical playfield rotation in a cab is 90 degrees
	// (so that the bottom of the playfield image is drawn
	// at the right edge of the window).
	int windowRotation;

	// Current mirroring settings for this window
	bool windowMirrorVert;
	bool windowMirrorHorz;

	// Target rotation for this media type, in degrees.  This
	// is the rotation used for media of this type as stored
	// on disk.  All media types except playfield are stored
	// with no rotation (0 degrees).  For compatibility with
	// existing HyperPin and PinballX media, playfield media
	// are stored at 270 degrees rotation (so that the bottom
	// of the playfield image is drawn at the left edge of
	// the window).
	int mediaRotation;

	// capture time in milliseconds, for videos
	DWORD captureTime;

	// manual start/stop mode
	bool manualStart;
	bool manualStop;
};

// Capture information.  This stores the settings for a capture run, common
// to all items in the batch if capturing multiple items.
struct CaptureInfo
{
	// initialization time (ms)
	static const DWORD initTime = 3000;

	// startup delay time, in milliseconds
	DWORD startupDelay = 5000;

	// estimated total capture time
	DWORD totalTime = 0;

	// two-pass encoding mode
	bool twoPassEncoding = false;

	// video codec options for pass 1 of a two-pass recording
	TSTRING vcodecPass1;

	// temporary file folder
	TSTRING tempFolder;

	// captured video resolution limit
	enum ResLimit
	{
		ResLimitNone,  // no limit; use native resolution
		ResLimitHD     // limit to HD resolution (1920x1080)
	};
	ResLimit videoResLimit = ResLimit::ResLimitNone;

	// Custom command options
	TSTRING customVideoSource;
	TSTRING customVideoCodec;
	TSTRING customImageCodec;
	TSTRING customAudioSource;
	TSTRING customAudioCodec;
	TSTRING customGlobalOptions;

	// translate the resolution limit to a string representation for javascript
	const WCHAR *videoResLimitStr() const
	{
		return videoResLimit == ResLimitNone ? L"none" :
			videoResLimit == ResLimitHD ? L"hd" :
			L"unknown";
	}

	// capture list
	std::list<CaptureItemDesc> items;

	// status window
	RefPtr<CaptureStatusWin> statusWin;
};

