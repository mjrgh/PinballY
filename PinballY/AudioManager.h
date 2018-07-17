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

	// Play a sound effect.  The name is the base file name, with
	// no path or ".wav" suffix.
	void PlaySoundEffect(const TCHAR *name);

	// Play a sound file
	void PlaySoundFile(const TCHAR *filename);

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

	// Sound table.  This is a table of loaded sound effects
	// indexed by base file name.  
	std::unordered_map<TSTRING, DirectX::SoundEffect*> sounds;

	// construction and destruction are handled through our own static methods,
	// so they're protected
	AudioManager();
	~AudioManager();
};

