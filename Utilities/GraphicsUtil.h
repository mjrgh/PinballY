// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Miscellaneous graphics utility functions

#pragma once
#include <functional>
#include "PngUtil.h"

// GDI+ initializer.  Instantiate one of these objects in the
// main entrypoint function to initialize the GDI+ subsystem.
// The initialization is global and lasts for the duration of
// the session, so it's only necessary to instantiate one of
// these objects in the program startup routine to provide
// GDI+ access throughout the application.
//
// The destructor cleans up the GDI+ instance, so as long as
// you create the object on the stack in the main entrypoint
// code, the subsystem will be automatically terminated when
// the program exits.
//
// DON'T USE THIS IN MFC APPLICATIONS.  MFC initializes GDI+
// automatically in its startup code, so it's not necessary
// and not correct to do a separate initialization via this
// class.
//
class GdiplusIniter
{
public:
	GdiplusIniter();
	~GdiplusIniter();

protected:
	// initialization token
	ULONG_PTR token;
};

// Get the dimension and type of an image.  This parses the file 
// header for known image types (JPG, PNG, GIF, SWF) and fills in
// the descriptor with what we found.  Returns true if we parsed
// the image header successfully, false if not.
//
// If 'readOrientation' is true, we'll also attempt to read any
// orientation metadata encoded in the image.  Some image types,
// such as JPEG, have metadata that allows a rotational and/or
// mirror transform to be specified.  We skip this by default to
// minimize the amount of time we spend parsing the image.
//
struct ImageFileDesc
{
	ImageFileDesc() : 
		imageType(Unknown),
		oriented(false)
	{
		size = { 0, 0 }; 
		dispSize = { 0, 0 };
	}

	// Raw image dimensions in pixels
	SIZE size;

	// Display image dimensions in pixels.  This adjusts the size for
	// the rotation metadata (that is, swapping cx and cy if the image
	// is rotated 90 or 270 degrees).
	SIZE dispSize;

	// JPEG Exif orientation transform matrix.  If 'oriented' is true, it
	// means that the orientation matrix is filled in with a transform
	// matrix to be applied to the raw image pixels to get the proper
	// rendering orientation.
	bool oriented;
	struct Orientation
	{
		Orientation() : m11(1.0f), m12(0.0f), m21(0.0f), m22(1.0f) { }
		Orientation(float m11, float m12, float m21, float m22) : m11(m11), m12(m12), m21(m21), m22(m22) { }
		float m11, m12;
		float m21, m22;

		SIZE operator*(SIZE &s)	
		{ 
			return { (LONG)fabsf(m11*(float)s.cx + m12*(float)s.cy), (LONG)fabsf(m21*(float)s.cx + m22*(float)s.cy) };
		}

		void ToMatrix(Gdiplus::Matrix &m) { m.SetElements(m11, m12, m21, m22, 0.0f, 0.0f); }
	} 
	orientation;

	// image type
	enum
	{
		Unknown,	// unknown image type
		PNG,		// PNG image
		JPEG,		// JPEG image
		GIF,		// GIF image
		SWF			// Shockwave Flash object
	} imageType;
};
bool GetImageFileInfo(const TCHAR *filename, ImageFileDesc &desc, bool readOrientation = false);
bool GetImageBufInfo(const BYTE *imageData, long len, ImageFileDesc &desc, bool readOrientation = false);


// Off-screen GDI drawing.  This sets up a memory context, creates a
// 32-bit RGBA DIB (device-independent bitmap) of the given size, 
// selects the DIB into the DC, and invokes the callback.  The callback
// can carry out GDI drawing operations using the DC to draw onto the
// DIB to create dynamically generated graphics.  The callback can then
// use the DIB's pixel array as a source image for a D3D texture or for
// any other purpose.
void DrawOffScreen(int width, int height, 
	std::function<void(HDC, HBITMAP, const void *dibits, const BITMAPINFO&)> func);

// Draw off-screen into a newly created bitmap, returning the bitmap
// to the caller.
void DrawOffScreen(HBITMAP *phBitmap, int width, int height,
	std::function<void(HDC, HBITMAP, const void*, const BITMAPINFO&)> func);

// Draw off-screen into a newly created bitmap, returning the bitmap
// and its DIB pixel array to the caller
struct DIBitmap
{
	DIBitmap() { hbitmap = NULL; }
	~DIBitmap() { Clear(); }

	void Clear()
	{
		if (hbitmap != NULL)
		{
			DeleteObject(hbitmap);
			hbitmap = NULL;
			dibits = nullptr;
		}
	}

	HBITMAP hbitmap;
	BITMAPINFO bmi;
	void *dibits;
};
void DrawOffScreen(DIBitmap &dib, int width, int height,
	std::function<void(HDC, HBITMAP, const void*, const BITMAPINFO&)> func);


// GDI+ color from Win32 COLORREF
inline Gdiplus::Color GPColorFromCOLORREF(COLORREF color, BYTE alpha = 0xff)
{
	return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}


// Simplified GDI+ font creation.  This uses the typical defaults for
// most settings, to avoid the need to fill out a LOGFONT struct to
// initialize a font object.
//
// If hdc is non-null, we'll scale the font according to the pixel
// pitch for the given device, otherwise we'll use the reference size
// of 96 dpi.  The reference size should be used for most of our D3D
// graphics, since we prepare those in device-independent format at a
// reference scale.
Gdiplus::Font *CreateGPFont(const TCHAR *faceName, int pointSize, int weight, bool italic, HDC hdc = NULL);

