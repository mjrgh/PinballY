// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Media Drop Target.  This implements a Windows OLE drop target
// associated with one of our windows.  We let the user drop media
// files on our windows to add media to the current game.

#pragma once
#include <ShlObj.h>
#include <Shellapi.h>
#include "../Utilities/Pointers.h"

struct MediaType;
class BaseView;

// Media Drop Target. 
//
// Each window that handles media file drag-and-drop creates one of 
// these objects and registers it with Windows as the IDropTarget for
// the window handle.  This implements the IDropTarget methods, which
// Windows calls during drag events that target our window.
//
class MediaDropTarget : public RefCounted, public IDropTarget
{
public:
	MediaDropTarget(BaseView *win);

	// Receive notification that the window is being destroyed.  This
	// revokes the drop target with the system.
	void OnDestroyWindow();

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

	// get the background image/video types for this window
	const MediaType *GetBackgroundImageType() const;
	const MediaType *GetBackgroundVideoType() const;

	// Convenience interface for IDataObject -> HDROP
	class FileDrop
	{
	public:
		FileDrop() : hDrop(NULL), fileGroupDesc(nullptr, ::free), nFiles(0) { }
		
		// initialize from an IDataObject interface
		bool Init(IDataObject *pDataObj);

		// clear fields
		void Clear()
		{
			pDataObj = nullptr;
			hDrop = NULL;
			fileGroupDesc.reset();
			nFiles = 0;
		}

		// is it valid?
		bool IsValid() const { return hDrop != NULL || fileGroupDesc != nullptr; }

		// get the number of files
		int GetNumFiles() const { return nFiles; }

		// iterate over the files
		void EnumFiles(std::function<void(const TCHAR *filename, IStream *stream)> func);

	protected:
		// For CF_HDROP transfers, the file drop handle
		HDROP hDrop;

		// For CFSTR_FILEDESCRIPTOR + CFSTR_FILECONTENT transfers, the 
		// FILEGROUPDESCIPTOR data
		std::unique_ptr<FILEGROUPDESCRIPTOR, decltype(&::free)> fileGroupDesc;

		// the number of files being transferred
		UINT nFiles;

		// underlying pDataObj
		RefPtr<IDataObject> pDataObj;
	};

protected:
	~MediaDropTarget();

	// Current FileDrop object.  This is initialized from the IDataObject
	// when DragEnter() is called.
	FileDrop fileDrop;

	// Last drop effect.  We cache this when DragEnter() is called, and
	// continue to return it throughout the drag operation for DragOver()
	// calls.  We don't have separate regions for dropping, so the drop
	// effect shouldn't change in the course of a drag operation.
	DWORD lastDropEffect;

	// target view
	RefPtr<BaseView> view;

	// adjust screen coordinates to local coordinates in the view,
	// adjusted for rotation and reflection
	POINT ScreenToView(POINTL pt) const;
};

