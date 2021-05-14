// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Shockwave Flash (SWF) file parser
//
// SWF (Shockwave Flash) file reader, parser, and mini-renderer.  This
// implements a limited replacement for Flash Player.  Adobe officially
// made Flash Player obsolete in January 2021 and pushed an update
// that disables existing installations, so we can no longer use Flash
// Player to display SWF files on most systems.  SWF files are used in
// a PinballY context for instruction cards.  Most of the HyperPin Media
// Packs available for download from the virtual pinball sites contain
// SWF instruction cards, so it's convenient for users if PinballY can
// display them directly.  This mini-renderer is designed to replace
// the obsolete Flash Player to provide direct SWF display.
//
// SWF is a fairly complex format, but the instruction cards found in
// the HyperPin Media Packs tend to use a small subset of SWF's
// capabilities.  The main thing they use is SWF's vector graphics
// facilities, to display the instruction card text.  In fact, that's
// the whole reason that the people who created all of those Media
// Packs chose SWF in the first place: they wanted a vector format
// so that the files would scale well to any display resolution, as
// a way of future-proofing the media for use on the higher-res
// displays of the indefinite future.  As well-intentioned as that
// was, it kind of backfired in that the entire SWF format is now
// dead, so it hardly matters that it scales up nicely to modern
// monitors.  At any rate, apart from the vector graphics, the
// Media Pack instruction cards don't use much else from SWF's
// feature set - they are by their nature just static images, so
// they mostly don't have any use for animation or scripting.
// I've observed the presence of ActionScript code in some of the
// instruction card SWF files I've tested this with, but I'm
// guessing it's just boilerplate code that was automatically
// inserted by the tools used to compile the files, and doesn't
// do anything that's actually necessary for proper display of
// the static image in the first frame.  This mini-renderer just
// ignores all scripting code, which not only simplifies the
// implementation, but also neatly avoids most of Flash Player's
// notorious security problems, which mostly arose from the
// lack of any consideration given to security in ActionScript's
// original design.  Ignoring the scripts largely eliminates the
// potential for malicious SWF files to do any harm, since all
// of that was tyically done through ActionScript.
//
// Adobe (for now) publishes a specification for the SWF file
// format on their site, at:
//
// https://www.adobe.com/content/dam/acom/en/devnet/pdf/swf-file-format-spec.pdf
//
// That file is copyrighted and not licensed for redistribution,
// so I unfortunately can't include it in the PinballY repository
// as insurance against Adobe removing it from their site in the
// future.  There are, however, numerous third-party descriptions
// of the SWF format available on the Web; none are as complete or
// informative as the Adobe spec, which I find rather good as this
// sort of technical documentation goes, but they should at least
// help you piece together anything that's missing from my code
// here if you find you need to add new capabilities or fix the
// existing ones.
//

#include "stdafx.h"
#include <Shlwapi.h>
#include "SWFParser.h"
#include "FileUtil.h"
#include "StringUtil.h"
#include "LogError.h"
#include "WinUtil.h"
#include "../PinballY/LogFile.h"

// statics
bool SWFParser::inited = false;
RefPtr<ID2D1Factory> SWFParser::d2dFactory;
RefPtr<IWICImagingFactory> SWFParser::wicFactory;
RefPtr<IDWriteFactory> SWFParser::dwFactory;
WCHAR SWFParser::locale[LOCALE_NAME_MAX_LENGTH];

// static initialization
bool SWFParser::Init(ErrorHandler &eh)
{
	// only do static initialization once
	if (!inited)
	{
		HRESULT hr = S_OK;
		auto HRError = [&eh, &hr](const TCHAR *where)
		{
			eh.SysError(_T("An error occurred initializing the SWF (Flash) mini-renderer. The program won't be able to display SWF files during this session."),
				MsgFmt(_T("%s failed, HRESULT=%lx"), where, hr));

			// release any resources we created
			dwFactory = nullptr;
			d2dFactory = nullptr;
			wicFactory = nullptr;
			return false;
		};

		// create the DWrite fatory
		if (!SUCCEEDED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwFactory))))
			return HRError(_T("DWriteCreateFactory"));

		// create the D2D factory
		if (!SUCCEEDED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory)))
			return HRError(_T("D2D1CreateFactory"));

		// retrieve the system default locale name
		if (GetSystemDefaultLocaleName(locale, countof(locale)) == 0)
			wcscpy_s(locale, L"en-US");

		// initialize a WIC factory
		if (!SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory))))
			return HRError(_T("CoCreateInstance(WICImagingFatory)"));
	}

	// success
	return true;
}

void SWFParser::Shutdown()
{
	if (inited)
	{
		// release D2D resources
		d2dFactory = nullptr;
		wicFactory = nullptr;

		// no longer initialized
		inited = false;
	}
}


SWFParser::SWFParser()
{
}

SWFParser::~SWFParser()
{
}

