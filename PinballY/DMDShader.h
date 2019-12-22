// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD Shader.  This is a specialized shader that we use to draw
// simulated DMD images in the DMD video window.  This shader renders
// a simulation of the visible 128x32 pixel structure of a DMD.
//
// Note that this doesn't have anything to do with drawing to real
// DMD devices.  This is purely for simulating a DMD on a regular
// video display.

#pragma once

#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "D3D.h"
#include "Shader.h"

class DMDShader : public Shader
{
public:
	DMDShader();
	virtual ~DMDShader();

	virtual const char *ID() const { return "DMDShader"; }

	// initialize
	virtual bool Init();

	// set shader inputs
	virtual void SetShaderInputs(Camera *camera);

	// set the alpha value in the shader resource
	void SetAlpha(float alpha) override;
	
	// set the background color
	void SetBgColor(RGBQUAD color, BYTE alpha);

protected:
	// alpha buffer type - must match the layout in TextureShaderPS.hlsl
	struct AlphaBufferType
	{
		float alpha;
		DirectX::XMFLOAT3 padding;
	};

	// background color buffer type
	struct BgColorBufferType
	{
		DirectX::XMFLOAT4 rgba;
	};

	// pixel shader inputs
	RefPtr<ID3D11Buffer> cbAlpha;
	RefPtr<ID3D11Buffer> cbBgColor;
};
