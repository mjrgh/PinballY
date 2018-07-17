// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "D3D.h"
#include "Shader.h"

class TextShader : public Shader
{
public:
	TextShader();
	virtual ~TextShader();

	virtual const char *ID() const { return "TextShader"; }

	// initialize
	virtual bool Init();

	// set shader inputs
	virtual void SetShaderInputs(Camera *camera);

	// set the color
	void SetColor(DirectX::XMFLOAT4 color);

	// Set the alpha transparency.  This shader doesn't support a separate
	// alpha level; use SetColor() instead.
	void SetAlpha(float alpha) override { };

protected:
	// color buffer type - must match the layout in TextShaderPS.hlsl
	struct ColorBufferType
	{
		DirectX::XMFLOAT4 color;
	};

	// pixel shader input
	RefPtr<ID3D11Buffer> cbColor;
};
