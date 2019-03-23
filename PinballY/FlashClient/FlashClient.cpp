// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Based upon "AX" (an ActiveX container window) by Michael Courdakis, from
// https://www.codeproject.com/Articles/18417/Use-an-ActiveX-control-in-your-Win-Project-witho
//
#include <Windows.h>
#include <comdef.h>
#include <ExDisp.h>
#include <OleDlg.h>
#include <gdiplus.h>
#include "FlashClient.h"
#include "../Resource.h"
#include "../../Utilities/StringUtil.h"
#include "../../Utilities/WinUtil.h"
#include "../../Utilities/LogError.h"


// Import the flash COM types.  The normal way to do this is via the #import
// directive below, which pulls the type library directly from the Flash
// ActiveX DLL (Flash.ocx) installed on the build machine.  However, that 
// makes the build process less portable, because (a) it requires the Flash
// OCX to be installed on the build machine, and (b) #import needs the full
// absolute path to the system folder containing the Flash OCX.  There doesn't
// seem to be a way to isolate the #import from the system folder layout.
//
// So to make the build more portable, we don't actually use the #import; that's
// commented out by default.  Instead, we use a .tlh file that we pre-generated
// and included in the build tree.  The .tlh file is produced by the #import, 
// and is what the C++ compiler actually reads the types from, so it's equivalent
// in terms of the imported type definitions.  We simply captured the .tlh file 
// from a build with the #import enabled and included it in the source tree.
// There really should be no need to run the full #import from the live OCX on
// every build, because COM interfaces by design are required to be permanent:
// so the snapshot we took in the .tlh file should be for the ages.  The only 
// reason that a fresh #import would be needed should be to pick up new types
// added in future Flash versions, but even that should only be necessary if we
// modify our code to depend on new types - and at that point we can simply 
// repeat the #import once, to generate an updated .tlh file, and then update
// the captured .tlh in our source tree from that update.
//
// If you do want to re-import the live OCX, simply uncomment the #import line,
// comment out the #include line that follows it, and compile this file.  That
// should re-generate the .tlh file with the Flash.ocx that you have installed
// on your system.  Note that you might need to adjust the path in the #import;
// that has to be the absolute path to the location where your Flash.ocx is
// installed, which is the big build portability problem with using the #import
// that we're working around with the #include alternative.  After you regenerate
// the .tlh file, you can go back to just using the .tlh and not re-#importing
// the OCX on every build.
//
// #import "c:\\windows\\system32\\macromed\\flash\\flash.ocx" named_guids include("IServiceProvider")
#include "flash.tlh"


FlashClientSite::FlashClientSite(const TCHAR *swfFile)
	: swfFile(swfFile)
{
    refCnt = 1;
    isActivated = 0;
	needRedraw = true;
	pInPlaceObj = NULL;
	SetRect(&rcLayout, 0, 0, 0, 0);
	bmpBits = NULL;
}

FlashClientSite::~FlashClientSite()
{
	if (hbmp != NULL)
	{
		DeleteObject(hbmp);
		hbmp = NULL;
		bmpBits = NULL;
	}
}

STDMETHODIMP_(ULONG) FlashClientSite::AddRef()
{
	return InterlockedIncrement(&refCnt);
}

STDMETHODIMP_(ULONG) FlashClientSite::Release()
{
	ULONG result = InterlockedDecrement(&refCnt);
	if (result == 0)
		delete this;

	return result;
}

STDMETHODIMP FlashClientSite::QueryInterface(REFIID iid, void **ppvObject)
{
	*ppvObject = 0;
	if (iid == IID_IOleClientSite)
		*ppvObject = static_cast<IOleClientSite*>(this);
	else if (iid == IID_IUnknown)
		*ppvObject = static_cast<IUnknown*>(static_cast<IOleClientSite*>(this));
	else if (iid == IID_IAdviseSink)
		*ppvObject = static_cast<IAdviseSink*>(this);
	else if (iid == IID_IDispatch)
		*ppvObject = static_cast<IDispatch*>(this);
	else if (iid == IID_IOleInPlaceSite)
		*ppvObject = static_cast<IOleInPlaceSite*>(this);
	else if (iid == IID_IOleInPlaceFrame)
		*ppvObject = static_cast<IOleInPlaceFrame*>(this);
	else if (iid == IID_IOleInPlaceUIWindow)
		*ppvObject = static_cast<IOleInPlaceUIWindow*>(this);
	else if (iid == IID_IOleInPlaceSiteEx)
		*ppvObject = static_cast<IOleInPlaceSiteEx*>(this);
	else if (iid == IID_IOleInPlaceSiteWindowless)
		*ppvObject = static_cast<IOleInPlaceSiteWindowless*>(this);

	if (*ppvObject != nullptr)
	{
		AddRef();
		return S_OK;
	}

	return E_NOINTERFACE;
}

