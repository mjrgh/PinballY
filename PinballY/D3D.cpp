// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Direct3D 11 interface

#include "stdafx.h"
#include <Windows.h>
#include <windowsx.h>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "Resource.h"
#include "D3D.h"
#include "D3DWin.h"
#include "shaders/FullScreenQuadShaderVS.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxguid.lib")


// DIRECT3D MEMORY LEAK DEBUGGING
// To enable Direct3D's detailed reporting for un-released objects
// at program exit, uncomment the #define line below.  The detailed
// report is disabled by default, because it inherently produces
// "false positives", due to the necessity to maintain a reference
// to the D3D debug object itself, and the other objects it keeps
// alive indirectly.  D3D provides some basic leak reporting even
// without enabling this, so the best way to use this is to leave
// it disabled most of the time, and it enable it only when the
// basic detection reports a leak.
//
//#define DEBUG_D3D_LEAKS


using namespace DirectX;

// static global instance
D3D *D3D::inst;

// initialize
bool D3D::Init()
{
	// do nothing if the instance already exists
	if (inst != 0)
		return true;

	// create the global instance
	inst = new D3D();

	// Initialize the D3D interfaces.  If that fails, delete the
	// object and return failure.
	if (!inst->InitD3D())
	{
		Shutdown();
		return false;
	}

	// success
	return true;
}

// shut down
void D3D::Shutdown()
{
	delete inst;
	inst = 0;
}

// construction
D3D::D3D()
{
	device = NULL;
	device1 = NULL;
	internalContextPointer = NULL;
	internalContext1Pointer = NULL;
	blendState = NULL;
	linearWrapSamplerState = NULL;
	linearNoWrapSamplerState = NULL;
	cbWorld = NULL;
	vsFullScreenQuad = NULL;
	depthStencilStateOn = NULL;
	depthStencilStateOff = NULL;
	depthStencilStateSetStencil = NULL;
	depthStencilStateDrawWhereStencilSet = NULL;
	depthStencilStateDrawWhereStencilClear = NULL;
	defaultRasterizerState = NULL;
	mirrorRasterizerState = NULL;
	useStencil = true;
}

// destruction
D3D::~D3D()
{
	ID3D11Debug *debug = 0;

	// If desired, get the debug interface so that we can get a
	// detailed list of unfreed objects before exiting.  This can
	// be enabled if desired to help resolve D3D memory leaks.
	// 
	// When running under the debugger, D3D will generate a warning
	// on process exit if any unfreed objects remain (even without
	// this special code here).  This code can be enabled to get
	// more detail on those leaks to track them down and fix them.
	//
#ifdef DEBUG_D3D_LEAKS
	if (device == 0 || FAILED(device->QueryInterface(&debug)))
		debug = 0;
#endif

	// release references to our D3D objects
	if (blendState != NULL) blendState->Release();
	if (linearWrapSamplerState != NULL) linearWrapSamplerState->Release();
	if (linearNoWrapSamplerState != NULL) linearNoWrapSamplerState->Release();
	if (cbWorld != NULL) cbWorld->Release();
	if (vsFullScreenQuad != NULL) vsFullScreenQuad->Release();
	if (depthStencilStateOn != NULL) depthStencilStateOn->Release();
	if (depthStencilStateOff != NULL) depthStencilStateOff->Release();
	if (depthStencilStateSetStencil != NULL) depthStencilStateSetStencil->Release();
	if (depthStencilStateDrawWhereStencilSet != NULL) depthStencilStateDrawWhereStencilSet->Release();
	if (depthStencilStateDrawWhereStencilClear != NULL) depthStencilStateDrawWhereStencilClear->Release();
	if (defaultRasterizerState != NULL) defaultRasterizerState->Release();
	if (mirrorRasterizerState != NULL) mirrorRasterizerState->Release();

	// clear internal references and release the main D3D interfaces
	if (internalContextPointer != 0)
	{
		internalContextPointer->ClearState();
		internalContextPointer->Flush();
		internalContextPointer->Release();
	}
	if (internalContext1Pointer != 0) internalContext1Pointer->Release();
	if (device1 != 0) device1->Release();
	if (device != 0) device->Release();

#ifdef DEBUG_D3D_LEAKS
	// show the live object list if desired
	if (debug != 0)
	{
		debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
		debug->Release();
	}
#endif
}

