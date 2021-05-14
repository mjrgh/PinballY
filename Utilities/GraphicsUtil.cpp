// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <gdiplus.h>
#include <ObjIdl.h>
#include "GraphicsUtil.h"
#include "ComUtil.h"
#include "StringUtil.h"
#include "WinUtil.h"
#include "SWFParser.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "zlibstat.lib")
#pragma comment(lib, "LZMA.lib")


// -----------------------------------------------------------------------
//
// Perform off-screen drawing.  This is a convenience function for
// doing off-screen rendering.  We create a DIB of the desired size,
// select it into a memory DC, and invoke a callback.  The callback
// can then draw into the DC using ordinary GDI calls to render into
// the DIB.  The callback can then retrieve the RGB pixels of the
// rendered image by reading from the pixel ("DI Bits") array.  When
// the callback finishes, we delete the DIB, so the callback has to
// use the DIB for whatever purpose it has in mind before returning.
//
void DrawOffScreen(int width, int height, 
	std::function<void(HDC, HBITMAP, const void*, const BITMAPINFO&)> func)
{
	// do the drawing into a new bitmap
	HBITMAP hbmp;
	DrawOffScreen(&hbmp, width, height, func);

	// delete the bitmap
	DeleteObject(hbmp);
}

// Perform off-screen drawing, returning the HBITMAP to the caller.
template<typename DibType>
void DrawOffScreen(HBITMAP *phBitmap, int width, int height, 
	std::function<void(HDC, HBITMAP, DibType*, const BITMAPINFO&)> func)
{
	// create a memory DC
	MemoryDC memdc;

	// create and select a DIB of the desired size
	void *dibits = 0;
	BITMAPINFO bmi;
	*phBitmap = memdc.CreateDIB(width, height, dibits, bmi);

	// invoke the callback to carry out the drawing
	func(memdc, *phBitmap, static_cast<DibType*>(dibits), bmi);
}

// Explicitly instantiate DrawOffScreen<DibType> for BYTE
static auto drawOffScreenBYTE = &DrawOffScreen<BYTE>;

// Perform off-screen drawing, returning the DIBitmap information
// to the caller.
void DrawOffScreen(DIBitmap &dib, int width, int height,
	std::function<void(HDC, HBITMAP, const void*, const BITMAPINFO&)> func)
{
	// discard any previous bitmap in the caller's struct
	dib.Clear();

	// create a memory DC
	MemoryDC memdc;

	// create and select a DIB of the desired size
	dib.hbitmap = memdc.CreateDIB(width, height, dib.dibits, dib.bmi);

	// invoke the callback to carry out the drawing
	func(memdc, dib.hbitmap, dib.dibits, dib.bmi);
}

// -----------------------------------------------------------------------
//
// GDI+ utilities
//

GdiplusIniter::GdiplusIniter()
{
	// initialize GDI
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&token, &gdiplusStartupInput, 0);
}

GdiplusIniter::~GdiplusIniter()
{
	Gdiplus::GdiplusShutdown(token);
}

