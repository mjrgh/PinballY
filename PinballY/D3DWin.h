// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// D3D Window.
//
// This encapsulates the D3D native interfaces that represent the
// drawing resources and state within a single window.  Each UI
// window object should create one of these objects to handle its
// rendering.

#pragma once
#include <vector>
#include <Windows.h>
#include <windowsx.h>
#include <d3d11_1.h>
#include <DirectXMath.h>

class D3DWin
{
	// This class is somewhat an extension of the global D3D
	// object.  Give D3D access to our private bits.
	friend class D3D;

public:
	// construction
	D3DWin();

	// destruction
	~D3DWin();

	// Initialize D3D resources.  Returns true on success,
	// false on failure.
	bool Init(HWND hwnd);

	// resize the window
	void ResizeWindow(int width, int height);

	// get the current screen size
	SIZE GetViewPortSize() const { return viewPortSize; }

	// Begin/end frame rendering
	void BeginFrame();
	void EndFrame();

	// Set the render target to the window
	void RenderToWindow();

	// Set a null render target
	void RenderToNull();

	// Render to the nth temp buffer.  We create these buffers as
	// needed.  These can be used to capture rendered pixels for
	// use as shader inputs to subsequent passes.  The scale can
	// be used to render at lower resolution than the screen:
	// e.g., set this to 0.5 to render to a half-size texture.
	void RenderToTemp(int tempBufferIndex, float scale = 1.0f);

	// Clear a temp buffer.  If 'rgba' is specified, it gives the color
	// to use for the fill.  If this is omitted, we'll clear the buffer
	// to all zeroes (the zero on the alpha makes it transparent, so
	// there's no color per se).
	void ClearTempTarget(int tempBufferIndex, const FLOAT *rgba = 0);

	// Set the nth temp buffer as a shader resource input.  This
	// allows using the pixels captured in previous rendering pass
	// via RenderToTemp() as input to a pixel shader.  This is useful
	// for 2D post-processing on rendered pixels.  Important: before
	// calling this, be sure the buffer isn't in use as the render
	// target, by setting a new render target first.
	void TempRenderTargetToShader(int shaderResourceIndex, int tempBufferIndex);

	// clear the depth stencil
	void ClearDepthStencil();

	// set the frame background color
	inline void SetBackgroundColor(float r, float g, float b, float a)
	{
		backgroundColor[0] = r;
		backgroundColor[1] = g;
		backgroundColor[2] = b;
		backgroundColor[3] = a;
	}

protected:
	// initialize the swap chain and depth stencil objects
	HRESULT InitSwapChain(int width, int height, const TCHAR **errLocation);

	// current window size
	SIZE viewPortSize;

	// swap chain
	IDXGISwapChain *swapChain;
	IDXGISwapChain1 *swapChain1;

	// Window render target view.  This is used for rendering
	// directly to the screen.
	ID3D11RenderTargetView *renderTargetView;

	// Temporary render targets.  These are used for capturing
	// rendered pixels for use as input to later render passes.
	struct TempRenderTarget
	{
		TempRenderTarget() : renderTargetView(0), shaderResourceView(0) { }

		void Clear()
		{
			if (renderTargetView != 0)
			{
				renderTargetView->Release();
				renderTargetView = 0;
			}
			if (shaderResourceView != 0)
			{
				shaderResourceView->Release();
				shaderResourceView = 0;
			}
		}

		float scale;
		ID3D11RenderTargetView *renderTargetView;
		ID3D11ShaderResourceView *shaderResourceView;
	};
	std::vector<TempRenderTarget> tempRenderTargets;

	// release the temporary render targets
	void ReleaseTempRenderTargets();

	// background color for new scenes
	float backgroundColor[4];

	// depth stencil texture and view
	ID3D11Texture2D *depthStencil;
	ID3D11DepthStencilView *depthStencilView;
};