// initialize
bool D3D::InitD3D()
{
	HRESULT hr;
	auto GenErr = [&hr](const TCHAR *details) {
		LogSysError(EIT_Error, LoadStringT(IDS_ERR_D3DINIT),
			MsgFmt(_T("%s, system error code %lx"), details, hr));
		return false;
	};

	// device flags
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

	// add the Debug flag if in debug mode
	IF_DEBUG(createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG);

	// desired driver types, in priority order
	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE
	};
	UINT numDriverTypes = ARRAYSIZE(driverTypes);

	// required feature levels, in priority order
	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	// try each driver type until we successfully create the device
	for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
	{
		// Try with gradually reducing feature levels.  We can accept as
		// low as 11.0.
		for (UINT startLevel = 0; featureLevels[startLevel] >= D3D_FEATURE_LEVEL_11_0; ++startLevel)
		{
			// Try with and without some of the feature flags.  The DEBUG flag
			// won't work unless the Developer SDK version of DX is installed, 
			// and the VIDEO_SUPPORT flag doesn't work on Windows 7.  
			const static UINT removeFlags[] = {
				0,
				D3D11_CREATE_DEVICE_DEBUG,
				D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
				D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_VIDEO_SUPPORT
			};
			for (UINT flagLevel = 0; flagLevel < ARRAYSIZE(removeFlags); ++flagLevel)
			{
				// remove the current exclusion flag
				UINT curDeviceFlags = createDeviceFlags & ~removeFlags[flagLevel];

				// try creating the driver with the current type and device flags
				driverType = driverTypes[driverTypeIndex];
				hr = D3D11CreateDevice(nullptr, driverType, nullptr, curDeviceFlags,
					&featureLevels[startLevel], numFeatureLevels - startLevel,
					D3D11_SDK_VERSION, &device, &featureLevel, &internalContextPointer);

				// if that succeeded, stop searching
				if (SUCCEEDED(hr))
					break;
			}

			// break out of the level search if successful
			if (SUCCEEDED(hr))
				break;
		}

		// stop searching on the first successful device creation
		if (SUCCEEDED(hr))
			break;
	}

	// if we couldn't create a device, return failure
	if (FAILED(hr))
		return GenErr(_T("D3D11CreateDevice failed"));

	// Try to get the upgraded Device1 and DeviceContext1 interfaces, available in
	// DirectX 11.1 or later.  These give us access to some additional functions;
	// if not available, we'll use fallbacks in the 11.0 interfaces that we
	// already have.
	if (SUCCEEDED(hr = device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&device1))))
		internalContextPointer->QueryInterface(
			__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&internalContext1Pointer));

	// turn multithread protection on
	RefPtr<ID3D11DeviceContext> pImmediateContext;
	RefPtr<ID3D10Multithread> pMultiThread;
	device->GetImmediateContext(&pImmediateContext);
	if (!SUCCEEDED(hr = pImmediateContext->QueryInterface(__uuidof(ID3D10Multithread), (void**)&pMultiThread)))
		return GenErr(_T("QueryInterface(ID3D10Multithread) failed"));
	pMultiThread->SetMultithreadProtected(TRUE);

	// create the rasterizer state for normal drawing
	D3D11_RASTERIZER_DESC rsdesc;
	rsdesc.FillMode = D3D11_FILL_MODE::D3D11_FILL_SOLID;
	rsdesc.CullMode = D3D11_CULL_BACK;
	rsdesc.FrontCounterClockwise = FALSE;
	rsdesc.DepthBias = 0;
	rsdesc.DepthBiasClamp = 0.0f;
	rsdesc.SlopeScaledDepthBias = 0.0f;
	rsdesc.DepthClipEnable = TRUE;
	rsdesc.ScissorEnable = FALSE;
	rsdesc.MultisampleEnable = TRUE;
	rsdesc.AntialiasedLineEnable = FALSE;
	device->CreateRasterizerState(&rsdesc, &defaultRasterizerState);

	// Create the state for mirror-image drawing, with the X or Y
	// coordinates reversed in the view.  This uses counter-clockwise
	// winding order for triangles.
	rsdesc.FrontCounterClockwise = TRUE;
	device->CreateRasterizerState(&rsdesc, &mirrorRasterizerState);

	// Create the depth stencil ON state
	D3D11_DEPTH_STENCIL_DESC dsDesc;
	dsDesc.DepthEnable = true;
	dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
	dsDesc.StencilEnable = true;
	dsDesc.StencilReadMask = 0xFF;
	dsDesc.StencilWriteMask = 0xFF;
	dsDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	dsDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	dsDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	dsDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	dsDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	dsDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	hr = device->CreateDepthStencilState(&dsDesc, &depthStencilStateOn);
	if (FAILED(hr))
		return GenErr(_T("Creating depth stencil ON state failed"));

	// set the depth stencil state to ON initially
	internalContextPointer->OMSetDepthStencilState(depthStencilStateOn, 0);
	useStencil = true;

	// Create the OFF state
	dsDesc.DepthEnable = false;
	dsDesc.StencilEnable = false;
	if (FAILED(hr = device->CreateDepthStencilState(&dsDesc, &depthStencilStateOff)))
		return GenErr(_T("Creating depth stencil OFF state failed"));

	// create the SET STENCIL state
	dsDesc.DepthEnable = true;
	dsDesc.StencilEnable = true;
	dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
	if (FAILED(hr = device->CreateDepthStencilState(&dsDesc, &depthStencilStateSetStencil)))
		return GenErr(_T("Creating depth stencil SET STENCIL state failed"));

	// Create the DRAW ONLY WHERE THE STENCIL IS SET state
	dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
	dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	if (FAILED(hr = device->CreateDepthStencilState(&dsDesc, &depthStencilStateDrawWhereStencilSet)))
		return GenErr(_T("Creating depth stencil DRAW WHERE SET state failed"));

	// Create the DRAW ONLY WHERE THE STENCIL IS SET state
	dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_GREATER;
	if (FAILED(hr = device->CreateDepthStencilState(&dsDesc, &depthStencilStateDrawWhereStencilClear)))
		return GenErr(_T("Creating depth stencil DRAW WHERE SET state failed"));

	// Create the world constant buffer
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBWorld);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	hr = device->CreateBuffer(&bd, nullptr, &cbWorld);
	if (FAILED(hr))
		return GenErr(_T("Creating world matrix constant buffer"));

	// set up the initial world matrix
	worldMatrix = XMMatrixIdentity();
	CBWorld cbw = { XMMatrixTranspose(worldMatrix) };
	internalContextPointer->UpdateSubresource(cbWorld, 0, nullptr, &cbw, 0, 0);

	// Create the default sample state: linear, wrap coordinates
	D3D11_SAMPLER_DESC sampDesc;
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampDesc.MaxAnisotropy = 1;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	sampDesc.MipLODBias = 0.0f;
	if (FAILED(hr = device->CreateSamplerState(&sampDesc, &linearWrapSamplerState)))
		return GenErr(_T("Creating linear+wrap sampler state"));

	// create the non-wrapping sampler
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	if (FAILED(hr = device->CreateSamplerState(&sampDesc, &linearNoWrapSamplerState)))
		return GenErr(_T("Creating linear+nowrap sampler state"));

	// Create the full-screen quad vertex shader
	if (FAILED(hr = CreateVertexShader(g_vsFullScreenQuadShader, sizeof(g_vsFullScreenQuadShader), &vsFullScreenQuad)))
		return GenErr(_T("Creating full-screen quad vertex shader"));

	// set up alpha blending
	D3D11_BLEND_DESC BlendState;
	ZeroMemory(&BlendState, sizeof(BlendState));
	BlendState.RenderTarget[0].BlendEnable = TRUE;
	BlendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	BlendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	BlendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	BlendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	BlendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	BlendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	BlendState.RenderTarget[0].RenderTargetWriteMask = 0x0f;
	device->CreateBlendState(&BlendState, &blendState);
	internalContextPointer->OMSetBlendState(blendState, 0, 0xffffffff);

	// success
	return true;
}

