// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include <wincodec.h>
#include "../Utilities/GraphicsUtil.h"
#include "../Utilities/ComUtil.h"
#include "../DirectXTK/Inc/DDSTextureLoader.h"
#include "../DirectXTK/Inc/WICTextureLoader.h"
#include "../DirectXTex/DirectXTex/DirectXTex.h"
#include "D3D.h"
#include "Sprite.h"
#include "Shader.h"
#include "TextureShader.h"
#include "Application.h"
#include "FlashClient/FlashClient.h"
#include "LogFile.h"
#include <png.h>

#pragma comment(lib, "libpng.lib")

using namespace DirectX;

Sprite::Sprite()
{
	alpha = 1.0f;
	offset = { 0.0f, 0.0f, 0.0f };
	scale = { 1.0f, 1.0f, 1.0f };
	rotation = { 0.0f, 0.0f, 0.0f };
	UpdateWorld();
}

Sprite::~Sprite()
{
	DetachFlash();
}

void Sprite::DetachFlash()
{
	if (flashSite != nullptr)
	{
		flashSite->Shutdown();
		flashSite = nullptr;
		stagingTexture = nullptr;
	}
}

void Sprite::UpdateWorld()
{
	// Apply world transformations - scale, rotate, translate
	world = XMMatrixIdentity();
	world = XMMatrixMultiply(world, XMMatrixScaling(scale.x, scale.y, scale.z));
	world = XMMatrixMultiply(world, XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z));
	world = XMMatrixMultiply(world, XMMatrixTranslation(offset.x, offset.y, offset.z));
	worldT = XMMatrixTranspose(world);
}

bool Sprite::Load(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, HWND msgHwnd, ErrorHandler &eh)
{
	// release any previous resources
	Clear();

	// remember the message window
	this->msgHwnd = msgHwnd;

	// set up a new load context
	loadContext.Attach(new LoadContext());

	// Try to determine the image type from the file contents
	if (ImageFileDesc desc; GetImageFileInfo(filename, desc, true, true))
	{
		// If it's an SWF, WIC can't handle it - we have to load it through
		// our Flash client site object
		if (desc.imageType == ImageFileDesc::SWF)
			return LoadSWF(filename, normalizedSize, pixSize, eh);

		// If it's a GIF or APNG, we need to load it specially in case it's animated
		if (desc.imageType == ImageFileDesc::GIF)
			return LoadGIF(filename, normalizedSize, pixSize, eh);
		else if (desc.imageType == ImageFileDesc::APNG)
			return LoadAPNG(filename, normalizedSize, pixSize, eh);

		// The WIC loader ignores orientation metadata (such as JPEG Exif data), so
		// we have to do some special work if it's rotated or reflected.
		if (desc.oriented)
		{
			// Load it as a bitmap.  Note that the final bitmap is at the DISPLAY size,
			// which might be rotated from the source size.
			return Load(desc.dispSize.cx, desc.dispSize.cy, [&desc, filename](Gdiplus::Graphics &g)
			{
				// load the image 
				std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromFile(filename));

				// Set up the drawing port with the origin at the center of the final
				// view size, to make the rotation and reflection transforms easier to
				// think about.
				g.TranslateTransform(float(desc.dispSize.cx / 2), float(desc.dispSize.cy / 2));

				// apply the JPEG Exif transform
				Gdiplus::Matrix m;
				desc.orientation.ToMatrix(m);
				g.MultiplyTransform(&m);

				// draw the image with the transforms applied, at the SOURCE image size
				float cx = float(desc.size.cx);
				float cy = float(desc.size.cy);
				g.DrawImage(bitmap.get(), Gdiplus::RectF(-cx / 2.0f, -cy / 2.0f, cx, cy),
					0.0f, 0.0f, cx, cy, Gdiplus::UnitPixel);

			}, eh, _T("Sprite::Load(file) with orientation metadata"));
		}
	}

	// It's didn't require special handling, so we'll just let DirectxTk 
	// load it directly via WIC.
	return LoadWICTexture(filename, normalizedSize, eh);
}

bool Sprite::LoadWICTexture(const WCHAR *filename, POINTF normalizedSize, ErrorHandler &eh)
{
	// WIC file loading can be kind of slow for large image files.
	// Do the loading in a thread.
	loadContext->ready = false;

	// set up the thread context
	struct ThreadContext
	{
		ThreadContext(LoadContext *loadContext, const WCHAR *filename) :
			loadContext(loadContext, RefCounted::DoAddRef),
			filename(filename)
		{ }

		RefPtr<LoadContext> loadContext;
		WSTRING filename;
	};
	std::unique_ptr<ThreadContext> ctx(new ThreadContext(loadContext, filename));

	auto ThreadMain = [](LPVOID params) -> DWORD
	{
		// get the context - we own it and must discard it when done, so use a unique_ptr
		std::unique_ptr<ThreadContext> ctx(static_cast<ThreadContext*>(params));

		// create the WIC texture
		HRESULT hr = CreateWICTextureFromFileEx(D3D::Get()->GetDevice(), ctx->filename.c_str(),
			0, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, WIC_LOADER_IGNORE_SRGB,
			&ctx->loadContext->texture, &ctx->loadContext->rv);

		if (FAILED(hr))
		{
			WindowsErrorMessage winMsg(hr);
			LogFileErrorHandler eh;
			eh.SysError(
				MsgFmt(IDS_ERR_IMGLOAD, ctx->filename.c_str()),
				MsgFmt(_T("CreateWICTextureFromFile failed, HRESULT %lx: %s"), static_cast<long>(hr), winMsg.Get()));
		}
		else
		{
			// resource is ready
			ctx->loadContext->ready = true;
		}
		
		return 0;
	};

	// create the mesh
	if (!CreateMesh(normalizedSize, eh, MsgFmt(_T("file \"%ws\""), filename)))
		return false;

	// kick off the loader thread
	DWORD tid;
	HandleHolder hThread(CreateThread(0, 0, ThreadMain, ctx.get(), 0, &tid));

	// if the thread startup succeeded, release the context object to the thread;
	// otherwise try doing the work inline
	if (hThread != nullptr)
		ctx.release();
	else
		ThreadMain(ctx.get());

	// success
	return true;
}

bool Sprite::LoadSWF(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh)
{
	// Create the new Flash site.  Our FlashClientSite creates a windowless
	// activation site for the Flash object, loads the file (as a "movie"),
	// and starts playback.  The windowless site captures the Flash graphics
	// into a DIB.
	if (FAILED(FlashClientSite::Create(&flashSite, filename, pixSize.cx, pixSize.cy, eh)))
		return false;

	// Get the initial image frame as an HBITMAP handle to a DIB
	BITMAPINFO bmi;
	void *bits = nullptr;
	HBITMAP hbmp = flashSite->GetBitmap(NULL, &bits, &bmi);
	if (hbmp == NULL)
		return false;

	// Load our D3D11 texture from the initial bitmap
	if (!Load(bmi, bits, eh, _T("Load Shockwave Flash frame")))
		return false;

	// Create a staging texture for frame updates
	if (!CreateStagingTexture(pixSize.cx, pixSize.cy, eh))
		return false;

	// success
	return true;
}

