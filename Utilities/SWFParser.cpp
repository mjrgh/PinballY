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
// That file is explicitly not licensed for redistribution, so I
// can't include it in the PinballY repository in case Adobe removes
// it from their site in the future (which doesn't seem unlikely,
// since their position on SWF is that it's dead).  If you can't
// the Adobe spec, there are a number of third-party descriptions of
// the SWF format available.  There are also a number of open-source 
// projects that at least parse the files, and in some cases render
// it; these might be better references than any of the documentary
// accounts, since evne the Adobe spec is awfully sketchy on the
// details; a lot of SWF behavior seems to just be a matter of what
// Flash Player happens to do.  The most complete open-source
// examples I've found are gameswf and swf2js.  The Google Swiffy
// project is probably more complete than eithr of those, but it
// appears to have been erased from the Web, in keeping with the
// general sentiment that SWF is better forgotten.
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
			d2dFactory = nullptr;
			wicFactory = nullptr;
			return false;
		};

		// create the D2D factory
		d2dFactory = nullptr;
		if (!SUCCEEDED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &d2dFactory)))
			return HRError(_T("D2D1CreateFactory"));

		// retrieve the system default locale name
		if (GetSystemDefaultLocaleName(locale, countof(locale)) == 0)
			wcscpy_s(locale, L"en-US");

		// initialize a WIC factory
		wicFactory = nullptr;
		if (!SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory))))
			return HRError(_T("CoCreateInstance(WICImagingFactory)"));

		// we're now initialized
		inited = true;
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

		case 5:   // RemoveObject
			reader.ReadRemoveObject(displayList, tagHdr);
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
			reader.ReadDefineFont(dict, tagHdr);
			break;

		case 11:  // DefineText
			reader.ReadDefineText(dict, tagHdr);
			break;

		case 13:  // DefineFontInfo
			// Not implemented - ignore.  This is used to provide a local device
			// font mapping for an SWF glyph font.  To keep things simple and to
			// make the display more consistent, we always display the SWF glyphs.
			break;

		case 33:  // DefineText2
			reader.ReadDefineText(dict, tagHdr);
			break;

		case 62:  // DefineFontInfo2
			// Not implemented - ignore.  We only display SWF glyphs.
			break;

		case 73:  // DefineFontAlignmentZones
			// Not implemented - ignore.
			break;

		case 88:  // DefineFontName
			// Not implemented - ignore.
			break;

		case 20:  // DefineBitsLossless
			reader.ReadDefineBitsLossless(dict, tagHdr);
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

		case 28:   // RemoveObject2
			reader.ReadRemoveObject(displayList, tagHdr);
			break;

		case 32:  // DefineShape3
			reader.ReadDefineShape(dict, 32);
			break;

		case 35:  // DefineBitsJPEG3
			reader.ReadDefineBits(dict, tagHdr);
			break;

		case 36:  // DefineBitsLossless2
			reader.ReadDefineBitsLossless(dict, tagHdr);
			break;

		case 39:  // DefineSprite
			// not implemented - ignore
			break;

		case 43:  // FrameLabel
			// used for scripting - ignore
			break;

		case 48:  // DefineFont2
			reader.ReadDefineFont(dict, tagHdr);
			break;

		case 56:  // ExportAssets
			// This is used to share assets such as shapes, fonts, or bitmaps
			// with other SWF files that are part of the same Web site.  We
			// don't implement the ImportAssets counterpart tag, so for our
			// purposes, exports will never be consumed, so we can silently
			// ignore them.
			break;

		case 57:  // ImportAssets
			// not implemented - ignore
			break;

		case 58:  // Enable Debugger
			// debugger not implemented - ignore
			break;

		case 64:  // EnableDebugger2
			// debugger not implemented - ignore
			break;

		case 65:  // ScriptLimits
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
			reader.ReadDefineFont(dict, tagHdr);
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

		case 90:  // DefineBitsJPEG4
			reader.ReadDefineBits(dict, tagHdr);
			break;

	    // Some tag types that are explicitly not handled, but which we might
		// wish to implement in the future if we find extant instruction cards
		// that rely on them.  (That much is true for *any* unimplemented tag;
		// it's just that these are some candidates I consider most likely to
		// show up in extant cards.)  I'm just breaking these out here for
		// the sake of documenting the tag IDs; they'll go into the log file
		// warning about unimplemented tags, as will any other tags without
		// explicit cases in the switch that fall through to 'default:'.
		// This is in contrast to the many "not implemented - ignore" cases
		// in the switch: those are cases that I've deliberately decided
		// not to implement because they're either not relevant to the
		// limited subset of functionality we wish to support (e.g., I won't
		// anything related to scripting) or because they're newer or more
		// "advanced" tags that I don't expect any of the instruction cards
		// to use.  A "not implemented" notice doesn't mean that I definitely
		// will never implement the tags; it just means that I don't expect
		// there will ever be any call for it within the scope of this
		// project, so I don't want to generate unnecessary error messages
		// for files that happen to incorporate those tags.
		case 70:  // PlaceObject3
		case 91:  // DefineFont4
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
		cur >>= (8 - nFetch);

		// Now shift the fetched bits into the result
		ret <<= nFetch;
		ret |= (cur & (0xFF >> (8 - nFetch)));

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
	auto imageBits = new LossyImageBits(tagHdr.id, charId);
	dict.emplace(charId, imageBits);

	// assume that the entire remainder of the record is the image data
	UINT32 imageDataLen = tagHdr.len - 2;

	// For DefineBitsJPGE3 (35) and DefineBitsJPEG4 (90), read the
	// AlphaDataOffset field.  This gives the offset from the start
	// of the image data to the start of the alpha data, which is
	// equivalent to the length of the image data.
	if (tagHdr.id == 35 || tagHdr.id == 90)
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

	// *** Mystery hack to fix broken JPEGs ***
	//
	// Some of the instruction cards contain ill-formed JPEG streams
	// that WIC rejects (as does libjpeg, Chrome, and several image
	// editor tools I've tried).  I suspect this is of a kind with
	// the weird extra initial EOI-SOI sequence mentioned in the SWF
	// spec for old SWF files, but the SWF spec is silent about this
	// considerably more elaborate error.  The specific problem I've
	// observed is that some JPEG streams have their APP0 frame buried
	// in the middle of the stream, rather than coming directly after
	// SOI as required in all modern JPEG/JFIF specs.  These streams
	// are otherwise valid (they're entirely made up of JFIF frames,
	// with valid frame types, and contain all of the required frame
	// types), so my guess is that these files were produced by very
	// old tools that either pre-dated the 1992 publication of the
	// JPEG specs, or just didn't bother to comply.  In any case, we
	// can make these acceptable to WIC by rearranging the frames
	// into the proper order; doing so makes them display properly,
	// so the ordering really does seem to be the only problem.
	// Apart from the misplaced APP0 frame, these files also might
	// contain an extra EOI-SOI just before the APP0 - that's what
	// makes me think it's related to the weirdness mentioned in the
	// SWF spec with the possibility of an extra EOI-SOI at the very
	// start of the stream.
	//
	// To correct this, scan the first 4 bytes to see if they look
	// like a JPEG file at all, and if so, if they contain SOI-APP0
	// as required.  If the first three bytes are right but the 4th
	// is a different marker type (not APP0), then do a deeper scan
	// of the file to see if it consists entirely of properly marked
	// JFIF frames, and if so, if we can find an APP0 frame later on.
	// If we find the APP0 frame, shuffle the stream around so that
	// the APP0 comes just after the SOI marker, and the part that
	// was between SOI and APP0 is moved after the APP0.  That'll
	// put the whole stream in the right order so that WIC can read
	// it and display the image.
	//
	// What's really not clear to me is why Flash Player can handle
	// these JFIF streams when not a single other tool seems to be
	// able to.  They must either do something like this same hack
	// I'm doing, or they're just using a custom JPEG decoder that
	// doesn't enforce the APP0 ordering rule.  I don't think there's
	// any practical implementation reason that a JPEG decoder
	// couldn't ignore the ordering, so perhaps all of the more
	// modern systems I've tested against are just being strict
	// about following the spec.
	p = imageBits->imageData.get();
	if (memcmp(p, "\xFF\xD8\xFF", 3) == 0 && p[4] != 0xE0)
	{
		// scan the JFIF frames until we find APP0 (FF E0) or run
		// out of segments
		BYTE *endp = p + imageBits->imageDataSize;
		for (BYTE *framep = p; framep < endp; )
		{
			// make sure it's a frame marker - if not, stop scanning,
			// as the stream if either not JFIF at all or is broken
			// beyond our ability to repair
			if (framep[0] != 0xff)
				break;

			// check for APP0
			if (framep[1] == 0xE0)
			{
				// This is the one.  Let's reassemble the file in an order
				// that WIC will accept:
				//
				//  SOI   (FF D8)
				//  APP0  (FF E0 frame)
				//  <the fragment that was originally between SOI and APP0>
				//  <the rest of stream after APP0>

				// allocate a new buffer for the reassembled file
				BYTE *newp = new BYTE[imageBits->imageDataSize];

				// copy the SOI
				newp[0] = 0xFF;
				newp[1] = 0xD8;

				// copy the APP0 frame
				BYTE *app0 = framep;
				size_t app0Len = (app0[2] << 16) | (app0[3]) + 2;
				memcpy(newp + 2, app0, app0Len);

				// pull out the fragment that was originally misplaced between the 
				// SOI and APP0 frame
				BYTE *fragp = p + 2;
				size_t fragLen = framep - fragp;

				// If the misplaced fragment has an EOI-SOI sequence at the end, delete
				// it.  I suspect that the source of the errant JPEG streams was doing
				// its own attempt at reassembling JPEG streams from pieces, and it
				// seems to have flagged the leading fragment as a sort of sub-JFIF
				// stream with its own end marker.
				if (fragLen > 8 && memcmp(fragp + fragLen - 4, "\xFF\xD9\xFF\xD8", 4) == 0)
					fragLen -= 4;

				// copy the fragment
				memcpy(newp + 2 + app0Len, fragp, fragLen);

				// copy the rest of the stream
				BYTE *restp = app0 + app0Len;
				size_t restLen = endp - restp;
				memcpy(newp + 2 + app0Len + fragLen, restp, restLen);

				// replace the original buffer with the reassembled buffer
				imageBits->imageData.reset(p = newp);
				imageBits->imageDataSize = static_cast<UINT32>(2 + app0Len + fragLen + restLen);

				// done
				break;
			}

			// skip the rest of the frame
			if (framep[1] >= 0xD0 && framep[1] <= 0xD9)
			{
				// frame types FF D0 to FF D9 have no payload - the marker
				framep += 2;
			}
			else
			{
				// all other frame types have the frame length (excluding the
				// marker, including the length) encoded in the next two bytes,
				// big-endian order
				UINT32 len = (framep[2] << 16) | (framep[3]);
				framep += len + 2;
			}
		}
	}

	// For tag DefineBites (6), the image data is the image portion
	// of a JPEG, with the common encoding tables.
	//
	// For tag DefineBitsJPEG2 (22), the image data can contain
	// a full JPEG stream with encoding tables, a PNG stream, or
	// a GIF89a stream (with no animation).
	if (tagHdr.id == 6)
	{
		// DefineBits - always a JPEG image section
		imageBits->type = LossyImageBits::Type::JPEGImageData;
	}
	else if (tagHdr.id == 21)
	{
		// DefineBitsJPEG2 - sense the type based on the file
		// signature at the start of the stream
		if (imageBits->imageDataSize >= 8 && memcmp(imageBits->imageData.get(), "\x89\x50\x4E\x47\x0D\0x0A\x1A\x0A", 8) == 0)
			imageBits->type = LossyImageBits::Type::PNG;
		else if (imageBits->imageDataSize >= 6 && memcmp(imageBits->imageData.get(), "\x47\x49\x46\x38\x39\x61", 6) == 0)
			imageBits->type = LossyImageBits::Type::GIF89a;
		else
			imageBits->type = LossyImageBits::Type::JPEG;
	}

	// Read the alpha data, if present.  This only applies to full JPEG
	// images, and only to DefineBitsJPEG3 and DefineBitsJPEG4.
	size_t bytesConsumed = startRem - rem;
	if (bytesConsumed < tagHdr.len
		&& imageBits->type == LossyImageBits::Type::JPEG
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

// read a DefineBitsLosslses or DefineBitsLossless2 record
void SWFParser::UncompressedReader::ReadDefineBitsLossless(Dictionary &dict, TagHeader &tagHdr)
{
	// note the starting point, so that we can calculate bytes used later
	auto startRem = rem;

	// read the fixed fields
	UINT charId = ReadUInt16();
	auto format = static_cast<LosslessImageBits::Format>(ReadByte());
	UINT width = ReadUInt16();
	UINT height = ReadUInt16();
	UINT colorTableSize = (format == LosslessImageBits::Format::ColorMappedImage ? ReadByte() + 1 : 0);

	// The rest of the record is in zlib compressed format.  Figure the
	// size of the zlib source data.
	auto bytesUsed = startRem - rem;
	auto zlen = tagHdr.len - bytesUsed;

	// Promote the format ID to our private distinguished formats for
	// DefineBitsLossless2, since these have different layouts
	if (tagHdr.id == 36)
	{
		if (format == LosslessImageBits::Format::ColorMappedImage)
			format = LosslessImageBits::Format::ColorMappedAlphaImage;
		else if (format == LosslessImageBits::Format::RGB24Image)
			format = LosslessImageBits::Format::ARGB32Image;
	}

	// Figure the size of the decompressed data - this depends on the
	// format.
	size_t imageDataSize = 0;
	auto PaddedRowWidth = [](UINT byteWidth) { return (byteWidth + 3) & ~3; };
	UINT rowSpan = 0;
	D2D1_ALPHA_MODE alphaMode = D2D1_ALPHA_MODE_IGNORE;
	switch (format)
	{
	case LosslessImageBits::Format::ColorMappedImage:
		// 8-bit RGB colormapped.  The image data consists of an RGB array
		// of 'colorTableSize' entries, followed by the image rows, with
		// one byte per pixel, rows padded to a multiple of 4 bytes.
		rowSpan = PaddedRowWidth(width);
		imageDataSize = (colorTableSize * 3) + (rowSpan * height);
		break;

	case LosslessImageBits::Format::ColorMappedAlphaImage:
		// 8-bit RGBA colormapped.  The image data consists of an RGBA array
		// of 'colorTableSize' entries, followed by the image rows, with
		// one byte per pixel, rows padded to a multiple of 4 bytes.
		rowSpan = PaddedRowWidth(width);
		imageDataSize = (colorTableSize * 4) + (rowSpan * height);
		break;

	case LosslessImageBits::Format::RGB15Image:
		// PIX15 format.  The image data is an array of pixel rows, two
		// bytes per pixel, rows padded to a multiple of 4 bytes.
		rowSpan = PaddedRowWidth(width * 2);
		imageDataSize = rowSpan * height;
		break;

	case LosslessImageBits::Format::RGB24Image:
		// PIX24 format.  The image data is an array of pixel rows, 4 bytes
		// per pixel.  (Row width is inherently a multiple of 4 in this case,
		// so no padding is required.)
		rowSpan = width * 4;
		imageDataSize = rowSpan * height;
		break;

	case LosslessImageBits::Format::ARGB32Image:
		// ALPHABITMAPDATA format.  The image data is an array of pixel rows, 4
		// bytes per pixel.  (Row width is inherently a multiple of 4 in this 
		// case, so no padding is required.)
		rowSpan = width * 4;
		imageDataSize = rowSpan * height;
		alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
		break;
	}

	// if the image size is valid, read the image
	if (imageDataSize != 0)
	{
		// allocate space and decompress the zlib buffer
		ZlibReader zr(this->p, zlen);
		std::unique_ptr<BYTE> imageData(new BYTE[imageDataSize]);
		if (zr.ReadBytes(imageData.get(), imageDataSize) == imageDataSize)
		{
			// we successly decompressed the data - create the dictionary entry
			auto imageBits = new LosslessImageBits(tagHdr.id, charId);
			dict.emplace(charId, imageBits);

			// set up the fixed fields
			imageBits->charId = charId;
			imageBits->format = format;
			imageBits->width = width;
			imageBits->height = height;
			imageBits->alphaMode = alphaMode;

			// allocate the DXGI R8G8B8A8 buffer
			imageBits->imageData.reset(new BYTE[width * height * 4]);
			BYTE *dst = imageBits->imageData.get();

			// translate the image to DXGI R8G8B8A8 format
			BYTE *colorTable = imageData.get();
			UINT rowNum = 0;
			UINT colNum = 0;
			const BYTE *src;
			switch (format)
			{
			case LosslessImageBits::Format::ColorMappedImage:
				for (BYTE *rowSrc = imageData.get() + colorTableSize * 3; rowNum < height; rowSrc += rowSpan, ++rowNum)
				{
					// each source pixel is a one-byte index into the packed RGB color table
					for (colNum = 0, src = rowSrc; colNum < width; ++colNum)
					{
						BYTE index = *src++;
						const BYTE *rgb = &colorTable[index * 3];
						*dst++ = rgb[0];
						*dst++ = rgb[1];
						*dst++ = rgb[2];
						*dst++ = 0xFF;
					}
				}
				break;

			case LosslessImageBits::Format::ColorMappedAlphaImage:
				// We need to pre-multiply the alpha channel for D2D
				for (UINT i = 0; i < colorTableSize*4; i += 4)
				{
					float a = static_cast<float>(colorTable[i + 3]) / 255.0f;
					colorTable[i] = static_cast<BYTE>(static_cast<float>(colorTable[i]) * a);
					colorTable[i + 1] = static_cast<BYTE>(static_cast<float>(colorTable[i + 1]) * a);
					colorTable[i + 2] = static_cast<BYTE>(static_cast<float>(colorTable[i + 2]) * a);
				}

				// translate the pixels
				for (BYTE *rowSrc = imageData.get() + colorTableSize * 4; rowNum < height; rowSrc += rowSpan, ++rowNum)
				{
					// each source pixel is a one-byte index into the RGBA color table
					for (colNum = 0, src = rowSrc; colNum < width; ++colNum)
					{
						BYTE index = *src++;
						const BYTE *rgba = &colorTable[index * 4];
						*dst++ = rgba[0];
						*dst++ = rgba[1];
						*dst++ = rgba[2];
						*dst++ = rgba[3];
					}
				}
				break;

			case LosslessImageBits::Format::RGB15Image:
				for (BYTE *rowSrc = imageData.get(); rowNum < height; rowSrc += rowSpan, ++rowNum)
				{
					// two bytes per pixel, 5-bit RGB format
					for (colNum = 0, src = rowSrc; colNum < width; ++colNum)
					{
						// the bits are laid out as [0RRRRRGG][GGGBBBBB]; convert
						// to 8-bit by shifting each field 3 bits to the left
						BYTE a = *src++;
						BYTE b = *src++;
						*dst++ = (a << 1) & 0xF8; // a[xRRRRRxx]
						*dst++ = (a << 6) | ((b >> 2) & 0x38);  // a[xxxxxxGG], b[GGGxxxxx]
						*dst++ = (a << 3) & 0xF8; // b[xxxBBBBB]
						*dst++ = 0xFF;
					}
				}
				break;

			case LosslessImageBits::Format::RGB24Image:
				for (BYTE *rowSrc = imageData.get(); rowNum < height; rowSrc += rowSpan, ++rowNum)
				{
					// four source bytes per pixel, 0 R G B format
					for (colNum = 0, src = rowSrc; colNum < width; ++colNum)
					{
						++src;  // skip the unused 0 byte
						*dst++ = *src++;
						*dst++ = *src++;
						*dst++ = *src++;
						*dst++ = 0xFF;
					}
				}
				break;

			case LosslessImageBits::Format::ARGB32Image:
				for (BYTE *rowSrc = imageData.get(); rowNum < height; rowSrc += rowSpan, ++rowNum)
				{
					// four source bytes per pixel, A R G B format
					for (colNum = 0, src = rowSrc; colNum < width; ++colNum)
					{
						// pre-multiply the alpha for the R G B components
						BYTE a = *src++;
						float pma = static_cast<float>(a) / 255.0f;
						*dst++ = static_cast<BYTE>(static_cast<float>(*src++) * pma);
						*dst++ = static_cast<BYTE>(static_cast<float>(*src++) * pma);
						*dst++ = static_cast<BYTE>(static_cast<float>(*src++) * pma);
						*dst++ = a;
					}
				}
				break;
			}
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

// Read a RemoveObject tag
void SWFParser::UncompressedReader::ReadRemoveObject(DisplayList &displayList, TagHeader &tagHdr)
{
	// read the ID of the item to remove
	UINT16 charId = (tagHdr.id == 5 ? ReadUInt16() : 0);
	UINT16 depth = ReadUInt16();

	// look for an object at the specified depth
	if (auto it = displayList.find(depth); it != displayList.end())
	{
		// For RemoveObject, check that the character ID matches.  For
		// RemoveObject2, we only need to match the depth.
		if (tagHdr.id == 28 || (tagHdr.id == 5 && it->second.charId == charId))
			displayList.erase(it);
	}
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

// Read a DefineFont tag
void SWFParser::UncompressedReader::ReadDefineFont(Dictionary &dict, TagHeader &tagHdr)
{
	// get the font ID
	UINT16 fontId = ReadUInt16();

	// read extra DefineFont2/3 records
	BYTE flags = 0;
	bool wideOffsets = false;
	LanguageCode lang = 0;
	std::unique_ptr<BYTE> name;
	UINT16 nGlyphs = 0;
	if (tagHdr.id == 48 || tagHdr.id == 75)
	{
		flags = ReadByte();
		wideOffsets = ((flags & 0x08) != 0) || tagHdr.id == 75;

		lang = ReadByte();

		BYTE nameLen = ReadByte();
		name.reset(new BYTE[nameLen + 1]);
		ReadBytes(name.get(), nameLen);
		name.get()[nameLen] = 0;

		nGlyphs = ReadUInt16();
	}

	// Remember the seek position of the start of the offset table.
	// All of the offsets are relative to this position.
	BYTE *pOffsetTable = this->p;
	auto remAtOffsetTable = this->rem;

	// Read the offset table.
	//
	// For DefineFont2, the number of entries is given explicitly
	// by the numGlyphs field, whcih we've already read.  For the
	// older DefineFont, the number of entries in the table is
	// implied by the first entry: since the offset table consists
	// of 16-bit (2-byte) entries, and the first offset entry is the
	// offset of the first byte past the end of the offset table,
	// the number of entries is simply the offset divided by two.
	UINT32 offset0 = wideOffsets ? ReadUInt32() : ReadUInt16();
	if (nGlyphs == 0)
		nGlyphs = offset0 / 2;

	// allocate space for the table
	std::unique_ptr<UINT32> upOffsets(new UINT32[nGlyphs]);
	UINT32 *offsets = upOffsets.get();
	offsets[0] = offset0;

	// read the rest of the offsets
	if (wideOffsets)
	{
		for (UINT i = 1; i < nGlyphs; ++i)
			offsets[i] = ReadUInt32();
	}
	else
	{
		for (UINT i = 1; i < nGlyphs; ++i)
			offsets[i] = ReadUInt16();
	}

	// for DefineFont2, read the code table offset
	UINT32 codeTableOffset = (tagHdr.id == 48 || tagHdr.id == 75 ? (wideOffsets ? ReadUInt32() : ReadUInt16()) : 0);

	// create the dictionary entry
	auto font = new Font(nGlyphs, tagHdr.id, fontId);
	dict.emplace(fontId, font);

	// save up the DefineFont2 values
	font->hasLayout = (flags & 0x80) != 0;
	font->shiftJIS = (flags & 0x40) != 0;
	font->smallText = (flags & 0x20) != 0;
	font->ANSI = (flags & 0x10) != 0;
	font->wideOffsets = wideOffsets;
	font->wideCodes = ((flags & 0x04) != 0) || tagHdr.id == 75;
	font->italic = (flags & 0x02) != 0;
	font->bold = (flags & 0x01) != 0;
	font->lang = lang;
	if (name != nullptr)
		font->name = CHARToTCHAR(reinterpret_cast<const CHAR*>(name.get()));

	// Read the shape records.  To ensure that we get the alignment
	// right on each record, seek directly to the correct byte offset
	// before each read, as given by the offsets table.
	for (UINT i = 0; i < nGlyphs; ++i)
	{
		// seek to the table offset
		this->p = pOffsetTable + offsets[i];
		this->rem = remAtOffsetTable - offsets[i];

		// read the shape record
		ReadShape(font->shapes[i], tagHdr.id);
	}

	// read the additional DefineFont2/3 fields
	if (tagHdr.id == 48 || tagHdr.id == 75)
	{
		// The rest of the record is layout information that only
		// applies to dynamic text, which we don't implement.

	}
}

// Read a DefineText tag
void SWFParser::UncompressedReader::ReadDefineText(Dictionary &dict, TagHeader &tagHdr)
{
	// figure the pointer to the end of the tag
	const BYTE *endp = this->p + tagHdr.len;

	// create the dictionary entry
	UINT16 charId = ReadUInt16();
	auto text = new Text(tagHdr.id, charId);
	dict.emplace(text->charId, text);

	// read the fixed fields
	text->bounds = ReadRect();
	text->matrix = ReadMatrix();
	int glyphBits = ReadByte();
	int advBits = ReadByte();

	// read the text records
	while (this->p < endp)
	{
		// read the next flags byte; the end marker is a zero byte in
		// the flags position
		BYTE flags = ReadByte();
		if (flags == 0)
			break;

		// add a text record
		auto& tr = text->text.emplace_back();

		// decode the flags
		tr.hasFont = (flags & 0x08) != 0;
		tr.hasColor = (flags & 0x04) != 0;
		tr.hasY = (flags & 0x02) != 0;
		tr.hasX = (flags & 0x01) != 0;

		// read the optional fields
		if (tr.hasFont)
			tr.fontId = ReadUInt16();
		if (tr.hasColor)
			tr.color = (tagHdr.id == 11 ? ReadRGB() : ReadRGBA());
		if (tr.hasX)
			tr.x = ReadInt16();
		if (tr.hasY)
			tr.y = ReadInt16();
		if (tr.hasFont)
			tr.height = static_cast<float>(ReadUInt16());

		// read the glyph entries
		int nGlyphs = ReadByte();
		StartBitField();
		for (int i = 0; i < nGlyphs; ++i)
		{
			auto& ge = tr.glyphs.emplace_back();
			ge.index = ReadUB(glyphBits);
			ge.advance = ReadSB(advBits);
		}
	}
}

// Read a DefineShape tag
void SWFParser::UncompressedReader::ReadDefineShape(Dictionary &dict, UINT16 tagId)
{
	// read the shape ID (== the character ID for the dictionary)
	UINT16 shapeId = ReadUInt16();

	// create the ShapeWithStyle object and add it to the dictionary
	auto shape = new ShapeWithStyle(tagId, shapeId);
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

	// read the shape records
	ReadShape(shape->shapeRecords, tagId);
}

void SWFParser::UncompressedReader::ReadShape(ShapeRecordList &shapeRecords, UINT16 tagId)
{
	// Figure the pixel resolution.  For DefineFont3, coordinates are
	// expressed in 1/20 of a twip.  All others use twips.
	float units = tagId == 75 ? 1.0f / 400.0f : 1.0f / 20.0f;

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
			shapeRecords.emplace_back(sr);
			sr->stateNewStyles = (tagId == 22 || tagId == 32) && (flags & 0x10) != 0;
			sr->stateLineStyle = (flags & 0x08) != 0;
			sr->stateFillStyle1 = (flags & 0x04) != 0;
			sr->stateFillStyle0 = (flags & 0x02) != 0;
			sr->stateMoveTo = (flags & 0x01) != 0;

			// read the Move To delta, if present
			if (sr->stateMoveTo)
			{
				int moveBits = ReadUB(5);
				sr->deltaX = static_cast<float>(ReadSB(moveBits)) * units;
				sr->deltaY = static_cast<float>(ReadSB(moveBits)) * units;
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
			shapeRecords.emplace_back(er);
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
					er->deltaX = static_cast<float>(ReadSB(numBits)) * units;
				if (er->general || er->vert)
					er->deltaY = static_cast<float>(ReadSB(numBits)) * units;
			}
			else
			{
				// curved edge
				er->deltaX = static_cast<float>(ReadSB(numBits)) * units;
				er->deltaY = static_cast<float>(ReadSB(numBits)) * units;
				er->anchorX = static_cast<float>(ReadSB(numBits)) * units;
				er->anchorY = static_cast<float>(ReadSB(numBits)) * units;
			}
		}
	}

}

bool SWFParser::Render(HDC hdc, HBITMAP hbitmap, SIZE targetPixSize, ErrorHandler &eh)
{
	// HRESULT error handler - log it and return
	HRESULT hr = S_OK;
	auto HRError = [&hr, &eh](const TCHAR *where) {
		WindowsErrorMessage msg(hr);
		eh.SysError(_T("Direct2D rendering error"), MsgFmt(_T("%s, HRESULT=%lx, %s"), where, hr, msg.Get()));
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
		// Check for expired clipping layers.  Any clipping layer with
		// a clipping depth less than the current display item depth
		// is expired, since it only applies to layers below this point.
		//
		// Start by removing each expired layer from the END of the
		// current layer list.  This lets us be as efficient as possible
		// with D2D's way of accessing the clipping layers, which is
		// strictly as a stack - the only layer we can remove with D2D
		// is the top of the stack, which corresponds to the last item
		// in our list.
		while (clippingLayers.size() != 0 && clippingLayers.back().depth < p->depth)
		{
			// discard the layer from our list and from D2D's stack
			clippingLayers.pop_back();
			target->PopLayer();
		}

		// Now do a scan of the remaining layers to see if we need to
		// remove any layers deeper down the stack.  We know that the
		// current top layer is a keeper, because we would have already
		// removed it if not.   But there could still be layers further 
		// down the stack that have expired, since SWF'2 model allows
		// for random access to the clipping layers, unlike D2D.
		if (clippingLayers.size() != 0)
		{
			// scan for expired layers
			std::list<ClippingLayer*> expired;
			for (auto &l : clippingLayers)
			{
				if (l.depth < p->depth)
					expired.push_back(&l);
			}
			if (expired.size() != 0)
			{
				// We found buried expired layers.  Remove them, working
				// from the top of the D2D stack == back of our list.
				std::list<ClippingLayer*> unexpired;
				for (auto it = clippingLayers.rbegin(); it != clippingLayers.rend() && expired.size() != 0; ++it)
				{
					// Whatever we decide about this layer, we're going to have to
					// remove it from the D2D stack.  The only question is whether
					// or not it's going to go back on later...
					target->PopLayer();

					// check if this is the topmost expired item
					ClippingLayer *l = &(*it);
					if (l == expired.back())
					{
						// This is the topmost expired layer.  Remove it from the list
						// of items we're seeking.
						expired.pop_back();
					}
					else
					{
						// This is an unexpired layer that's still on top of an expired
						// layer.  Remove it from the D2D stack so that we can remove
						// the unexpired one deeper down.  But add it to our list of
						// keepers so that we can push it back onto the D2D stack when
						// we've finished removing the buried expired layers.
						target->PopLayer();
						unexpired.push_front(l);
					}

					// Okay, we've removed all of the expired layers from D2D.  Now we
					// can discard them from our internal list - we didn't do that already
					// just because it's inconvenient to do so during a reverse iteration.
					for (auto it = clippingLayers.begin(); it != clippingLayers.end(); )
					{
						// remember the next one, in case we delete the current one and
						// make the iterator invalid
						auto nxt = it;
						++nxt;

						// if this item has expired, remove it from the list
						if ((*it).depth < p->depth)
							clippingLayers.erase(it);

						// move on to the next one
						it = nxt;
					}

					// One last step!  If we have anything in the "unexpired" list, 
					// add those back to the D2D layer stack.  We had to remove them
					// temporarily to be able to reach the buried expired ones, but
					// now we're done with that, so we have to restore them.
					for (auto &u : unexpired)
						target->PushLayer(D2D1_LAYER_PARAMETERS{ D2D1::InfiniteRect(), u->geometry }, u->layer);
				}
			}
		}

		// get the character
		if (auto it = dict.find(p->charId); it != dict.end())
		{
			// draw the Character object
			Character *cp = it->second.get();
			cp->Draw(cdc, p);
		}
	}

	// Pop any remaining clipping layers.  (This isn't just being picky;
	// D2D actually checks.)
	while (clippingLayers.size() != 0)
	{
		target->PopLayer();
		clippingLayers.pop_back();
	}

	// close drawing in the render target
	if (!SUCCEEDED(hr = target->EndDraw()))
		return HRError(_T("EndDraw"));

	// successful completion
	return true;
}

void SWFParser::Text::Draw(CharacterDrawingContext &cdc, PlaceObject *po)
{
	// we'll need the dictionary to look up Font objects
	auto& dict = cdc.parser->dict;

	// set up the style elements
	Font *font = nullptr;
	float x = 0.0f, y = 0.0f;
	float scale = 1.0f;

	// there's only one fill style: solid, current color
	std::vector<FillStyle> fillStyles;
	fillStyles.emplace_back();
	fillStyles[0].type = FillStyle::FillType::Solid;

	// same for line styles
	std::vector<LineStyle> lineStyles;
	lineStyles.emplace_back();

	// Set up a private PlaceObject record to apply to the individual
	// glyphs, starting with the PlaceObject for the overall text run.
	PlaceObject placeGlyph = *po;

	// Figure the basic transform matrix: we'll apply the Text matrix
	// first, then the PlaceObject matrix, or P*(T*point) = (P*T)*point.
	// Both P and T are fixed, so we can precompute (P*T) and then only
	// have to do one matrix multiply per point later on.
	MATRIX baseMatrix = po->matrix.Compose(matrix);

	// set up the shape drawing context
	ShapeRecord::ShapeDrawingContext sdc{ cdc, &placeGlyph, fillStyles, lineStyles };

	// draw each text record
	for (auto& t : text)
	{
		// get the new font
		if (t.hasFont)
		{
			if (auto it = dict.find(t.fontId); it != dict.end())
				font = dynamic_cast<Font*>(it->second.get());
		}

		// get the new color
		if (t.hasColor)
			fillStyles[0].color = t.color;

		// get the new offset
		if (t.hasX)
			x = t.x / 20.0f;
		if (t.hasY)
			y = t.y / 20.0f;

		// get the new scale, based on the height
		if (t.hasFont)
			scale = t.height / 1024.0f;

		// if there's a font, draw the glyph shapes
		if (font != nullptr)
		{
			// visit each glyph
			for (auto& g : t.glyphs)
			{
				// make sure the shape index is valid
				if (g.index < font->shapes.size())
				{
					// Set up the transform matrix for the glyph scale and translation.
					// The glyph transform is done first, then the base transform:
					// Base*(Glyph*point) = (Base*Glyph)*point.
					placeGlyph.matrix = baseMatrix.Compose(MATRIX{
						scale, scale,
						0.0f, 0.0f,
						x, y
					});

					// draw the shapes making up the glyph
					for (auto& shape : font->shapes[g.index])
						shape->Draw(sdc);

					// Render the shapes in the style maps
					sdc.RenderMaps();
					sdc.ClearMaps();

					// advance to the next position
					x += g.advance/20.0f;
				}
			}
		}
	}
}

void SWFParser::ShapeWithStyle::Draw(CharacterDrawingContext &cdc, PlaceObject *po)
{
	// set up the shape drawing context, starting with the style arrays
	// defined at the shape-with-style level
	ShapeRecord::ShapeDrawingContext sdc{ cdc, po, fillStyles, lineStyles };

	// start drawing at the shape origin
	sdc.pt = { 0.0f, 0.0f };

	// Draw each shape record.  This won't actually render anything yet; it
	// just populates the style-keyed line and edge maps in the drawing context.
	for (auto &sp : shapeRecords)
		sp->Draw(sdc);

	// Now render the shapes in the style maps
	sdc.RenderMaps();
}

void SWFParser::LossyImageBits::Draw(CharacterDrawingContext &dc, PlaceObject *po)
{
	if (auto bitmap = GetOrCreateBitmap(dc); bitmap != nullptr)
		dc.target->DrawBitmap(bitmap);
}

ID2D1Bitmap *SWFParser::LossyImageBits::GetOrCreateBitmap(CharacterDrawingContext &dc)
{
	// if we haven't already cached the bitmap, create it
	if (bitmap == nullptr)
	{
		// temporary image stream, in case we need to combine JPEG fragments
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
			{
				// save the cached bitmap
				this->bitmap = bitmap;
			}
		}
	}

	// return the cached bitmap
	return bitmap;
}

void SWFParser::LosslessImageBits::Draw(CharacterDrawingContext &dc, PlaceObject *po)
{
	if (auto bitmap = GetOrCreateBitmap(dc); bitmap != nullptr)
		dc.target->DrawBitmap(bitmap);
}

ID2D1Bitmap *SWFParser::LosslessImageBits::GetOrCreateBitmap(CharacterDrawingContext &dc)
{
	// if we haven't already cached the bitmap, create it
	if (bitmap == nullptr)
	{
		D2D1_BITMAP_PROPERTIES props{ { DXGI_FORMAT_R8G8B8A8_UNORM, alphaMode }, 96.0f, 96.0f };
		RefPtr<ID2D1Bitmap> bitmap;
		if (SUCCEEDED(dc.target->CreateBitmap({ width, height }, imageData.get(), width*4, props, &bitmap)))
			this->bitmap = bitmap;
	}

	// return the cached bitmap
	return bitmap;
}

void SWFParser::ShapeRecord::ShapeDrawingContext::RenderMaps()
{
	// draw the fills, from the fill style map
	for (auto &pair : fillEdges)
	{
		// skip empty edge lists
		auto& edges = pair.second;
		if (edges.size() == 0)
			continue;

		// create and open a path geometry 
		RefPtr<ID2D1PathGeometry> path;
		RefPtr<ID2D1GeometrySink> geomSink;
		if (!(SUCCEEDED(d2dFactory->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&geomSink))))
			break;

		// open the initial figure at the start of the first segment
		D2D1_POINT_2F pt = edges.front().start;
		D2D1_POINT_2F startPt = pt;
		geomSink->BeginFigure(TargetCoords(edges.front().start), D2D1_FIGURE_BEGIN_FILLED);

		// visit each segment
		for (auto& edge : edges)
		{
			// if the new segment isn't continuous with the last one, end the current figure
			if (edge.start.x != pt.x || edge.start.y != pt.y)
			{
				// end the current figure
				geomSink->EndFigure(startPt.x == pt.x && startPt.y == pt.y ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN); 

				// start a new one
				startPt = pt = edge.start;
				geomSink->BeginFigure(TargetCoords(startPt), D2D1_FIGURE_BEGIN_FILLED);
			}
			
			// add this segment
			if (edge.straight)
				geomSink->AddLine(TargetCoords(edge.end));
			else
				geomSink->AddQuadraticBezier(D2D1_QUADRATIC_BEZIER_SEGMENT{ TargetCoords(edge.control), TargetCoords(edge.end) });

			// move to the end of the segment
			pt = edge.end;
		}

		// Finish the last path and close the geometry sink
		geomSink->EndFigure(startPt.x == pt.x && startPt.y == pt.y ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
		geomSink->Close();

		// If this is a clipping layer, set up the D2D clipping.  Oherwise
		// execute the fill.
		if (po->clipDepth != 0)
		{
			// This is a clipping layer, so we don't actually draw anything;
			// we just set up the geometry as a clipping mask for layers up
			// through the clipDepth.
			//
			// Create the D2D clipping layer
			RefPtr<ID2D1Layer> d2dLayer;
			if (SUCCEEDED(chardc.target->CreateLayer(nullptr, &d2dLayer)))
			{
				// Create our clipping layer list entry
				auto& layer = chardc.parser->clippingLayers.emplace_back();
				layer.depth = po->clipDepth;
				layer.layer = d2dLayer;
				layer.geometry = path;

				// push the layer in the D2D target
				chardc.target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), path), d2dLayer);
			}
		}
		else
		{
			// regular drawing layer - get the fill style
			auto fillStyle = reinterpret_cast<FillStyle*>(pair.first);

			switch (fillStyle->type)
			{
			case FillStyle::FillType::Solid:
				{
					// fill with a solid-color brush
					RefPtr<ID2D1SolidColorBrush> brush;
					if (SUCCEEDED(chardc.target->CreateSolidColorBrush(fillStyle->color.ToD2D(), &brush)))
						chardc.target->FillGeometry(path, brush);
				}
				break;

			case FillStyle::FillType::RepeatingBitmap:
			case FillStyle::FillType::NonSmoothedRepeatingBitmap:
				// TO DO - for now just draw the same as clipped
				// fall through...

			case FillStyle::FillType::ClippedBitmap:
			case FillStyle::FillType::NonSmoothedClippedBitmap:
				// look up the image object from the dictionary
				if (auto it = chardc.parser->dict.find(fillStyle->bitmapId); it != chardc.parser->dict.end())
				{
					// make sure it's an ImageBits object
					if (auto image = dynamic_cast<ImageBits*>(it->second.get()); image != nullptr)
					{
						// retrieve the bitmap
						if (auto bitmap = image->GetOrCreateBitmap(chardc); bitmap != nullptr)
						{
							// set up a clipping layer based on the path
							RefPtr<ID2D1Layer> layer;
							if (SUCCEEDED(chardc.target->CreateLayer(&layer)))
							{
								// push the layer
								chardc.target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), path), layer);

								// draw the bitmap
								D2D1_RECT_F rcBounds, rcSrc;
								path->GetBounds(D2D1::IdentityMatrix(), &rcBounds);
								auto bitmapSize = bitmap->GetPixelSize();
								auto topLeft = fillStyle->matrix.Apply(D2D1_POINT_2F{ 0.0f, 0.0f });
								auto botRight = fillStyle->matrix.Apply(D2D1_POINT_2F{ static_cast<float>(bitmapSize.width), static_cast<float>(bitmapSize.height) });
								rcSrc = { topLeft.x, topLeft.y, botRight.x, botRight.y };
								chardc.target->DrawBitmap(bitmap, rcBounds, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, rcSrc);

								// pop the layer
								chardc.target->PopLayer();
							}
						}
					}
				}
				break;

			case FillStyle::FillType::LinearGradient:
				// TO DO
				break;

			case FillStyle::FillType::RadialGradient:
				// TO DO
				break;

			case FillStyle::FillType::FocalRadialGradient:
				// TO DO
				break;
			}
		}
	}

	// Now draw the outlines, from the line style map.  This will draw
	// the outlines on top of the fill, which is how they'd appear if
	// we had D2D draw the fill and outline together.  But don't draw
	// outlines for a clipping layer.
	if (po->clipDepth == 0)
	{
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
}


