// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "D3D.h"
#include "Shader.h"

class TextureShader : public Shader
{
public:
	TextureShader();
	virtual ~TextureShader();

	virtual const char *ID() const { return "TextureShader"; }

	// initialize
	virtual bool Init();

	// set shader inputs
	virtual void SetShaderInputs(Camera *camera);

	// set the alpha value in the shader resource
	void SetAlpha(float alpha) override;

protected:
	// alpha buffer type - must match the layout in TextureShaderPS.hlsl
	struct AlphaBufferType
	{
		float alpha;
		DirectX::XMFLOAT3 padding;
	};

	// pixel shader input
	RefPtr<ID3D11Buffer> cbAlpha;
};
