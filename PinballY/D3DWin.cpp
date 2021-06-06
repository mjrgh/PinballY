// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// D3D Window interfaces

#include "stdafx.h"
#include <Windows.h>
#include <windowsx.h>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "D3D.h"
#include "Resource.h"
#include "D3DWin.h"
#include "shaders/FullScreenQuadShaderVS.h"

// vertical sync mode
int D3DWin::vsyncMode = 0;

D3DWin::D3DWin()
{
	swapChain = 0;
	swapChain1 = 0;
	renderTargetView = 0;
	depthStencil = 0;
	depthStencilView = 0;

	// set the default background color to opaque black
	backgroundColor[0] = 0.0f;
	backgroundColor[1] = 0.0f;
	backgroundColor[2] = 0.0f;
	backgroundColor[3] = 0.0f;
}

D3DWin::~D3DWin()
{
	// make sure I'm no longer the current window
	D3D::Get()->UnsetWin(this);

	// release D3D objects
	if (swapChain != 0) swapChain->Release();
	if (swapChain1 != 0) swapChain1->Release();
	if (renderTargetView != 0) renderTargetView->Release();
	ReleaseTempRenderTargets();
	if (depthStencil != 0) depthStencil->Release();
	if (depthStencilView != 0) depthStencilView->Release();
}

void D3DWin::ReleaseTempRenderTargets()
{
	for (auto it = tempRenderTargets.begin(); it != tempRenderTargets.end(); ++it)
		it->Clear();
}

bool D3DWin::Init(HWND hWnd)
{
	HRESULT hr;
	auto GenErr = [hr](const TCHAR *details) {
		LogSysError(ErrorIconType::EIT_Error, LoadStringT(IDS_ERR_D3DINIT),
			MsgFmt(_T("%s, system error code %lx"), details, hr));
		return false;
	};

	// get the device object
	ID3D11Device *device = D3D::Get()->GetDevice();
	ID3D11Device1 *device1 = D3D::Get()->GetDevice1();

	// get the display window area
	RECT rc;
	GetClientRect(hWnd, &rc);
	LONG width = rc.right - rc.left;
	LONG height = rc.bottom - rc.top;

	// make sure it's not completely empty
	if (width < 1) width = 1;
	if (height < 1) height = 1;

	// set the view port size
	viewPortSize = { width, height };

	// Obtain DXGI factory from device
	IDXGIFactory1 *dxgiFactory = nullptr;
	{
		// try getting the IDXGIDevice interface
		IDXGIDevice *dxgiDevice = nullptr;
		hr = device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
		if (SUCCEEDED(hr))
		{
			// got it - get the adapter interface
			IDXGIAdapter *adapter = nullptr;
			hr = dxgiDevice->GetAdapter(&adapter);
			if (SUCCEEDED(hr))
			{
				// got that - get the factory
				hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));

				// done with the adapter
				adapter->Release();
			}

			// done with the device
			dxgiDevice->Release();
		}
	}

	// make sure we got the factory interface
	if (FAILED(hr))
		return GenErr(_T("Unable to get Direct3D DXGI factory interface"));

	// Create swap chain
	IDXGIFactory2 *dxgiFactory2 = nullptr;
	hr = dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2));
	if (SUCCEEDED(hr))
	{
		// DirectX 11.1 - set up the swap chain for the hwnd
		DXGI_SWAP_CHAIN_DESC1 sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.Width = width;
		sd.Height = height;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 1;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		// create the DX11.1 SwapChain1 interface; if that succeeds, query the
		// base SwapChain interface from it as well
		if (SUCCEEDED(hr = dxgiFactory2->CreateSwapChainForHwnd(device, hWnd, &sd, nullptr, nullptr, &swapChain1)))
			hr = swapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&swapChain));

		// done with the factory interface
		dxgiFactory2->Release();
	}
	else
	{
		// DirectX 11.0 
		DXGI_SWAP_CHAIN_DESC sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = 1;
		sd.BufferDesc.Width = width;
		sd.BufferDesc.Height = height;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = hWnd;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		hr = dxgiFactory->CreateSwapChain(device, &sd, &swapChain);
	}

	// make sure that succeeded
	if (FAILED(hr))
		return GenErr(_T("CreateSwapChain failed"));

	// We don't support full-screen swapchains, so block ALT+ENTER
	dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

	// done with the factory
	dxgiFactory->Release();

	// initialize the swap chain objects
	const TCHAR *errLoc;
	if (FAILED(hr = InitSwapChain(width, height, &errLoc)))
		return GenErr(MsgFmt(_T("%s failed"), errLoc));

	return true;
}