static Gdiplus::Font *CreateGPFont0(const TCHAR *faceName, float emSize, Gdiplus::FontStyle style)
{
	// Try loading a font by name.  Returns a Gdiplus::Font* on success,
	// nullptr on failure.
	auto TryFont = [emSize, style](const TCHAR *name) -> Gdiplus::Font*
	{
		// Try creating the font in the style requested
		Gdiplus::FontFamily family(name);
		std::unique_ptr<Gdiplus::Font> font(new Gdiplus::Font(&family, emSize, style, Gdiplus::UnitPixel));
		if (font->IsAvailable())
			return font.release();

		// Windows 7 GDI+ seems to be pickier about matching fonts.  If the
		// installed font only has a "bold" or "italic" style, loading it
		// with the "regular" style will fail.  Windows 8 and later, in 
		// contrast, will load the font in any case.  So try again with
		// italic style inverted.
		font.reset(new Gdiplus::Font(&family, emSize, style ^ Gdiplus::FontStyleItalic, Gdiplus::UnitPixel));
		if (font->IsAvailable())
			return font.release();

		// Inverting 'italic' didn't make any difference.  Try flipping the
		// 'bold' status next.
		font.reset(new Gdiplus::Font(&family, emSize, style ^ Gdiplus::FontStyleBold, Gdiplus::UnitPixel));
		if (font->IsAvailable())
			return font.release();

		// Okay, this is getting ridiculous, but just in case, try flipping BOTH
		// bold and italic, so that we've covered every possible combination.
		font.reset(new Gdiplus::Font(&family, emSize, style ^ Gdiplus::FontStyleBoldItalic, Gdiplus::UnitPixel));
		if (font->IsAvailable())
			return font.release();

		// nothing worked - the font must really not be here
		return nullptr;
	};

	// if multiple comma-separated names were provided, try them in order
	if (_tcschr(faceName, ',') != nullptr)
	{
		// try each item in the list
		for (auto &s : StrSplit<TSTRING>(faceName, ','))
		{
			// try this item
			if (auto font = TryFont(TrimString<TSTRING>(s.c_str()).c_str()); font != nullptr)
				return font;
		}
	}

	// try the name string exactly as given
	if (auto font = TryFont(faceName); font != nullptr)
		return font;

	// Failed - the requested font must not be installed.  Try using the
	// generic sans serif font instead.
	if (auto genericSansSerif = Gdiplus::FontFamily::GenericSansSerif(); genericSansSerif != nullptr)
	{
		if (std::unique_ptr<Gdiplus::Font> genFont(new Gdiplus::Font(genericSansSerif, emSize, style, Gdiplus::UnitPixel)); genFont->IsAvailable())
			return genFont.release();
	}

	// Still no good.  Use the first available installed font.
	static std::unique_ptr<Gdiplus::FontFamily> fallbackFont;
	if (fallbackFont == nullptr)
	{
		// Get a list of available fonts
		Gdiplus::InstalledFontCollection coll;
		int nFonts = coll.GetFamilyCount();
		std::unique_ptr<Gdiplus::FontFamily[]> families(new Gdiplus::FontFamily[nFonts]);
		coll.GetFamilies(nFonts, families.get(), &nFonts);

		// find an available font
		for (int i = 0; i < nFonts; ++i)
		{
			if (families[i].IsAvailable())
			{
				fallbackFont.reset(families[i].Clone());
				break;
			}
		}
	}

	// use the fallback font if possible
	if (fallbackFont != nullptr)
	{
		if (std::unique_ptr<Gdiplus::Font> fbFont(new Gdiplus::Font(fallbackFont.get(), emSize, style, Gdiplus::UnitPixel)); fbFont->IsAvailable())
			return fbFont.release();
	}

	// We're totally out of luck.  Use "Arial", since that's almost always present.
	return new Gdiplus::Font(_T("Arial"), emSize, style, Gdiplus::UnitPixel);
}

Gdiplus::Font *CreateGPFont(const TCHAR *faceName, int pointSize, int weight, bool italic, HDC hdc)
{
	// Figure the pixel pitch in pix/inch.  If a DC was specified, use
	// its pixel pitch, otherwise use the reference 96 dpi.
	int dpi = hdc != NULL ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;

	// figure the em size in pixels: 1 point = 1/72"
	float emSize = (float)pointSize * (float)dpi / (float)72.0f;

	// figure the Gdiplus style bits
	int style = weight >= 700 ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
	if (italic) style |= Gdiplus::FontStyleItalic;

	// create the font
	return CreateGPFont0(faceName, emSize, static_cast<Gdiplus::FontStyle>(style));
}

Gdiplus::Font *CreateGPFontPixHt(const TCHAR *faceName, int pixHeight, Gdiplus::FontStyle style, HDC hdc)
{
	// figure the pixel pitch in pix/inch: use the pixel pitch specific
	// to the device if a DC was provided, otherwise use the reference 96dpi
	int dpi = hdc != NULL ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;

	// scale the height for the monitor DPI
	float emSize = 96.0f/float(dpi) * float(pixHeight);

	// create the font
	return CreateGPFont0(faceName, emSize, style);
}

void GPDrawStringAdv(Gdiplus::Graphics &g, const TCHAR *str,
	Gdiplus::Font *font, Gdiplus::Brush *br,
	Gdiplus::PointF &origin, Gdiplus::RectF &bbox)
{
	// set up the layout rect
	Gdiplus::RectF layoutRect(
		origin.X, origin.Y,
		bbox.Width - fmaxf(0.0f, origin.X - bbox.X), 
		bbox.Height - fmaxf(0.0f, origin.Y - bbox.Y));

	// draw the string within the layout rect
	Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
	format.SetFormatFlags(format.GetFormatFlags() & ~Gdiplus::StringFormatFlagsLineLimit);
	g.DrawString(str, -1, font, layoutRect, &format, br);

	// measure it
	Gdiplus::RectF newBounds;
	g.MeasureString(str, -1, font, layoutRect, &format, &newBounds);

	// advance the origin vertically
	origin.Y += newBounds.Height;
}

// -----------------------------------------------------------------------
//
// GDI+ string drawing context
//