bool SWFParser::Load(const TCHAR *filename, ErrorHandler &eh, bool incremental)
{
	// initialize statics, if we haven't already
	if (!Init(eh))
		return false;

	// remember the filename
	this->filename = filename;

	// Load the file into memory.  This assumes that the file isn't very
	// large, which is a safe assumption for the intended use case of
	// HyperPin Media Pack instruction cards.
	long buflen;
	fileContents.reset(ReadFileAsStr(filename, eh, buflen, 0));

	// fail if we couldn't read the file
	if (fileContents == nullptr)
		return false;

	// Check the file signature.  If it's not an SWF file, fail.
	BYTE *buf = fileContents.get();
	bool isSWF = (buf[1] == 'W' && buf[2] == 'S' && (buf[0] == 'F' || buf[0] == 'C' || buf[0] == 'Z'));
	if (!isSWF || buflen < 8)
	{
		eh.Error(MsgFmt(_T("%s is not an SWF file"), filename));
		return false;
	}

	// store the SWF version
	version = buf[3];

	// Get the stream size.  This is the size of the *decompressed*
	// stream, not of the physical file.  This size includes the
	// 8-byte header.
	UINT32 uncompressedSize = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);

	// The rest of the stream after the 8-byte header can be compressed,
	// as indicated by the first byte of the signature:
	//
	//  F -> uncompressed
	//  C -> zlib compressed
	//  Z -> LZMA compressed
	//
	// For greater efficiency, let's decompress the entire file into
	// a memory buffer up front.  That lets us scan through the rest of
	// the file as plain bytes, without having to descend into the
	// decompression algorithm on every byte read.  This wouldn't be
	// a good approach in general, because some SWF files can be quite
	// large.  But for our limited use case of HyperPin Media Pack
	// instruction cards, even the fully decompressed sizes should be
	// fairly small.
	if (buf[0] == 'C')
	{
		// Zlib compression.  Set up a zlib decompression stream.
		z_stream zstr;
		ZeroMemory(&zstr, sizeof(zstr));
		zstr.next_in = &buf[8];
		zstr.avail_in = buflen - 8;
		inflateInit(&zstr);

		// set up an output buffer, and copy the uncompressed header portion
		std::unique_ptr<BYTE> outbuf(new BYTE[uncompressedSize]);
		memcpy(outbuf.get(), buf, 8);

		// decompress the file into the output buffer
		zstr.next_out = outbuf.get() + 8;
		zstr.avail_out = uncompressedSize - 8;
		int zerr = inflate(&zstr, Z_FINISH);
		inflateEnd(&zstr);

		// check that we finished reading the stream successfully
		if (zerr != Z_STREAM_END)
		{
			eh.Error(MsgFmt(_T("%s: zlib decompression failed, error %d"), filename, zerr));
			return false;
		}

		// replace the original file buffer with the uncompressed buffer
		fileContents.reset(buf = outbuf.release());
	}
	else if (buf[0] == 'Z')
	{
		// LZMA compression.  Set up an LZMA decoder.
		RefPtr<ISequentialInStream> istream;
		RefPtr<NCompress::NLzma::CDecoder> decoder;
		decoder.Attach(new NCompress::NLzma::CDecoder());
		istream.Attach(new LZMAReader::ByteInStream(&buf[8], buflen - 8));

		// set up an output buffer, and copy the uncompressed header portion
		std::unique_ptr<BYTE> outbuf(new BYTE[uncompressedSize]);
		memcpy(outbuf.get(), buf, 8);

		// decompress the file into the output buffer
		UINT32 bytesRead = 0;
		HRESULT hr = decoder->Read(outbuf.get() + 8, uncompressedSize - 8, &bytesRead);
		if (!SUCCEEDED(hr) || bytesRead != uncompressedSize - 8)
		{
			eh.Error(MsgFmt(_T("%s: LZMA decompression failed, HRESULT %lx, bytes read %ld"),
				filename, static_cast<unsigned long>(hr), static_cast<unsigned long>(bytesRead)));
			return false;
		}

		// replace the original file buffer with the uncompressed buffer
		fileContents.reset(buf = outbuf.release());
	}

	// Set up the stream reader.  Since we've already decompressed the whole
	// file, we can just set up a plain ByteReader.  And since we've already
	// read the header, we can start 8 bytes in.
	reader.Init(buf + 8, uncompressedSize - 8);

	// set the foramt version in the reader, so that it can apply appropriate
	// handling to types that vary by format version (e.g., String)
	reader.fileFormatVersion = version;

	// Read the frame bounds.  This is given as a RECT element with
	// the top/left set to zero.
	frameRect = reader.ReadRect();

	// get the frame rate and count
	frameRate = static_cast<float>(reader.ReadUInt16()) / 256.0f;
	frameCount = reader.ReadUInt16();

	// figure the frame delay in milliseconds
	frameDelay = static_cast<DWORD>(1000.0f / frameRate);

	// create the first frame
	frames.emplace_back();

	// parse one frame or all frames, depending on the mode
	for (;;)
	{
		// parse the next frame; return failure if that fails
		if (!ParseFrame(eh))
			return false;

		// if we're in incremental mode, stop here
		if (incremental)
			return true;
	}

	// successful completion
	return true;
}

bool SWFParser::ParseFrame(ErrorHandler &eh)
{
	// get the last frame and make it current
	Frame &curFrame = frames.back();

	// keep track of unhandled tags, for logging diagnostics
	std::unordered_map<UINT16, bool> unimplementedTags;

	// The rest of the file consists of "tags".  Read them all.
	for (bool frameDone = false; !frameDone && reader.BytesRemaining() != 0; )
	{
		// read the tag header
		auto tagHdr = reader.ReadTagHeader();

		// remember the starting position, so that we can skip any unused part
		// of the record after finishing the type-specific reading
		size_t startingRem = reader.BytesRemaining();

		// process the tag
		switch (tagHdr.id)
		{
		case 0:   // End
			// End of file marker tag.  There shouldn't be anything left in
			// the stream at this point; if more bytes remain, log a warning,
			// but otherwise ignore it.
			if (reader.BytesRemaining() != 0)
				eh.Error(MsgFmt(_T("Warning: SWF reader: END tag found before end of stream (file %s, bytes remaining: %ld\n"),
					filename.c_str(), static_cast<unsigned long>(reader.BytesRemaining())));
			break;

		case 1:   // ShowFrame
			// if this isn't the last frame, allocate a new frame
			if (frames.size() < frameCount)
				curFrame = frames.emplace_back();

			// the frame is now done
			frameDone = true;
			break;

		case 2:   // DefineShape
			reader.ReadDefineShape(dict, 2);
			break;

		case 4:   // PlaceObject
			reader.ReadPlaceObject(displayList, tagHdr.len);
			break;

		case 6:   // DefineBits
			reader.ReadDefineBits(dict, tagHdr);
			break;

		case 8:   // JPEGTables
			// allocate space for the data
			jpegTables.len = tagHdr.len;
			jpegTables.data.reset(new BYTE[tagHdr.len]);

			// Per the SWF spec: "Before version 8 of the SWF file format, SWF files
			// could contain an erroneous header of FF D9 FF D8 before the JPEG SOI
			// marker [FF D8]."  Read the first 6 bytes to check for this and remove
			// the extra 4 bytes.
			{
				UINT32 bytesToRead = tagHdr.len;
				auto p = jpegTables.data.get();
				if (bytesToRead >= 6 && version < 8)
				{
					// scan for the errant header
					reader.ReadBytes(p, 6);
					bytesToRead -= 6;
					if (memcmp(p, "\xFF\xD9\xFF\xD8\xFF\xD8", 6) == 0)
					{
						// remove the first four bytes
						p[0] = p[4];
						p[1] = p[5];
						jpegTables.len -= 4;

						// now keep the remaining two bytes from the pre-read
						p += 2;
					}
					else
					{
						// keep all six bytes from the pre-read
						p += 6;
					}
				}

				// read the rest of the bytes
				reader.ReadBytes(p, bytesToRead);
			}
			break;

		case 9:   // SetBackgroundColor
			bgColor = reader.ReadRGB();
			break;

		case 10:  // DefineFont
			// TO DO
			break;

		case 11:  // DefineText
			// TO DO
			break;

		case 13:  // DefineFontInfo
			// TO DO
			break;

		case 20:  // DefineBitsLossless
			// TO DO #6
			break;

		case 21:  // DefineBitsJPEG2
			reader.ReadDefineBits(dict, tagHdr);
			break;

		case 22:  // DefineShape2
			reader.ReadDefineShape(dict, 22);
			break;

		case 24:  // Protect
			// we're not an authoring tool, so we can just ignore this
			break;

		case 25:  // PathsArePostscript
			// advisory tag - ignore
			break;

		case 26: // PlaceObject2
			reader.ReadPlaceObject2(displayList, tagHdr.len);
			break;

		case 32:  // DefineShape3
			reader.ReadDefineShape(dict, 32);
			break;

		case 33:  // DefineText2
			// TO DO
			break;

		case 35:  // DefineBitsJPEG3
			reader.ReadDefineBits(dict, tagHdr);
			break;

		case 36:  // DefineBitsLossless2
			// TO DO
			break;

		case 39:  // DefineSprite
			// TO DO #10
			break;

		case 43:  // FrameLabel
			// used for scripting - ignore
			break;

		case 48:  // DefineFont2
			// TO DO
			break;

		case 56:  // ExportAssets
			// This is used to share assets such as shapes, fonts, or bitmaps
			// with other SWF files that are part of the same Web site.  We
			// don't implement the ImportAssets counterpart tag, so for our
			// purposes, exports will never be consumed, so we can silently
			// ignore them.
			break;

		case 57:  // ImportAssets
			// Not implemented - ignore
			break;

		case 58:  // Enable Debugger
			// debugger not implemented - ignore
			break;

		case 62:  // DefineFontInfo2
			// TO DO
			break;

		case 64:  // EnableDebugger2
			// debugger not implemented - ignore
			break;

		case 65:  // ScriptLImits
			// scripting is not imlemented - ignore
			break;

		case 66:  // SetTabIndex
			// interactive input is not implemented - ignore
			break;
			
		case 69:  // FileAttributes
			// This tag contains bits indicating which capabilities the file
			// uses.  This is advisory information that we don't currently
			// need, so just silently ignore it.
			break;

		case 71:  // ImportAssets2
			// not implemented - ignore
			break;

		case 75:  // DefineFont3
			// TO DO
			break;

		case 76:  // SymbolClass
			// this is for scripting, which we don't implement - ignore
			break;

		case 77:  // Metadata
			// not implemented - ignore
			break;

		case 78:  // DefineScalingGrid
			// not implemented - ignore
			break;

		case 83:  // DefineShape4
			reader.ReadDefineShape(dict, 83);
			break;

		case 86:  // DefineSceneAndFrameLabelData
			// not implemented - ignore
			break;

		case 88:  // DefineFontName
			// TO DO
			break;

		case 90:  // DefineBitsJPEG4
			reader.ReadDefineBits(dict, tagHdr);
			break;

		case 91:  // DefineFont4
			// TO DO
			break;

		case 70:  // PlaceObject3
		default:
			// remember the unhandled type
			if (unimplementedTags.find(tagHdr.id) == unimplementedTags.end()) 
				unimplementedTags.emplace(tagHdr.id, true);

			// Skip the record data
			reader.SkipBytes(tagHdr.len);
			break;
		}

		// Figure how many bytes we consumed, and skip any remaining
		// bytes in the record, so that we're aligned with the start
		// of the next tag
		size_t bytesConsumed = startingRem - reader.BytesRemaining();
		if (tagHdr.len > bytesConsumed)
			reader.SkipBytes(tagHdr.len - bytesConsumed);
	}

	// log unimplemented tags
	if (unimplementedTags.size() != 0)
	{
		LogFileErrorHandler lfeh;
		TSTRING tags;
		for (auto &t : unimplementedTags)
		{
			if (tags.length() != 0) tags.append(_T(", "));
			tags.append(MsgFmt(_T("%d"), t.first));
		}
		lfeh.Error(MsgFmt(_T("Warning: %s uses SWF features that aren't implemented in the simplified built-in ")
			_T("SWF renderer, so it might not be displayed as designed. Consider converting the file to an ")
			_T("image file format such as PNG or JPG to ensure proper display. (Unimplemented tag types: %s)"),
			filename.c_str(), tags.c_str()));
	}

	// if we've reached the end of the file, we can release the decompressed
	// file stream buffer
	if (reader.BytesRemaining() == 0)
	{
		reader.Init(nullptr, 0);
		fileContents.reset(nullptr);
	}

	// successful completion
	return true;
}

