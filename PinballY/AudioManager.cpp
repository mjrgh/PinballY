// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "AudioManager.h"

// statics
AudioManager *AudioManager::inst;

// initialize
void AudioManager::Init()
{
	if (inst == 0)
		inst = new AudioManager();
}

// terminate
void AudioManager::Shutdown()
{
	delete inst;
	inst = 0;
}

AudioManager::AudioManager()
{
	// no error yet
	criticalError = false;

	// create the DXTK audio engine object
	DirectX::AUDIO_ENGINE_FLAGS aeFlags =
		DirectX::AudioEngine_Default
		IF_DEBUG(| DirectX::AudioEngine_Debug);
	engine = new DirectX::AudioEngine(aeFlags);
}

AudioManager::~AudioManager()
{
	// Make a first pass through the sound table.  For each
	// sound, delete it if it's not in use, and add it to a
	// pending-deletion list if it is.
	std::list<DirectX::SoundEffect*> pending;
	for (auto const &s : sounds)
	{
		// check if it's been deleted yet
		if (s.second->IsInUse())
		{
			// it's still being played, so we can't delete it yet; 
			// just add it to the pending list
			pending.push_back(s.second);
		}
		else
		{
			// it's ready for deletion 
			delete s.second;
		}
	}

	// Now wait for the remaining ones to finish, within reason
	DWORD t0 = GetTickCount();
	while (pending.size() != 0 && GetTickCount() - t0 < 30000)
	{
		// pause briefly
		Sleep(15);

		// do engine housekeeping
		engine->Update();

		// visit the pending list again
		for (auto it = pending.begin(); it != pending.end(); )
		{
			// get the next item now, in case we remove this item
			auto nxt = it;
			nxt++;

			// if this one is finished playing, we can delete it
			if (!(*it)->IsInUse())
			{
				delete *it;
				pending.erase(it);
			}

			// move on
			it = nxt;
		}
	}

	// delete the DXTK audio engine object
	delete engine;
}

void AudioManager::PlaySoundEffect(const TCHAR *name)
{
	// look up the sound effect in our effect table
	if (auto it = sounds.find(name); it != sounds.end())
	{
		// got it - simply reuse the existing effect
		it->second->Play();
	}
	else
	{
		// build the full filename
		MsgFmt base(_T("assets\\%s.wav"), name);
		TCHAR path[MAX_PATH];
		GetDeployedFilePath(path, base, _T(""));

		// load the effect and start it playing
		auto sound = new DirectX::SoundEffect(engine, path);
		sound->Play();

		// add it to our cache
		sounds[name] = sound;
	}
}

void AudioManager::PlaySoundFile(const TCHAR *filename)
{
	auto sound = new DirectX::SoundEffect(engine, filename);
	sound->Play();
}

void AudioManager::Update()
{
	if (!engine->Update() && engine->IsCriticalError())
		criticalError = true;
}