GPDrawString::GPDrawString(Gdiplus::Graphics &g, const Gdiplus::RectF &bbox) :
	g(g),
	bbox(bbox),
	curOrigin(bbox.GetLeft(), bbox.GetTop())
{
}

GPDrawString::GPDrawString(Gdiplus::Graphics &g) :
	g(g)
{
}

void GPDrawString::DrawString(
	const TCHAR *str, Gdiplus::Font *font, Gdiplus::Brush *br, 
	bool newline, int align)
{
	// figure the current layout area
	Gdiplus::RectF layoutRect(
		curOrigin.X, curOrigin.Y,
		bbox.GetRight() - curOrigin.X, bbox.GetBottom() - curOrigin.Y);

	// set up our formatter
	Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
	format.SetFormatFlags(format.GetFormatFlags() & ~Gdiplus::StringFormatFlagsLineLimit);

	// set the alignment
	if (align == 0)
		format.SetAlignment(Gdiplus::StringAlignmentCenter);
	else if (align > 0)
		format.SetAlignment(Gdiplus::StringAlignmentFar);

	// draw the string
	g.DrawString(str, -1, font, layoutRect, &format, br);

	// measure it
	Gdiplus::RectF newBounds;
	g.MeasureString(str, -1, font, layoutRect, &format, &newBounds);

	// advance horizontally or vertically, as desired
	if (newline)
	{
		curOrigin.Y += newBounds.Height;
		curOrigin.X = bbox.GetLeft();
	}
	else
	{
		curOrigin.X += newBounds.Width;
	}
}

// -----------------------------------------------------------------------
//
// Image format information.  This utility analyzes an image file byte's
// stream to determine the image type and dimensions.  This lets us find
// the image information for a raw byte stream in memory, or identify a
// file type independently of the filename extension.
//
class ImageDimensionsReader
{
public:
	ImageDimensionsReader() { }
	virtual ~ImageDimensionsReader() { }

	// read data from an offset in the file
	virtual bool Read(long ofs, BYTE *buf, size_t len) = 0;

