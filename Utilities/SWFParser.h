// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// SWF (Shockwave Flash) file reader, parser, and mini-renderer
//

#pragma once
#include <d2d1.h>
#include <wincodec.h>
#include <dwrite.h>
#include "../zlib/zlib.h"
#include "../LZMA/CPP/7zip/Compress/LzmaDecoder.h"
#include <gdiplus.h>
#include "GraphicsUtil.h"
#include "LogError.h"
#include "Pointers.h"

class SWFParser
{
public:
	SWFParser();
	~SWFParser();

	// Static shutdown.  The main application entrypoint should call this
	// before process exit.
	static void Shutdown();

	// Load an SWF file.  If incremental is true, we only parse the file up
	// through the point where the first frame is displayable, and then return.
	// This allows the caller to render the first frame with minimal latency,
	// and spread out the work of loading the subsequent frames over the
	// playback time of the animation.
	bool Load(const TCHAR *filename, ErrorHandler &eh, bool incremental = false);

	// Parse the next frame.  If Load() was called in incremental mode, call
	// this when you want to fetch the next frame, then call it again when
	// it's time for the next frame after that, and so on.
	bool ParseFrame(ErrorHandler &eh);

	// Render the current display list
	bool Render(HDC hdc, HBITMAP hbitmap, SIZE targetPixSize, ErrorHandler &eh);

	// Have we reached the end of the file yet?  This can be used to determine
	// if there's more data to parse when in incremental mode.
	bool AtEof() const { return reader.BytesRemaining() != 0; }

	// get the frame count
	int GetFrameCount() const { return frameCount; }

	// get the frame delay in milliseconds
	DWORD GetFrameDelay() const { return frameDelay; }

	// get the frame size
	int GetFrameWidth() const { return static_cast<int>(frameRect.right); }
	int GetFrameHeight() const { return static_cast<int>(frameRect.bottom); }

	// SWF Tag Header
	struct TagHeader
	{
		UINT id;
		UINT32 len;
	};

	// SWF RGBA color record
	struct RGBA
	{
		BYTE r;
		BYTE g;
		BYTE b;
		BYTE a = 255;

		D2D1_COLOR_F ToD2D() const { return D2D1_COLOR_F{ r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f }; }

		bool operator==(const RGBA& other) const { return r == other.r && g == other.g && b == other.b && a == other.a; }
	};

	// SWF MATRIX type
	struct MATRIX
	{
		float scaleX = 1.0f;
		float scaleY = 1.0f;
		float rotateSkew0 = 0.0f;
		float rotateSkew1 = 0.0f;
		float translateX = 0.0f;
		float translateY = 0.0f;

		D2D1_POINT_2F Apply(D2D1_POINT_2F pt) const
		{
			return {
				pt.x * scaleX + pt.y * rotateSkew1 + translateX,
				pt.x * rotateSkew0 + pt.y * scaleY + translateY
			};
		}
	};

	// SWF Color Transform type
	struct CXFORM
	{
		INT redMult = 1;
		INT greenMult = 1;
		INT blueMult = 1;
		INT alphaMult = 1;
		INT redAdd = 0;
		INT greenAdd = 0;
		INT blueAdd = 0;
		INT alphaAdd = 0;
	};

	// PlaceObject tags
	struct PlaceObject
	{
		UINT16 charId = 0;
		UINT16 depth = 0;
		MATRIX matrix;
		CXFORM cxform;
		UINT16 morphRatio = 0;
		TSTRING name;
		UINT16 clipDepth = 0;
	};

	// SWF Character
	struct Character
	{
		virtual ~Character() { }
		UINT tagId;
		UINT16 charId;

		// Drawing context
		struct CharacterDrawingContext
		{
			// parser object
			SWFParser *parser;

			// render target
			ID2D1DCRenderTarget *target;

			// scale transform, to translate from SWF frame coordinates
			// to render target pixel coordinates
			D2D1_POINT_2F scale;
		};

		// draw it
		virtual void Draw(CharacterDrawingContext &dc, PlaceObject *p) = 0;
	};