// Initialize the swap chain
HRESULT D3DWin::InitSwapChain(int width, int height, const TCHAR **errLocation)
{
	HRESULT hr;
	ID3D11Device *device = D3D::Get()->GetDevice();

	// Create the back buffer
	ID3D11Texture2D* pBackBuffer = nullptr;
	if (FAILED(hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&pBackBuffer)))
	{
		*errLocation = _T("Creating swap chain back buffer");
		return hr;
	}

	// create the render target view
	hr = device->CreateRenderTargetView(pBackBuffer, nullptr, &renderTargetView);
	pBackBuffer->Release();
	if (FAILED(hr))
	{
		*errLocation = _T("Creating render target view");
		return hr;
	}

	// Create depth stencil texture
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	hr = device->CreateTexture2D(&descDepth, nullptr, &depthStencil);
	if (FAILED(hr))
	{
		*errLocation = _T("Creating depth stencil");
		return hr;
	}

	// Create the depth stencil view
	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
	ZeroMemory(&descDSV, sizeof(descDSV));
	descDSV.Format = descDepth.Format;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	descDSV.Texture2D.MipSlice = 0;
	hr = device->CreateDepthStencilView(depthStencil, &descDSV, &depthStencilView);
	if (FAILED(hr))
	{
		*errLocation = _T("Creating depth stencil view");
		return hr;
	}

	// success
	return S_OK;
}

// Adjust Direct3D objects for a change in the window size.  This destroys
// and re-creates the swap chain buffers and depth stencil texture.  Called
// from the window system message handler on any change in window size.
//
// Note that this routine allocates D3D resources, so errors are at least
// theoretically possible.  In practice the chances are probably pretty
// small, since we can't get here unless we successfully completed the
// initial program setup, which creates all of the same resources we do.
// That pretty much guarantees that we won't run into any errors due to 
// D3D version/capabilities issues or our own misconfiguration.  We could
// still run into resource errors, but we know we had enough resources to 
// allocate the substantially similar previous versions of these objects, 
// so even resource errors seem unlikely except on a very stressed system.
// If we do run into any errors, I think all we can do is pop up a fatal 
// error dialog and abort, since any failure to create resources here will 
// make it impossible to do any more rendering, and we'd probably crash 
// pretty quickly from a null object anyway.  And if the system is so
// depleted that we can't create our D3D resources, the application is
// probably on the verge of crashing anyway, so sudden termination with
// an error message is about as good as it gets; at least the user gets
// an explanation of why we couldn't keep running, rather than a mystery
// exit, or an opaque system error box.
void D3DWin::ResizeWindow(int width, int height)
{
	// do nothing if the size isn't changing
	if (viewPortSize.cx == width && viewPortSize.cy == height)
		return;

	// generic error handler
	HRESULT hr;
	auto GenErr = [hr](const TCHAR *details) {
		LogSysError(ErrorIconType::EIT_Error, LoadStringT(IDS_ERR_D3DRESIZE),
			MsgFmt(_T("%s, system error code %lx"), details, hr));
		PostQuitMessage(0);
	};

	// if I'm the current output window, remove my resources from the
	// device context
	D3D::Get()->UnsetWin(this);

	// don't allow sizing below 8x8
	if (width < 1) width = 8;
	if (height < 1) height = 8;

	// remember the new size
	viewPortSize.cx = width;
	viewPortSize.cy = height;

	// release the window render target view
	if (renderTargetView != 0)
	{
		renderTargetView->Release();
		renderTargetView = 0;
	}

	// release the temporary render targets
	ReleaseTempRenderTargets();

	// release the depth stencil buffer and view
	if (depthStencil != 0)
	{
		depthStencil->Release();
		depthStencil = 0;
	}
	if (depthStencilView != 0)
	{
		depthStencilView->Release();
		depthStencilView = 0;
	}

	// hold the device context lock while performing DXGI operations
	D3D::DeviceContextLocker context;

	// preserve the existing buffer count and format
	if (swapChain != 0)
	{
		if (FAILED(hr = swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)))
			return GenErr(_T("Resizing swap chain buffers, error %lx"));

		// re-create the swap chain objects
		const TCHAR *errLoc;
		hr = InitSwapChain(width, height, &errLoc);
		if (FAILED(hr))
			return GenErr(MsgFmt(_T("%s, error %lx"), errLoc));
	}
}

// Begin rendering a frame
void D3DWin::BeginFrame()
{
	// clear the target view
	D3D::DeviceContextLocker context;
	context->ClearRenderTargetView(renderTargetView, backgroundColor);

	// clear the depth buffer
	context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

	// set our standard input topology, common to all models
	D3D::Get()->SetTriangleTopology();
}