	// Retrieve image file information
	bool GetInfo(ImageFileDesc &desc, bool readOrientation, bool readAPNG)
	{
		// get the base information
		if (!GetInfoBase(desc, readOrientation, readAPNG))
			return false;

		// Figure the display dimensions.  These are the native image
		// dimensions transformed by the orientation matrix.
		desc.dispSize = desc.orientation * desc.size;

		// success
		return true;
	}

private:
	bool GetInfoBase(ImageFileDesc &desc, bool readOrientation, bool readAPNG)
	{
		// Set up a bit mask of the parts required
		const int NeedSize = 0x0001;
		const int NeedOrientation = 0x0002;
		const int NeedAPNG = 0x0004;
		int need = NeedSize
			| (readOrientation ? NeedOrientation : 0)
			| (readAPNG ? NeedAPNG : 0);

		// Check the header to determine the image type.  For GIF, we can
		// identify both the type and image dimensions with the first 10
		// bytes; wtih PNG, we can do the same with the first 24 bytes; for
		// JPEG, we can identify the type with 12 bytes, but will need to
		// scan further into the file to find the size.  So start with the
		// first 24 bytes, which will at least let us determine the type, 
		// and is even enough to give us the size for everything but JPEG.
		BYTE buf[512];
		const size_t initialBytes = 24;
		if (!Read(0, buf, initialBytes))
			return false;

		// check for the minimal JPEG signature (FF D8 FF)
		if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF)
		{
			// scan the segment list
			for (long ofs = 2; ; )
			{
				// read the next segment header
				if (!Read(ofs, buf, 12))
					return false;

				// make sure it's a segment header
				if (buf[0] != 0xFF)
					return false;

				// check for an end marker
				if (buf[1] == 0xD9)
					return false;

				// Figure the chunk size.  For frame types with payloads,
				// this is given by the next two bytes following the marker
				// (bytes [2] and [3], as a 16-bit big-endian value).  For
				// non-payload frame types (D0-D8), this is always zero.
				// Note that the size includes the two size bytes.
				UINT chunkSize = (buf[1] >= 0xD0 && buf[1] <= 0xD8) ? 0 : ((UINT)buf[2] << 8) + (UINT)buf[3];

				// check for an SOFn marker - these are where we can find the
				// image size
				if (buf[1] == 0xC0 || buf[1] == 0xC1 || buf[1] == 0xC2
					|| buf[1] == 0xC9 || buf[1] == 0xCA || buf[1] == 0xCB)
				{
					// SOFn marker - the size is in bytes 5:6 and 7:8
					desc.imageType = ImageFileDesc::JPEG;
					desc.size.cy = (buf[5] << 8) + buf[6];
					desc.size.cx = (buf[7] << 8) + buf[8];

					// mark the size as done, and stop if we have everything
					if ((need &= ~NeedSize) == 0)
						return true;
				}

				// If we're reading orientation, check for an Exif marker.  This takes
				// more work, so skip this whole thing if we don't care about this
				// metadata information.
				if (readOrientation
					&& buf[1] == 0xE1
					&& chunkSize > 16
					&& memcmp(buf + 4, "Exif\0\0", 6) == 0)
				{
					// The information we're looking for is always in the first IFD entry,
					// so limit the read to a reasonable size, say 100 IFD entries @ 12 bytes
					// plus the 8-byte header plus the two-byte entry count
					UINT exifSize = min(chunkSize, 100*12 + 8 + 2);

					// read the whole Exif chunk 
					std::unique_ptr<BYTE> exifBuf(new BYTE[exifSize - 8]);
					BYTE *exif = exifBuf.get(), *exifEnd = exif + exifSize;
					if (!Read(ofs + 10, exif, exifSize - 8))
						return false;

					// TIFF can use either big- or little-endian format
					auto B2LE = [](const BYTE *p) { return (UINT32)p[0] + ((UINT32)p[1] << 8); };
					auto B2BE = [](const BYTE *p) { return ((UINT32)p[0] << 8) + (UINT32)p[1]; };
					auto B4LE = [](const BYTE *p) { return (UINT32)p[0] + ((UINT32)p[1] << 8) + ((UINT32)p[2] << 16) + ((UINT32)p[3] << 24); };
					auto B4BE = [](const BYTE *p) { return ((UINT32)p[0] << 24) + ((UINT32)p[1] << 16) + ((UINT32)p[2] << 8) + (UINT32)p[3]; };
					std::function<int(const BYTE*)> B2 = nullptr;
					std::function<int(const BYTE*)> B4 = nullptr;

					// make sure it looks like a TIFF header
					if (memcmp(exif, "II\x2a\x00", 4) == 0)
					{
						// Intel little-endian mode
						B2 = B2LE;
						B4 = B4LE;
					}
					else if (memcmp(exif, "MM\x00\x2a", 4) == 0)
					{
						// Motorola big-endian mode
						B2 = B2BE;
						B4 = B4BE;
					}

					// if we got a valid marker, proceed
					if (B2 != nullptr)
					{
						// start at the first IFD
						BYTE *p = exif + B4(exif + 4);

						// read the number of entries in the first IFD
						int n = 0;
						if (p + 1 < exifEnd)
						{
							n = B2(p);
							p += 2;
						}

						// parse the first IFD entries
						for (int i = 0; i < n && p + 11 < exifEnd; ++i, p += 12)
						{
							switch (B2(p))
							{
							case 0x112:
								// orientation marker: must be type 3 (uint16) and 1 component
								if (B2(p + 2) == 3 && B4(p + 4) == 1)
								{
									// read the JPEG orientation code
									switch (B2(p + 8))
									{
									case 1:
										// normal orientation
										desc.orientation = { 1.0f, 0.0f, 0.0f, 1.0f };
										break;

									case 2:
										// horizontal mirror
										desc.orientation = { -1.0f, 0.0f, 0.0f, 1.0f };
										desc.oriented = true;
										break;

									case 3:
										// rotate 180
										desc.orientation = { -1.0f, 0.0f, 0.0f, -1.0f };
										desc.oriented = true;
										break;

									case 4:
										// vertical mirror
										desc.orientation = { 1.0f, 0.0f, 0.0f, -1.0f };
										desc.oriented = true;
										break;

									case 5:
										// flip vertically, then rotate 90 degrees clockwise
										desc.orientation = { 0.0f, 1.0f, 1.0f, 0.0f };
										desc.oriented = true;
										break;

									case 6:
										// rotate 90 degrees clockwise
										desc.orientation = { 0.0f, 1.0f, -1.0f, 0.0f };
										desc.oriented = true;
										break;

									case 7:
										// mirror horizontally, then rotate 90 degrees clockwise
										desc.orientation = { 0.0f, -1.0f, -1.0f, 0.0f };
										desc.oriented = true;
										break;

									case 8:
										// rotate 270 degrees clockwise (equivalent to 90 degrees CCW)
										desc.orientation = { 0.0f, -1.0f, 1.0f, 0.0f };
										desc.oriented = true;
										break;
									}

									// mark orientation as done, and return if done
									if ((need &= ~NeedOrientation) == 0)
										return true;

									// in any case, we don't need to keep looping
									i = n;
									break;
								}
							}
						}
					}
				}

				// Advance to the next segment header, by skipping the fixed
				// 2 bytes of the header plus the encoded chunk size.
				ofs += 2 + chunkSize;
			}

			// failed to find an SOFn segment
			return false;
		}