	// Gradient Record
	struct GRADRECORD
	{
		BYTE ratio = 0;
		RGBA color;

		bool operator==(const GRADRECORD& other) const { return ratio == other.ratio && color == other.color; }
	};

	// Gradient
	struct GRADIENT
	{
		enum SpreadMode 
		{
			Pad = 0,
			Reflect = 1,
			Repeat = 2
		};
		enum InterpolationMode
		{
			Normal = 0,
			Linear = 1
		};
		SpreadMode spreadMode = Pad;
		InterpolationMode interpolationMode = Normal;
		std::vector<GRADRECORD> gradients;
		float focalPoint;

		bool operator==(const GRADIENT& other) const
		{
			if (spreadMode != other.spreadMode
				|| interpolationMode != other.interpolationMode
				|| gradients.size() != other.gradients.size())
				return false;

			
			for (size_t i = 0 ; i < gradients.size(); ++i)
			{
				if (!(gradients[i] == other.gradients[i]))
					return false;
			}

			return true;
		}
	};

	// Fill Style
	struct FillStyle
	{
		enum FillType
		{
			Solid = 0x00,
			LinearGradient = 0x10,
			RadialGradient = 0x12,
			FocalRadialGradient = 0x13,
			RepeatingBitmap = 0x40,
			ClippedBitmap = 0x41,
			NonSmoothedRepeatingBitmap = 0x42,
			NonSmoothedClippedBitmap = 0x43
		};
		FillType type = Solid;
		RGBA color;
		MATRIX matrix;
		GRADIENT gradient;
		UINT16 bitmapId = 0;

		bool operator==(const FillStyle& other) const
		{
			if (type != other.type)
				return false;

			switch (type)
			{
			case Solid:
				return color == other.color;

			case LinearGradient:
			case RadialGradient:
				return gradient == other.gradient;

			case FocalRadialGradient:
				return gradient == other.gradient && gradient.focalPoint == other.gradient.focalPoint;

			default:
				return false;
			}
		}
	};

	// Line style
	struct LineStyle
	{
		float width;
		RGBA color;

		// LINESTYLE2 extensions
		enum CapStyle { RoundCap = 0, NoCap = 1, SquareCap = 2 };
		enum JoinStyle { RoundJoin = 0, BevelJoin = 1, MiterJoin = 2 };

		CapStyle startCapStyle = RoundCap;
		CapStyle endCapStyle = RoundCap;
		JoinStyle joinStyle = RoundJoin;
		float miterLimitFactor = 0.0f;

		bool noHScale = false;
		bool noVScale = false;
		bool pixelHinting = false;
		bool noClose = false;

		FillStyle fillType;
	};

	// Shape Record
	struct ShapeWithStyle;
	struct ShapeRecord
	{
		// Shape drawing context
		struct ShapeDrawingContext 
		{
			// Character drawing context
			Character::CharacterDrawingContext &chardc;

			// Shape-with-style that we're part of
			ShapeWithStyle *sws;

			// PlaceObject context
			PlaceObject *po;

			// Current line styles and fill styles arrays.  The style arrays
			// are essentially scoped to the shape-with-style group, and as we 
			// proceed through the shape records, Style Change records can
			// switch the group-level arrays to new ones specified within
			// the Style Change.  That change applies to the rest of the
			// items within the group.  We store references to the currently
			// active arrays here, so that we can change them as needed and
			// pass the change along to subsequent items.
			std::vector<FillStyle> &fillStyles;
			std::vector<LineStyle> &lineStyles;

			// Current drawing position, in shape-relative coordinates
			D2D1_POINT_2F pt{ 0.0f, 0.0f };

			// Current fill style and line style
			FillStyle *curFillStyle0 = nullptr;
			FillStyle *curFillStyle1 = nullptr;
			LineStyle *curLineStyle = nullptr;

