// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// PNG file utilities

#pragma once


// Load a PNG resource into an HBITMAP object.  Note that the
// caller must initialize GDI+ prior to calling this.
HBITMAP LoadPNG(int resid);

// Load a PNG resource into a GDI+ Bitmap object.  The caller
// must initialize GDI+ prior to calling this.
Gdiplus::Bitmap *GPBitmapFromPNG(int resid);

