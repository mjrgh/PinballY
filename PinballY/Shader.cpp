// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "Resource.h"
#include "D3D.h"
#include "Shader.h"
#include "Sprite.h"

using namespace DirectX;

// statics
Shader *Shader::currentPreparedShader;


Shader::Shader()
{
	vs = 0;
	ps = 0;
	gs = 0;
	layout = 0;
}

Shader::~Shader()
{
	// release our shaders and input layout
	if (vs != 0) vs->Release();
	if (ps != 0) ps->Release();
	if (gs != 0) gs->Release();
	if (layout != 0) layout->Release();

	// Note that we don't have to delete our texture or
	// model object lists, because they keep only weak
	// references to their objects.
}

bool Shader::CreateInputLayout(
	D3D *d3d, 
	D3D11_INPUT_ELEMENT_DESC *layoutDesc, int layoutDescCount,
	const BYTE *shaderByteCode, int shaderByteCodeLen)
{
	HRESULT hr;
	const TCHAR *genErr = _T("An error occurred preparing the display graphics. Your system ")
		_T("might be running low on resources (system memory or graphics memory).");

	if (FAILED(hr = d3d->CreateInputLayout(layoutDesc, layoutDescCount, shaderByteCode, shaderByteCodeLen, &layout)))
	{
		LogSysError(ErrorIconType::EIT_Error, LoadStringT(IDS_ERR_GENERICD3DINIT),
			MsgFmt(_T("Shader -> CreateInputLayout, error %lx"), hr));
		return false;
	}

	// success
	return true;
}

void Shader::PrepareForRendering(Camera *camera)
{
	// proceed if I'm not already the prepared shader
	if (currentPreparedShader != this)
	{
		D3D *d3d = D3D::Get();

		// set up the shaders
		d3d->VSSetShader(vs);
		d3d->PSSetShader(ps);
		d3d->GSSetShader(gs);

		// set the pixel and vertex shader input buffers
		SetShaderInputs(camera);

		// set the sampler
		d3d->PSSetSampler();

		// mark me as the current prepared shader
		currentPreparedShader = this;
	}
}