		// Check for GIF: 'GIF' v0 v1 v2 x0 x1 y0 y2
		if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F')
		{
			// Set the type and size.  GIF has no orientation metadata.
			desc.imageType = ImageFileDesc::GIF;
			desc.size.cx = buf[6] + (buf[7] << 8);
			desc.size.cy = buf[8] + (buf[9] << 8);
			return true;
		}

		// Check for PNG: 89 'PNG' 0D 0A 1A 0A, then an IHDR frame with dimensions
		if (buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G' 
			&& buf[4] == 0x0D && buf[5] == 0x0A && buf[6] == 0x1A && buf[7] == 0x0A
			&& buf[12] == 'I' && buf[13] == 'H' && buf[14] == 'D' && buf[15] == 'R')
		{
			// Set the type and size.  PNG has no orientation metadata.  (Unless it's
			// a newer PNG format that allows Exif tags, but that seems rare, so we'll
			// ignore it for now.)
			desc.imageType = ImageFileDesc::PNG;
			desc.size.cx = (buf[16] << 24) + (buf[17] << 16) + (buf[18] << 8) + (buf[19] << 0);
			desc.size.cy = (buf[20] << 24) + (buf[21] << 16) + (buf[22] << 8) + (buf[23] << 0);

			// If desired, scan for APNG (animation) metadata.  If it's an animated
			// PNG, it's required to have an acTL chunk before the first IDAT chunk.
			if ((need & NeedAPNG) != 0)
			{
				// scan chunks until reaching an acTL or IDAT - whichever comes
				// first will tell us if it's animated or not
				for (long chunkHeaderOffset = 8; ; )
				{
					// read the 8-byte header for the next chunk
					if (!Read(chunkHeaderOffset, buf, 8))
						break;

					// check the chunk type
					if (memcmp(&buf[4], "acTL", 4) == 0)
					{
						// it's an animated PNG
						desc.imageType = ImageFileDesc::APNG;
						break;
					}
					if (memcmp(&buf[4], "IDAT", 4) == 0)
					{
						// no acTL before the first IDAT - not animated
						break;
					}

					// decode the chunk data content length
					UINT32 chunkDataLen = static_cast<UINT32>(buf[0] << 24)
						| static_cast<UINT32>(buf[1] << 16)
						| static_cast<UINT32>(buf[2] << 8)
						| static_cast<UINT32>(buf[3]);

					// skip ahead by the header size (8 bytes), the chunk data
					// length, and the CRC size (4 bytes)
					chunkHeaderOffset += chunkDataLen + 12;
				}
			}

			// success
			return true;
		}

		// Check for SWF: "SWF" or "CWF" or "ZWF"
		if (buf[1] == 'W' && buf[2] == 'S' && (buf[0] == 'F' || buf[0] == 'C' || buf[0] == 'Z'))
		{
			// it's an SWF
			desc.imageType = ImageFileDesc::SWF;

			// The first byte specifies the stream compression format:
			//
			//  F -> uncompressed
			//  C -> zlib compressed
			//  Z -> LZMA compressed
			//

			// If the file is compressed, populate more of the initial buffer, so that
			// we have enough compression stream header information to inflate the stream.
			if (buf[0] == 'C' || buf[0] == 'Z')
				Read(initialBytes, buf + initialBytes, sizeof(buf) - initialBytes);

			// set up the appropriate reader based on the compression type
			std::unique_ptr<SWFParser::SWFReader> reader(
				buf[0] == 'C' ? static_cast<SWFParser::SWFReader*>(new SWFParser::ZlibReader(buf + 8, sizeof(buf) - 8)) :
				buf[0] == 'Z' ? static_cast<SWFParser::SWFReader*>(new SWFParser::LZMAReader(buf + 8, sizeof(buf) - 8)) :
				new SWFParser::UncompressedReader(buf + 8, sizeof(buf) - 8));

			// Read the frame size.  This is encoded as a rectangle, but the
			// top/left coordinates are always zero, so we can just pull out the
			// bottom/right to get the size.
			D2D1_RECT_F rc = reader->ReadRect();
			desc.size.cx = static_cast<LONG>(rc.right);
			desc.size.cy = static_cast<LONG>(rc.bottom);

			// success
			return true;
		}

		// unrecognized type
		return false;
	}
};

