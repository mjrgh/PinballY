// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Media Drop Target.  This implements a Windows OLE drop target
// associated with one of our windows.  We let the user drop media
// files on our windows to add media to the current game.

#pragma once
#include <ShlObj.h>
#include "../Utilities/Pointers.h"

struct MediaType;

class MediaDropTarget : public RefCounted, public IDropTarget
{
public:
	// When creating a drop target, provide the video and image media
	// type information for the 
	MediaDropTarget(const MediaType *imageType, const MediaType *videoType);

	// IUnknown implementation
	virtual ULONG STDMETHODCALLTYPE AddRef() override { return RefCounted::AddRef(); }
	virtual ULONG STDMETHODCALLTYPE Release() override { return RefCounted::Release(); }
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppUnk) override;

	// IDropTarget implementation
	virtual HRESULT STDMETHODCALLTYPE DragEnter(
		IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
	virtual HRESULT STDMETHODCALLTYPE DragLeave() override;
	virtual HRESULT STDMETHODCALLTYPE DragOver(
		DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
	virtual HRESULT STDMETHODCALLTYPE Drop(
		IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;

	// Get the media types for the associated window
	const MediaType *GetImageMediaType() const { return imageType; }
	const MediaType *GetVideoMediaType() const { return videoType; }

protected:
	~MediaDropTarget();

	// Last drop effect.  We cache this when DragEnter() is called, and
	// continue to return it throughout the drag operation for DragOver()
	// calls.  We don't have separate regions for dropping, so the drop
	// effect shouldn't change in the course of a drag operation.
	DWORD lastDropEffect;

	// Image and video types for this window's main background media
	const MediaType *imageType;
	const MediaType *videoType;
};