// Create a GDI+ font at a given pixel height.  If a DC is provided,
// we'll scale 
Gdiplus::Font *CreateGPFontPixHt(const TCHAR *faceName, int pixHeight, int weight, bool italic, HDC hdc = NULL);

// Draw a string via GDI+, advancing the origin to the next line
// vertically.
void GPDrawStringAdv(
	Gdiplus::Graphics &g, const TCHAR *str, 
	Gdiplus::Font *font, Gdiplus::Brush *br,
	Gdiplus::PointF &origin, Gdiplus::RectF &bbox);

// GDI+ string drawing context, with support for advancing vertically
// or horizontally on each string segment.
struct GPDrawString
{
	// set up a drawing context within the given area
	GPDrawString(Gdiplus::Graphics &g);
	GPDrawString(Gdiplus::Graphics &g, Gdiplus::RectF &bbox);

	Gdiplus::Graphics &g;
	Gdiplus::RectF bbox;
	Gdiplus::PointF curOrigin;

	// Draw a string.  
	//
	// If newline is true, we'll advance to the start of the next line; 
	// otherwise we'll advance horizontally.
	//
	// 'align' is -1 for left alignment (default), 0 for center, 1 for
	// right alignment.
	void DrawString(const TCHAR *str, Gdiplus::Font *font, Gdiplus::Brush *br, 
		bool newline = true, int align = -1);

	// add vertical whitespace
	void VertSpace(float dy) { curOrigin.Y += dy; }
};



// Screen DC.  This is a convenience class for accessing the
// device context for the main display.  We automatically release
// the DC when the object goes out of scope.
class ScreenDC 
{
public:
	ScreenDC() { hdc = GetDC(0); }
	~ScreenDC() { ReleaseDC(0, hdc); }

	operator HDC() { return hdc; }

	HDC hdc;
};

// Memory DC.  This is a convenience class for creating a GDI
// device context for off-screen rendering.  This is mostly for
// the sake of resource management, as the DC is automatically
// destroyed when the object goes out of scope.  We also reset
// the selected bitmap, if applicable.
class MemoryDC
{
public:
	MemoryDC() 
	{
		hdc = CreateCompatibleDC(0);
		oldbmp = 0;
	}

	~MemoryDC()
	{
		// restore the prior selected bitmap, if any
		if (oldbmp != 0)
			SelectObject(hdc, oldbmp);

		// delete the DC
		DeleteDC(hdc);
	}

	operator HDC() { return hdc; }

	// Create and select a screen-compatible bitmap of the desired size.
	HBITMAP CreateCompatibleBitmap(int width, int height)
	{
		// get the screen DC for the device compatibility reference
		ScreenDC screenDC;

		// create the bitmap
		HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, width, height);

		// select it into the device context
		HGDIOBJ prv = ::SelectObject(hdc, bmp);

		// If we haven't already stashed the old bitmap, stash this one.
		// Don't do it again if we've already done this, as the point is
		// to restore the one from before the MemoryDC was created.
		if (oldbmp == 0)
			oldbmp = prv;

		// return the new bitmap
		return bmp;
	}

	// Create a DIB (device-independent bitmap) of the desired size, using
	// 32-bit RGBA format, and select it into the memory device context. 
	// Fills in 'bits' with the newly allocated pixel buffer, and populates
	// 'bmi' with the bitmap descriptor.
	HBITMAP CreateDIB(int width, int height, void* &bits, BITMAPINFO &bmi)
	{
		// set up the bitmap desriptor
		ZeroMemory(&bmi, sizeof(BITMAPINFOHEADER));
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height;			// negative -> top-down format
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;				// 32-bit RGBA format
		bmi.bmiHeader.biCompression = BI_RGB;		// uncompressed

		// create the DIB
		HBITMAP bmp = ::CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, 0, 0);

		// select it into the device context
		HGDIOBJ prv = ::SelectObject(hdc, bmp);

		// if we haven't already stashed a prior bitmap, save the old one
		if (oldbmp == 0)
			oldbmp = prv;

		// return the new bitmap
		return bmp;
	}

	HDC hdc;
	HGDIOBJ oldbmp;
};

// Bitmap handle holder.  Automatically deletes the bitmap when
// the holder object is destroyed.
struct HBITMAPHolder
{
	HBITMAPHolder() : h(NULL) { }
	HBITMAPHolder(HBITMAP h) : h(h) { }
	~HBITMAPHolder() { Clear(); }
	HBITMAP h;

	operator HBITMAP() const { return h; }
	HBITMAP* operator&() { return &h; }
	void operator=(HBITMAP h)
	{
		Clear();
		this->h = h;
	}

	void Clear()
	{
		if (h != NULL)
			DeleteObject(h);

		h = NULL;
	}

	bool operator==(HBITMAP h) { return this->h == h; }

	// detach the handle from this holder
	HBITMAP Detach()
	{
		HBITMAP ret = h;
		h = NULL;
		return ret;
	}
};


// Color space conversions
void RGBtoYUV(BYTE r, BYTE g, BYTE b, BYTE &y, BYTE &u, BYTE &v);
void YUVtoRGB(BYTE y, BYTE u, BYTE v, BYTE &r, BYTE &g, BYTE &b);