bool GetImageFileInfo(const TCHAR *filename, ImageFileDesc &desc, bool readOrientation, bool readAPNG)
{
	class Reader : public ImageDimensionsReader
	{
	public:
		Reader(const TCHAR *filename)
		{
			if (_tfopen_s(&fp, filename, _T("rb")) != 0)
				fp = 0;
		}
		~Reader() { if (fp != 0) fclose(fp); }
		FILE *fp;

		virtual bool Read(long ofs, BYTE *buf, size_t len) override
		{
			return fp != 0 && fseek(fp, ofs, SEEK_SET) == 0 && fread(buf, 1, len, fp) == len;
		}
	};
	Reader reader(filename);
	return reader.GetInfo(desc, readOrientation, readAPNG);
}

bool GetImageBufInfo(const BYTE *imageData, long len, ImageFileDesc &desc, bool readOrientation, bool readAPNG)
{
	class Reader : public ImageDimensionsReader
	{
	public:
		Reader(const BYTE *imageData, long len) : imageData(imageData), imageDataLen(len) { }
		const BYTE *imageData;
		long imageDataLen;

		virtual bool Read(long ofs, BYTE *buf, size_t len) override
		{
			if (ofs + (long)len > imageDataLen)
				return false;

			memcpy(buf, imageData + ofs, len);
			return true;
		}
	};
	Reader reader(imageData, len);
	return reader.GetInfo(desc, readOrientation, readAPNG);
}

// -----------------------------------------------------------------------
//
// GDI+ effects
//

Gdiplus::Bitmap *Gdiplus::DilationEffect(Gdiplus::Bitmap *bitmap, UINT rx, UINT ry, Gdiplus::DilationMode mode)
{
	// figure the bounds of the bitmap
	const int startX = 0, startY = 0;
	const UINT width = bitmap->GetWidth(), height = bitmap->GetHeight();
	const int stopX = static_cast<int>(width), stopY = static_cast<int>(height);

	// lock the bitmap data in 32bpp ARGB mode
	Gdiplus::Rect rcEffect(0, 0, width, height);
	Gdiplus::BitmapData bdSrc;
	bitmap->LockBits(&rcEffect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bdSrc);
	const BYTE *srcBase = static_cast<const BYTE*>(bdSrc.Scan0);

	// create the destination bitmap
	std::unique_ptr<Gdiplus::Bitmap> dstBitmap(new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB));
	Gdiplus::BitmapData bdDst;
	dstBitmap->LockBits(&rcEffect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bdDst);
	BYTE *dstRow = static_cast<BYTE*>(bdDst.Scan0);

	// figure the structuring element size
	UINT seSizeX = rx * 2 + 1;
	UINT seSizeY = ry * 2 + 1;

	// construct a circular structuring element matrix
	std::unique_ptr<bool> se(new bool[seSizeX * seSizeY]);
	if (mode == DilationModeDisc)
	{
		bool *pse = se.get();
		const UINT rMax = (rx > ry ? rx : ry);
		const UINT r2 = rMax * rMax;
		for (UINT row = 0; row < seSizeY; ++row)
		{
			for (UINT col = 0; col < seSizeX; ++col, ++pse)
			{
				int dx = col - rx;
				int dy = row - ry;
				UINT d2 = dx*dx + dy*dy;
				*pse = (d2 <= r2);
			}
		}
	}
	else if (mode == DilationModeDiamond)
	{
		bool *pse = se.get();
		for (UINT row = 0; row < seSizeY; ++row)
		{
			for (UINT col = 0; col < seSizeX; ++col, ++pse)
				*pse = false;
		}
		for (UINT row = 0; row < seSizeY; ++row)
		{
			for (int col = seSizeX / 2 - row; col < static_cast<int>(seSizeX / 2 + row); ++col)
			{
				if (col >= 0 && col < static_cast<int>(seSizeX))
					se.get()[row * seSizeY + col] = true;
			}
		}
	}
	else // default is rect
	{
		// rect mode - pick every pixel
		bool *pse = se.get();
		for (UINT i = seSizeX * seSizeY; i != 0; --i)
			*pse++ = true;
	}

	// scan line
	for (int y = startY; y < stopY; ++y, dstRow += bdDst.Stride)
	{
		// scan pixels in the line
		BYTE *dst = dstRow;
		for (int x = startX; x < stopX; ++x, dst += 4)
		{
			// clear the current maximum for each channel
			BYTE rMax = 0, gMax = 0, bMax = 0, aMax = 0;

			// iterate over the structuring element by row
			for (UINT i = 0; i < seSizeY; ++i)
			{
				int ir = i - ry;
				int ySrc = y + ir;

				// skip rows before the starting row
				if (ySrc < startY)
					continue;

				// stop after passing the last row
				if (ySrc >= stopY)
					break;

				// for each structuring element's column
				const bool *pse = se.get() + i*seSizeX;
				for (UINT j = 0; j < seSizeX; ++j, ++pse)
				{
					int jr = j - rx;
					int xSrc = x + jr;

					// skip columns before the starting column
					if (xSrc < startX)
						continue;

					// stop after the last column
					if (xSrc >= stopX)
						break;

					if (*pse)
					{
						// red
						const BYTE *src = srcBase + (ySrc * bdSrc.Stride) + (xSrc * 4);
						BYTE v = src[0];
						if (v > rMax)
							rMax = v;

						// green
						v = src[1];
						if (v > gMax)
							gMax = v;

						// blue
						v = src[2];
						if (v > bMax)
							bMax = v;

						// alpha
						v = src[3];
						if (v > aMax)
							aMax = v;
					}
				}
			}

			// result pixel
			dst[0] = rMax;
			dst[1] = gMax;
			dst[2] = bMax;
			dst[3] = aMax;
		}
	}

	// done with the bitmap data
	bitmap->UnlockBits(&bdSrc);
	dstBitmap->UnlockBits(&bdDst);

	// return the destination bitmap
	return dstBitmap.release();
}