// Load a PNG, with animation support
bool Sprite::LoadAPNG(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh)
{
	// Try interpreting it as an animated PNG through the incremental
	// loader context.  If that succeeds, the loader context will be
	// set up to load frames on demand.  If not, we'll simply fall back
	// on the generic WIC loader, to attempt to load the file as a
	// contentional single-frame PNG or some other image type.
	std::unique_ptr<APNGLoaderState> loader(new APNGLoaderState());
	if (loader->Init(this, filename, normalizedSize, pixSize))
	{
		// create the mesh
		if (!CreateMesh(normalizedSize, eh, MsgFmt(_T("file \"%ws\""), filename)))
			return false;

		// transfer ownership of the loader to the Sprite
		animation.reset(loader.release());

		// initialize the animation
		animRunning = true;
		curAnimFrame = 0;
		curAnimFrameEndTime = GetTickCount64() + animFrames[0]->dt;

		// success
		return true;
	}
	else
	{
		// It's not an animated PNG - use the basic WIC loader.
		// Explicitly discard the APNG loader state first, to make sure
		// we don't have an open handle to the file that could conflict
		// with the WIC loader opening it.
		loader.reset();
		return LoadWICTexture(filename, normalizedSize, eh);
	}
}

// Initialize the Animated PNG incremental loader
bool Sprite::APNGLoaderState::Init(Sprite *sprite, const WCHAR *filename, POINTF normalizedSize, SIZE pixSize)
{
	// open the file
	if (_tfopen_s(&fp, filename, _T("rb")) != 0)
		return false;

	// Check that it's a PNG; if not, fail
	BYTE sig[8];
	if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0)
		return false;

	// Read the IHDR chunk; if it's not an IHDR, fail
	if (ReadChunk(IHDR) != ID_IHDR || IHDR.size != 25)
		return false;

	// Decode the IHDR
	rcFull.left = rcFull.top = 0;
	rcFull.right = png_get_uint_32(IHDR.data.get() + 8);
	rcFull.bottom = png_get_uint_32(IHDR.data.get() + 12);

	// Initialize the raw frame buffer
	frameRaw.Init(rcFull.right, rcFull.bottom, 1, 10);

	// Start the image processing
	StartProcessing();

	// Read the first frame.  If decoding fails, or the PNG file doesn't
	// have the added chunks that make it an APNG, abort and use the
	// standard WIC loader.
	if (!ReadThroughNextFrame() || !isAnimated)
		return false;

	// Create first snimation frame
	if (!CreateAnimFrame(sprite))
		return false;

	// success
	return true;
}

void Sprite::APNGLoaderState::DecodeNext(Sprite *sprite)
{
	// read through the next frame
	if (!eof && ReadThroughNextFrame())
		CreateAnimFrame(sprite);
}

bool Sprite::APNGLoaderState::CreateAnimFrame(Sprite *sprite)
{
	// make sure there's a current frame
	if (frameCur.data == nullptr)
		return false;

	// set up the D3D texture descriptor
	D3D11_TEXTURE2D_DESC txd = CD3D11_TEXTURE2D_DESC(
		DXGI_FORMAT_R8G8B8A8_UNORM, frameCur.width, frameCur.height, 1, 1,
		D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE,
		1, 0, 0);

	// set up the subresource descriptor
	D3D11_SUBRESOURCE_DATA srd;
	ZeroMemory(&srd, sizeof(srd));
	srd.pSysMem = frameCur.data.get();
	srd.SysMemPitch = frameCur.width * 4;
	srd.SysMemSlicePitch = srd.SysMemPitch * frameCur.height;

	// set up the shader resource view
	D3D11_SHADER_RESOURCE_VIEW_DESC svd;
	svd.Format = txd.Format;
	svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	svd.Texture2D.MipLevels = txd.MipLevels;
	svd.Texture2D.MostDetailedMip = 0;

	// add an animation frame
	auto af = sprite->animFrames.emplace_back(new AnimFrame()).get();

	// Figure the display time.  APNG expresses the time in seconds,
	// as a fraction (numerator divided by denominator) of two 16-bit.
	// ints.  If the denominator is 0, the implied denominator is 100.
	// Refigure it as a number of milliseconds.
	af->dt = static_cast<DWORD>((frameCur.delayNum * 1000) / (frameCur.delayDen == 0 ? 100 : frameCur.delayDen));

	// create the texture
	HRESULT hr = D3D::Get()->CreateTexture2D(&txd, &srd, &svd, &af->rv, &af->texture);
	if (!SUCCEEDED(hr))
	{
		// log the error
		WindowsErrorMessage winMsg(hr);
		LogFileErrorHandler eh;
		eh.SysError(
			MsgFmt(IDS_ERR_IMGCREATE, _T("Decoding Animated PNG frame")),
			MsgFmt(_T("Sprite::Load, CreateTexture2D failed, HRESULT %lx: %s"), (long)hr, winMsg.Get()));

		// discard the aborted frame
		sprite->animFrames.pop_back();

		// return failure
		return false;
	}

	// success
	return true;
}

static inline bool IsValidPngIdByte(DWORD b)
{
	return b >= 65 && b <= 122 && (b <= 90 || b >= 97);
}

static inline bool IsValidPngId(DWORD id)
{
	return IsValidPngIdByte(id & 0xFF)
		&& IsValidPngIdByte((id >> 8) & 0xFF)
		&& IsValidPngIdByte((id >> 16) & 0xFF)
		&& IsValidPngIdByte((id >> 24) & 0xFF);
}