D2D1_RECT_F SWFParser::SWFReader::ReadRect()
{
	// read the number of bits per element
	StartBitField();
	int nBits = ReadUB(5);

	// read the four elements, converting from twips to pixels
	D2D1_RECT_F rc;
	rc.left = static_cast<float>(ReadSB(nBits)) / 20.0f;
	rc.right = static_cast<float>(ReadSB(nBits)) / 20.0f;
	rc.top = static_cast<float>(ReadSB(nBits)) / 20.0f;
	rc.bottom = static_cast<float>(ReadSB(nBits)) / 20.0f;

	// return the rectangle
	return rc;
}

UINT32 SWFParser::SWFReader::ReadUB(int nBits)
{
	// read blocks of bits until we fill the request
	UINT32 ret = 0;
	while (nBits != 0)
	{
		// if the bit cache is empty, load a new byte
		if (bitCache.nBits == 0)
		{
			bitCache.b = ReadByte();
			bitCache.nBits = 8;
		}

		// Fetch the number of requested bits or the number of
		// available bits, whichever is lower
		int nFetch = nBits < bitCache.nBits ? nBits : bitCache.nBits;
		BYTE cur = bitCache.b;

		// Bits come out of 'b' from the high end first, so
		// shift these bits to right-align them in 'cur'
		cur >>= 8 - nFetch;

		// Now shift the fetched bits into the result
		ret <<= nFetch;
		ret |= cur;

		// deduct the bits read from the remaining to read
		nBits -= nFetch;

		// Shift the bits out of the cache, leaving the remaining
		// cached bits at the high end of the cache byte
		bitCache.b <<= nFetch;
		bitCache.nBits -= nFetch;
	}

	// return the result
	return ret;
}

// read an SWF varying-bit signed int
INT32 SWFParser::SWFReader::ReadSB(int nBits)
{
	// read the unsigned value
	UINT32 u = ReadUB(nBits);

	// if it's negative, sign-extend it to 32 bits
	if ((u & (1 << (nBits - 1))) != 0)
	{
		// start with a UINT32 set to all '1' bits
		UINT32 ext = ~0;

		// shift it left by the size of the fetched bits, which
		// will pad it with 0 bits in all of the significant
		// bit positions in 'u'
		ext <<= nBits;

		// OR the two together - this will preserve all of the
		// significant bits of 'u', and fill in all of the
		// remaining higher-order bits with '1' bits
		u |= ext;
	}

	// reinterpret it as a signed 32-bit value
	return (INT32)u;
}

// read an SWF fixed-point varying-bit-length value
float SWFParser::SWFReader::ReadFB(int nBits)
{
	// SWF fixed-point values use a signed 16.16 representation.
	// Effectively, it represents the numerator of a fraction with
	// a denominator of 2^16 (65536).
	return static_cast<float>(static_cast<double>(ReadSB(nBits)) / 65536.0);
}

// read a single bit from the stream
BYTE SWFParser::SWFReader::ReadBit()
{
	// fetch the next input stream byte if we're out of cached bits
	if (bitCache.nBits == 0)
	{
		bitCache.b = ReadByte();
		bitCache.nBits = 8;
	}

	// shift out the high bit
	BYTE bit = (bitCache.b & 0x80) >> 7;
	bitCache.b <<= 1;
	--bitCache.nBits;

	// return the bit
	return bit;
}

// read an SWF string
TSTRING SWFParser::UncompressedReader::ReadString()
{
	// read the bytes of the string
	CSTRING buf;
	for (BYTE b = ReadByte(); b != 0; buf.push_back(b), b = ReadByte());

	// The interpretation of the bytes varies by file format version:
	//
	//   SWF <= 5  -> ANSI string
	//   SWF >= 6  -> UTF-8
	//
	// In either case, we just need to translate from the single-byte
	// CSTRING type to our native TSTRING type, which we can do with
	// StringUtil macros.  Translating ANSI to TSTRING is especially
	// easy because we have a macro specifically for that task.  Going
	// from UTF8 to TSTRING requires an intermediate conversion from
	// UTF8 to UTF16 via AnsiToWide().  From there we can get to the
	// native TSTRING representation (whatever that currently is for
	// this build) with WSTRINGToTSTRING().
	if (fileFormatVersion <= 5)
		return CSTRINGToTSTRING(buf);
	else
		return WSTRINGToTSTRING(AnsiToWide(buf.c_str(), CP_UTF8));
}

