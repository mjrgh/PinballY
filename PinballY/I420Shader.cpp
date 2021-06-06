// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "Resource.h"
#include "D3D.h"
#include "camera.h"
#include "I420Shader.h"
#include "Sprite.h"
#include "shaders/I420ShaderVS.h"
#include "shaders/I420ShaderPS.h"
#include "shaders/I420AShaderPS.h"
#include "shaders/I444A10ShaderPS.h"

using namespace DirectX;

I420Shader::I420Shader()
{
}

I420Shader::~I420Shader()
{
}

bool I420Shader::CommonInit(const BYTE *pixelShaderBytes, size_t pixelShaderBytesCnt, const char *idForErrorLog)
{
	D3D *d3d = D3D::Get();
	HRESULT hr;
	auto GenErr = [hr, idForErrorLog](const TCHAR *details) {
		LogSysError(ErrorIconType::EIT_Error, LoadStringT(IDS_ERR_GENERICD3DINIT),
			MsgFmt(_T("%hs -> %s, system error code %lx"), idForErrorLog, details, hr));
		return false;
	};

	// Create the vertex shader
	if (FAILED(hr = d3d->CreateVertexShader(g_vsI420Shader, sizeof(g_vsI420Shader), &vs)))
		return GenErr(_T("CreateVertexShader"));

	// create the input layout
	D3D11_INPUT_ELEMENT_DESC layoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	if (!CreateInputLayout(d3d, layoutDesc, countof(layoutDesc), g_vsI420Shader, sizeof(g_vsI420Shader)))
		return false;

	// create the pixel shader
	if (FAILED(hr = d3d->CreatePixelShader(pixelShaderBytes, pixelShaderBytesCnt, &ps)))
		return GenErr(_T("CreatePixelShader"));

	// create the pixel shader input buffer
	D3D11_BUFFER_DESC desc;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = sizeof(AlphaBufferType);
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;
	if (FAILED(hr = d3d->CreateBuffer(&desc, &cbAlpha, "I420Shader::cbAlpha")))
		return GenErr(_T("create color constant buffer"));

	// set the initial global alpha to opaque
	SetAlpha(1.0f);

	// success
	return true;
}

bool I420Shader::Init() 
{
	return CommonInit(g_psI420Shader, sizeof(g_psI420Shader), "I420Shader");
}

void I420Shader::SetAlpha(float alpha)
{
	D3D *d3d = D3D::Get();
	AlphaBufferType cb = { alpha };
	d3d->UpdateResource(cbAlpha, &cb);
}

void I420Shader::SetShaderInputs(Camera *camera)
{
	D3D *d3d = D3D::Get();

	// Vertex shader inputs - these must match the 'cbuffer' definition 
	// order in LightShaderVS.hlsl
	camera->VSSetViewConstantBuffer(0);
	camera->VSSetProjectionConstantBuffer(1);
	d3d->VSSetWorldConstantBuffer(2);

	// set the pixel shader inputs
	d3d->PSSetConstantBuffers(0, 1, &cbAlpha);

	// Set the input layout
	d3d->SetInputLayout(layout);
	d3d->SetTriangleTopology();
}

// I420A variant
bool I420AShader::Init()
{
	return CommonInit(g_psI420AShader, sizeof(g_psI420AShader), "I420AShader");
}

// I444A10 variant
bool I444A10Shader::Init()
{
	return CommonInit(g_psI444A10Shader, sizeof(g_psI444A10Shader), "I444A10Shader");
}