bool Sprite::APNGLoaderState::ReadThroughNextFrame()
{
	// apply the disposal operator for the outgoing frame
	switch (disposal.dop)
	{
	case DOP_NONE:
		// no disposal - keep the current frame's contents
		break;

	case DOP_BKG:
		// clear the frame's sub-region to transparent black
		if (frameCur.data != nullptr)
		{
			for (UINT row = 0; row < disposal.height; ++row)
				memset(frameCur.rows.get()[disposal.y + row] + disposal.x * 4, 0, disposal.width * 4);
		}
		break;

	case DOP_PREV:
		// revert to prior frame
		frameCur.Take(framePrev);
		break;
	}

	// process the file until finishing the next frame or reaching EOF
	while (!feof(fp)) 
	{
		// read the next chunk
		Chunk chunk;
		auto id = ReadChunkSizeAndID(chunk);

		// let's see what we have
		switch (id)
		{
		case ID_acTL:
			// Animation control - mark it as animated
			isAnimated = true;
			ReadChunkContents(chunk);

			// decode and save the contents
			acTL.numFrames = png_get_uint_32(chunk.data.get() + 8);
			acTL.numPlays = png_get_uint_32(chunk.data.get() + 12);
			break;

		case ID_fcTL:
			// Animation frame control.  This marks the start of the
			// virtual PNG sub-stream for an animation frame.

			// count the fcTL
			fcTLCount += 1;

			// If we've encountered an IDAT record, an fcTL also marks
			// the end of the virtual sub-stream for the current frame,
			// so we need to end processing for the curent frame.
			//
			// The exception is that the first fcTL can precede the first
			// IDAT.  This indicates that the image in the IDAT record(s)
			// is included in the animation stream.  Since there's no
			// preceding image in this case, don't end processing.
			if (hasIDAT && !EndProcessing())
				return false;

			// If we have frame data available, compose the new frame
			if (frameDataAvail)
			{
				// If the outgoing frame's disposal op is PREVIOUS, save 
				// the current frame before composing the new frame, so that
				// we can revert it after we're done.  There's no need to
				// save a copy of the old frame for other disposal operations.
				if (fcTL.dop == DOP_PREV && frameCur.data != nullptr)
					framePrev.Copy(frameCur);

				// Compose the current frame from the current raw subframe. 
				// If there is no current frame, the raw frame simply becomes
				// the current frame.
				if (frameCur.data != nullptr)
				{
					// compose the old and new frames
					ComposeFrame(frameCur.rows.get(), frameRaw.rows.get(), fcTL.bop, fcTL.x, fcTL.y, fcTL.width, fcTL.height);
				}
				else
				{
					// there's no current frame yet, so the raw frame is now
					// the current frame
					frameCur.Take(frameRaw);
				}

				// set the timing data
				frameCur.delayNum = fcTL.delayNum;
				frameCur.delayDen = fcTL.delayDen;

				// remember the disposal settings
				disposal.dop = fcTL.dop;
				disposal.x = fcTL.x;
				disposal.y = fcTL.y;
				disposal.width = fcTL.width;
				disposal.height = fcTL.height;
			}

			// Decode the new fcTL
			ReadChunkContents(chunk);
			fcTL.width = png_get_uint_32(chunk.data.get() + 12);
			fcTL.height = png_get_uint_32(chunk.data.get() + 16);
			fcTL.x = png_get_uint_32(chunk.data.get() + 20);
			fcTL.y = png_get_uint_32(chunk.data.get() + 24);
			fcTL.delayNum = png_get_uint_16(chunk.data.get() + 28);
			fcTL.delayDen = png_get_uint_16(chunk.data.get() + 30);
			fcTL.dop = chunk.data.get()[32];
			fcTL.bop = chunk.data.get()[33];

			// limit the size to the IHDR frame size
			if (fcTL.x > frameRaw.width)
				fcTL.x = frameRaw.width;
			if (fcTL.y > frameRaw.height)
				fcTL.y = frameRaw.height;
			if (fcTL.x + fcTL.width > frameRaw.width)
				fcTL.width = frameRaw.width - fcTL.x;
			if (fcTL.y + fcTL.height > frameRaw.height)
				fcTL.height = frameRaw.height - fcTL.y;

			// For the first frame, the disposal op can't be PREVIOUS;
			// if it is, force it to BKG
			// and the blend op must be OVER.
			if (fcTLCount == 1 && fcTL.dop == DOP_PREV)
				fcTL.dop = DOP_BKG;

			// If we ended processing for the last frame, start processing
			// for the next frame
			if (hasIDAT)
			{
				// allocate a new raw frame if necessary
				if (frameRaw.data == nullptr)
					frameRaw.Init(rcFull.right, rcFull.bottom, fcTL.delayNum, fcTL.delayDen);

				// Start processing for the new sub-stream that follows
				// the fcTL chunk.  Patch the original IHDR with the
				// new size data from the fcTL, so that we create the
				// raw frame in the proper size.
				memcpy(IHDR.data.get() + 8, chunk.data.get() + 12, 8);
				StartProcessing();
			}

			// If we generated a frame, stop here and pass it back to
			// the caller.  This lets the caller immediately render the
			// next frame as soon as we've finished reading it.  The
			// caller will make a new call when it's time to render the
			// next frame.
			if (frameDataAvail)
			{
				// we've now consumed this frame
				frameDataAvail = false;

				// tell the caller a frame is ready
				return true;
			}

			// no frame yet - continue reading
			break;

		case ID_IDAT:
			// Image data chunk.  In an animated image file, this is the
			// first frame's image data, and we'll know it's an animated
			// PNG becuase we will have seen an acTL chunk by now.  If
			// it's not an animated file, simply stop here, so that the
			// caller can abort to the WIC loader instead.
			if (!isAnimated)
				return false;

			// read and process the IDAT chunk
			ReadChunkContents(chunk);
			ProcessChunk(chunk.data.get(), chunk.size);

			// If we've encountered an fcTL record already, we now have
			// image data to include in the animation sequence.  The IDAT
			// image only goes into the animation if the first fcTL
			// precedes the first IDAT.
			if (fcTLCount != 0)
				frameDataAvail = true;

			// flag that we've found the IDAT
			hasIDAT = true;
			break;

		case ID_fdAT:
			// Animation frame data chunk.  Each animation frame after the
			// first is represented by one of these chunks.  This is the
			// equivalent of an IDAT chunk, but simply uses a separate ID
			// so that the file obeys the rule that IDAT is unique.  Process
			// this through libpng as though it were an IDAT image frame.
			// Note that we have to make the fdAT look like an IDAT, by 
			// patching bytes +4..+11 to make them look like an IDAT
			// header that's 16 bytes smaller than the actual chunk.
			ReadChunkContents(chunk);
			png_save_uint_32(chunk.data.get() + 4, chunk.size - 16);
			memcpy(chunk.data.get() + 8, "IDAT", 4);
			ProcessChunk(chunk.data.get() + 4, chunk.size - 4);

			// we now have image data for the current animation frame
			frameDataAvail = true;
			break;

		case ID_IEND:
			// end marker - flag that we're at EOF
			ReadChunkContents(chunk);
			eof = true;

			// if we have image data, compose the last frame into frameCur
			if (hasIDAT && EndProcessing())
			{
				// compose the frame
				ComposeFrame(frameCur.rows.get(), frameRaw.rows.get(), fcTL.bop, fcTL.x, fcTL.y, fcTL.width, fcTL.height);
				frameCur.delayNum = fcTL.delayNum;
				frameCur.delayDen = fcTL.delayDen;

				// we have a frame available
				return true;
			}
			else
			{
				// no frame available
				return false;
			}

		default:
			// read the rest of the chunk; ignore it if it doesn't have a valid PNG ID
			ReadChunkContents(chunk);
			if (IsValidPngId(id))
			{
				// process it
				ProcessChunk(chunk.data.get(), chunk.size);

				// if we haven't reached the first frame's IDAT record yet, 
				// save it for replay on subsequent fraames
				if (!hasIDAT)
					infoChunks.emplace_back(chunk.data.release(), chunk.size);
			}
			break;
		}
	}

	// unexpected end of file - no more frames available
	return false;
}

// PNG chunk header reader.  This reads just the chunk length and ID.
// Returns the ID.
DWORD Sprite::APNGLoaderState::ReadChunkSizeAndID(Chunk &chunk)
{
	// read the chunk length
	if (fread(chunk.header, 1, 8, fp) == 8)
	{
		// save the size
		chunk.size = png_get_uint_32(chunk.header) + 12;

		// return the ID
		return png_get_uint_32(chunk.header + 4);
	}

	// failed - return ID zero
	return 0;
}

// Read the rest of a chunk
void Sprite::APNGLoaderState::ReadChunkContents(Chunk &chunk)
{
	// allocate space
	chunk.data.reset(new BYTE[chunk.size]);

	// copy the 8 header bytes we've already read
	memcpy(chunk.data.get(), chunk.header, 8);
	
	// read the rest of the chunk
	fread(chunk.data.get() + 8, 1, chunk.size - 8, fp);
}

// Skip the rest of a chunk
void Sprite::APNGLoaderState::SkipChunkContents(Chunk &chunk)
{
	// skip the rest of the chunk after the 8 header bytes we've already read
	fseek(fp, chunk.size - 8, SEEK_CUR);
}

// PNG chunk reader
DWORD Sprite::APNGLoaderState::ReadChunk(Chunk &chunk)
{
	// read the chunk length
	if (fread(chunk.header, 1, 4, fp) == 4)
	{
		// set up the Chunk descriptor and allocate space for the contents
		chunk.size = png_get_uint_32(chunk.header) + 12;
		chunk.data.reset(new BYTE[chunk.size]);

		// the first 4 bytes are the length
		memcpy(chunk.data.get(), chunk.header, 4);

		// read the remainder
		if (fread(chunk.data.get() + 4, 1, chunk.size - 4, fp) == chunk.size - 4)
			return png_get_uint_32(chunk.data.get() + 4);
	}

	// failed - return ID zero
	return 0;
}