			// Line/curve segment maps.  To draw a figure, we first build a
			// set of maps that group the segments making up a shape by line
			// style and by fill style, then we turn each group into a D2D
			// path.  This is necessary because the SWF geometry model allows
			// heterogeneous styles in a single path, whereas D2D only allows
			// one stroke style and one fill style per path.  See the rendering
			// code for a more detailed explanation.
			struct Segment
			{
				bool straight;
				D2D1_POINT_2F start;
				D2D1_POINT_2F control;
				D2D1_POINT_2F end;
			};
			typedef std::unordered_map<UINT_PTR, std::list<Segment>> EdgeMap;
			EdgeMap fillEdges;
			EdgeMap lineEdges;

			// Add a segment to one of the style maps.  'reversed' means that the
			// edge is being added with its point sequence in reverse order from
			// its SWF definition, to reverse the winding order of is shape; this
			// requires adding it at the front of its style group's edge list,
			// so that the whole path that this edge is part of can be rendered
			// in reverse order.
			void AddEdge(EdgeMap &map, void *stylePtr, const Segment &edge, bool reversed);

			// render the shapes in the style maps
			void RenderMaps();

			// Transform coordinates from shape-relative coordinates to
			// Direct2D render target coordinates.  This first applies the
			// coordinate transform matrix from the PlaceObject record to
			// get the SWF frame coordinates, then applies the rendering
			// scaling factor to get the final render target coordinates.
			D2D1_POINT_2F TargetCoords(D2D1_POINT_2F shapeRelativePt) const
			{
				auto frameRelativePt = po->matrix.Apply(shapeRelativePt);
				return { frameRelativePt.x * chardc.scale.x, frameRelativePt.y * chardc.scale.y };
			}
		};

		virtual ~ShapeRecord() { }
		virtual void Draw(ShapeDrawingContext &sdc) = 0;
	};

	struct EdgeRecord : ShapeRecord
	{
		bool straight;           // straight/curved
		bool general;            // true -> X/Y stored, false -> vertical or horizontal only
		bool vert;               // if general -> true -> vertical, false -> horizontal
		float deltaX, deltaY;    // delta for straight, control delay for curved
		float anchorX, anchorY;  // anchor point delta for curved

		virtual void Draw(ShapeDrawingContext &sdc) override;
	};

	struct StyleChangeRecord : ShapeRecord
	{
		bool stateNewStyles;     // true -> contains new styles
		bool stateLineStyle;     // true -> line style change
		bool stateFillStyle1;    // true -> fill style 1 change
		bool stateFillStyle0;    // true -> fill style 0 change
		bool stateMoveTo;        // true -> this is a Move To record
		float deltaX, deltaY;    // if stateMoveTo -> offset to the new draw position
		UINT fillStyle0;         // if stateFillStyle0 -> new fill style 0
		UINT fillStyle1;         // if stateFillStyle1 -> new fill style 1
		UINT lineStyle;          // if stateLineStyle -> new line style

		std::vector<FillStyle> fillStyles;  // if stateNewStyles -> array of new fill styles
		std::vector<LineStyle> lineStyles;  // if stateNewStyles -> array of new line styles

		virtual void Draw(ShapeDrawingContext &sdc) override;
	};

	// ShapeWithStyle
	struct ShapeWithStyle : Character
	{
		virtual void Draw(CharacterDrawingContext &cdc, PlaceObject *p) override;

		D2D1_RECT_F bounds;
		std::vector<FillStyle> fillStyles;
		std::vector<LineStyle> lineStyles;
		std::vector<std::unique_ptr<ShapeRecord>> shapeRecords;

		// DefineShape4 extra fields
		D2D1_RECT_F edgeBounds{ 0.0f, 0.0f, 0.0f, 0.0f };
		bool usesFillWindingRule = false;
		bool usesNonScalingStrokes = false;
		bool usesScalingStrokes = false;
	};

	// Bitmap image bits
	struct ImageBits : Character
	{
		virtual void Draw(CharacterDrawingContext &cdc, PlaceObject *p) override;

		// image data size and bytes
		UINT32 imageDataSize = 0;
		std::unique_ptr<BYTE> imageData;

		// alpha array, one byte per pixel (width * height)
		UINT32 alphaDataSize = 0;
		std::unique_ptr<BYTE> alphaData;

		// JPEG deblocking filter
		float deblockParam = 0.0f;