// Read an SWF tag header
SWFParser::TagHeader SWFParser::UncompressedReader::ReadTagHeader()
{
	// Read the next UI16, and decode it: the high 10 bits are the
	// tag ID, and the low 6 bits are the length.  A length of 0x3f
	// is special: it flags a long tag, which has a spearate UI32
	// length field following the UI16.
	UINT code = ReadUInt16();
	TagHeader h{ code >> 6, code & 0x3f };
	if (h.len == 0x3f)
		h.len = ReadUInt32();

	// return the tag header
	return h;
};

// read RGB color records in various formats
SWFParser::RGBA SWFParser::UncompressedReader::ReadRGB()
{
	RGBA rgba;
	rgba.r = ReadByte();
	rgba.g = ReadByte();
	rgba.b = ReadByte();
	return rgba;
}

SWFParser::RGBA SWFParser::UncompressedReader::ReadRGBA()
{
	RGBA rgba;
	rgba.r = ReadByte();
	rgba.g = ReadByte();
	rgba.b = ReadByte();
	rgba.a = ReadByte();
	return rgba;
}

SWFParser::RGBA SWFParser::UncompressedReader::ReadARGB()
{
	RGBA rgba;
	rgba.a = ReadByte();
	rgba.r = ReadByte();
	rgba.g = ReadByte();
	rgba.b = ReadByte();
	return rgba;
}

SWFParser::MATRIX SWFParser::UncompressedReader::ReadMatrix()
{
	MATRIX m;
	StartBitField();

	if (ReadBit())
	{
		int nScaleBits = ReadUB(5);
		m.scaleX = ReadFB(nScaleBits);
		m.scaleY = ReadFB(nScaleBits);
	}

	if (ReadBit())
	{
		int nRotateBits = ReadUB(5);
		m.rotateSkew0 = ReadFB(nRotateBits);
		m.rotateSkew1 = ReadFB(nRotateBits);
	}

	int nTranslateBits = ReadUB(5);
	m.translateX = static_cast<float>(ReadSB(nTranslateBits)) / 20.0f;
	m.translateY = static_cast<float>(ReadSB(nTranslateBits)) / 20.0f;

	return m;
}

// read a Color Transform (CXFORM) record
SWFParser::CXFORM SWFParser::UncompressedReader::ReadCXFORM(bool hasAlpha)
{
	CXFORM c;
	StartBitField();

	bool hasAddTerms = ReadBit();
	bool hasMultTerms = ReadBit();
	int nBits = ReadUB(4);

	if (hasMultTerms)
	{
		c.redMult = ReadSB(nBits);
		c.greenMult = ReadSB(nBits);
		c.blueMult = ReadSB(nBits);
		if (hasAlpha)
			c.alphaMult = ReadSB(nBits);
	}

	if (hasAddTerms)
	{
		c.redAdd = ReadSB(nBits);
		c.greenAdd = ReadSB(nBits);
		c.blueAdd = ReadSB(nBits);
		if (hasAlpha)
			c.alphaAdd = ReadSB(nBits);
	}

	return c;
}

// read a DefineBits record
void SWFParser::UncompressedReader::ReadDefineBits(Dictionary &dict, TagHeader &tagHdr)
{
	// remember the starting point in the stream, so that we can
	// figure how much we've consumed so far at any later point
	size_t startRem = this->rem;

	// read the character ID
	UINT16 charId = ReadUInt16();

	// create the dictionary entry
	auto imageBits = new ImageBits();
	dict.emplace(charId, imageBits);

	// assume that the entire remainder of the record is the image data
	UINT32 imageDataLen = tagHdr.len - 2;

	// For DefineBitsJPGE3 (35) and DefineBitsJPEG4 (90), read the
	// AlphaDataOffset field.  This gives the offset from the start
	// of the image data to the start of the alpha data, which is
	// equivalent to the length of the image data.
	if (tagHdr.id == 35)
		imageDataLen = ReadUInt32();

	// for DefineBitsJPEG4 (90), read the deblocking filter field
	if (tagHdr.id == 90)
		imageBits->deblockParam = static_cast<float>(ReadUInt16()) / 256.0f;

	// allocate the image data
	imageBits->imageDataSize = imageDataLen;
	auto p = new BYTE[tagHdr.len];
	imageBits->imageData.reset(p);

	// Per the SWF spec: "Before version 8 of the SWF file format, SWF files
	// could contain an erroneous header of FF D9 FF D8 before the JPEG SOI
	// marker [FF D8]."  Read the first 6 bytes to check for this and remove
	// the extra 4 bytes.
	UINT32 imageBytesToRead = imageDataLen;
	if (imageBytesToRead >= 6 && fileFormatVersion < 8)
	{
		// scan for the errant header
		ReadBytes(p, 6);
		imageBytesToRead -= 6;
		if (memcmp(p, "\xFF\xD9\xFF\xD8\xFF\xD8", 6) == 0)
		{
			// remove the first four bytes
			p[0] = p[4];
			p[1] = p[5];
			imageBits->imageDataSize -= 4;

			// now keep the remaining two bytes from the read
			p += 2;
		}
		else
		{
			// keep all six bytes we've read so far
			p += 6;
		}
	}

	// read the rest of the image bytes
	ReadBytes(p, imageBytesToRead);

	// For tag DefineBites (6), the image data is the image portion
	// of a JPEG, with the common encoding tables.
	//
	// For tag DefineBitsJPEG2 (22), the image data can contain
	// a full JPEG stream with encoding tables, a PNG stream, or
	// a GIF89a stream (with no animation).
	if (tagHdr.id == 6)
	{
		// DefineBits - always a JPEG image section
		imageBits->type = ImageBits::Type::JPEGImageData;
	}
	else if (tagHdr.id == 21)
	{
		// DefineBitsJPEG2 - sense the type based on the file
		// signature at the start of the stream
		if (imageBits->imageDataSize >= 8 && memcmp(imageBits->imageData.get(), "\x89\x50\x4E\x47\x0D\0x0A\x1A\x0A", 8) == 0)
			imageBits->type = ImageBits::Type::PNG;
		else if (imageBits->imageDataSize >= 6 && memcmp(imageBits->imageData.get(), "\x47\x49\x46\x38\x39\x61", 6) == 0)
			imageBits->type = ImageBits::Type::GIF89a;
		else
			imageBits->type = ImageBits::Type::JPEG;
	}

	// Read the alpha data, if present.  This only applies to full JPEG
	// images, and only to DefineBitsJPEG3 and DefineBitsJPEG4.
	size_t bytesConsumed = startRem - rem;
	if (bytesConsumed < tagHdr.len
		&& imageBits->type == ImageBits::Type::JPEG
		&& (tagHdr.id == 35 || tagHdr.id == 90))
	{
		// figure the image size
		ImageFileDesc desc;
		if (GetImageBufInfo(imageBits->imageData.get(), imageBits->imageDataSize, desc))
		{
			// Allocate a decompression buffer based on the image size.  The
			// alpha image is width*height bytes.
			imageBits->alphaDataSize = desc.size.cx * desc.size.cy;
			imageBits->alphaData.reset(new BYTE[imageBits->alphaDataSize]);

			// set up a ZLIB reader on the buffer segment containing the
			// compressed alpha data, and decompress the data
			ZlibReader zr(this->p, min(this->rem, tagHdr.len - bytesConsumed));
			zr.ReadBytes(imageBits->alphaData.get(), imageBits->alphaDataSize);
		}
	}
}