// Start processing a PNG image
bool Sprite::APNGLoaderState::StartProcessing()
{
	// create the libpng reading context and info struct
	if ((png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr)) == nullptr
		|| (pInfo = png_create_info_struct(png)) == nullptr)
		return false;

	// set up libpng's C-setjmp-style exception handler
	if (setjmp(png_jmpbuf(png)))
	{
		png_destroy_read_struct(&png, &pInfo, nullptr);
		return false;
	}

	// initialize reading
	png_set_crc_action(png, PNG_CRC_QUIET_USE, PNG_CRC_QUIET_USE);
	png_set_progressive_read_fn(png, &frameRaw, &InfoCallback, &RowCallback, nullptr);

	// process the standard PNG file header
	BYTE header[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	png_process_data(png, pInfo, header, 8);

	// process the IHDR chunk
	png_process_data(png, pInfo, IHDR.data.get(), IHDR.size);

	// process pre-IDAT info chunks
	if (this->hasIDAT)
	{
		for (auto &chunk : infoChunks)
			png_process_data(png, pInfo, chunk.data.get(), chunk.size);
	}

	// success
	return true;
}

// libpng info callback
void PNGAPI Sprite::APNGLoaderState::InfoCallback(png_structp png, png_infop pInfo)
{
	png_set_expand(png);
	png_set_strip_16(png);
	png_set_gray_to_rgb(png);
	png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
	(void)png_set_interlace_handling(png);
	png_read_update_info(png, pInfo);
}

// libpng progressive row callback
void PNGAPI Sprite::APNGLoaderState::RowCallback(png_structp png, png_bytep pRow, png_uint_32 rowNum, int pass)
{
	APNGFrame *frame = static_cast<APNGFrame*>(png_get_progressive_ptr(png));
	png_progressive_combine_row(png, frame->rows.get()[rowNum], pRow);
}

// Process a PNG IDAT frame
void Sprite::APNGLoaderState::ProcessChunk(BYTE *data, UINT size)
{
	// validate the context
	if (png == nullptr || pInfo == nullptr)
		return;

	// set up error handling
	if (setjmp(png_jmpbuf(png)))
	{
		png_destroy_read_struct(&png, &pInfo, nullptr);
		return;
	}

	// process the data
	png_process_data(png, pInfo, data, size);
}

// Finish image processing
bool Sprite::APNGLoaderState::EndProcessing()
{
	// validate the context
	if (png == nullptr || pInfo == nullptr)
		return false;

	// set up error handling
	if (setjmp(png_jmpbuf(png)))
	{
		png_destroy_read_struct(&png, &pInfo, nullptr);
		return false;
	}

	// process the image end chunk
	BYTE endChunk[12] = { 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130 };
	png_process_data(png, pInfo, endChunk, 12);

	// tear down the libpng context objects
	png_destroy_read_struct(&png, &pInfo, nullptr);

	// success
	return true;
}

// Compose an APNG frame
void Sprite::APNGLoaderState::ComposeFrame(BYTE **dst, const BYTE *const *src, UINT bop, UINT x, UINT y, UINT width, UINT height)
{
	// iterate over rows
	for (UINT row = 0; row < height; row++)
	{
		// figure the pixel pointer in the source, at the start of the current row
		const BYTE *sp = src[row];

		// figure the pixel position in the destination, at the start of the
		// current row within the (x,y) x (width,height) window
		BYTE *dp = dst[y + row] + 4*x;

		// check the blend operation
		if (bop == BOP_SRC)
		{
			// source copy
			memcpy(dp, sp, width * 4);
		}
		else if (bop == BOP_OVER)
		{
			// apha blend - iterate over columns
			for (UINT col = 0; col < width; col++, sp += 4, dp += 4)
			{
				// check the source alpha
				if (sp[3] == 255)
				{
					// opaque source - just copy
					memcpy(dp, sp, 4);
				}
				else if (sp[3] != 0)
				{
					// check the destination alpha
					if (dp[3] == 0)
					{
						// transparent destination - just copy
						memcpy(dp, sp, 4);
					}
					else
					{
						// alpha blend the pixels
						int u = sp[3] * 255;
						int v = (255 - sp[3])*dp[3];
						int a = u + v;
						dp[0] = static_cast<BYTE>((sp[0] * u + dp[0] * v) / a);
						dp[1] = static_cast<BYTE>((sp[1] * u + dp[1] * v) / a);
						dp[2] = static_cast<BYTE>((sp[2] * u + dp[2] * v) / a);
						dp[3] = static_cast<BYTE>(a / 255);
					}
				}
			}
		}
	}
}

// Fill a rectangle in a GIF image under construction
static void FillGIFRect(const Image &img, const RECT &destRect, uint32_t color)
{
	RECT clipped =
	{
		(destRect.left < 0) ? 0 : destRect.left,
		(destRect.top < 0) ? 0 : destRect.top,
		(destRect.right > static_cast<long>(img.width)) ? static_cast<long>(img.width) : destRect.right,
		(destRect.bottom > static_cast<long>(img.height)) ? static_cast<long>(img.height) : destRect.bottom
	};

	uint8_t *ptr = img.pixels
		+ static_cast<size_t>(clipped.top) * img.rowPitch 
		+ static_cast<size_t>(clipped.left) * sizeof(uint32_t);

	for (long y = clipped.top; y < clipped.bottom; ++y)
	{
		auto pixelPtr = reinterpret_cast<uint32_t*>(ptr);
		for (long x = clipped.left; x < clipped.right; ++x)
			*pixelPtr++ = color;

		ptr += img.rowPitch;
	}
}

// Blend a rectangle in a GIF image under construction.  This has
// a very restricted notion of alpha transparency: the alpha in the
// source image is either 00 (fully transparent) or FF (fully opaque).
// Anything else is considered opaque.  This won't accomplish full
// alpha blending, but full alpha isn't needed for GIF loading, as
// GIF only has transparent and opaque pixels.  (So why, you might
// ask, do we have an alpha channel at all here?  It's because we're
// working in terms of WIC-decoded image frames, which WIC has already
// converted to ABGR format in memory.  WIC translates the transparent
// color index in the GIF to A=00 in the ABGR pixel data.  So we have
// what looks like a full alpha-channel image.  But we know we'll
// never see any alpha values other than 00 or FF, because of the
// nature of the source data, so we can skip the computationally
// expensive alpha blend step; the blend is always 100% source or
// 100% destination, so we just pick the pixel to keep.)
static void BlendGIFRect(const Image &composed, const Image &raw, const RECT &destRect)
{
	RECT clipped =
	{
		(destRect.left < 0) ? 0 : destRect.left,
		(destRect.top < 0) ? 0 : destRect.top,
		(destRect.right > static_cast<long>(composed.width)) ? static_cast<long>(composed.width) : destRect.right,
		(destRect.bottom > static_cast<long>(composed.height)) ? static_cast<long>(composed.height) : destRect.bottom
	};

	uint8_t *rawPtr = raw.pixels;
	uint8_t *composedPtr = composed.pixels 
		+ size_t(clipped.top) * composed.rowPitch 
		+ size_t(clipped.left) * sizeof(uint32_t);

	for (long y = clipped.top; y < clipped.bottom; ++y)
	{
		auto srcPtr = reinterpret_cast<uint32_t*>(rawPtr);
		auto destPtr = reinterpret_cast<uint32_t*>(composedPtr);
		for (long x = clipped.left; x < clipped.right; ++x, ++destPtr, ++srcPtr)
		{
			if ((*srcPtr & 0xFF000000) != 0)
				*destPtr = *srcPtr;
		}

		rawPtr += raw.rowPitch;
		composedPtr += composed.rowPitch;
	}
}