void FlashClientSite::Shutdown()
{
	// if the Flash object is active, deactivate it
	if (IsInPlaceActive())
	{
		SetInPlaceActive(false);
		RefPtr<IOleInPlaceObject> iib;
		if (pOleObj != NULL && SUCCEEDED(pOleObj->QueryInterface(IID_PPV_ARGS(&iib))))
		{
			iib->UIDeactivate();
			iib->InPlaceDeactivate();
		}
	}

	// remove the Advise connection
	if (adviseToken && pOleObj)
	{
		pOleObj->Unadvise(adviseToken);
		adviseToken = 0;
	}

	// release the objects
	pInPlaceObj = nullptr;
	pOleObj = nullptr;
}

void FlashClientSite::SetLayoutSize(SIZE sz)
{
	// check for a size change
	if (sz.cx != rcLayout.right || sz.cy != rcLayout.bottom)
	{
		// set the new layout
		SetRect(&rcLayout, 0, 0, sz.cx, sz.cy);

		// if we have a bitmap, delete it so that we create a new
		// one at the target size the next time we redraw
		if (hbmp != NULL)
		{
			DeleteObject(hbmp);
			hbmp = NULL;
			bmpBits = NULL;
		}

		// we need to redraw at the new size
		needRedraw = true;
	}
}

HBITMAP FlashClientSite::GetBitmap(RECT *rcBitmap, LPVOID *pbits, BITMAPINFO *pbmi)
{
	// have flash redraw the contents if necessary
	if (needRedraw)
	{
		// create a memory DC for drawing
		HDC hdc = CreateCompatibleDC(0);

		// if we don't have a bitmap for the off-screen rendering,
		// create one
		if (hbmp == NULL)
		{
			// create a new DIB section of the required size
			ZeroMemory(&bmi, sizeof(bmi));
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biWidth = rcLayout.right;
			bmi.bmiHeader.biHeight = -rcLayout.bottom;  // negative -> top-down format
			bmi.bmiHeader.biCompression = BI_RGB;
			bmi.bmiHeader.biSizeImage = 0;
			hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bmpBits, 0, 0);

			// set the object layout rectangle in the underlying Flash object
			RefPtr<IOleInPlaceObject> ipl;
			if (SUCCEEDED(pOleObj->QueryInterface(IID_PPV_ARGS(&ipl))))
				ipl->SetObjectRects(&rcLayout, &rcLayout);
		}

		// redraw the frame
		Redraw(hdc);

		// done with the DC
		DeleteDC(hdc);
	}

	// fill in the caller's bitmap dimensions if requested
	if (rcBitmap != NULL)
		SetRect(rcBitmap, 0, 0, rcLayout.right, rcLayout.bottom);

	// return the bitmap
	if (pbits != NULL) *pbits = bmpBits;
	if (pbmi != NULL) *pbmi = bmi;
	return hbmp;
}

bool FlashClientSite::UpdateBitmap()
{
	// if we don't have a bitmap, tell the caller that they need to call
	// UpdateBitmap() to create or re-create it
	if (hbmp == NULL)
		return false;

	// redraw the frame if necessary
	if (needRedraw)
	{
		// create a memory DC for drawing
		HDC hdc = CreateCompatibleDC(0);

		// redraw the frame
		Redraw(hdc);

		// done with the memory DC
		DeleteDC(hdc);
	}

	// success
	return true;
}

