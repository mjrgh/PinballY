// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// I420 Shader.  
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

#pragma once

#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "D3D.h"
#include "Shader.h"

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

	// set the alpha value
	void SetAlpha(float alpha) override;

protected:
	// alpha buffer type - must match the layout in YUVShader.hlsl
	struct AlphaBufferType
	{
		float alpha;
		DirectX::XMFLOAT3 padding;
	};

	// pixel shader input
	RefPtr<ID3D11Buffer> cbAlpha;
};