// Load a GIF, with animation support
bool Sprite::LoadGIF(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh)
{
	// system errors
	HRESULT hr = E_FAIL;
	auto SysErr = [filename, &hr, &eh](const CHAR *details)
	{
		WindowsErrorMessage sysErr(hr);
		eh.SysError(MsgFmt(IDS_ERR_IMGLOAD, filename), 
			MsgFmt(_T("GIF loader: %hs (HRESULT %lx: %s)"), details, hr, sysErr.Get()));
		return false;
	};

	// get the WIC factory
	bool isWIC2;
	auto pWIC = GetWICFactory(isWIC2);
	if (pWIC == nullptr)
		return (hr = E_NOINTERFACE), SysErr("Unable to get WIC factory");

	// create the image decoder
	RefPtr<IWICBitmapDecoder> decoder;
	if (FAILED(hr = pWIC->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
		return SysErr("Unable to create bitmap decoder");

	// read the frame count
	UINT nFrames;
	if (FAILED(hr = decoder->GetFrameCount(&nFrames)))
		return SysErr("Unable to read frame count");

	// If the frame count is zero or one, there's no need to do anything
	// fancy for animation support.  We can just use the regular WIC loader.
	if (nFrames <= 1)
		return LoadWICTexture(filename, normalizedSize, eh);

	// get the file format
	GUID containerFormat;
	if (FAILED(hr = decoder->GetContainerFormat(&containerFormat)))
		return SysErr("Unable get container file format");

	// verify that it's a GIF file - if it's not, load it using
	// the basic WIC image file loader instead
	if (memcmp(&containerFormat, &GUID_ContainerFormatGif, sizeof(GUID)) != 0)
		return LoadWICTexture(filename, normalizedSize, eh);

	// get the metadata reader
	RefPtr<IWICMetadataQueryReader> meta;
	if (FAILED(hr = decoder->GetMetadataQueryReader(&meta)))
		return SysErr("Unable to get metadata reader");

	// get the frame size
	auto ReadInt = [&meta, &hr, &SysErr](UINT &val, const WCHAR *metaName)
	{
		PROPVARIANTEx prop;
		if (FAILED(hr = meta->GetMetadataByName(metaName, &prop)))
			return SysErr("Unable to read metadata property");

		if (prop.vt != VT_UI2)
			return (hr = E_UNEXPECTED), SysErr("Metadata property is wrong type");

		val = prop.uiVal;
		return true;
	};

	UINT width, height;
	if (!ReadInt(width, L"/logscrdesc/Width") || !ReadInt(height, L"/logscrdesc/Height"))
		return false;

	// presume we'll use transparency as the background color
	WICColor bgColor = 0;

	// Check for an explicit background color 
	PROPVARIANTEx colorTableFlag;
	if (SUCCEEDED(meta->GetMetadataByName(L"/logscrdesc/GlobalColorTableFlag", &colorTableFlag))
		&& colorTableFlag.vt == VT_BOOL
		&& colorTableFlag.boolVal)
	{
		PROPVARIANTEx bgColorIndex;
		if (SUCCEEDED(meta->GetMetadataByName(L"/logscrdesc/BackgroundColorIndex", &bgColorIndex))
			&& bgColorIndex.vt == VT_UI1)
		{
			// create a palette
			RefPtr<IWICPalette> palette;
			if (FAILED(hr = pWIC->CreatePalette(&palette)))
				return SysErr("Unable to create palette");

			// copy the image palette
			if (FAILED(hr = decoder->CopyPalette(palette)))
				return SysErr("Unable to copy palette");

			// retrieve the colors
			WICColor rgColors[256];
			UINT actualColors = 0;
			if (FAILED(hr = palette->GetColors(_countof(rgColors), rgColors, &actualColors)))
				return SysErr("Unable to retrieve palette colors");

			// look up the color in the palette
			uint8_t index = bgColorIndex.bVal;
			if (index < actualColors)
				bgColor = rgColors[index];
		}
	}

	// Set up the frame decoder state
	std::unique_ptr<GIFLoaderState> loader(new GIFLoaderState());
	loader->Init(pWIC, decoder, width, height, nFrames, bgColor, filename);

	// create the mesh
	if (!CreateMesh(normalizedSize, eh, MsgFmt(_T("file \"%ws\""), filename)))
		return false;

	// decode the first frame; if that doesn't leave us with one frame in the 
	// frame list, the decoding failed, so fail the whole load
	loader->DecodeFrame(this);
	if (animFrames.size() == 0)
		return false;

	// Initialize the animation.  Start at the first frame, and set the end time
	// for the frame to the current time plus the frame's display time.
	animRunning = true;
	curAnimFrame = 0;
	curAnimFrameEndTime = GetTickCount64() + animFrames[0]->dt;

	// allocate a media player cookie, so that we can generate AVPXxx messages
	// related to the playback
	animCookie = AudioVideoPlayer::AllocMediaCookie();

	// transfer ownership of the loader to the Sprite
	animation.reset(loader.release());

	// success
	return true;
}

void Sprite::GIFLoaderState::DecodeFrame(Sprite *sprite)
{
	// if we've decoded the last frame, we're done
	if (iFrame >= nFrames)
		return;

	// log any errors
	HRESULT hr;
	auto SysErr = [this, &hr](const CHAR *details)
	{
		// log the error
		LogFileErrorHandler eh;
		WindowsErrorMessage sysErr(hr);
		eh.SysError(MsgFmt(IDS_ERR_IMGLOAD, filename.c_str()),
			MsgFmt(_T("GIF loader: %hs (HRESULT %lx: %s)"), details, hr, sysErr.Get()));

		// clear resources to stop further decoding
		Clear();
	};

	// create a scratch image frame
	std::unique_ptr<ScratchImage> image(new (std::nothrow) ScratchImage);
	if (image == nullptr)
		return (hr = E_OUTOFMEMORY), SysErr("Unable to allocate frame memory");

	// initialize the frame, using the previous frame if we have one,
	// otherwise a blank background
	if (disposal == DM_PREVIOUS)
		hr = image->InitializeFromImage(*frames[prevFrame]->GetImage(0, 0, 0));
	else if (iFrame > 0)
		hr = image->InitializeFromImage(*frames[iFrame - 1]->GetImage(0, 0, 0));
	else
		hr = image->Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM, rcFull.right, rcFull.bottom, 1, 1);

	if (FAILED(hr))
		return SysErr("Unable to initialize image frame");

	// get the current image as the starting point for composition
	auto composedImage = image->GetImage(0, 0, 0);

	// fill the whole first frame with the background; fill later
	// frames over the update area
	if (iFrame == 0)
		FillGIFRect(*composedImage, rcFull, bgColor);
	else if (disposal == DM_BACKGROUND)
		FillGIFRect(*composedImage, rcSub, bgColor);

	// decode the frame
	RefPtr<IWICBitmapFrameDecode> decodedFrame;
	if (FAILED(hr = decoder->GetFrame(iFrame, &decodedFrame)))
		return SysErr("Unable to decode frame");

	// get the pixel format
	WICPixelFormatGUID pixFmt;
	if (FAILED(hr = decodedFrame->GetPixelFormat(&pixFmt)))
		return SysErr("Unable to get decoded frame foramt");

	// Make sure it's an indexed (paletted) 8-bit format, as that's the
	// only format GIF should support.
	if (memcmp(&pixFmt, &GUID_WICPixelFormat8bppIndexed, sizeof(GUID)) != 0)
		return (hr = E_UNEXPECTED), SysErr("Wrong pixel format for frame (should be 8bpp indexed)");

	// Try getting the metadata for this frame.  It's not an error
	// if we can't get the reader, as the frame might not have any
	// metadata.
	LONG delay = 0;
	RefPtr<IWICMetadataQueryReader> meta;
	if (SUCCEEDED(decodedFrame->GetMetadataQueryReader(&meta)))
	{
		// Read the sub-rectangle metadata for this frame.  The
		// frame might only have partial metadata, so it's not an
		// error if we can't read any of the individual items.
		LONG lval;
		auto ReadDim = [&meta, &lval](const WCHAR *name)
		{
			PROPVARIANTEx prop;
			if (SUCCEEDED(meta->GetMetadataByName(name, &prop))
				&& prop.vt == VT_UI2)
			{
				lval = static_cast<LONG>(prop.uiVal);
				return true;
			}
			return false;
		};
		if (ReadDim(L"/imgdesc/Left"))
			rcSub.left = lval;
		if (ReadDim(L"/imgdesc/Top"))
			rcSub.top = lval;
		if (ReadDim(L"/imgdesc/Width"))
			rcSub.right = rcSub.left + lval;
		if (ReadDim(L"/imgdesc/Height"))
			rcSub.bottom = rcSub.top + lval;

		// get the disposal for the frame
		disposal = DM_UNDEFINED;
		PROPVARIANTEx dprop;
		if (SUCCEEDED(meta->GetMetadataByName(L"/grctlext/Disposal", &dprop))
			&& dprop.vt == VT_UI1)
			disposal = static_cast<disposal_t>(dprop.bVal);

		// get the frame delay time - this is in 10ms units in the GIF file
		if (ReadDim(L"/grctlext/Delay"))
			delay = lval * 10;
	}

	UINT w, h;
	if (FAILED(hr = decodedFrame->GetSize(&w, &h)))
		return SysErr("Unable to read frame size");

	// initialize a working frame
	ScratchImage rawFrame;
	if (FAILED(hr = rawFrame.Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM, w, h, 1, 1)))
		return SysErr("Unable to initialize working frmae for composition");

	// set up a converter
	RefPtr<IWICFormatConverter> conv;
	if (FAILED(hr = pWIC->CreateFormatConverter(&conv)))
		return SysErr("Unable to create format converter");

	// initialize the converter
	if (FAILED(hr = conv->Initialize(decodedFrame, GUID_WICPixelFormat32bppBGRA,
		WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)))
		return SysErr("Unable to initialize format converter");

	// get the frame contents
	auto img = rawFrame.GetImage(0, 0, 0);
	if (FAILED(hr = conv->CopyPixels(nullptr, static_cast<UINT>(img->rowPitch), static_cast<UINT>(img->slicePitch), img->pixels)))
		return SysErr("Unable to copy pixels to raw frame");

	// copy the first frame, or blend the new frame with the last frame
	if (iFrame == 0)
	{
		Rect rcFull(0, 0, img->width, img->height);
		if (FAILED(hr = CopyRectangle(*img, rcFull, *composedImage, TEX_FILTER_DEFAULT,
			static_cast<size_t>(rcSub.left), static_cast<size_t>(rcSub.top))))
			return SysErr("Unable to copy first frame");
	}
	else
	{
		BlendGIFRect(*composedImage, *img, rcSub);
	}

	// if we're not reverting to the previous frame, this frame
	// will be the previous frame for the next frame with 
	// disposal method DM_PREVIOUS
	if (disposal != DM_PREVIOUS)
		prevFrame = iFrame;

	// Create a D3D texture and shader resource view for the frame
	{
		// get the image data
		auto imageData = image->GetImage(0, 0, 0);

		// create an animation frame
		auto animFrame = sprite->animFrames.emplace_back(new AnimFrame()).get();
		animFrame->dt = static_cast<DWORD>(delay);

		// set up the D3D texture descriptor
		D3D11_TEXTURE2D_DESC txd = CD3D11_TEXTURE2D_DESC(
			DXGI_FORMAT_B8G8R8A8_UNORM,
			static_cast<UINT>(imageData->width), static_cast<UINT>(imageData->height),
			1, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE,
			1, 0, 0);

		// set up the subresource descriptor
		D3D11_SUBRESOURCE_DATA srd;
		ZeroMemory(&srd, sizeof(srd));
		srd.pSysMem = imageData->pixels;
		srd.SysMemPitch = static_cast<UINT>(imageData->rowPitch);
		srd.SysMemSlicePitch = static_cast<UINT>(imageData->slicePitch);

		// set up the shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC svd;
		svd.Format = txd.Format;
		svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		svd.Texture2D.MipLevels = txd.MipLevels;
		svd.Texture2D.MostDetailedMip = 0;

		// create the texture and resource view
		if (FAILED(hr = D3D::Get()->CreateTexture2D(&txd, &srd, &svd, &animFrame->rv, &animFrame->texture)))
			return SysErr("CreateTexture2D failed");
	}

	// Add the image to the results, to facilitate composing subsequent frames
	frames.emplace_back(std::move(image));

	// advance to the next frame
	++iFrame;

	// if we're done, clear resources
	if (iFrame >= nFrames)
		Clear();
}

