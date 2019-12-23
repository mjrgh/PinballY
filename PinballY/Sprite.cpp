// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include <wincodec.h>
#include "../Utilities/GraphicsUtil.h"
#include "../Utilities/ComUtil.h"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "../DirectXTex/DirectXTex/DirectXTex.h"
#include "D3D.h"
#include "Sprite.h"
#include "Shader.h"
#include "TextureShader.h"
#include "Application.h"
#include "FlashClient/FlashClient.h"


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

bool Sprite::Load(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh)
{
	// release any previous resources
	Clear();

	// Try to determine the image type from the file contents
	if (ImageFileDesc desc; GetImageFileInfo(filename, desc, true))
	{
		// If it's an SWF, WIC can't handle it - we have to load it through
		// our Flash client site object
		if (desc.imageType == ImageFileDesc::SWF)
			return LoadSWF(filename, normalizedSize, pixSize, eh);

		// If it's a GIF, we need to load it specially in case it's animated
		if (desc.imageType == ImageFileDesc::GIF)
			return LoadGIF(filename, normalizedSize, pixSize, eh);

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
	// create the WIC texture
	HRESULT hr = CreateWICTextureFromFileEx(D3D::Get()->GetDevice(), filename, 
		0, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, WIC_LOADER_IGNORE_SRGB, &texture, &rv);
	if (FAILED(hr))
	{
		WindowsErrorMessage winMsg(hr);
		eh.SysError(
			MsgFmt(IDS_ERR_IMGLOAD, filename),
			MsgFmt(_T("CreateWICTextureFromFile failed, HRESULT %lx: %s"), (long)hr, winMsg.Get()));
		return false;
	}

	// create the mesh
	if (!CreateMesh(normalizedSize, eh, MsgFmt(_T("file \"%ws\""), filename)))
		return false;

	// success
	return true;
}

bool Sprite::LoadSWF(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh)
{
	// release any previous resources
	Clear();

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
	// release any previous resources
	Clear();

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

	// read the frames
	std::vector<std::unique_ptr<ScratchImage>> frames;
	UINT prevFrame = 0;
	enum disposal_t { 
		DM_UNDEFINED = 0, 
		DM_NONE = 1,         // keep this frame, draw next frame on top of it
		DM_BACKGROUND = 2,   // clear the frame with the background color
		DM_PREVIOUS = 3      // revert to previous frame
	} disposal = DM_UNDEFINED;
	RECT rc = { 0, 0, 0, 0 };
	for (UINT iFrame = 0; iFrame < nFrames; ++iFrame)
	{
		// create a scratch image frame
		std::unique_ptr<ScratchImage> image(new (std::nothrow) ScratchImage);
		if (image == nullptr)
			return (hr = E_OUTOFMEMORY), "Unable to allocate frame memory";

		// initialize the frame, using the previous frame if we have one,
		// otherwise a blank background
		if (disposal == DM_PREVIOUS)
			hr = image->InitializeFromImage(*frames[prevFrame]->GetImage(0, 0, 0));
		else if (iFrame > 0)
			hr = image->InitializeFromImage(*frames[iFrame - 1]->GetImage(0, 0, 0));
		else
			hr = image->Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM, width, height, 1, 1);

		if (FAILED(hr))
			return SysErr("Unable to initialize image frame");

		// get the current image as the starting point for composition
		auto composedImage = image->GetImage(0, 0, 0);

		// fill the whole first frame with the background; fill later
		// frames over the update area
		if (iFrame == 0)
		{
			RECT rcFull = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			FillGIFRect(*composedImage, rcFull, bgColor);
		}
		else if (disposal == DM_BACKGROUND)
		{
			FillGIFRect(*composedImage, rc, bgColor);
		}

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
		meta = nullptr;
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
				rc.left = lval;
			if (ReadDim(L"/imgdesc/Top"))
				rc.top = lval;
			if (ReadDim(L"/imgdesc/Width"))
				rc.right = rc.left + lval;
			if (ReadDim(L"/imgdesc/Height"))
				rc.bottom = rc.top + lval;

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
				static_cast<size_t>(rc.left), static_cast<size_t>(rc.top))))
				return SysErr("Unable to copy first frame");
		}
		else
		{
			BlendGIFRect(*composedImage, *img, rc);
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
			auto animFrame = animFrames.emplace_back(new AnimFrame()).get();
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
			HRESULT hr = D3D::Get()->CreateTexture2D(&txd, &srd, &svd, &animFrame->rv, &animFrame->texture);
			if (!SUCCEEDED(hr))
			{
				WindowsErrorMessage winMsg(hr);
				eh.SysError(
					MsgFmt(IDS_ERR_IMGCREATE, filename),
					MsgFmt(_T("GIF loader, CreateTexture2D failed, HRESULT %lx: %s"), (long)hr, winMsg.Get()));
				return false;
			}
		}

		// Add the image to the results, to facilitate composing subsequent frames
		frames.emplace_back(std::move(image));
	}

	// create the mesh
	if (!CreateMesh(normalizedSize, eh, MsgFmt(_T("file \"%ws\""), filename)))
		return false;

	// initialize the animation
	curAnimFrame = 0;
	curAnimFrameEndTime = GetTickCount64();
	if (animFrames.size() > 0)
		curAnimFrameEndTime += animFrames[0]->dt;

	// success
	return true;
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
	DrawOffScreen(pixWidth, pixHeight, [this, &ret, drawingFunc, &eh, descForErrors]
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
	// release any previous texture
	texture = nullptr;
	stagingTexture = nullptr;
	rv = nullptr;

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
	HRESULT hr = D3D::Get()->CreateTexture2D(&txd, &srd, &svd, &rv, &texture);
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
				devctx->CopyResource(texture, stagingTexture);
			}
		}
	}

	// Assume we'll use the still-frame shader resource view
	ID3D11ShaderResourceView *rvCur = rv;

	// check for animation
	if (animFrames.size() != 0)
	{
		// Check if it's time to advance to the next frame.  Note that we
		// might have to advance past multiple frames, because it's possible
		// for a frame to have a zero delay, which means that it's only
		// there for composition purposes and won't actually be on the
		// screen for finite time.
		UINT64 now = GetTickCount64();
		while (now >= curAnimFrameEndTime)
		{
			// it's time - advance to the next frame
			if (++curAnimFrame >= animFrames.size())
				curAnimFrame = 0;

			// figure the frame end time
			curAnimFrameEndTime = now + animFrames[curAnimFrame]->dt;
		}

		// use the current frame's shader resource view
		rvCur = animFrames[curAnimFrame]->rv;
	}

	// do nothing if we don't have a shader resource view
	if (rvCur == nullptr)
		return;

	// prepare my shader
	Shader *ts = GetShader();
	ts->PrepareForRendering(camera);
	ts->SetAlpha(UpdateFade());

	// load our texture into the pixel shader
	D3D::Get()->PSSetShaderResources(0, 1, &rvCur);

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

	// if we have a Flash site, release it
	DetachFlash();

	// release D3D resources
	vertexBuffer = nullptr;
	indexBuffer = nullptr;
	texture = nullptr;
	rv = nullptr;
}