// read a PlaceObject record
void SWFParser::UncompressedReader::ReadPlaceObject(DisplayList &displayList, UINT32 len)
{
	// remember the starting point, so we can calculate how many
	// bytes we've used so far at any point later
	size_t startingRem = rem;

	// read the character ID and depth
	auto charId = ReadUInt16();
	auto depth = ReadUInt16();

	// emplace the display list item at the specified depth
	auto &p = displayList.emplace(std::piecewise_construct, std::forward_as_tuple(depth), std::forward_as_tuple()).first->second;
	p.charId = charId;
	p.depth = depth;

	// read the positioning matrix
	p.matrix = ReadMatrix();

	// if we have any data left, there's a CXFORM record
	size_t bytesUsed = startingRem - rem;
	if (len - bytesUsed > 0)
		p.cxform = ReadCXFORM(false);

}

void SWFParser::UncompressedReader::SkipClipActions()
{
	// read the fixed header fields (UI16 reserved, UI16/UI32 All Event Flags)
	ReadUInt16(); // reserved, must be 0
	fileFormatVersion <= 5 ? ReadUInt16() << 16 : ReadUInt32();

	// read CLIPACTIONRECORD elements until we encounter the end flags (0)
	for (;;)
	{
		// read the next flags UI16/UI32
		UINT32 flags = fileFormatVersion <= 5 ? ReadUInt16() << 16 : ReadUInt32();
		if (flags == 0)
			break;

		// read the record size (number of bytes in record following
		// this length prefix)
		UINT32 len = ReadUInt32();

		// skip the rest of the field
		SkipBytes(len);
	}
}

void SWFParser::UncompressedReader::ReadPlaceObject2(DisplayList &displayList, UINT32 len)
{
	// Decode the flags.  (For efficiency, read them as a full byte
	// and mask bit-by-bit.  This is a little faster than going through
	// the bit cache reader, since that needs to do a bunch of variable
	// bit shifts - we know up front the shift distances needed so we
	// can do them with faster constant shifts.)
	BYTE flags = ReadByte();
	bool hasClipActions = fileFormatVersion >= 5 && (flags & 0x80) != 0;
	bool hasClipDepth = (flags & 0x40) != 0;
	bool hasName = (flags & 0x20) != 0;
	bool hasRatio = (flags & 0x10) != 0;
	bool hasColorTransform = (flags & 0x08) != 0;
	bool hasMatrix = (flags & 0x04) != 0;
	bool hasCharacter = (flags & 0x02) != 0;
	bool moveFlag = (flags & 0x01) != 0;

	// read the depth
	auto depth = ReadUInt16();

	// Figure out whether we're creating or modifying a display list entry
	PlaceObject *p = nullptr;
	if (!moveFlag && hasCharacter)
	{
		// creating a new object
		p = &displayList.emplace(std::piecewise_construct, std::forward_as_tuple(depth), std::forward_as_tuple()).first->second;
	}
	else if (moveFlag && !hasCharacter)
	{
		// modifying the existing object
		if (auto it = displayList.find(depth); it != displayList.end())
			p = &it->second;
	}
	else if (moveFlag && hasCharacter)
	{
		// replacing an existing object - delete the object if it exists
		if (auto it = displayList.find(depth); it != displayList.end())
			displayList.erase(it);

		// emplace the new object
		p = &displayList.emplace(std::piecewise_construct, std::forward_as_tuple(depth), std::forward_as_tuple()).first->second;
	}

	// If we didn't identify an object to create/modify/replace, abort.
	// This shouldn't happen in a well-formed SWF file, but we shouldn't
	// assume that every SWF is well-formed.
	if (p == nullptr)
		return;

	// set the depth, in case the object was newly created
	p->depth = depth;

	// read the fields
	if (hasCharacter)
		p->charId = ReadUInt16();
	if (hasMatrix)
		p->matrix = ReadMatrix();
	if (hasColorTransform)
		p->cxform = ReadCXFORM(true);
	if (hasRatio)
		p->morphRatio = ReadUInt16();
	if (hasName)
		p->name = ReadString();
	if (hasClipDepth)
		p->clipDepth = ReadUInt16();
	if (hasClipActions)
		SkipClipActions();
}

// Read a GRADIENT element of a DefineShape tag
void SWFParser::UncompressedReader::ReadGradient(UINT16 tagId, GRADIENT &g)
{
	// read the initial byte and decode its bit fields
	BYTE b = ReadByte();
	g.spreadMode = static_cast<GRADIENT::SpreadMode>((b >> 6) & 0x03);
	g.interpolationMode = static_cast<GRADIENT::InterpolationMode>((b >> 4) & 0x03);
	int numGrads = b & 0x0f;

	// Enforce tag constraints: for DefineShape, DefineShape2, and 
	// DefineShape3 tags, the SpreadMode and InterpolationMode must
	// both be 0, and the number of gradients can't exceed 8.
	if (tagId == 2 || tagId == 22 || tagId == 32)
	{
		g.spreadMode = GRADIENT::SpreadMode::Pad;
		g.interpolationMode = GRADIENT::InterpolationMode::Normal;
		if (numGrads > 8)
			numGrads = 8;
	}

	// read the gradient records
	g.gradients.reserve(numGrads);
	for (int i = 0; i < numGrads; ++i)
	{
		auto &gr = g.gradients.emplace_back();
		gr.ratio = ReadByte();
		gr.color = (tagId == 2 || tagId == 22) ? ReadRGB() : ReadRGBA();
	}
}

// Read a focal gradient
void SWFParser::UncompressedReader::ReadFocalGradient(UINT16 tagId, GRADIENT &g)
{
	// read the base gradient record
	ReadGradient(tagId, g);

	// read the focal point
	g.focalPoint = static_cast<float>(ReadUInt16()) / 256.0f;
}

// read a FillStyle record
void SWFParser::UncompressedReader::ReadFillStyle(UINT16 tagId, FillStyle &f)
{
	// read the type
	f.type = static_cast<FillStyle::FillType>(ReadByte());

	// for Solid fill only, read the color; it's in RGB format
	// for DefineShape and DefineShape2, and RGBA format for
	// DefineShape3 and DefineShape4
	if (f.type == FillStyle::FillType::Solid)
		f.color = (tagId == 32 || tagId == 83) ? ReadRGBA() : ReadRGB();

	// for gradient fills, read the gradient matrix and gradient
	if (f.type == FillStyle::LinearGradient
		|| f.type == FillStyle::RadialGradient
		|| f.type == FillStyle::FocalRadialGradient)
	{
		f.matrix = ReadMatrix();
		if (f.type == FillStyle::FocalRadialGradient)
			ReadFocalGradient(tagId, f.gradient);
		else
			ReadGradient(tagId, f.gradient);
	}

	// for bitmap fills, read the bitmap ID and matrix
	if (f.type == FillStyle::RepeatingBitmap
		|| f.type == FillStyle::ClippedBitmap
		|| f.type == FillStyle::NonSmoothedClippedBitmap
		|| f.type == FillStyle::NonSmoothedRepeatingBitmap)
	{
		f.bitmapId = ReadUInt16();
		f.matrix = ReadMatrix();
	}
}

