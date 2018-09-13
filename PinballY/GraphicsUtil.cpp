// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <gdiplus.h>
#include <ObjIdl.h>
#include "GraphicsUtil.h"
#include "../zlib/zlib.h"
#include "../LZMA/CPP/7zip/Compress/LzmaDecoder.h"

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
void DrawOffScreen(HBITMAP *phBitmap, int width, int height, 
	std::function<void(HDC, HBITMAP, const void*, const BITMAPINFO&)> func)
{
	// create a memory DC
	MemoryDC memdc;

	// create and select a DIB of the desired size
	void *dibits = 0;
	BITMAPINFO bmi;
	*phBitmap = memdc.CreateDIB(width, height, dibits, bmi);

	// invoke the callback to carry out the drawing
	func(memdc, *phBitmap, dibits, bmi);

	// done with the bitmap
	SelectObject(memdc, memdc.oldbmp);
}

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

	// done with the bitmap
	SelectObject(memdc, memdc.oldbmp);
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

// Load a PNG resource into a GDI+ Bitmap
Gdiplus::Bitmap *GPBitmapFromPNG(int resid)
{
	// find the PNG resource
	HRSRC hres = FindResource(G_hInstance, MAKEINTRESOURCE(resid), _T("PNG"));
	if (hres == 0)
		return 0;

	// get its size
	DWORD sz = SizeofResource(G_hInstance, hres);
	if (sz == 0)
		return 0;

	// load it
	const void *pres = LockResource(LoadResource(G_hInstance, hres));
	if (pres == 0)
		return 0;

	// allocate space
	HGLOBAL hglobal = GlobalAlloc(GMEM_MOVEABLE, sz);
	if (hglobal == 0)
		return 0;

	// no bitmap yet
	Gdiplus::Bitmap *bmp = 0;

	// load the data into the hglobal
	void *pbuf = GlobalLock(hglobal);
	if (pbuf != 0)
	{
		// copy the image contents
		CopyMemory(pbuf, pres, sz);

		// create a stream
		IStream *pstr = 0;
		if (CreateStreamOnHGlobal(hglobal, FALSE, &pstr) == S_OK)
		{
			// finally! read the PNG from the stream
			bmp = Gdiplus::Bitmap::FromStream(pstr);

			// done with the stream
			pstr->Release();
		}

		// unlock the hglobal
		GlobalUnlock(hglobal);
	}

	// done with the hglobal
	GlobalFree(hglobal);

	// return the Bitmap object
	return bmp;
}

Gdiplus::Font *CreateGPFont(const TCHAR *faceName, int pointSize, int weight, HDC hdc)
{
	// Figure the pixel pitch in pix/inch.  If a DC was specified, use
	// its pixel pitch, otherwise use the reference 96 dpi.
	int dpi = hdc != NULL ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;

	// figure the em size in pixels: 1 point = 1/72"
	float emSize = (float)pointSize * (float)dpi / (float)72.0f;

	// figure the style
	int style = weight >= 700 ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;

	// create the font
	Gdiplus::FontFamily family(faceName);
	return new Gdiplus::Font(&family, emSize, style, Gdiplus::UnitPixel);
}

