// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "AudioManager.h"
#include "GameList.h"

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
	// Go through the cached sounds to check for items that are
	// still playing.  Move each item that's actively playing to
	// a separate pending list.
	std::list<std::unique_ptr<DirectX::SoundEffect>> pending;
	for (auto &s : cache)
	{
		// if it hasn't been updated, transfer it to the pending list
		if (s.second->IsInUse())
			pending.emplace_back(s.second.release());
	}

	// Anything left in the cache can now be deleted, as we 
	// transfered all active items over to 'pending'.  We don't
	// actually have to clear the cache manually, as the map
	// destructor would do that anyway, but we might as well
	// do that work while we're waiting for sounds to finish.
	cache.clear();

	// Now wait for the remaining sounds to finish, within reason
	DWORD t0 = GetTickCount();
	while (pending.size() != 0 && GetTickCount() - t0 < 30000)
	{
		// pause briefly
		Sleep(15);

		// do engine housekeeping
		engine->Update();

		// clean the pending list
		CleanSoundList(pending);
	}

	// delete the DXTK audio engine object
	delete engine;
}

void AudioManager::PlayFile(const TCHAR *path)
{
	// look for an existing instance in our cache
	if (auto it = cache.find(path); it != cache.end())
	{
		// got it - simply reuse the existing effect
		it->second->Play();
	}
	else
	{
		// load the effect 
		auto sound = std::make_unique<DirectX::SoundEffect>(engine, path);
		if (sound->GetFormat() != nullptr)
		{
			// start it playing
			sound->Play();

			// add it to the cache
			cache.emplace(path, sound.release());
		}
	}
}

void AudioManager::Update()
{
	// update the engine
	if (!engine->Update() && engine->IsCriticalError())
		criticalError = true;
}

void AudioManager::CleanSoundList(std::list<std::unique_ptr<DirectX::SoundEffect>> &list)
{
	for (auto it = list.begin(); it != list.end(); )
	{
		// remember the next item in case we unlink this one
		auto nxt = it;
		nxt++;

		// if this one is done, remove it
		if (!(*it)->IsInUse())
			list.erase(it);

		// move on to the next item
		it = nxt;
	}
}
