// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "Resource.h"
#include "D3D.h"
#include "TextShader.h"
#include "camera.h"
#include "shaders/TextShaderVS.h"
#include "shaders/TextShaderPS.h"


using namespace DirectX;

TextShader::TextShader()
{
}

TextShader::~TextShader()
{
}

bool TextShader::Init()
{
	D3D *d3d = D3D::Get();
	HRESULT hr;
	auto GenErr = [hr](const TCHAR *details) {
		LogSysError(EIT_Error, LoadStringT(IDS_ERR_GENERICD3DINIT),
			MsgFmt(_T("%s, system error code %lx"), details, hr));
		return false;
	};

	// Create the vertex shader
	if (FAILED(hr = d3d->CreateVertexShader(g_vsTextShader, sizeof(g_vsTextShader), &vs)))
		return GenErr(_T("Text Shader -> CreateVertexShader"));

	// Define the input layout
	D3D11_INPUT_ELEMENT_DESC layoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	if (!CreateInputLayout(d3d, layoutDesc, countof(layoutDesc), g_vsTextShader, sizeof(g_vsTextShader)))
		return false;

	// create the pixel shader
	if (FAILED(hr = d3d->CreatePixelShader(g_psTextShader, sizeof(g_psTextShader), &ps)))
		return GenErr(_T("Text Shader -> CreatePixelShader"));

	// create the pixel shader input buffer
	D3D11_BUFFER_DESC desc;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = sizeof(ColorBufferType);
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;
	if (FAILED(hr = d3d->CreateBuffer(&desc, &cbColor, "TextShader::cbColor")))
		return GenErr(_T("Text Shader -> create color constant buffer"));

	// set the default color to opaque white
	SetColor(XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));

	// success
	return true;
}


void TextShader::SetColor(XMFLOAT4 color)
{
	D3D *d3d = D3D::Get();
	ColorBufferType cb = { color };
	d3d->UpdateResource(cbColor, &cb);
}

void TextShader::SetShaderInputs(Camera *camera)
{
	D3D *d3d = D3D::Get();

	// Vertex shader inputs - these must match the 'cbuffer' definition 
	// order in LightShaderVS.hlsl
	camera->VSSetTextViewConstantBuffer(0);
	camera->VSSetTextProjectionConstantBuffer(1);
	d3d->VSSetWorldConstantBuffer(2);

	// set the pixel shader inputs
	d3d->PSSetConstantBuffers(0, 1, &cbColor);

	// Set the input layout
	d3d->SetInputLayout(layout);
	d3d->SetTriangleTopology();
}