Gdiplus::Bitmap *Gdiplus::DilationEffectRect(Gdiplus::Bitmap *bitmap, UINT rx, UINT ry)
{
	// figure the bounds of the bitmap
	const int startX = 0, startY = 0;
	const UINT width = bitmap->GetWidth(), height = bitmap->GetHeight();
	const int stopX = static_cast<int>(width), stopY = static_cast<int>(height);

	// lock the bitmap data in 32bpp ARGB mode
	Gdiplus::Rect rcEffect(0, 0, width, height);
	Gdiplus::BitmapData bdInput;
	bitmap->LockBits(&rcEffect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bdInput);

	// Create two temporary working buffers.  We'll use these to perform
	// multiple passes with the decomposed structuring element - one will
	// be the output in each pass, and will swap to being the input on the
	// next pass.  The one used for output on the last pass will become
	// the returned bitmap, and the other will be discarded.
	INT stride = abs(bdInput.Stride);
	std::unique_ptr<BYTE> tmp1(new BYTE[height * stride]);
	std::unique_ptr<BYTE> tmp2(new BYTE[height * stride]);

	// on the first pass, the input bitmap is the source, and the output is temp 1
	const BYTE *bufSrc = static_cast<BYTE*>(bdInput.Scan0);
	BYTE *bufDst = tmp1.get();

	// swap outputs
#define SwapTempBufs() \
    if (bufDst == tmp1.get()) \
        bufSrc = tmp1.get(), bufDst = tmp2.get(); \
    else \
        bufSrc = tmp2.get(), bufDst = tmp1.get();

	// process a source pixel
	BYTE s0, s1, s2, s3;
#define ReadPixN(src, n) \
    if (*((src) + n) > s##n) s##n = *((src) + n);
#define ReadPix(src) \
    ReadPixN(src, 0) \
    ReadPixN(src, 1) \
    ReadPixN(src, 2) \
    ReadPixN(src, 3)

// write a destination pixel
#define WritePixN(dst, n) \
    *((dst) + n) = s##n;
#define WritePix(dst) \
	WritePixN(dst, 0) \
	WritePixN(dst, 1) \
	WritePixN(dst, 2) \
	WritePixN(dst, 3)

	// Do the horizontal passes.  Each pass does a horizontal dilation with
	// radius 1, which increases the overall radius by 1.
	for (UINT hPass = 1; hPass <= rx; ++hPass)
	{
		// get the pixel buffer pointers
		BYTE *dst = bufDst;
		const BYTE *src = bufSrc;

		// scan the image
		for (UINT y = 0; y < height; ++y)
		{
			for (UINT x = 0; x < width; ++x, src += 4, dst += 4)
			{
				// read the three adjacent pixels
				s0 = s1 = s2 = s3 = 0;
				if (x >= 1) { ReadPix(src - 4); }
				ReadPix(src);
				if (x + 1 < width) { ReadPix(src + 4); }

				// write the pixel
				WritePix(dst);
			}
		}

		// swap temporary buffers
		SwapTempBufs();
	}

	// Do the vertical passes.  Each pass does a vertical dilation with 
	// radius 1, increasing the overall radius by 1.
	for (UINT vPass = 1; vPass <= ry; ++vPass)
	{
		// get the pixel buffer pointers
		BYTE *dst = bufDst;
		const BYTE *src = bufSrc;

		// scan the image
		for (UINT y = 0; y < height; ++y)
		{
			for (UINT x = 0; x < width; ++x, src += 4, dst += 4)
			{
				// read the three adjacent pixels
				s0 = s1 = s2 = s3 = 0;
				if (y >= 1) { ReadPix(src - stride); }
				ReadPix(src);
				if (y + 1 < height) { ReadPix(src + stride); }

				// write the pixel
				WritePix(dst);
			}
		}

		// swap temporary buffers
		SwapTempBufs();
	}

	// unlock the source bitmap
	bitmap->UnlockBits(&bdInput);

	// create a new bitmap for the result
	auto result = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);

	// lock its pixel buffer
	Gdiplus::BitmapData bdResult;
	result->LockBits(&rcEffect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bdResult);
	
	// Copy the bits from the current source buffer.  We always
	// leave the results of the last pass in the source buffer, 
	// since we swap buffers at the end of each pass.
	memcpy(bdResult.Scan0, bufSrc, height * stride);

	// unlock it and return the new bitmap object
	result->UnlockBits(&bdResult);
	return result;
}