		// image type
		enum Type
		{
			Unknown,
			JPEGImageData,   // JPEG image data only, no headers; uses the common JPEG Tables header
			JPEG,            // full JPEG stream with encoding tables and image data
			PNG,             // PNG stream
			GIF89a           // GIF89a stream, non-animated
		} type = Unknown;

		// cached D2D bitmap
		RefPtr<ID2D1Bitmap> bitmap;

		// get the D2D bitmap, or create and cache it if we haven't already done so
		ID2D1Bitmap *GetOrCreateBitmap(CharacterDrawingContext &dc);
	};

	// SWF Frame
	struct Frame
	{
		int dummy;
	};

	// Display list type.  It's nominally a list, but we implement it
	// as an unordered map keyed by depth.  This is the most efficient
	// way to look at it, because per the spec, there can be only one
	// element at each depth, and the SWF file manipulates the list by
	// random-access operations keyed by depth.  When it comes time to
	// render the list, we'll need to build an index that sorts by depth,
	// but it's more efficient to do that on demand at render time
	// rather than maintaining the list in sorted order during the
	// construction process, when the SWF will perform random accesses.
	typedef std::unordered_map<UINT, PlaceObject> DisplayList;

	// SWF dictionary type
	typedef std::unordered_map<UINT16, std::unique_ptr<Character>> Dictionary;

	// SWF stream reader.  This is a simple/ sequential, non-seekable
	// byte stream reader interface, which makes it easy to implement
	// for the variety of compression formats that SWF files can use
	// (uncompressed, zlib, LZA).  We take a byte buffer as input, and
	// expose a sequential byte reader operation.
	//
	// The base class implementation works on ordinary uncompressed
	// byte streams - it's just a pass-through for the underlying buffer.
	// Subclasses can implement decoding for different compression 
	// formats.
	//
	// A more sophisticated version of this class would use a second
	// layer of stream input as the source of the raw bytes, rather than
	// a single memory block, to allow for buffering the source.  But
	// we only plan to use this for relatively small files that we can
	// comfortably fit into memory all at once, so let's keep it simple.
	// This single-read approach is also faster than adding a buffering
	// layer, albeit at the cost of using more memory.
	//
	// Note: most of the record decoding methods are implemented on the
	// UncompressedReader subclass instead of on the base class.  This
	// allows for more efficient access to the stream, so that we don't
	// have to go through as many virtual methods for decoding the
	// primitive types.  It's not as nice a design; it would be cleaner
	// architecturally to implement the decoder methods on the abstract
	// base class, and rely on the virtual byte reader for all access.
	// But we don't have any practical need to be able to decode an
	// abstract stream.  For our purposes, we can always load the entire
	// file into memory and decompress it in memory, then access that
	// memory block with the byte array reader.
	//
	class SWFReader
	{
	public:
		SWFReader() { }
		virtual ~SWFReader() { }
		virtual BYTE ReadByte() = 0;
		virtual size_t ReadBytes(BYTE *buf, size_t len) = 0;
		virtual void SkipBytes(size_t n) = 0;

		// SWF rectangle reader
		D2D1_RECT_F ReadRect();

		// read an SWF varying-bit unsigned int
		UINT32 ReadUB(int nBits);

		// read an SWF varying-bit signed int
		INT32 ReadSB(int nBits);

		// read an SWF fixed-point varying-bit-length value
		float ReadFB(int nBits);

		// read a single bit from the stream
		BYTE ReadBit();

		// read various fixed-size SWF types
		UINT32 ReadUInt16() { BYTE a = ReadByte(), b = ReadByte();  return a | (b << 8); }
		UINT32 ReadUInt32() { BYTE a = ReadByte(), b = ReadByte(), c = ReadByte(), d = ReadByte();  return a | (b << 8) | (c << 16) | (d << 24); }

