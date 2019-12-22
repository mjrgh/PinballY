// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "Resource.h"
#include "D3D.h"
#include "camera.h"
#include "DMDShader.h"
#include "Sprite.h"
#include "shaders/DMDShaderVS.h"
#include "shaders/DMDShaderPS.h"

using namespace DirectX;

DMDShader::DMDShader()
{
}

DMDShader::~DMDShader()
{
}

bool DMDShader::Init()
{
	D3D *d3d = D3D::Get();
	HRESULT hr;
	auto GenErr = [hr](const TCHAR *details) {
		LogSysError(EIT_Error, LoadStringT(IDS_ERR_GENERICD3DINIT),
			MsgFmt(_T("%s, system error code %lx"), details, hr));
		return false;
	};

	// Create the vertex shader
	if (FAILED(hr = d3d->CreateVertexShader(g_vsDMDShader, sizeof(g_vsDMDShader), &vs)))
		return GenErr(_T("DMD Shader -> CreateVertexShader"));

	// create the input layout
	D3D11_INPUT_ELEMENT_DESC layoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	if (!CreateInputLayout(d3d, layoutDesc, countof(layoutDesc), g_vsDMDShader, sizeof(g_vsDMDShader)))
		return false;

	// create the pixel shader
	if (FAILED(hr = d3d->CreatePixelShader(g_psDMDShader, sizeof(g_psDMDShader), &ps)))
		return GenErr(_T("DMD Shader -> CreatePixelShader"));

	// create the pixel shader alpha input buffer
	D3D11_BUFFER_DESC desc;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = sizeof(AlphaBufferType);
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;
	if (FAILED(hr = d3d->CreateBuffer(&desc, &cbAlpha, "DMDShader::cbAlpha")))
		return GenErr(_T("DMD Shader -> create color constant buffer"));

	// create the background input buffer
	desc.ByteWidth = sizeof(BgColorBufferType);
	if (FAILED(hr = d3d->CreateBuffer(&desc, &cbBgColor, "DMDShader::cbBgColor")))
		return GenErr(_T("DMD Shader -> create color constant buffer"));

	// set the initial alpha to opaque
	SetAlpha(1.0f);

	// success
	return true;
}

void DMDShader::SetAlpha(float alpha)
{
	D3D *d3d = D3D::Get();
	AlphaBufferType cb = { alpha };
	d3d->UpdateResource(cbAlpha, &cb);
}

void DMDShader::SetBgColor(RGBQUAD color, BYTE alpha)
{
	D3D *d3d = D3D::Get();
	BgColorBufferType cb = { XMFLOAT4(
		static_cast<float>(color.rgbRed) / 255.0f,
		static_cast<float>(color.rgbGreen) / 255.0f,
		static_cast<float>(color.rgbBlue) / 255.0f,
		static_cast<float>(alpha) / 255.0f
	)};
	d3d->UpdateResource(cbBgColor, &cb);
}

void DMDShader::SetShaderInputs(Camera *camera)
{
	D3D *d3d = D3D::Get();

	// Vertex shader inputs - these must match the 'cbuffer' definition 
	// order in LightShaderVS.hlsl
	camera->VSSetViewConstantBuffer(0);
	camera->VSSetProjectionConstantBuffer(1);
	d3d->VSSetWorldConstantBuffer(2);

	// set the pixel shader inputs
	ID3D11Buffer *bufs[] = { cbAlpha.Get(), cbBgColor.Get() };
	d3d->PSSetConstantBuffers(0, countof(bufs), bufs);

	// Set the input layout
	d3d->SetInputLayout(layout);
	d3d->SetTriangleTopology();
}