// -----------------------------------------------------------------------
//
// Color space conversions
//

void RGBtoYUV(BYTE r, BYTE g, BYTE b, BYTE &y, BYTE &u, BYTE &v)
{
	y = (( 66*r + 129*g +  25*b + 128) >> 8) + 16;
	u = ((-38*r -  74*g + 112*b + 128) >> 8) + 128;
	v = ((112*r -  94*g -  18*b + 128) >> 8) + 128;
}

void YUVtoRGB(BYTE y, BYTE u, BYTE v, BYTE &r, BYTE &g, BYTE &b)
{
	int c = 298*(y - 16);
	int d = u - 128;
	int e = v - 128;
	int rp = (c         + 409*e + 128) >> 8;
	int gp = (c - 100*d - 208*e + 128) >> 8;
	int bp = (c + 516*d         + 128) >> 8;
	rp = min(rp, 255); r = max(rp, 0);
	gp = min(gp, 255); g = max(gp, 0);
	bp = min(bp, 255); b = max(bp, 0);
}

void RGBtoHSL(BYTE r, BYTE g, BYTE b, BYTE &h, BYTE &s, BYTE &l)
{
	// get the normalized RGB components
	float rr = static_cast<float>(r) / 255.0f;
	float gg = static_cast<float>(g) / 255.0f;
	float bb = static_cast<float>(b) / 255.0f;

	// figure the min and max components, and the range
	float cmin = fminf(rr, fminf(gg, bb));
	float cmax = fmaxf(rr, fmaxf(gg, bb));
	float delta = cmax - cmin;

	// figure the lightness
	float ll = (cmax + cmin) / 2.0f;

	// figure the hue and saturation
	float hh, ss;
	if (delta == 0.0f)
	{
		hh = 0.0f;
		ss = 0.0f;
	}
	else
	{
		if (cmax == rr)
			hh = 60.0f * fmodf((gg - bb) / delta, 6.0f);
		else if (cmax == gg)
			hh = 60.0f * ((bb - rr) / delta + 2.0f);
		else
			hh = 60.0f * ((rr - gg) / delta + 4.0f);

		ss = delta / (1.0f - fabsf(2 * ll - 1.0f));
	}

	// convert back to BYTE space
	h = static_cast<BYTE>(hh / 360.f * 255.0f);
	s = static_cast<BYTE>(ss * 255.0f);
	l = static_cast<BYTE>(ll * 255.0f);
}

void HSLtoRGB(BYTE h, BYTE s, BYTE l, BYTE &r, BYTE &g, BYTE &b)
{
	// normalize the HSL values
	float hh = static_cast<float>(h) / 255.0f * 360.0f;
	float ss = static_cast<float>(s) / 255.0f;
	float ll = static_cast<float>(l) / 255.0f;

	float c = (1.0f - fabsf(2.0f * ll - 1.0f)) * ss;
	float x = c * (1.0f - fabsf(fmodf(hh / 60.f, 2.0f) - 1.0f));
	float m = ll - c / 2.0f;

	float rr, gg, bb;
	if (hh < 60.0f)
		rr = c, gg = x, bb = 0.0f;
	else if (hh < 120.0f)
		rr = x, gg = c, bb = 0.0f;
	else if (hh < 180.0f)
		rr = 0.0f, gg = c, bb = x;
	else if (hh < 240.0f)
		rr = 0.0f, gg = x, bb = c;
	else if (hh < 300.0f)
		rr = x, gg = 0.0f, bb = c;
	else
		rr = c, gg = 0.0f, bb = x;

	r = static_cast<BYTE>((rr + m) * 255.0f);
	g = static_cast<BYTE>((gg + m) * 255.0f);
	b = static_cast<BYTE>((bb + m) * 255.0f);
}