// Read a FillStyles array
void SWFParser::UncompressedReader::ReadFillStylesArray(std::vector<FillStyle> &fillStyles, int tagId)
{
	// Read the count.  The count is one byte, but if the tag type
	// is DefineShape2 (22) or DefineShape3 (32), the special value 
	// 0xFF means that a UI16 follows with the *actual* count.
	int fillStyleCount = ReadByte();
	if (fillStyleCount == 0xff && (tagId == 22 || tagId == 32))
		fillStyleCount = ReadUInt16();

	// reserve space
	fillStyles.reserve(fillStyleCount);

	// read the style entries
	for (int i = 0; i < fillStyleCount; ++i)
		ReadFillStyle(tagId, fillStyles.emplace_back());
}

// Read a LineStyles array
void SWFParser::UncompressedReader::ReadLineStylesArray(std::vector<LineStyle> &lineStyles, int tagId)
{
	// Read the count.  This is a one-byte value, but if the tag
	// is DefineShape2 or DefineShape3, the special value 0xFF means
	// that the *actual* count is in a UI16 that follows.
	int lineStyleCount = ReadByte();
	if (lineStyleCount == 0xff && (tagId == 22 || tagId == 32))
		lineStyleCount = ReadUInt16();

	// read the line styles
	lineStyles.reserve(lineStyleCount);
	for (int i = 0; i < lineStyleCount; ++i)
	{
		// read the LINESTYLE (for DefineShape, DefineShape2, DefineShape3)
		// or LINESTYLE4 (for DefineShape3)
		auto &ls = lineStyles.emplace_back();
		ls.width = static_cast<float>(ReadUInt16()) / 20.0f;
		if (tagId == 83)
		{
			// LINESTYLE2
			BYTE flags = ReadByte();
			ls.startCapStyle = static_cast<LineStyle::CapStyle>((flags >> 6) & 0x03);
			ls.joinStyle = static_cast<LineStyle::JoinStyle>((flags >> 4) & 0x03);
			bool hasFill = (flags & 0x08) != 0;
			ls.noHScale = (flags & 0x04) != 0;
			ls.noVScale = (flags & 0x02) != 0;
			ls.pixelHinting = (flags & 0x01) != 0;

			BYTE flags2 = ReadByte();
			ls.noClose = (flags2 & 0x04) != 0;
			ls.endCapStyle = static_cast<LineStyle::CapStyle>(flags2 & 0x03);

			if (ls.joinStyle == LineStyle::JoinStyle::MiterJoin)
				ls.miterLimitFactor = static_cast<float>(ReadUInt16()) / 256.0f;

			if (hasFill)
				ls.color = ReadRGBA();
			else
				ReadFillStyle(tagId, ls.fillType);
		}
		else
		{
			// LINESTYLE
			ls.color = (tagId == 2 || tagId == 22) ? ReadRGB() : ReadRGBA();
		}
	}
}

// Read a DefineShape tag
void SWFParser::UncompressedReader::ReadDefineShape(Dictionary &dict, UINT16 tagId)
{
	// read the shape ID (== the character ID for the dictionary)
	UINT16 shapeId = ReadUInt16();

	// create the ShapeWithStyle object and add it to the dictionary
	auto shape = new ShapeWithStyle();
	dict.emplace(shapeId, shape);
	shape->tagId = tagId;

	// read the bounds
	shape->bounds = ReadRect();

	// read extra fields for DefineShape4
	if (tagId == 83)
	{
		shape->edgeBounds = ReadRect();

		BYTE flags = ReadByte();
		shape->usesFillWindingRule = (flags & 0x04) != 0;
		shape->usesNonScalingStrokes = (flags & 0x02) != 0;
		shape->usesScalingStrokes = (flags & 0x01) != 0;
	}

	// 
	// The rest of the file data is what SWF calls the ShapeWithStyles
	// array
	//

	// read the fill styles array
	ReadFillStylesArray(shape->fillStyles, tagId);

	// read the line styles array
	ReadLineStylesArray(shape->lineStyles, tagId);

	//
	// That's it for the styles; on to the Shape
	//

	// read the number of index bits for fill and line references
	int b = ReadByte();
	int numFillBits = (b >> 4);
	int numLineBits = (b & 0x0f);

	//
	// Read the Shape Records.  We have one or more; the last is
	// indicated by an End record.
	//
	StartBitField();
	for (;;)
	{
		// read the 6-bit flags field
		BYTE flags = ReadUB(6);
		if (flags == 0)
		{
			// End record - this terminates the shape record list
			break;
		}
		else if ((flags & 0x20) == 0)
		{
			// Style Change Record - decode the flags
			auto sr = new StyleChangeRecord();
			shape->shapeRecords.emplace_back(sr);
			sr->stateNewStyles = (tagId == 22 || tagId == 32) && (flags & 0x10) != 0;
			sr->stateLineStyle = (flags & 0x08) != 0;
			sr->stateFillStyle1 = (flags & 0x04) != 0;
			sr->stateFillStyle0 = (flags & 0x02) != 0;
			sr->stateMoveTo = (flags & 0x01) != 0;

			// read the Move To delta, if present
			if (sr->stateMoveTo)
			{
				int moveBits = ReadUB(5);
				sr->deltaX = static_cast<float>(ReadUB(moveBits)) / 20.0f;
				sr->deltaY = static_cast<float>(ReadUB(moveBits)) / 20.0f;
			}

			// read the fill style 0 and 1 indices if present
			if (sr->stateFillStyle0)
				sr->fillStyle0 = ReadUB(numFillBits);
			if (sr->stateFillStyle1)
				sr->fillStyle1 = ReadUB(numFillBits);

			// read the line style index if present
			if (sr->stateLineStyle)
				sr->lineStyle = ReadUB(numLineBits);

			// read the new style arrays
			if (sr->stateNewStyles)
			{
				// read the fill styles array
				ReadFillStylesArray(sr->fillStyles, tagId);

				// read the line styles array
				ReadLineStylesArray(sr->lineStyles, tagId);

				// read the new index bit sizes for subsequent styles
				BYTE b = ReadByte();
				numFillBits = (b >> 4) & 0x0F;
				numLineBits = b & 0x0F;

				// start a new bit field
				StartBitField();
			}
		}
		else
		{
			// Edge Record
			auto er = new EdgeRecord();
			shape->shapeRecords.emplace_back(er);
			er->straight = (flags & 0x10) != 0;
			int numBits = (flags & 0x0f) + 2;
			if (er->straight)
			{
				// straight edge
				er->general = ReadBit() != 0;
				if (!er->general)
					er->vert = ReadBit() != 0;

				er->deltaX = er->deltaY = 0.0f;
				if (er->general || !er->vert)
					er->deltaX = static_cast<float>(ReadSB(numBits)) / 20.0f;
				if (er->general || er->vert)
					er->deltaY = static_cast<float>(ReadSB(numBits)) / 20.0f;
			}
			else
			{
				// curved edge
				er->deltaX = static_cast<float>(ReadSB(numBits)) / 20.0f;
				er->deltaY = static_cast<float>(ReadSB(numBits)) / 20.0f;
				er->anchorX = static_cast<float>(ReadSB(numBits)) / 20.0f;
				er->anchorY = static_cast<float>(ReadSB(numBits)) / 20.0f;
			}
		}
	}

}

