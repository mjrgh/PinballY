// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "AudioVideoPlayer.h"

// statics
DWORD AudioVideoPlayer::nextCookie = 1;
std::list<RefPtr<AudioVideoPlayer>> AudioVideoPlayer::pendingDeletion;
CriticalSection AudioVideoPlayer::pendingDeletionLock;

AudioVideoPlayer::AudioVideoPlayer(HWND hwndVideo, HWND hwndEvent, bool audioOnly) :
	hwndVideo(hwndVideo),
	hwndEvent(hwndEvent),
	audioOnly(audioOnly)
{
	// Assign a cookie
	cookie = InterlockedIncrement(&nextCookie);
}

AudioVideoPlayer::~AudioVideoPlayer()
{
}

void AudioVideoPlayer::SetPendingDeletion()
{
	// lock the queue while manipulating it
	CriticalSectionLocker locker(pendingDeletionLock);

	// add the object to the queue
	pendingDeletion.emplace_back(this);

	// note that the RefPtr constructor doesn't add a reference,
	// so we have to add one explicitly on behalf of the queue
	AddRef();
}

bool AudioVideoPlayer::ProcessDeletionQueue()
{
	// lock the queue while manipulating it
	CriticalSectionLocker locker(pendingDeletionLock);

	// scan the list
	for (auto it = pendingDeletion.begin(); it != pendingDeletion.end(); )
	{
		// get the next list entry, in case we remove this one
		auto nxt = it;
		++nxt;

		// if this object is ready to delete, release it
		if ((*it)->IsReadyToDelete())
			pendingDeletion.erase(it);

		// move on to the next element
		it = nxt;
	}

	// return true if any objects are left in the queue
	return pendingDeletion.size() != 0;
}

void AudioVideoPlayer::WaitForDeletionQueue(DWORD timeout)
{
	// note the starting time
	DWORD t0 = GetTickCount();

	// keep going until the queue is empty or we reach the timeout
	while (timeout == INFINITE || (DWORD)(GetTickCount() - t0) < timeout)
	{
		// process the queue; if it's empty, we're done
		if (!ProcessDeletionQueue())
			break;

		// pause briefly before trying again
		Sleep(100);
	}
}