	protected:
		// For bit-field operations, cache one byte of the input stream.
		// 'b' is the cached byte, and 'bit' is the current number of
		// bits available in 'b'.  (Upon loading 'b' from the next byte
		// in the input stream, we have 8 bits available in 'b'.)
		//
		// Bits are read from the high-order end first.  As we read bits
		// out of the cache, we keep the remaining bits at the HIGH end
		// of 'b', by shifting 'b' left by one bit for each bit removed.
		//
		// Caching one byte at a time for bit reading works pretty
		// naturally with the way the stream is defined, particulary
		// for byte-alignment for fixed-size types.  When we switch from
		// reading a varying-bit type to a fixed-size type, we'll
		// naturally discard the remaining padding bits before the
		// next byte alignment point, because the fixed-size type
		// readers don't look at 'b' at all.  The only point where
		// we have to pay attention to the transitions is when we
		// *start* reading a varying-bit type: at that point, we have
		// to explicitly clear the cache, so that we don't try to use
		// bits left over from a previous varying-bit read.
		struct
		{
			BYTE b = 0;
			int nBits = 0;
		} bitCache;

		// start reading a varying-bit-size field
		void StartBitField() { bitCache.nBits = 0; }
	};

	// Uncompressed byte reader
	class UncompressedReader : public SWFReader
	{
	public:
		UncompressedReader() : p(nullptr), rem(0) { }
		UncompressedReader(BYTE *buf, size_t buflen) : p(buf), rem(buflen) { }

		void Init(BYTE *buf, size_t buflen)
		{
			this->p = buf;
			this->rem = buflen;
		}

		virtual BYTE ReadByte() override
		{
			if (rem != 0)
				return --rem, *p++;
			else
				return 0;
		}

		virtual size_t ReadBytes(BYTE *buf, size_t len) override
		{
			// limit the read to the amount remaining
			if (len > rem)
				len = rem;

			// copy the data
			if (len != 0)
				memcpy(buf, p, len);

			// advance counters
			p += len;
			rem -= len;

			// return the amount read
			return len;
		}

		virtual void SkipBytes(size_t n) override
		{
			if (n > rem) n = rem;
			rem -= n;
			p += n;
		}

		// bytes remaining in the buffer
		size_t BytesRemaining() const { return rem; }

		// File format version
		BYTE fileFormatVersion = 0;

		// read an SWF string
		TSTRING ReadString();

		// Read an SWF tag header
		TagHeader ReadTagHeader();

		// read RGB color records in various formats
		RGBA ReadRGB();
		RGBA ReadRGBA();
		RGBA ReadARGB();

		// read a geometry transform matrix record
		MATRIX ReadMatrix();

		// read a Color Transform (CXFORM) record
		CXFORM ReadCXFORM(bool hasAlpha);

		// read a DefineBits record
		void ReadDefineBits(Dictionary &dict, TagHeader &tagHdr);

		// read a PlaceObject record
		void ReadPlaceObject(DisplayList &displayList, UINT32 len);

		// read a PlaceObject2 record
		void ReadPlaceObject2(DisplayList &displayList, UINT32 len);

		// skip a Clip Actions list
		void SkipClipActions();

		// Read a GRADIENT element of a DefineShape tag
		void ReadGradient(UINT16 tagId, GRADIENT &g);

		// Read a focal gradient
		void ReadFocalGradient(UINT16 tagId, GRADIENT &g);

		// read a FillStyle record
		void ReadFillStyle(UINT16 tagId, FillStyle &f);

		// Read a FillStyles array
		void ReadFillStylesArray(std::vector<FillStyle> &fillStyles, int tagId);

		// Read a LineStyles array
		void ReadLineStylesArray(std::vector<LineStyle> &lineStyles, int tagId);

		// Read a DefineShape tag
		void ReadDefineShape(Dictionary &dict, UINT16 tagId);

	protected:
		BYTE *p;
		size_t rem;
	};

	// Zlib stream reader, using the Zlib Inflate format
	class ZlibReader : public SWFReader
	{
	public:
		ZlibReader(BYTE *buf, size_t avail)
		{
			ZeroMemory(&zstr, sizeof(zstr));
			zstr.next_in = buf;
			zstr.avail_in = (uInt)avail;
			inflateInit(&zstr);
		}
		~ZlibReader() { inflateEnd(&zstr); }