bool SWFParser::Render(HDC hdc, HBITMAP hbitmap, SIZE targetPixSize, ErrorHandler &eh)
{
	// HRESULT error handler - log it and return
	HRESULT hr = S_OK;
	auto HRError = [&hr, &eh](const TCHAR *where) {
		eh.SysError(_T("DirectWrite error drawing formatted text"), MsgFmt(_T("%s, HRESULT=%lx"), where, hr));
		return false;
	};

	// create a DC render target
	RefPtr<ID2D1DCRenderTarget> target;
	D2D1_RENDER_TARGET_PROPERTIES targetProps = {
		D2D1_RENDER_TARGET_TYPE_DEFAULT, { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED },
		0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
	};
	if (!SUCCEEDED(hr = d2dFactory->CreateDCRenderTarget(&targetProps, &target)))
		return HRError(_T("CreateDCRenderTarget"));

	// Bind the DC to the render target
	// graphics area, to allow for text that overhangs the layout rectangle.
	RECT rcTarget{ 0, 0, targetPixSize.cx, targetPixSize.cy };
	if (!SUCCEEDED(hr = target->BindDC(hdc, &rcTarget)))
		return HRError(_T("BindDC"));

	// open the drawing in the render target
	target->BeginDraw();

	// fill the frame with the background color
	RefPtr<ID2D1SolidColorBrush> br;
	if (!SUCCEEDED(hr = target->CreateSolidColorBrush(D2D1_COLOR_F{ bgColor.r / 255.0f, bgColor.g / 255.0f, bgColor.b / 255.0f, 1.0f }, &br)))
		return HRError(_T("Create default brush"));
	target->FillRectangle(D2D1_RECT_F{ 0.0f, 0.0f, static_cast<float>(targetPixSize.cx), static_cast<float>(targetPixSize.cy) }, br);

	// Build an index on the display list so that we can sort into
	// display order, by depth
	std::vector<PlaceObject*> displayListIndex;
	displayListIndex.reserve(displayList.size());
	for (auto &dp : displayList)
		displayListIndex.emplace_back(&dp.second);

	// sort in ascending order of depth
	std::sort(displayListIndex.begin(), displayListIndex.end(),
		[](const PlaceObject *a, const PlaceObject *b) { return a->depth < b->depth; });

	// figure the scaling size
	D2D1_POINT_2F scale{
		static_cast<float>(targetPixSize.cx) / frameRect.right,
		static_cast<float>(targetPixSize.cy) / frameRect.bottom 
	};

	// draw the display list
	Character::CharacterDrawingContext cdc{ this, target, scale };
	for (auto p : displayListIndex)
	{
		// get the character
		if (auto it = dict.find(p->charId); it != dict.end())
		{
			// draw the Character object
			Character *cp = it->second.get();
			cp->Draw(cdc, p);
		}
	}

	// close drawing in the render target
	if (!SUCCEEDED(hr = target->EndDraw()))
		return HRError(_T("EndDraw"));

	// successful completion
	return true;
}

void SWFParser::ShapeWithStyle::Draw(CharacterDrawingContext &cdc, PlaceObject *po)
{
	// set up the shape drawing context, starting with the style arrays
	// defined at the shape-with-style level
	ShapeRecord::ShapeDrawingContext sdc{ cdc, this, po, fillStyles, lineStyles };

	// start drawing at the shape origin
	sdc.pt = { 0.0f, 0.0f };

	// Draw each shape record.  This won't actually render anything yet; it
	// just populates the style-keyed line and edge maps in the drawing context.
	for (auto &sp : shapeRecords)
		sp->Draw(sdc);

	// Now render the shapes in the style maps
	sdc.RenderMaps();
}

void SWFParser::ImageBits::Draw(CharacterDrawingContext &dc, PlaceObject *po)
{
	std::unique_ptr<BYTE> tmpImageStream;
	BYTE *imageStream = nullptr;
	size_t imageStreamLen = 0;

	switch (type)
	{
	case Type::JPEGImageData:
		// This is a "DefineBits" record, which only has the pixel section of the
		// JPEG file.  We need to combine this with the common JPEG file header
		// from the "JPEGTables" record.  The JPEGTables record has an extra
		// end-of-image tag (FF D9) at the end, and the DefineBits record has
		// an extra IOS tag (FF D8) at the beginning.  So to merge them, we just
		// need to lop off the last two bytes of the tables record and the
		// first two bytes of the pixel record, and concatenate the results.
		tmpImageStream.reset(new BYTE[dc.parser->jpegTables.len + imageDataSize - 4]);
		imageStream = tmpImageStream.get();
		imageStreamLen = dc.parser->jpegTables.len - 2;
		memcpy(imageStream, dc.parser->jpegTables.data.get(), imageStreamLen);
		memcpy(imageStream + imageStreamLen, imageData.get() + 2, imageDataSize - 2);
		imageStreamLen += imageDataSize - 2;
		break;

	default:
		// for other types, our record contains the full image stream
		imageStream = imageData.get();
		imageStreamLen = imageDataSize;
		break;
	}

	// if we have an image stream, create the WIC object
	if (imageStream != nullptr)
	{
		// create a memory stream on the image data
		RefPtr<IStream> istream(SHCreateMemStream(imageStream, static_cast<UINT>(imageStreamLen)));

		// create a WIC decoder
		RefPtr<IWICBitmapDecoder> decoder;
		RefPtr<IWICBitmapFrameDecode> frameDec;
		RefPtr<IWICFormatConverter> converter;
		RefPtr<ID2D1Bitmap> bitmap;
		if (SUCCEEDED(wicFactory->CreateDecoderFromStream(istream, NULL, WICDecodeMetadataCacheOnDemand, &decoder))
			&& SUCCEEDED(decoder->GetFrame(0, &frameDec))
			&& SUCCEEDED(wicFactory->CreateFormatConverter(&converter))
			&& SUCCEEDED(converter->Initialize(frameDec, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0f, WICBitmapPaletteTypeCustom))
			&& SUCCEEDED(dc.target->CreateBitmapFromWicBitmap(converter, &bitmap)))
			dc.target->DrawBitmap(bitmap);
	}
}

