// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// PNG file utilities

#include "stdafx.h"
#include <gdiplus.h>
#include <Shlwapi.h>
#include "PngUtil.h"
#include "Pointers.h"
#include "WinUtil.h"

// -----------------------------------------------------------------------
//
// Load a PNG resource into a GDI+ Bitmap
//
Gdiplus::Bitmap *GPBitmapFromPNG(int resid)
{
	// load and lock the PNG resource
	ResourceLocker res(resid, _T("PNG"));
	if (res.GetData() == nullptr)
		return nullptr;

	// create a stream on the HGLOBAL
	RefPtr<IStream> pstr(SHCreateMemStream(static_cast<const BYTE*>(res.GetData()), res.GetSize()));
	if (pstr == nullptr)
		return nullptr;

	// create the bitmap
	return Gdiplus::Bitmap::FromStream(pstr);
}

// -----------------------------------------------------------------------
//
// Load a PNG resource into an HBITMAP
//

HBITMAP LoadPNG(int resid)
{
	// load the PNG into a GDI+ bitmap
	std::unique_ptr<Gdiplus::Bitmap> bmp(GPBitmapFromPNG(resid));
	if (bmp == nullptr)
		return NULL;

	// get its HBITMAP
	HBITMAP hbitmap;
	bmp->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbitmap);

	// return the HBITMAP
	return hbitmap;
}