// Render a full-screen quad
void D3D::RenderFullScreenQuad()
{
	// lock the device context
	DeviceContextLocker ctx;

	ctx->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	ctx->VSSetShader(vsFullScreenQuad, nullptr, 0);
	ctx->Draw(4, 0);
}

// Start stencil masking
void D3D::StartStencilMasking()
{
	// lock the device context
	DeviceContextLocker ctx;

	// set the SET STENCIL state, with reference value 1
	ctx->OMSetDepthStencilState(depthStencilStateSetStencil, 1);
	useStencil = true;
}

// Use the stencil mask for drawing
void D3D::UseStencilMask(bool drawWhereSet)
{
	// remember the new state
	useStencil = true;

	// set the DRAW WHERE SET or DRAW WHERE CLEAR state
	DeviceContextLocker ctx;
	ctx->OMSetDepthStencilState(
		drawWhereSet ? depthStencilStateDrawWhereStencilSet : depthStencilStateDrawWhereStencilClear, 
		1);
}

// Update the world transform
void D3D::UpdateWorldTransform(const XMMATRIX &matrix)
{
	// set up the world matrix
	CBWorld cbw;
	cbw.world = matrix;

	// update the resource
	DeviceContextLocker ctx;
	ctx->UpdateSubresource(cbWorld, 0, nullptr, &cbw, 0, 0);
}