void FlashClientSite::Redraw(HDC hdc)
{
	// select our bitmap into the DC
	HGDIOBJ oldbmp = SelectObject(hdc, hbmp);

	// Erase the background.  Flash only works in a windowless container when
	// in "transparent" mode, and transparent mode means that Flash doesn't erase
	// the background on its own, since the whole point of transparency is to
	// combine the Flash graphics with an existing background.  So we have to
	// provide the blank background.  
	//
	// The choice of background color is arbitrary, and in the best case it won't
	// matter at all because the Flash object will provide its own background
	// fill that will completely cover whatever background we supply.  But this
	// isn't a foregone conclusion; some Flash objects will only draw foreground
	// objects so that they can be composited onto different backgrounds.  For
	// PinballY, we only use SWF objects for instruction cards, and white is the
	// best default background for these, as the original physical instruction
	// cards usually use black text printed on white paper.  So if an SWF image
	// of an instruction card doesn't specify a background, it probably expects
	// to be drawn against a white field.  Beyond this typical-case default,
	// there's really no way to guess at a better background color, so we'll
	// simply use this across the board.
	{
		Gdiplus::Graphics graphics(hdc);
		Gdiplus::SolidBrush br(Gdiplus::Color(255, 255, 255, 255));
		graphics.FillRectangle(&br, 0, 0, rcLayout.right, rcLayout.bottom);
		graphics.Flush();
	}

	// redraw the Flash object
	RefPtr<IViewObject> viewObj;
	if (pOleObj != NULL && SUCCEEDED(pOleObj->QueryInterface(IID_PPV_ARGS(&viewObj))))
	{
		// have flash do the drawing
		RECTL rcl = { rcLayout.left, rcLayout.top, rcLayout.right, rcLayout.bottom };
		viewObj->Draw(DVASPECT_CONTENT, 0, NULL, NULL, NULL, hdc, &rcl, NULL, NULL, NULL);

		// redrawing is no longer pending
		needRedraw = false;
	}

	// clean up the DC
	SelectObject(hdc, oldbmp);

	// synchronize GDI
	GdiFlush();
}

STDMETHODIMP FlashClientSite::GetMoniker(DWORD dwA, DWORD dwW, IMoniker **pm)
{
    *pm = 0;
    return E_NOTIMPL;
}

STDMETHODIMP FlashClientSite::GetContainer(IOleContainer**pc)
{
    *pc = 0;
    return E_FAIL;
}

STDMETHODIMP FlashClientSite::GetWindow(HWND *p)
{
	// we're in windowless mode -> no window handle
	if (p != NULL) *p = NULL;
	return E_FAIL;
}

STDMETHODIMP FlashClientSite::CanInPlaceActivate()
{
	return isActivated ? S_OK : S_FALSE;
}

STDMETHODIMP FlashClientSite::GetWindowContext(
	IOleInPlaceFrame **ppFrame, 
	IOleInPlaceUIWindow **ppDoc,
	LPRECT r1, LPRECT r2,
	LPOLEINPLACEFRAMEINFO o)
{
    *ppFrame = static_cast<IOleInPlaceFrame*>(this);
    AddRef();

    *ppDoc = NULL;
	*r1 = *r2 = rcLayout;
    o->cb = sizeof(OLEINPLACEFRAMEINFO);
    o->fMDIApp = false;
	o->hwndFrame = NULL;
    o->haccel = 0;
    o->cAccelEntries = 0;

    return S_OK;
}

STDMETHODIMP FlashClientSite::GetBorder(LPRECT l)
{
	*l = rcLayout;
    return S_OK;
}

STDMETHODIMP FlashClientSite::SetActiveObject(IOleInPlaceActiveObject *pV, LPCOLESTR s)
{
	pInPlaceObj = pV;
	return S_OK;
}

STDMETHODIMP FlashClientSite::InvalidateRect(LPCRECT, BOOL /*fErase*/)
{
	// Flag the redraw.  We don't bother trying to keep track of a dirty
	// region to minimize redraws, since the underlying windowless ActiveX
	// drawing mechanism (IViewObject::Draw()) doesn't have a way to clip
	// redraws to a dirty region.  So it's enough to track this with a
	// bool.  Note also that we can't even optimize out erasing the
	// background (fErase), because Flash has to be in "transparent" mode
	// to run windowless, which means that we have to supply a blank
	// background explicitly on every redraw.
	needRedraw = true;
	return S_OK;
}