bool Sprite::CreateStagingTexture(int pixWidth, int pixHeight, ErrorHandler &eh)
{
	// release any prior texture
	stagingTexture = 0;

	// create the new one
	D3D11_TEXTURE2D_DESC txd = CD3D11_TEXTURE2D_DESC(
		DXGI_FORMAT_B8G8R8A8_UNORM, pixWidth, pixHeight, 1, 1,
		0, D3D11_USAGE_STAGING,	D3D11_CPU_ACCESS_WRITE, 1, 0, 0);
	HRESULT hr;
	if (FAILED(hr = D3D::Get()->GetDevice()->CreateTexture2D(&txd, NULL, &stagingTexture)))
	{
		WindowsErrorMessage winMsg(hr);
		eh.SysError(
			MsgFmt(IDS_ERR_IMGCREATE, _T("Create staging texture")),
			MsgFmt(_T("Sprite::Load, CreateTexture2D() failed, system error %ld: %s"), (long)hr, winMsg.Get()));
		return false;
	}

	// success
	return true;
}

bool Sprite::Load(int pixWidth, int pixHeight, std::function<void(Gdiplus::Graphics&)> drawingFunc,
	ErrorHandler &eh, const TCHAR *descForErrors)
{
	return Load(pixWidth, pixHeight, [&drawingFunc](HDC hdc, HBITMAP)
	{
		// set up the Gdiplus context from the HDC
		Gdiplus::Graphics g(hdc);

		// do the drawing through the user's callback
		drawingFunc(g);

		// flush the Gdiplus context to the underlying bitmap
		g.Flush();
	}, eh, descForErrors);
}

bool Sprite::Load(int pixWidth, int pixHeight, std::function<void(HDC, HBITMAP)> drawingFunc,
	ErrorHandler &eh, const TCHAR *descForErrors)
{
	// set up a bitmap and do the off-screen drawing
	bool ret;
	DrawOffScreen(pixWidth, pixHeight, [this, &ret, &drawingFunc, &eh, descForErrors]
	    (HDC hdc, HBITMAP hbmp, const void *dibits, const BITMAPINFO &bmi)
	{
		// invoke the caller's drawing function
		drawingFunc(hdc, hbmp);

		// load the sprite texture from the memory bitmap
		ret = Load(bmi, dibits, eh, descForErrors);
	});

	// return the result
	return ret;
}