// Create a 2D texture
HRESULT D3D::CreateTexture2D(
	D3D11_TEXTURE2D_DESC *texDesc,
	const D3D11_SUBRESOURCE_DATA *initData,
	D3D11_SHADER_RESOURCE_VIEW_DESC *viewDesc,
	ID3D11ShaderResourceView **rv,
	ID3D11Resource **texture)
{
	HRESULT hr;

	// Create the texture
	ID3D11Texture2D *t2d = nullptr;
	if (FAILED(hr = device->CreateTexture2D(texDesc, initData, &t2d)))
		return hr;

	// create the shader resource view
	hr = device->CreateShaderResourceView(t2d, viewDesc, rv);

	// if the caller wants the texture, pass it back, letting the caller assume
	// our reference count; otherwise just release it
	if (texture != nullptr)
		*texture = t2d;
	else
		t2d->Release();

	// return the result
	return hr;
}

void D3D::SetUseDepthStencil(bool on)
{
	// remember the new usage
	useStencil = on;

	// set the new state object
	DeviceContextLocker ctx;
	ctx->OMSetDepthStencilState(on ? depthStencilStateOn : depthStencilStateOff, 0);
}

void D3D::SetWin(D3DWin *win)
{
	// if the window isn't current, make it current
	if (win != curwin)
	{
		// set the render targets
		DeviceContextLocker ctx;
		ctx->OMSetRenderTargets(1, &win->renderTargetView, win->depthStencilView);

		// Setup the viewport
		D3D11_VIEWPORT vp;
		SIZE sz = win->GetViewPortSize();
		vp.Width = float(sz.cx);
		vp.Height = float(sz.cy);
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		ctx->RSSetViewports(1, &vp);

		// set this as the new window
		curwin = win;
	}
}

void D3D::UnsetWin(D3DWin *win)
{
	if (win == curwin)
	{
		// clear the render targets
		DeviceContextLocker ctx;
		ctx->OMSetRenderTargets(0, 0, 0);
		curwin = 0;
	}
}

