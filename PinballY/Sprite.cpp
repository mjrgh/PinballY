// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "GraphicsUtil.h"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
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
	if (flashSite != 0)
	{
		flashSite->Shutdown();
		flashSite = 0;
	}
}

void Sprite::UpdateWorld()
{
	world = XMMatrixIdentity();
	world = XMMatrixMultiply(world, XMMatrixScaling(scale.x, scale.y, scale.z));
	world = XMMatrixMultiply(world, XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z));
	world = XMMatrixMultiply(world, XMMatrixTranslation(offset.x, offset.y, offset.z));
	worldT = XMMatrixTranspose(world);
}

bool Sprite::Load(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh)
{
	// release any previous texture
	texture = 0;
	stagingTexture = 0;
	rv = 0;

	// If the filename ends in SWF, check if it's actually an SWF.  For historical
	// reasons*, we might encounter a JPEG or PNG image file whose extension has
	// been changed to SWF.  So go by the file content rather than the extension.
	// Likewise, treat it as an SWF if the file has an SWF signature inside, even
	// if it has a different extension.
	//
	// * Said historical reasons date back to HyperPin, the first widely used
	// pin cab front-end menu system.  According to lore, HyperPin only recognized
	// Instruction Card media files if the filenames had .SWF extensions.  But it
	// actually accepted JPEG and PNG files in these slots as long as they used
	// .SWF extensions.  PinballX reproduced this quirk, naturally, because they
	// wanted to maintain bug-for-bug compatibility with HyperPin media collections
	// to make migration easier.  Ditto for us.  I think this peculiarty in HyperPin 
	// and PinballX only applied to files with .swf suffixes, but we'll just ignore
	// the suffix across the board and go by the actual contents.  That's really 
	// the better way to do content type sensing anyway.
	ImageFileDesc desc;
	if (GetImageFileInfo(filename, desc) && desc.imageType == ImageFileDesc::SWF)
		return LoadSWF(filename, normalizedSize, pixSize, eh);

	// It's not an SWF.  Load the texture from the image file using WIC.
	HRESULT hr = CreateWICTextureFromFile(D3D::Get()->GetDevice(), filename, &texture, &rv);
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
	// release any previous texture
	texture = 0;
	stagingTexture = 0;
	rv = 0;

	// clear any old Flash site
	flashSite = nullptr;

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
	texture = 0;
	stagingTexture = 0;
	rv = 0;

	// Figure the pixel width and height from the bitmap header.  Note
	// that the header height will be negative for a top-down bitmap
	// (the normal arrangement), so use the absolute value.
	int wid = bmi.bmiHeader.biWidth;
	int ht = abs(bmi.bmiHeader.biHeight);

	// set up the D3D text descriptor
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

bool Sprite::CreateMesh(POINTF sz, ErrorHandler &eh, const TCHAR *descForErrors)
{
	// remove any prior resources
	vertexBuffer = 0;
	indexBuffer = 0;

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
	if (flashSite != 0 && flashSite->NeedsRedraw())
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

	// do nothing if we don't have a shader resource view
	if (rv == 0)
		return;

	// prepare my shader
	Shader *ts = GetShader();
	ts->PrepareForRendering(camera);
	ts->SetAlpha(UpdateFade());

	// load our texture into the pixel shader
	D3D::Get()->PSSetShaderResources(0, 1, &rv);

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
	if (vertexBuffer == 0 || indexBuffer == 0)
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