bool Sprite::Load(HDC hdc, HBITMAP hbitmap, ErrorHandler &eh, const TCHAR *descForErrors)
{
	// get the size of the bitmap
	BITMAP bm;
	if (!GetObject(hbitmap, sizeof(bm), &bm))
	{
		DWORD err = GetLastError();
		eh.SysError(
			MsgFmt(IDS_ERR_IMGCREATE, descForErrors),
			MsgFmt(_T("Sprite::Load, GetObject(HBITMAP) failed, system error %ld"), (long)err));
		return false;
	}

	// retrieve the pixels from the bitmap
	BITMAPINFO bmi;
	ZeroMemory(&bmi, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biWidth = bm.bmWidth;
	bmi.bmiHeader.biHeight = -bm.bmHeight;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biSizeImage = 0;
	std::unique_ptr<BYTE> pixels(new BYTE[bmi.bmiHeader.biBitCount / 4 * bm.bmWidth * bm.bmHeight]);
	int rows = GetDIBits(hdc, hbitmap, 0, bm.bmHeight, pixels.get(), &bmi, DIB_RGB_COLORS);
	if (rows == 0)
	{
		DWORD err = GetLastError();
		eh.SysError(
			MsgFmt(IDS_ERR_IMGCREATE, descForErrors),
			MsgFmt(_T("Sprite::Load, GetDIBits failed, system error %ld"), (long)err));
		return false;
	}

	// load from the DI bits
	return Load(bmi, pixels.get(), eh, descForErrors);
}

bool Sprite::Load(const DIBitmap &dib, ErrorHandler &eh, const TCHAR *descForErrors)
{
	return Load(dib.bmi, dib.dibits, eh, descForErrors);
}

bool Sprite::Load(const BITMAPINFO &bmi, const void *dibits, ErrorHandler &eh, const TCHAR *descForErrors)
{
	// load the bitmap
	if (!CreateTextureFromBitmap(bmi, dibits, eh, descForErrors))
		return false;

	// create the mesh, scaled to our reference 1920-pixel height
	if (!CreateMesh({ float(bmi.bmiHeader.biWidth) / 1920.0f, float(abs(bmi.bmiHeader.biHeight)) / 1920.0f }, eh, descForErrors))
		return false;

	// success
	return true;
}

bool Sprite::CreateTextureFromBitmap(const BITMAPINFO &bmi, const void *dibits, ErrorHandler &eh, const TCHAR *descForErrors)
{
	// set up a new load context
	loadContext.Attach(new LoadContext());

	// clear the old staging texture, if any
	stagingTexture = nullptr;

	// Figure the pixel width and height from the bitmap header.  Note
	// that the header height will be negative for a top-down bitmap
	// (the normal arrangement), so use the absolute value.
	int wid = bmi.bmiHeader.biWidth;
	int ht = abs(bmi.bmiHeader.biHeight);

	// set up the D3D texture descriptor
	D3D11_TEXTURE2D_DESC txd = CD3D11_TEXTURE2D_DESC(
		DXGI_FORMAT_B8G8R8A8_UNORM, wid, ht, 1, 1, 
		D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE,
		1, 0, 0);

	// set up the subresource descriptor
	D3D11_SUBRESOURCE_DATA srd;
	ZeroMemory(&srd, sizeof(srd));
	srd.pSysMem = dibits;
	srd.SysMemPitch = bmi.bmiHeader.biBitCount/8 * wid;
	srd.SysMemSlicePitch = srd.SysMemPitch * ht;

	// set up the shader resource view
	D3D11_SHADER_RESOURCE_VIEW_DESC svd;
	svd.Format = txd.Format;
	svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	svd.Texture2D.MipLevels = txd.MipLevels;
	svd.Texture2D.MostDetailedMip = 0;

	// create the texture
	HRESULT hr = D3D::Get()->CreateTexture2D(&txd, &srd, &svd, &loadContext->rv, &loadContext->texture);
	if (!SUCCEEDED(hr))
	{
		WindowsErrorMessage winMsg(hr);
		eh.SysError(
			MsgFmt(IDS_ERR_IMGCREATE, descForErrors),
			MsgFmt(_T("Sprite::Load, CreateTexture2D failed, HRESULT %lx: %s"), (long)hr, winMsg.Get()));
		return false;
	}

	// success
	return true;
}

void Sprite::ReCreateMesh()
{
	CreateMesh(loadSize, SilentErrorHandler(), _T("Sprite::ReCreateMesh"));
}

bool Sprite::CreateMesh(POINTF sz, ErrorHandler &eh, const TCHAR *descForErrors)
{
	// remove any prior resources
	vertexBuffer = nullptr;
	indexBuffer = nullptr;

	// get the D3D interface
	D3D *d3d = D3D::Get();

	// vertex list for our rectangle
	const CommonVertex v[] = {
		{ XMFLOAT4(-sz.x / 2.0f, sz.y / 2.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f), XMFLOAT3(0, 1, 0) },  // top left
		{ XMFLOAT4(sz.x / 2.0f, sz.y / 2.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f), XMFLOAT3(0, 1, 0) },   // top right
		{ XMFLOAT4(sz.x / 2.0f, -sz.y / 2.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f), XMFLOAT3(0, 1, 0) },   // bottom right
		{ XMFLOAT4(-sz.x / 2.0f, -sz.y / 2.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f), XMFLOAT3(0, 1, 0) }  // bottom left
	};

	// index list for a rectangle
	static const WORD flIndex[] = {
		0, 1, 2,	// top face 1
		2, 3, 0		// top face 2
	};

	// set up the vertex buffer descriptor
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = (UINT)(sizeof(CommonVertex) * 4);
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	// set up the subresource descriptor
	D3D11_SUBRESOURCE_DATA sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.pSysMem = v;

	// create the vertex buffer
	HRESULT hr = d3d->CreateBuffer(&bd, &sd, &vertexBuffer, "Sprite::vertexBuffer");
	if (!SUCCEEDED(hr))
	{
		WindowsErrorMessage winMsg(hr);
		eh.SysError(
			MsgFmt(IDS_ERR_IMGMESH, descForErrors),
			MsgFmt(_T("D3D CreateBuffer(vertices) failed, HRESULT %lx: %s"), (long)hr, winMsg.Get()));
		return false;
	}

	// create the index buffer
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = (UINT)(sizeof(WORD) * countof(flIndex));
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = 0;
	sd.pSysMem = flIndex;
	hr = d3d->CreateBuffer(&bd, &sd, &indexBuffer, "Sprite::indexBuffer");
	if (!SUCCEEDED(hr))
	{
		WindowsErrorMessage winMsg(hr);
		eh.SysError(
			MsgFmt(IDS_ERR_IMGMESH, descForErrors),
			MsgFmt(_T("D3D CreateBuffer(indices) failed, HRESULT %lx: %s"), (long)hr, winMsg.Get()));
		return false;
	}

	// remember the load size
	loadSize = sz;

	// success
	return true;
}

void Sprite::Render(Camera *camera)
{
	// If there's no loader context, or it's not ready, we don't have 
	// anything to render
	if (loadContext == nullptr || !loadContext->ready)
		return;

	// If we have a flash object, update its bitmap contents if necessary.
	// This requires copying the DIB bits into the D3D texture, so it's
	// a fairly time-consuming operation that we want to avoid when
	// possible.  Fortunately, Flash uses an invalidate/paint model to
	// update the bitmap, so we can easily tell whether or not a redraw
	// is needed on each rendering cycle.  If the Flash backing bitmap
	// hasn't been invalidated since we last copied it into our texture,
	// we can simply reuse the existing texture, which is very fast.
	if (flashSite != nullptr && flashSite->NeedsRedraw())
	{
		// Note if the size has changed
		bool sizeChanged = flashSite->IsSizeChanged();

		// Get the updated bitmap.  Note that the Flash client site
		// owns the bitmap, so we're not responsible for deleting the
		// HBITMAP or the pixel bits.
		void *bits = nullptr;
		BITMAPINFO bmi;
		HBITMAP hbmp = flashSite->GetBitmap(NULL, &bits, &bmi);

		// Presume we'll need to copy the updated bitmap into our texture.  
		// We'll need to do this unless the layout size changed, in which
		// case we're going to create a whole new main texture, which we'll
		// initialize from the updated bitmap we just got.
		bool copyBitmap = true;

		// If the size changed, we need to re-create our texture at the new size
		if (sizeChanged)
		{
			// re-create our main texture and shader resource view
			SilentErrorHandler eh;
			if (!CreateTextureFromBitmap(bmi, bits, eh, _T("Load Shockwave Flash frame")))
				return;

			// re-create our staging texture
			if (!CreateStagingTexture(bmi.bmiHeader.biWidth, abs(bmi.bmiHeader.biHeight), eh))
				return;

			// We no longer need to copy the updated bitmap to the texture,
			// because we just created a brand new texture using the bitmap
			// as the initial contents.
			copyBitmap = false;
		}

		// If necessary, copy the updated Flash bitmap to the shader texture
		if (copyBitmap)
		{
			// map the staging texture
			D3D::DeviceContextLocker devctx;
			D3D11_MAPPED_SUBRESOURCE msr;
			if (SUCCEEDED(devctx->Map(stagingTexture, 0, D3D11_MAP_WRITE, 0, &msr)))
			{
				// Copy the DIB bits into the staging texture.  Note that we could
				// potentially make this faster by limiting the copy to the invalid
				// region of the Flash drawing area.  That's possible because Flash
				// tells the container site which areas are invalid via the
				// IOleInPlaceSiteWindowless::InvalidateRect and InvalidateRgn
				// functions.  Our container site currently ignores the invalid 
				// region information, but it could collect it and hand it back to
				// us, and we could use it to limit the pixel copying to the areas
				// marked as invalid.  At the moment, I don't think the added
				// complexity is justified, because we only use SWF objects for
				// instruction cards.  Those tend to consist of a single animation
				// frame with the static image of the card, so we'll have exactly
				// one update (for the first frame) in the course of displaying a
				// card.  And that one update will cover the whole area.  So we'd
				// gain nothing at all for our typical case from this optimization.
				const BYTE *src = (const BYTE *)bits;
				BYTE *dst = (BYTE *)msr.pData;
				DWORD srcRowPitch = bmi.bmiHeader.biWidth * 4;
				for (UINT row = abs(bmi.bmiHeader.biHeight); row != 0; --row)
				{
					// copy one BGRA row
					memcpy(dst, src, srcRowPitch);

					// advance pointers
					dst += msr.RowPitch;
					src += srcRowPitch;
				}

				// unmap the texture
				devctx->Unmap(stagingTexture, 0);

				// Copy the staging texture to the shader texture.  Note that if
				// we had information about the invalid subregion (see the comments
				// before the bitmap copy above), we could use CopySubresourceRegion() 
				// here to do a faster copy in cases where only a portion of the 
				// texture has been updated.  As discussed above, that optimization
				// doesn't gain us anything for our typical "instruction card" use
				// case, so we keep it simple and just copy the whole texture.
				devctx->CopyResource(loadContext->texture, stagingTexture);
			}
		}
	}

	// Assume we'll use the still-frame shader resource view
	ID3D11ShaderResourceView *rvToRender = loadContext->rv;

	// check for animation
	if (animation != nullptr)
	{
		// If the animation is running, check if it's time to advance to the 
		// next frame.  We might have to advance past multiple frames, because
		// it's possible in a GIF for a frame to have a delay time of zero,
		// which means that the frame only contains a fragment, for composing
		// with the next frame to make a complete frame, and isn't meant to
		// be displayed as a separate frame.  So we skip these frames on
		// rendering.
		UINT64 now = GetTickCount64();
		while (animRunning && now >= curAnimFrameEndTime)
		{
			// If decoding is still in progress, decode the next frame
			animation->DecodeNext(this);

			// advance to the next frame; loop after the last frame
			if (++curAnimFrame >= animFrames.size())
			{
				// If we have a message window, post an end-of-loop message.
				// (Do this with a Post rather than a Send, so that we don't
				// have to the processing inline with the rendering.)
				if (msgHwnd != NULL)
					::PostMessage(msgHwnd, animLooping ? AVPMsgLoopNeeded : AVPMsgEndOfPresentation, animCookie, 0);

				// If we're looping, return to the first frame; otherwise,
				// pause the animation and stay on the last frame.
				if (animLooping)
					curAnimFrame = 0;
				else
				{
					curAnimFrame = animFrames.size() > 0 ? static_cast<UINT>(animFrames.size() - 1) : 0;
					animRunning = false;
				}
			}
			
			// Stop if we don't have a frame available.  (This can only
			// occur if decoding fails, but that's always a possibility.)
			if (curAnimFrame >= animFrames.size())
				break;

			// figure the frame end time
			curAnimFrameEndTime = now + animFrames[curAnimFrame]->dt;
		}

		// use the current frame's shader resource view
		if (curAnimFrame < animFrames.size())
			rvToRender = animFrames[curAnimFrame]->rv;
	}

	// do nothing if we don't have a shader resource view
	if (rvToRender == nullptr)
		return;

	// prepare my shader
	Shader *ts = GetShader();
	ts->PrepareForRendering(camera);
	ts->SetAlpha(UpdateFade());

	// load our texture into the pixel shader
	D3D::Get()->PSSetShaderResources(0, 1, &rvToRender);

	// do the basic mesh rendering
	RenderMesh();
}

Shader *Sprite::GetShader() const
{
	// return the basic Texture Shader by default
	return Application::Get()->textureShader.get();
}

void Sprite::RenderMesh()
{
	// we can only proceed if we have valid vertex and index buffers
	if (vertexBuffer == nullptr || indexBuffer == nullptr)
		return;

	// get the D3D context
	D3D *d3d = D3D::Get();

	// set the vertex buffer
	d3d->IASetVertexBuffer(vertexBuffer, sizeof(CommonVertex));

	// set the index buffer
	d3d->IASetIndexBuffer(indexBuffer);

	// load our world coordinates
	d3d->UpdateWorldTransform(worldT);

	// draw the vertex list
	d3d->DrawIndexed(6);
}

void Sprite::StartFade(int dir, DWORD milliseconds)
{
	alpha = dir > 0 ? 0.0f : 1.0f;
	fadeDone = false;
	fadeDir = dir;
	fadeStartTime = GetTickCount();
	fadeDuration = milliseconds;
}

float Sprite::UpdateFade()
{
	// update the fade if a fade is in progress
	if (fadeDir != 0)
	{
		// figure the delta since the starting time, as a fraction of the total time
		DWORD dt = GetTickCount() - fadeStartTime;
		float progress = fminf(1.0f, float(dt) / float(fadeDuration));

		// adjust the alpha on a linear ramp
		alpha = fadeDir > 0 ? progress : 1.0f - progress;

		// check if the fade is done
		if (progress == 1.0f)
		{
			fadeDir = 0;
			fadeDone = true;
		}
	}

	// return the updated alpha
	return alpha;
}

bool Sprite::IsFadeDone(bool reset)
{
	// stash the result
	bool result = fadeDone;

	// reset the flag if desired
	if (reset)
		fadeDone = false;

	// return the result
	return result;
}

void Sprite::AdviseWindowSize(SIZE szLayout)
{
	// If we have a Flash object, advise it of the new layout size
	if (flashSite != nullptr)
	{
		// Calculate the pixel size of the display area
		int pixWidth = (int)((float)szLayout.cy * loadSize.x * scale.x);
		int pixHeight = (int)((float)szLayout.cy * loadSize.y * scale.y);
		flashSite->SetLayoutSize({ pixWidth, pixHeight });
	}
}

void Sprite::Clear()
{
	// clear the animation frame list
	curAnimFrame = 0;
	animFrames.clear();
	animation = nullptr;
	animRunning = false;
	animation.reset();

	// forget any message target window
	msgHwnd = NULL;

	// if we have a Flash site, release it
	DetachFlash();

	// release D3D resources
	vertexBuffer = nullptr;
	indexBuffer = nullptr;
	loadContext = nullptr;
}