STDMETHODIMP FlashClientSite::InvalidateRgn(HRGN hRGN, BOOL fErase)
{
	return InvalidateRect(0, fErase);
}

STDMETHODIMP FlashClientSite::OnDefWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *plResult)
{
	*plResult = 0;
	return S_OK;
}

HRESULT FlashClientSite::Create(FlashClientSite **ppClientSite,
	const TCHAR *swfFile, int width, int height, ErrorHandler &eh)
{
	// create our client site object
	RefPtr<FlashClientSite> pClientSite(new FlashClientSite(swfFile));

	// error return
	HRESULT hr = S_OK;
	auto RetHR = [&hr, &eh, &pClientSite](const TCHAR *where, bool preLoad = false)
	{
		// log the error
		WindowsErrorMessage winErr(hr);
		eh.SysError(LoadStringT(preLoad ? IDS_ERR_CREATESWFOBJ : IDS_ERR_INITSWFOBJ),
			MsgFmt(_T("%s failed, error %lx: %s"), where, (long)hr, winErr.Get()));

		// shut down the client site, to disentangle COM references
		pClientSite->Shutdown();

		// return the HRESULT
		return hr;
	};

	// create our storage object
	RefPtr<IStorage> pStorage;
	if (FAILED(hr = StgCreateDocfile(0, STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_DIRECT | STGM_CREATE, 0, &pStorage)))
		return RetHR(_T("StgCreateDocfile"), true);

	// create the ShockwaveFlash OLE object - this is the main ActiveX control
	if (FAILED(hr = OleCreate(
		__uuidof(ShockwaveFlashObjects::ShockwaveFlash), IID_IOleObject,
		OLERENDER_DRAW, 0, pClientSite, pStorage, (void**)&pClientSite->pOleObj)))
		return RetHR(_T("OleCreate(ShockwaveFlash)"), true);

	// Get the IShockwaveFlash interface
	RefPtr<ShockwaveFlashObjects::IShockwaveFlash> isf;
	if (FAILED(hr = pClientSite->pOleObj->QueryInterface(IID_PPV_ARGS(&isf))))
		return RetHR(_T("QueryInterface(IShockwaveFlash)"));

	// Set Transparent mode.  Flash requires this to run in a windowless site.
	isf->put_WMode(BString(L"Transparent"));

	// set the contained object
	if (FAILED(hr = OleSetContainedObject(pClientSite->pOleObj, TRUE)))
		return RetHR(_T("OleSetContainedObject"));

	// set up the Advise connection
	if (FAILED(hr = pClientSite->pOleObj->Advise(pClientSite, &pClientSite->adviseToken)))
		return RetHR(_T("ClientSite::Advise"));

	// set up the view object Advise connection
	RefPtr<IViewObject> pViewObj;
	if (FAILED(hr = pClientSite->pOleObj->QueryInterface(IID_IViewObject, (void**)&pViewObj)))
		return RetHR(_T("QueryInterface(IViewObject"));
	if (FAILED(hr = pViewObj->SetAdvise(DVASPECT_CONTENT, 0, pClientSite)))
		return RetHR(_T("IViewObject::SetAdvise"));

	// Navigate to our .swf resource
	BString movie(MsgFmt(_T("file:///%s"), swfFile));
	if (FAILED(isf->LoadMovie(0, (BSTR)movie)))
		return RetHR(_T("Loading the .swf file"));

	// set our internal layout rectangle
	SetRect(&pClientSite->rcLayout, 0, 0, width, height);

	// activate the object
	RECT rc;
	SetRect(&rc, 0, 0, width, height);
	pClientSite->SetInPlaceActive(true);
	if (FAILED(hr = pClientSite->pOleObj->DoVerb(OLEIVERB_INPLACEACTIVATE, 0, pClientSite, 0, NULL, &rc)))
		return RetHR(_T("In-place activating"));

	RefPtr<IOleInPlaceObject> ipl;
	if (SUCCEEDED(pClientSite->pOleObj->QueryInterface(IID_PPV_ARGS(&ipl))))
		ipl->SetObjectRects(&rc, &rc);

	// start playback
	isf->Play();

	// add a reference on behalf of the caller and pass it back
	(*ppClientSite = pClientSite)->AddRef();

	// success
	return S_OK;
}