// Add an edge to a style map.  For each line style and each fill
// style used in a shape, we build a list of line/curve segments
// using that style, in the same order as they appear in the SWF.
// This lets us find each continuous sub-path with a single style
// and render it as a single Direct2D path.
//
// We have to jump through these hoops because SWF's geometry
// model allows for heterogeneous line and fill styles in a single
// path, whereas Direct2D's only allows one line style and one fill
// style per path.
//
// For the line styles, a simpler way to handle this would be to
// render each SWF segment as a separate D2D Path.  But that has
// two big drawbacks.  The first is efficiency; it's more efficient
// to use fewer Path objects when possible.  The second, and more
// important reason, is rendering quality: combining segments when
// we can allows D2D to do proper smoothing and corner joins where
// segments meet.
//
// The situation with fill styles is much more complex.  SWF has
// the notion of "left" and "right" fill styles for each segment.
// As with line styles, each segment in a composite shape can have
// its own fill styles - but we don't actually care about that,
// because Adobe seems to consider that a design flaw in SWF, and
// says in the spec only that the results of using heterogeneous
// fill styles are "unpredictable".  What is important, though, is
// the left/right distinction. 
//
// The left/right fill seems to have three uses cases: self-
// overlapping shapes, edges shared between two filled regions,
// and embedded holes:
//
// - The self-overlapping case is described in the SWF spec, so
//   I won't go into details here.  Curiously, that's the only
//   use case for left/right fill that they describe, but it seems
//   to be the least important - at least, I haven't seen any
//   examples in the wild in the SWF's I've tested.  It's hard
//   to imagine taking advantage of this scenario intentionally,
//   actually, although perhaps my imagination is too limited.
//
// - The shared-border case is described in P.A.M. Senster's
//   "The design and implementation of Google Swiffy: A Flash to
//   HTML5 converter:
//
//      https://repository.tudelft.nl/islandora/object/uuid%3Acab4b862-d662-432a-afa4-45ccb725177f
//
//   There, the author describes an algorithm for converting an
//   SWF shape to SVG, which has the same constraints on style
//   homogeneity that Direct2D has.  Like the self-overlapping
//   case, I haven't seen any SWFs that actually use the shared
//   border capabiilty of the left/right fill, but this at least
//   seems like a case that might actually come up, if only
//   because I can imagine SWF creation tools deliberately using
//   it as an optimization to reduce file size.  In the days
//   when SWF was relevant, file size reduction was so critical
//   that nearly trivial optimizations like this are believable.
//
// - I haven't seen the donut-hole case described anywhere, but
//   it seems to be the only one that actually occurs in the SWF
//   samples I've examined.  It seems to be an almost accidental
//   artifact of the Flash rendering algorithm that isn't described
//   in the SWF spec or anywhere else I've seen, but it's critical
//   to proper rendering of virtually all of the HyperPin Media
//   Pack instruction card samples, so it's the one case that we
//   particularly care about.  It's so important for the extant
//   Media Pack instruction cards because they're mostly
//   vectorized text outlines, hence they use lots of donut
//   holes for the interiors of closed letter shapes like "O"
//   and "D" and "B".
//
//   To see how the donut-hole case works, lets look at the
//   letter "O" (which is, after all, practically the epitome of
//   donut-shaped).  In this case, the SWF shape definition will
//   consist of two paths, one for the outline of the outside
//   perimeter of the "O", and one for the perimeter of the hole.
//   The outer path will consist of perhaps four curved segments
//   winding counter-clockwise, with FillStyle0 ("left fill") set
//   to the fill color for the letter.  The inner path will
//   consist of a similar set of line segments, but will wind
//   clockwise, also with FillStyle0.  FillStyle0 means that
//   we fill the closed region to the left of the line, so the
//   outer CCW loop fills the interior of the "O".  By itself,
//   that would describe a completely filled-in circle.  But the
//   inner path winds CW, so its "left fill" is the OUTSIDE of
//   the circle it describes.  That's why I think this is an
//   artifact of the Flash Player rendering algorithm: it seems
//   that, by itself, this inner circle wouldn't render a fill
//   at all - fills only seem to apply to the enclosed interior
//   of a path.  However, taken in combination, the "fill inside
//   this line" on the outside and "fill outside this line" on
//   the inside combine to say "flll the region between these 
//   two lines".
//
//   It so happens that SVG, D2D, and probably many other 2D
//   graphics models use a similar "winding direction" model for
//   exclusion regions, so we get this rendering feature almost
//   for free as long as we (a) maintain the original winding
//   orders from the SWF when translating to D2D, and (b) combine
//   all of the paths with shared fill style into a single "Group
//   Geometry" (in D2D terms).  Maintaining the SWF winding
//   direction just means building the paths out of segments
//   in the same order as the SWF, which is the easy approach
//   that you'd use default if you weren't even thinking about
//   any of this.  Combining the paths by common fill style
//   takes some more work, though: we have to deliberately
//   regroup the segments by fill style rather than by SWF file
//   order.  Doing that, while preserving the SWF order for the
//   segments within each group, will naturally reproduce the
//   SWF donut-hole fill rendering.
//
// D2D's winding-direction fill rule is direction-independent:
// it only cares about *changes* in winding direction between
// nested shapes, and doesn't have a way to specify that there's
// one fill for clockwise shapes and another for counterclockwise
// shapes.  So we can't reproduce SWF's behavior with perfect
// fidelity; D2D might get things backwards in some cases.  But
// I don't think it would happen much in practice, and probably
// not at all with the limited set of SWF instruction cards,
// which is all we really care about reproducing properly.
//
// My algorithm to handle the various cases involves three
// passes over the SWF shape records during rendering:
//
// On the first pass, we group all of the line segments making
// up the shape records by line style and by fill style.  Each
// shape record might go into three separate maps: one for its
// line style, one for its Fill Style 0 style, and one for its
// Fill Style 1 style.  In practice, I haven't seen any examples
// of a shape that uses both Fill Style 0 and Fill Style 1, so
// most segments will just go into a Line Style map and one
// Fill Style map.
//
// What about choosing between left fill and right fill?  Well,
// D2D doesn't have a way to specify different fill styles for
// clockwise and counterclockwise paths, so all we can do is
// enlist a path under BOTH of its fill styles, and hope that
// the shape is defined in a way that happens to work out
// properly under D2D's winding order rules.  It does work
// well for donut-holes, which is the only use case that seems
// to arise in the Instruction Card samples I've examined.
// The experience of other tools using a similar algorithm to
// render SWF files with HTML5 and SVG (Swiffy, swf2js) suggests
// that it also works for a broader sampling of extant SWFs.
// I think the winding-rule approach that all of the modern
// vector systems use is essentially a more formally correct
// way to express what SWF was trying to express with the
// left/right fill system, with more predictable results, so
// I expect that most real-world cases in SWF will happen to
// map naturally to the winding-rule model.  The SWF left/right
// model can express additional types of cases that the winding
// model can't, but my intuition is that they're probably
// mostly pathological - unpredictable from the spec alone
// and dependent upon the particular implementation.
//
// On the second pass, we go through the fill style collection,
// one style at a time, and draw each continuous figure we find.
// A continuous figure is a set of segments where the end point
// of one segment is the same as the start point of the next
// segment.  For each continuous figure, we create a path for
// the figure and add it to a D2D Geometry Group, using the
// Winding-Order Fill mode for the group, so that enclosed
// paths with opposite winding orders will create donut holes
// properly.  After populating the group with all of the paths
// in the fill style collection, we fill the group.
//
// On the third pass, we go through the line style collection,
// one style at a time, and draw each continuous path section
// we find.  This draws the outlines on top of the fill, which
// is the same effect that we'd get if we were to have D2D draw
// the outline and fill at the same time.  It's a little less
// efficient than doing them together, but it's the only way
// to allow for line style changes in the course of a shape.
//
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

	// Add the edge in reversed order for fillStyle1 ("right fill").  This will
	// reverse the D2D winding order with respect to any shapes enlisted under the
	// same fill style for fillStyle0, so that D2D sees this path as a fill
	// transition for the fill type if it's embedded within or entirely encloses
	// a path with the same style for its left fill.
	ShapeDrawingContext::Segment rev = seg;
	rev.end = seg.start;
	rev.start = seg.end;
	sdc.AddEdge(sdc.fillEdges, sdc.curFillStyle1, rev, true);
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
