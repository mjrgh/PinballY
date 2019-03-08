// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Audio manager.  This is a wrapper for the DirectXTK audio objects.

#pragma once
#include <Audio.h>
#include <memory>
#include <unordered_map>

class AudioManager
{
public:
	// initialize the global singleton
	static void Init();

	// shut down and delete the global singleton
	static void Shutdown();

	// get the global singleton
	static AudioManager *Get() { return inst; }

	// Play a sound file.  The file is given as a full path.
	void PlayFile(const TCHAR *filename, float volume = 1.0f);

	// Update.  This takes care of timed housekeeping work in the DXTK
	// engine.  This must be called regularly, typically at the same
	// time that we render a D3D frame.
	void Update();

	// Have we encountered a critical error?  If an error occurs in
	// Update() processing, we set an internal flag.  This can be
	// interrogated periodically to report errors in the UI.

protected:
	// global singleton instance
	static AudioManager *inst;

	// DirectXTK audio engine object
	DirectX::AudioEngine *engine;

	// Critical audio engine error detected
	bool criticalError;

	// Sound cache.  This is a table of reusable sound effects,
	// indexed by filename.  
	std::unordered_map<TSTRING, std::unique_ptr<DirectX::SoundEffect>> cache;

	// Clean up a play list.  This takes a list of SoundEffect objects, scans
	// it for finished items, and erases each item that's no longer playing.
	void CleanSoundList(std::list<std::unique_ptr<DirectX::SoundEffect>> &list);

	// construction and destruction are handled through our own static methods,
	// so they're protected
	AudioManager();
	~AudioManager();
};