Gdiplus::Font *CreateGPFontPixHt(const TCHAR *faceName, int pixHeight, int weight, HDC hdc)
{
	// figure the pixel pitch in pix/inch: use the pixel pitch specific
	// to the device if a DC was provided, otherwise use the reference 96dpi
	int dpi = hdc != NULL ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;

	// scale the height for the monitor DPI
	float emSize = 96.0f/float(dpi) * float(pixHeight);

	// figure the style
	int style = weight >= 700 ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;

	// create the font
	Gdiplus::FontFamily family(faceName);
	return new Gdiplus::Font(&family, emSize, style, Gdiplus::UnitPixel);
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

GPDrawString::GPDrawString(Gdiplus::Graphics &g, Gdiplus::RectF &bbox) :
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
	const TCHAR *str, 
	Gdiplus::Font *font, Gdiplus::Brush *br, 
	bool newline)
{
	// figure the current layout area
	Gdiplus::RectF layoutRect(
		curOrigin.X, curOrigin.Y,
		bbox.GetRight() - curOrigin.X, bbox.GetBottom() - curOrigin.Y);

	// draw the string
	Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
	format.SetFormatFlags(format.GetFormatFlags() & ~Gdiplus::StringFormatFlagsLineLimit);
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
// Load a PNG resource
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

	bool GetInfo(ImageFileDesc &desc)
	{
		// Check the header to determine the image type.  For GIF, we can
		// identify both the type and image dimensions with the first 10
		// bytes; wtih PNG, we can do the same with the first 24 bytes; for
		// JPEG, we can identify the type with 12 bytes, but will need to
		// scan further into the file to find the size.  So start with the
		// first 24 bytes, which will at least let us determine the type, 
		// and is even enough to give us the size for everything but JPEG.
		BYTE buf[256];
		const size_t initialBytes = 24;
		if (!Read(0, buf, initialBytes))
			return false;

		// check for the JFIF signature (FF D8 FF E0 s1 s2 'JFIF')
		if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF && buf[3] == 0xE0
			&& buf[6] == 'J' && buf[7] == 'F' && buf[8] == 'I' && buf[9] == 'F')
		{
			// it's a JPEG - scan the segment list
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

				// check for an SOFn marker - these are where we can find the
				// image size
				if (buf[1] == 0xC0 || buf[1] == 0xC1 || buf[1] == 0xC2
					|| buf[1] == 0xC9 || buf[1] == 0xCA || buf[1] == 0xCB)
				{
					// SOFn marker - the size is in bytes 5:6 and 7:8
					desc.imageType = ImageFileDesc::JPEG;
					desc.size.cy = (buf[5] << 8) + buf[6];
					desc.size.cx = (buf[7] << 8) + buf[8];
					return true;
				}

				// Advance to the next segment header.  For frame types with
				// payloads, the two bytes following the marker give the
				// big-endian segment size (not counting the marker bytes).
				// For payload-less frames, just skip the two-byte marker.
				if (buf[1] >= 0xD0 && buf[1] <= 0xD8)
					ofs += 2;
				else
					ofs += 2 + (buf[2] << 8) + buf[3];
			}

			// failed to find an SOFn segment
			return false;
		}

		// Check for GIF: 'GIF' v0 v1 v2 x0 x1 y0 y2
		if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F')
		{
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
			desc.imageType = ImageFileDesc::PNG;
			desc.size.cx = (buf[16] << 24) + (buf[17] << 16) + (buf[18] << 8) + (buf[19] << 0);
			desc.size.cy = (buf[20] << 24) + (buf[21] << 16) + (buf[22] << 8) + (buf[23] << 0);
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
			if (buf[0] == 'C')
				Read(initialBytes, buf + initialBytes, sizeof(buf) - initialBytes);

			// basic uncompressed stream reader
			class ByteReader
			{
			public:
				ByteReader(const BYTE *buf) : p(buf), bit(0) { }
				virtual ~ByteReader() { }
				virtual BYTE ReadByte() { return *p++; }

				// read a bit from the stream
				BYTE ReadBit()
				{
					if (bit == 0)
					{
						b = ReadByte();
						bit = 8;
					}

					--bit;
					return (b >> bit) & 0x01;
				}

				// read an n-bit unsigned int
				UINT32 ReadUIntN(int nBits)
				{
					DWORD val = 0;
					for (int i = 0; i < nBits; ++i)
						val = (val << 1) | ReadBit();

					return val;
				}

				// read an n-bit signed int
				INT32 ReadIntN(int nBits)
				{
					// read the unsigned value
					UINT32 u = ReadUIntN(nBits);

					// if it's negative, sign-extend it to 32 bits
					if ((u & (1 << (nBits - 1))) != 0)
					{
						// set all higher-order bits to 1
						for (int i = nBits; i < 32; ++i)
							u |= 1 << i;
					}

					// reinterpret it as a signed 32-bit value
					return (INT32)u;
				}

			protected:
				const BYTE *p;
				BYTE b;
				int bit;
			};

			// Zlib stream reader, using the Zlib Inflate format
			class ZlibByteReader : public ByteReader
			{
			public:
				ZlibByteReader(BYTE *buf, size_t avail) : ByteReader(buf)
				{
					ZeroMemory(&zstr, sizeof(zstr));
					zstr.next_in = buf;
					zstr.avail_in = (uInt)avail;
					inflateInit(&zstr);
				}
				~ZlibByteReader() { inflateEnd(&zstr); }

				virtual BYTE ReadByte() override
				{
					zstr.next_out = outbuf;
					zstr.avail_out = 1;
					inflate(&zstr, Z_NO_FLUSH);
					return outbuf[0];
				}

			protected:
				z_stream zstr;
				BYTE outbuf[1];
			};

			// LZMA stream reader
			class LZMAByteReader : public ByteReader
			{
			public:
				LZMAByteReader(const BYTE *buf, size_t len) : ByteReader(buf)
				{
					decoder.Attach(new NCompress::NLzma::CDecoder());
					istream.Attach(new ByteInStream(buf, len));
					decoder->SetInStream(istream);
				}

				virtual BYTE ReadByte() override
				{
					BYTE b;
					UINT32 bytesRead = 0;
					if (SUCCEEDED(decoder->Read(&b, 1, &bytesRead) && bytesRead == 1))
						return b;
					else
						return 0;
				}

				RefPtr<ISequentialInStream> istream;
				RefPtr<NCompress::NLzma::CDecoder> decoder;

				class ByteInStream : public ISequentialInStream, public RefCounted
				{
				public:
					ByteInStream(const BYTE *buf, size_t len) : buf(buf), rem(len), refCnt(1) { }
					ULONG STDMETHODCALLTYPE AddRef() { return RefCounted::AddRef(); }
					ULONG STDMETHODCALLTYPE Release() { return RefCounted::Release(); }
					HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID *ppUnk)
					{
						if (riid == IID_ISequentialInStream)
							*ppUnk = static_cast<ISequentialInStream*>(this);
						else if (riid == IID_IUnknown)
							*ppUnk = static_cast<IUnknown*>(this);
						else
							return (*ppUnk = NULL), E_NOINTERFACE;

						AddRef();
						return S_OK;
					}

					HRESULT STDMETHODCALLTYPE Read(void *data, UInt32 size, UInt32 *processedSize)
					{
						if (size < rem)
							size = (UInt32)rem;

						if (size == 0 && *processedSize == NULL)
							return S_FALSE;

						if (size != 0)
						{
							memcpy(data, buf, size);
							buf += size;
							rem -= size;
						}

						if (processedSize != NULL)
							*processedSize = size;

						return S_OK;
					}

					const BYTE *buf;
					size_t rem;
					ULONG refCnt;
				};
			};

			// set up the appropriate reader based on the compression type
			std::unique_ptr<ByteReader> reader(
				buf[0] == 'C' ? new ZlibByteReader(buf + 8, sizeof(buf) - 8) :
				buf[0] == 'Z' ? new LZMAByteReader(buf + 8, sizeof(buf) - 8) :
				new ByteReader(buf + 8));

			// Read the number of bits per RECT element.  This is given as
			// a 5-bit unsigned int preceding the RECT elements.
			int bitsPerEle = reader->ReadUIntN(5);

			// Now read the four RECT elements.  These are stored as Xmin, 
			// Xmax, Ymin, Ymax.  The min values are required to be zero
			// for this particular rect (which makes one wonder why they're
			// stored at all; sigh), so we just need the max values, which
			// give the image dimensions.  The coordinates are in "twips",
			// which are 20ths of a screen pixel.
			(void)reader->ReadIntN(bitsPerEle);
			desc.size.cx = reader->ReadIntN(bitsPerEle);
			(void)reader->ReadIntN(bitsPerEle);
			desc.size.cy = reader->ReadIntN(bitsPerEle);

			// success
			return true;
		}

		// unrecognized type
		return false;
	}
};

bool GetImageFileInfo(const TCHAR *filename, ImageFileDesc &desc)
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
	return reader.GetInfo(desc);
}

bool GetImageBufInfo(const BYTE *imageData, long len, ImageFileDesc &desc)
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
	return reader.GetInfo(desc);
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