		virtual BYTE ReadByte() override
		{
			zstr.next_out = outbuf;
			zstr.avail_out = 1;
			inflate(&zstr, Z_NO_FLUSH);
			return outbuf[0];
		}

		virtual size_t ReadBytes(BYTE *buf, size_t len) override
		{
			zstr.next_out = outbuf;
			zstr.avail_out = static_cast<uInt>(len);
			auto err = inflate(&zstr, Z_NO_FLUSH);
			return (err == Z_OK || err == Z_STREAM_END) ? len - zstr.avail_out : 0;
		}
			
		virtual void SkipBytes(size_t n) override
		{
			BYTE buf[1024];
			while (n != 0)
			{
				// read into a temporary buffer (which we'll just discard)
				UINT32 cur = n < sizeof(buf) ? static_cast<UINT32>(n) : sizeof(buf);
				inflate(&zstr, Z_NO_FLUSH);

				// deduct the current read from the total
				n -= cur;
			}
		}

	protected:
		z_stream zstr;
		BYTE outbuf[1];
	};

	// LZMA stream reader
	class LZMAReader : public SWFReader
	{
	public:
		LZMAReader(const BYTE *buf, size_t len)
		{
			decoder.Attach(new NCompress::NLzma::CDecoder());
			istream.Attach(new ByteInStream(buf, len));
			decoder->SetInStream(istream);
		}

		virtual BYTE ReadByte() override
		{
			BYTE b;
			UINT32 bytesRead = 0;
			if (SUCCEEDED(decoder->Read(&b, 1, &bytesRead)) && bytesRead == 1)
				return b;
			else
				return 0;
		}

		virtual size_t ReadBytes(BYTE *buf, size_t len) override
		{
			UINT32 bytesRead = 0;
			return SUCCEEDED(decoder->Read(buf, static_cast<UInt32>(len), &bytesRead)) ? bytesRead : 0;
		}

		virtual void SkipBytes(size_t n) override
		{
			BYTE buf[1024];
			while (n != 0)
			{
				// read into a temporary buffer (which we'll just discard)
				UINT32 cur = n < sizeof(buf) ? static_cast<UINT32>(n) : sizeof(buf);
				UINT32 bytesRead = 0;
				if (!SUCCEEDED(decoder->Read(buf, cur, &bytesRead)) || bytesRead != cur)
					break;

				// deduct the current read from the total
				n -= cur;
			}
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

protected:
	// Static initialization.  We call this automatically when loading the
	// first SWF file, so that we don't allocate any static resources until
	// we know we're actually going to use the SWF mini-renderer.
	static bool Init(ErrorHandler &eh);

	// static initialization completed?
	static bool inited;

	// D2D factory
	static RefPtr<ID2D1Factory> d2dFactory;

	// DirectWrite fatcory
	static RefPtr<IDWriteFactory> dwFactory;

	// WIC factory
	static RefPtr<IWICImagingFactory> wicFactory;

	// system locale name, for locale-sensitive DWrite text operations
	static WCHAR locale[LOCALE_NAME_MAX_LENGTH];

	// filename we're loading
	TSTRING filename;

	// decompressed file contents - we keep this during incremental loading
	std::unique_ptr<BYTE> fileContents;
	
	// stream reader for the decompressed file
	UncompressedReader reader;

	// SWF format version
	BYTE version = 0;

	// language code
	BYTE language = 0;

	// frame rate, frames per second
	float frameRate = 0;

	// frame delay, in millisecond
	DWORD frameDelay = 0;

	// number of frames
	int frameCount = 0;

	// Frame rectangle, in pixels
	D2D1_RECT_F frameRect = { 0.0f, 0.0f, 0.0f, 0.0f };

	// Frame list
	std::list<Frame> frames;

	// Dictionary
	Dictionary dict;

	// current background color
	RGBA bgColor;

	// the display list
	DisplayList displayList;

	// JPEG Tables.  This contains the JPEG header (the Tables/Misc
	// segment of a JPEG file) common to all JPEG images stored in
	// the file.  There can only be one of these per SWF file.
	struct
	{
		size_t len;
		std::unique_ptr<BYTE> data;
	} jpegTables;
};