// End rendering a frame
void D3DWin::EndFrame()
{
	// present the back buffer to the screen
	D3D::DeviceContextLocker context;
	swapChain->Present(vsyncMode, 0);
}

void D3DWin::RenderToWindow()
{
	// set the render targets
	D3D::DeviceContextLocker context;
	context->OMSetRenderTargets(
		1, &renderTargetView,
		D3D::Get()->GetUseStencil() ? depthStencilView : 0);
}

void D3DWin::RenderToNull()
{
	bool useStencil = D3D::Get()->GetUseStencil();
	D3D::DeviceContextLocker context;
	context->OMSetRenderTargets(0, 0, useStencil ? depthStencilView : 0);
}

void D3DWin::RenderToTemp(int n, float scale)
{
	ID3D11Device *device = D3D::Get()->GetDevice();

	// ensure that the slot is available
	if (size_t(n) >= tempRenderTargets.size())
		tempRenderTargets.resize(n + 1);

	// if we have an entry for this slot, but it's at the wrong scale, 
	// delete it and create a new one
	if (tempRenderTargets[n].scale != scale)
		tempRenderTargets[n].Clear();

	// if we haven't created an entry for this slot yet, create one
	if (tempRenderTargets[n].renderTargetView == 0 && tempRenderTargets[n].shaderResourceView == 0)
	{
		// Set the scale
		tempRenderTargets[n].scale = scale;

		// Set up the texture description.  Create the texture at the current
		// view port size, and set it for binding as a render target or as
		// a shader resource.  The shader resource binding flag lets us use
		// the captured pixels as input to a pixel shader.
		D3D11_TEXTURE2D_DESC textureDesc;
		ZeroMemory(&textureDesc, sizeof(textureDesc));
		textureDesc.Width = UINT(viewPortSize.cx * scale);
		textureDesc.Height = UINT(viewPortSize.cy * scale);
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		textureDesc.CPUAccessFlags = 0;
		textureDesc.MiscFlags = 0;

		// create the texture
		ID3D11Texture2D *pTexture;
		if (SUCCEEDED(device->CreateTexture2D(&textureDesc, NULL, &pTexture)))
		{
			// Create the render target view for the texture.  This is used to set
			// the texture as the pixel output surface for a rendering pass.
			D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
			renderTargetViewDesc.Format = textureDesc.Format;
			renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			renderTargetViewDesc.Texture2D.MipSlice = 0;
			device->CreateRenderTargetView(
				pTexture, &renderTargetViewDesc,
				&tempRenderTargets[n].renderTargetView);

			// Create the shader resource view for the texture.  This is used to
			// set the texture as an input to a pixel shader.
			D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
			shaderResourceViewDesc.Format = textureDesc.Format;
			shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
			shaderResourceViewDesc.Texture2D.MipLevels = 1;

			// Create the shader resource view.
			device->CreateShaderResourceView(
				pTexture, &shaderResourceViewDesc,
				&tempRenderTargets[n].shaderResourceView);

			// we don't need to keep a separate reference to the texture now
			// that we've created the necessary views
			pTexture->Release();
		}
	}

	// set the render target
	bool useStencil = D3D::Get()->GetUseStencil();
	D3D::DeviceContextLocker context;
	context->OMSetRenderTargets(
		1, &tempRenderTargets[n].renderTargetView,
		useStencil ? depthStencilView : 0);
}

// Set a temp buffer as shader input
void D3DWin::TempRenderTargetToShader(int shaderResourceIndex, int tempBufferIndex)
{
	if (size_t(tempBufferIndex) < tempRenderTargets.size()
		&& tempRenderTargets[tempBufferIndex].shaderResourceView != 0)
	{
		D3D::DeviceContextLocker context;
		context->PSSetShaderResources(
			shaderResourceIndex,
			1, &tempRenderTargets[tempBufferIndex].shaderResourceView);
	}
}

// clear a temp buffer
void D3DWin::ClearTempTarget(int tempBufferIndex, const FLOAT *rgba)
{
	static const FLOAT rgba0[] = { 0, 0, 0, 0 };
	if (size_t(tempBufferIndex) < tempRenderTargets.size()
		&& tempRenderTargets[tempBufferIndex].shaderResourceView != 0)
	{
		D3D::DeviceContextLocker context;
		context->ClearRenderTargetView(tempRenderTargets[tempBufferIndex].renderTargetView,
			rgba != 0 ? rgba : rgba0);
	}
}

// Clear the depth stencil state
void D3DWin::ClearDepthStencil()
{
	// clear the depth buffer
	D3D::DeviceContextLocker context;
	context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

