// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// I420 Shader (YUV 4:2:0)
//
// I420 is a three-plane YUV format.  The Y plane contains 8 bits
// of luminance per pixel; the U and V planes store 8 bits per 2x2
// pixel block, so they each have half the spatial resolution of 
// the Y plane.
//
// Our I420 shader takes its input as three separate textures,
// each in DXGI_FORMAT_R8_UNORM format - one each for Y, U, and V.
// The U and V textures are half the width and height of the Y
// texture.
//
// We also have a separate, related shader for YUVA 4:2:0, which
// is exactly like YUV 4:2:0, but adds a fourth plane for an alpha
// channel (per-pixel transparency).  The fourth plane is identical
// to the Y plane (8 bits per pixel).
//
// Note that D3D DXGI 11.1 has native support for YUV formats.  We
// explicitly and intentionally DO NOT use any of the native DXGI
// YUV support, because it only exists in 11.1 and later, which
// means that it requires Windows 8 or later.  Microsoft has stated
// that Windows 7 will never have 11.1 support.  Windows 7 is still
// (as of 2019) widely used on pin cabs, so we want don't want to
// lock it out by including Win8+ API dependencies in our code. 
// If we *were* using the native support, we'd just create a single
// texture using one of the DXGI_FORMAT_*YUV* codes; but since we
// can't do that, we can instead break up the planes into separate
// textures that look like raw byte buffers to DXGI, and pass them
// to our shader for conversion to RGB.
//
// Note also that libvlc can perform conversions from YUV formats 
// to RGB, so we *could* have avoided the need for a separate shader
// by asking libvlc to the conversions and then working entirely
// with RGB formats when talking to D3D.  The reason we don't do
// that, and instead go to all of this extra trouble to provide
// our own YUV shader, is that libvlc would do the conversions on
// the CPU, which is S L O W.  The shader runs on the GPU instead.
// This kind of pixel-by-pixel operation is exactly what GPUs are 
// made for, and a mediocre GPU runs rings around a high-end CPU at
// this kind of operation.

#pragma once

#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "D3D.h"
#include "Shader.h"

// YUV 4:2:0 shader
class I420Shader : public Shader
{
public:
	I420Shader();
	virtual ~I420Shader();

	virtual const char *ID() const { return "I420Shader"; }

	// initialize
	virtual bool Init();

	// set shader inputs
	virtual void SetShaderInputs(Camera *camera);

	// Set the global alpha value.  (This is a global alpha that applies
	// to the entire image.  The 420A subclass below also allows for a
	// separate per-pixel alpha channel embedded in the video itself.)
	void SetAlpha(float alpha) override;

protected:
	// common initialization
	bool CommonInit(const BYTE *pixelShaderBytes, size_t pixelShaderBytesCnt, const char *idForErrorLog);

	// Global alpha buffer type - must match the layout in YUVShader.hlsl.
	//
	// (Don't confuse this with the Alpha channel in the YUVA subclass.  This
	// is a separate global transparency value to apply to the whole image.)
	struct AlphaBufferType
	{
		float alpha;
		DirectX::XMFLOAT3 padding;
	};

	// pixel shader input
	RefPtr<ID3D11Buffer> cbAlpha;
};


// YUVA 4:2:0 shader (YUV with Alpha)
class I420AShader : public I420Shader
{
public:
	virtual const char *ID() const { return "I420AShader"; }
	virtual bool Init();

protected:
};

// YUVA 4:4:4:4 10-bit shader (YUV with Alpha, 10-bit pixels)
class I444A10Shader : public I420Shader
{
public:
	virtual const char *ID() const { return "I444A10Shader"; }
	virtual bool Init();

protected:
};