void SWFParser::ShapeRecord::ShapeDrawingContext::RenderMaps()
{
	// draw the fills, from the fill style map
	for (auto &pair : fillEdges)
	{
		// get the fill style
		auto fillStyle = reinterpret_cast<FillStyle*>(pair.first);

		// create a brush for it
		RefPtr<ID2D1SolidColorBrush> brush;
		if (!SUCCEEDED(chardc.target->CreateSolidColorBrush(fillStyle->color.ToD2D(), &brush)))
			continue;

		// Set up the geometry list.  We're going to defer the fill until
		// we've collected all of the paths, at which point we'll create
		// a Geometry Group and do the fill at the group level.  That
		// will properly handle donut holes.
		std::list<RefPtr<ID2D1PathGeometry>> paths;

		// visit each segment
		RefPtr<ID2D1PathGeometry> path;
		RefPtr<ID2D1GeometrySink> geomSink;
		D2D1_POINT_2F pt{ 0.0f, 0.0f };
		D2D1_POINT_2F startPt{ 0.0f, 0.0f };
		for (auto& seg : pair.second)
		{
			// if the new segment isn't continuous with the last one, draw the last one
			if (path != nullptr && (seg.start.x != pt.x || seg.start.y != pt.y))
			{
				// end the figure
				geomSink->EndFigure(startPt.x == pt.x && startPt.y == pt.y ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
				geomSink->Close();
				//chardc.target->FillGeometry(path, brush);

				// forget it - we need to start a new one
				path = nullptr;
				geomSink = nullptr;
			}

			// open a new path if necessary
			if (path == nullptr)
			{
				// create the geometry and open the path
				if (!(SUCCEEDED(d2dFactory->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&geomSink))))
					break;

				// add it to the geometry list
				paths.emplace_back(path, RefCounted::DoAddRef);

				// start the figure at the segment starting point
				geomSink->BeginFigure(TargetCoords(seg.start), D2D1_FIGURE_BEGIN_FILLED);
				startPt = pt = seg.start;
			}

			// add this segment
			if (seg.straight)
				geomSink->AddLine(TargetCoords(seg.end));
			else
				geomSink->AddQuadraticBezier(D2D1_QUADRATIC_BEZIER_SEGMENT{ TargetCoords(seg.control), TargetCoords(seg.end) });

			// move to the end of the segment
			pt = seg.end;
		}

		// Finish the last path, if we didn't close it out.  Don't fill
		// it yet - we'll defer that until we've collected all of the paths,
		// at which point we'll fill them together as a group, so that we
		// respect embedded donut holes.
		if (geomSink != nullptr)
		{
			geomSink->EndFigure(startPt.x == pt.x && startPt.y == pt.y ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
			geomSink->Close();
		}

		// Create a geometry group from the path collection, and apply the fill
		// style to the entire group.
		RefPtr<ID2D1GeometryGroup> group;
		std::unique_ptr<ID2D1Geometry*> geometries(new ID2D1Geometry*[paths.size()]);
		{
			int i = 0;
			for (auto &path : paths)
				geometries.get()[i++] = path;
		}
		if (SUCCEEDED(d2dFactory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, geometries.get(), static_cast<UINT32>(paths.size()), &group)))
			chardc.target->FillGeometry(group, brush);
	}

	// Now draw the outlines, from the line style map.  This will draw
	// the outlines on top of the fill, which is how they'd appear if
	// we had D2D draw the fill and outline together.
	for (auto &pair : lineEdges)
	{
		// get the line style
		auto lineStyle = reinterpret_cast<LineStyle*>(pair.first);

		// create a brush for it
		RefPtr<ID2D1SolidColorBrush> brush;
		if (!SUCCEEDED(chardc.target->CreateSolidColorBrush(lineStyle->color.ToD2D(), &brush)))
			continue;

		// visit each segment
		RefPtr<ID2D1PathGeometry> path;
		RefPtr<ID2D1GeometrySink> geomSink;
		D2D1_POINT_2F pt{ 0.0f, 0.0f };
		D2D1_POINT_2F startPt{ 0.0f, 0.0f };
		for (auto& seg : pair.second)
		{
			// if the new segment isn't continuous with the last one, draw the last one
			if (path != nullptr && (seg.start.x != pt.x || seg.start.y != pt.y))
			{
				// close the figure and draw the path
				geomSink->EndFigure(startPt.x == pt.x && startPt.y == pt.y ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
				geomSink->Close();
				chardc.target->DrawGeometry(path, brush, lineStyle->width);

				// forget it - we need to start a new one
				path = nullptr;
				geomSink = nullptr;
			}

			// open a new path if necessary
			if (path == nullptr)
			{
				// create the geometry and open the path
				if (!(SUCCEEDED(d2dFactory->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&geomSink))))
					break;

				// start the figure at the segment starting point
				geomSink->BeginFigure(TargetCoords(seg.start), D2D1_FIGURE_BEGIN_HOLLOW);
				startPt = pt = seg.start;
			}

			// add this segment
			if (seg.straight)
				geomSink->AddLine(TargetCoords(seg.end));
			else
				geomSink->AddQuadraticBezier(D2D1_QUADRATIC_BEZIER_SEGMENT{ TargetCoords(seg.control), TargetCoords(seg.end) });

			// move to the end of the segment
			pt = seg.end;
		}

		// draw the last path, if we didn't close it out
		if (geomSink != nullptr)
		{
			geomSink->EndFigure(startPt.x == pt.x && startPt.y == pt.y ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
			geomSink->Close();
			chardc.target->DrawGeometry(path, brush, lineStyle->width);
		}
	}
}

void SWFParser::ShapeRecord::ShapeDrawingContext::AddEdge(EdgeMap &map, void *stylePtr, const Segment &seg, bool reversed)
{
	// ignore null styles
	if (stylePtr == nullptr)
		return;

	// look up the style
	auto styleKey = reinterpret_cast<UINT_PTR>(stylePtr);
	auto it = map.find(styleKey);
	if (it == map.end())
	{
		// there's no entry yet - create one
		it = map.emplace(std::piecewise_construct, std::forward_as_tuple(styleKey), std::forward_as_tuple()).first;
	}

	// add the edge to the style's list
	if (reversed)
		it->second.emplace_front(seg);
	else
		it->second.emplace_back(seg);
}

void SWFParser::EdgeRecord::Draw(ShapeDrawingContext &sdc)
{
	// set up the line segment description
	ShapeDrawingContext::Segment seg;
	seg.straight = straight;
	seg.start = sdc.pt;

	// figure the next position	D2D1_POINT_2F ptNext = sdc.pt;
	sdc.pt.x += deltaX;
	sdc.pt.y += deltaY;

	if (straight)
	{
		// straight line - the next point is the end position
		seg.end = sdc.pt;
	}
	else
	{
		// curve - the initial delta gave us the control point position
		seg.control = sdc.pt;

		// now figure the anchor position
		sdc.pt.x += anchorX;
		sdc.pt.y += anchorY;
		seg.end = sdc.pt;
	}

	// add the edge to the line and fill collections for the respective current styles
	sdc.AddEdge(sdc.lineEdges, sdc.curLineStyle, seg, false);
	sdc.AddEdge(sdc.fillEdges, sdc.curFillStyle0, seg, false);
	sdc.AddEdge(sdc.fillEdges, sdc.curFillStyle1, seg, false);
}

void SWFParser::StyleChangeRecord::Draw(ShapeDrawingContext &sdc)
{
	// switch to the new style arrays if desired
	if (stateNewStyles)
	{
		sdc.fillStyles = fillStyles;
		sdc.lineStyles = lineStyles;
	}

	// set the current line style
	if (stateLineStyle)
		sdc.curLineStyle = lineStyle > 0 && lineStyle <= sdc.lineStyles.size() ? &sdc.lineStyles[lineStyle - 1] : nullptr;
	
	// set the current fill styles
	if (stateFillStyle0)
		sdc.curFillStyle0 = fillStyle0 > 0 && fillStyle0 <= sdc.fillStyles.size() ? &sdc.fillStyles[fillStyle0 - 1] : nullptr;
	if (stateFillStyle1)
		sdc.curFillStyle1 = fillStyle1 > 0 && fillStyle1 <= sdc.fillStyles.size() ? &sdc.fillStyles[fillStyle1 - 1] : nullptr;

	// move the pen
	if (stateMoveTo)
		sdc.pt = { deltaX, deltaY };
}
