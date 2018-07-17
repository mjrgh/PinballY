// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Direct3D 11 interface.
//
// This class encapsulates the D3D native interfaces that represent
// to the display adapter hardware.  These objects are application-
// wide, since they correspond more or less directly to the physical 
// display.  This class therefore is meant to be used as a singleton,
// with one global instance shared among all windows.  The separate
// D3DWin class is instantiated per window to represent the rendering
// resources and state in each window.

#pragma once

#include <vector>
#include <Windows.h>
#include <windowsx.h>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "CommonVertex.h"

// constant buffer definitions
struct CBWorld
{
	DirectX::XMMATRIX world;
};
struct CBOrtho
{
	DirectX::XMMATRIX ortho;
};

class D3DWin;

class D3D : public Align16
{
public:
	// Initialize.  This is called at application startup to create
	// the global D3D object.  Returns true on success, false on
	// failure.
	static bool Init();

	// Shut down.  This is called before application exit to release
	// D3D resources.
	static void Shutdown();

	// Get the global instance.  The instance is created via Init().
	static D3D *Get() { return inst; }

	// Get/set the current rendering window.
	D3DWin *GetWin() const { return curwin; }
	void SetWin(D3DWin *win);

	// Unset a rendering window.  If the given window is current,
	// this removes its resources from the output merge system.
	// This has no effect if a different window is active.
	void UnsetWin(D3DWin *win);

	// Device context locker.  Device context methods aren't natively
	// thread-safe, so we have to provide our own thread protection
	// when using the device context.  This object provides the context
	// pointer, and acquires a Critical Section lock as long as the
	// pointer is in scope.
	//
	// WARNING:  Watch out for potential deadlocks in any code that 
	// acquires or holds other locks besides this one.  The standard
	// method for avoiding deadlocks when multiple locks must be held
	// is to make sure that all code paths that must hold two or more
	// locks at the same time all acquire those locks in the same order.
	//
	// WARNING:  DON'T actually do multi-threaded device context access,
	// even though that's what it was specifically designed to allow it.
	// The goal was to provide a structured and easy-to-use idiom that
	// would make it almost impossible to get device context usage wrong,
	// by making it so that you could only get the device context pointer 
	// by getting the lock at the same time (by instantiating this class).
	// The class works as far as that goes, and it does in fact allow for
	// largely problem-free multi-threaded access to the dc.  But here's 
	// the catch: it's only *largely* problem-free.  In practice, it 
	// turns out that it's not good enough to protect our explicit calls
	// into the device context with a lock, because D3D11 itself can make 
	// *implicit* calls into the dc as side-effects of innocuous calls.
	// For example, I observed that ID3D11Texture2D::Release() can call
	// into the dc.  That put me off on this whole idea of multi-threaded 
	// dc access, because it would be all but impossible to catch all of 
	// the secret internal side-effect calls within D3D11.  This is
	// disappointing, because Microsoft suggests in the SDK that multi-
	// threaded dc access is possible if you provide your own locking to
	// serialize access across threads.  But it's probably telling that
	// they stop there without suggesting any best practices.  It makes 
	// me think that there really are no "best practices" to be had.  So
	// the bottom line is that you should stick to a single-threaded 
	// architecture for all device context access.  You *can* allocate 
	// D3D11 resources in background threads, since the D3D11Device 
	// object is explicitly thread-safe, but that's about it.  Do
	// everything else D3D-related on the the main foreground UI/render 
	// thread.
	//
	// Even so, please continue to use this class as the exclusive
	// way to get the device context.  That will leave us in good 
	// shape if anyone ever wants to revisit this and see if there
	// is some way to deal with the implicit/internal dc call issue,
	// since all of the explicit calls in our code will already be 
	// properly protected.
	// 
	class DeviceContextLocker
	{
	public:
		DeviceContextLocker() { EnterCriticalSection(D3D::Get()->contextLock); }
		~DeviceContextLocker() { LeaveCriticalSection(D3D::Get()->contextLock); }

		// For convenience, the locker object itself can be used as though
		// it were the device context pointer.
		operator ID3D11DeviceContext*() const { return GetContext(); }
		ID3D11DeviceContext* operator ->() const { return GetContext(); }
		
		// get the context
		ID3D11DeviceContext *GetContext() const { return D3D::Get()->internalContextPointer; }

		// get the context1 interface
		ID3D11DeviceContext1 *GetContext1() const { return D3D::Get()->internalContext1Pointer; }
	};

	// Create a buffer with a single resource
	inline HRESULT CreateBuffer(
		const D3D11_BUFFER_DESC *bd, 
		ID3D11Buffer **buffer, 
		const char *debugName)
	{
		HRESULT hr = device->CreateBuffer(bd, nullptr, buffer);
		IF_DEBUG(if (SUCCEEDED(hr) && *buffer != 0)
			(*buffer)->SetPrivateData(
				WKPDID_D3DDebugObjectName, (UINT)strlen(debugName), debugName));
		return hr;
	}

	// Create a buffer with a subresource.  (This is typically used
	// for textures, which can have multiple mipmap levels represented
	// by subresources.)
	inline HRESULT CreateBuffer(
		const D3D11_BUFFER_DESC *bd, 
		D3D11_SUBRESOURCE_DATA *sd, 
		ID3D11Buffer **buffer,
		const char *debugName)
	{ 
		HRESULT hr = device->CreateBuffer(bd, sd, buffer);	
		IF_DEBUG(if (SUCCEEDED(hr) && *buffer != 0)
			(*buffer)->SetPrivateData(
				WKPDID_D3DDebugObjectName, (UINT)strlen(debugName), debugName));
		return hr;
	}

	// create a texture view
	HRESULT CreateTexture2D(
		D3D11_TEXTURE2D_DESC *texDesc, 
		const D3D11_SUBRESOURCE_DATA *initData,
		D3D11_SHADER_RESOURCE_VIEW_DESC *viewDesc,
		ID3D11ShaderResourceView **rv,
		ID3D11Resource **texture = 0);

	// update a resource
	inline void UpdateResource(ID3D11Resource *resource, const void *srcData)
	{
		DeviceContextLocker ctx;
		ctx->UpdateSubresource(resource, 0, nullptr, srcData, 0, 0); 
	}

	// create a vertex shader
	inline HRESULT CreateVertexShader(const void *byteCode, SIZE_T byteCodeLength, ID3D11VertexShader **vs)
		{ return device->CreateVertexShader(byteCode, byteCodeLength, nullptr, vs); }

	// create a pixel shader
	inline HRESULT CreatePixelShader(const void *byteCode, SIZE_T byteCodeLength, ID3D11PixelShader **ps)
		{ return device->CreatePixelShader(byteCode, byteCodeLength, nullptr, ps); }

	// create a geometry shader
	inline HRESULT CreateGeometryShader(const void *byteCode, SIZE_T byteCodeLength, ID3D11GeometryShader **gs)
		{ return device->CreateGeometryShader(byteCode, byteCodeLength, nullptr, gs); }

	// create an input layout
	inline HRESULT CreateInputLayout(
		D3D11_INPUT_ELEMENT_DESC *desc, UINT numElements, 
		const void *byteCode, SIZE_T byteCodeLength, ID3D11InputLayout **inputLayout)
		{ return device->CreateInputLayout(desc, numElements, byteCode, byteCodeLength, inputLayout); }

	// set the input layout
	inline void SetInputLayout(ID3D11InputLayout *layout)
	{
		DeviceContextLocker ctx;
		ctx->IASetInputLayout(layout);
	}

	// set the primitive topology to triangle list
	inline void SetTriangleTopology()
	{
		DeviceContextLocker ctx;
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); 
	}

	// load a resource view into the pixel shader
	inline void PSSetShaderResources(int startSlot, int numResources, ID3D11ShaderResourceView *const *resources)
	{
		DeviceContextLocker ctx;
		ctx->PSSetShaderResources(startSlot, numResources, resources); 
	}

	// clear a PS resource view slot
	inline void PSClearShaderResource(int slot)
	{ 
		static ID3D11ShaderResourceView *const r[1] = { 0 };
		DeviceContextLocker ctx;
		ctx->PSSetShaderResources(slot, 1, r);
	}

	// set shaders
	inline void VSSetShader(ID3D11VertexShader *vs)
	{
		DeviceContextLocker ctx;
		ctx->VSSetShader(vs, nullptr, 0); 
	}
	inline void PSSetShader(ID3D11PixelShader *ps)
	{
		DeviceContextLocker ctx;
		ctx->PSSetShader(ps, nullptr, 0);
	}
	inline void GSSetShader(ID3D11GeometryShader *gs)
	{
		DeviceContextLocker ctx;
		ctx->GSSetShader(gs, nullptr, 0);
	}

	// set shader constant buffers
	inline void VSSetConstantBuffers(int startIdx, int numBuffers, ID3D11Buffer *const *buffers)
	{
		DeviceContextLocker ctx;
		ctx->VSSetConstantBuffers(startIdx, numBuffers, buffers); 
	}
	inline void PSSetConstantBuffers(int startIdx, int numBuffers, ID3D11Buffer *const *buffers)
	{ 
		DeviceContextLocker ctx;
		ctx->PSSetConstantBuffers(startIdx, numBuffers, buffers); 
	}
	inline void GSSetConstantBuffers(int startIdx, int numBuffers, ID3D11Buffer *const *buffers)
	{ 
		DeviceContextLocker ctx;
		ctx->GSSetConstantBuffers(startIdx, numBuffers, buffers); 
	}

	// set the input assembler vertex buffer
	inline void IASetVertexBuffer(ID3D11Buffer *buffer, UINT stride)
	{
		UINT offset = 0;
		DeviceContextLocker ctx;
		ctx->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
	}

	// set the index buffer using WORD (16-bit unsigned int) format
	inline void IASetIndexBuffer(ID3D11Buffer *buffer)
	{
		DeviceContextLocker ctx;
		ctx->IASetIndexBuffer(buffer, DXGI_FORMAT_R16_UINT, 0); 
	}

	// update the world transform matrix
	void UpdateWorldTransform(const DirectX::XMMATRIX &matrix);

	// set the world constant buffer in a shader
	inline void VSSetWorldConstantBuffer(int startIdx)
	{
		DeviceContextLocker ctx;
		ctx->VSSetConstantBuffers(startIdx, 1, &cbWorld); 
	}
	inline void PSSetWorldConstantBuffer(int startIdx)
	{
		DeviceContextLocker ctx;
		ctx->PSSetConstantBuffers(startIdx, 1, &cbWorld);
	}

	// Set the pixel shader sampler to the linear sampler, with
	// wrapping (default) or clamping when outside the 0..1 range.
	inline void PSSetSampler(bool wrap = true)
	{ 
		DeviceContextLocker ctx;
		ctx->PSSetSamplers(0, 1, wrap ? &linearWrapSamplerState : &linearNoWrapSamplerState); 
	}

	// set the normal or mirrored rasterizer state
	inline void SetMirroredRasterizerState(bool mirrored)
	{
		DeviceContextLocker ctx;
		ctx->RSSetState(mirrored ? mirrorRasterizerState : defaultRasterizerState);
	}

	// draw
	inline void DrawIndexed(INT indexCount)
	{
		DeviceContextLocker ctx;
		ctx->DrawIndexed(indexCount, 0, 0);
	}

	// turn the depth stencil on or off
	void SetUseDepthStencil(bool useDepth);

	// start a stencil masking pass: call this, then render objects
	// to update the stencil
	void StartStencilMasking();

	// Render a full-screen quad.  This can be used to render all
	// pixels from a texture buffer.
	void RenderFullScreenQuad();

	// Use the stencil mask to draw only where the stencil is set
	// or only where it's not set.
	void UseStencilMask(bool drawWhereSet);

	// is the stencil in use?
	bool GetUseStencil() const { return useStencil; }

	// get interfaces (note: no automatic references are added)
	ID3D11Device *GetDevice() { return device; }
	ID3D11Device1 *GetDevice1() { return device1; }
	ID3D11DepthStencilState *GetDepthStencilState() { return depthStencilStateOn; }

protected:
	// The constructor and destructor are internal because clients
	// go thorugh our static initialize and shutdown methods.
	D3D();
	~D3D();

	// The global singleton instance
	static D3D *inst;

	// Initialize the D3D objects.  Returns true on success, false 
	// on failure.
	bool InitD3D();

	// driver and version information
	D3D_DRIVER_TYPE driverType;
	D3D_FEATURE_LEVEL featureLevel;

	// device interface, with Device1 version if available
	ID3D11Device *device;
	ID3D11Device1 *device1;

	// Device context, with Context1 version if available.  The context
	// pointers shouldn't be dereferenced directly; always use the locker
	// object instead (DeviceContextLocker) for thread safety.
	ID3D11DeviceContext *internalContextPointer;
	ID3D11DeviceContext1 *internalContext1Pointer;

	// Critical section for locking the device context for thread safety
	CriticalSection contextLock;

	// is the stencil in use?
	bool useStencil;

	// basic depth stencil On and Off states
	ID3D11DepthStencilState *depthStencilStateOn;
	ID3D11DepthStencilState *depthStencilStateOff;

	// special depth stencil state: set the stencil
	ID3D11DepthStencilState *depthStencilStateSetStencil;

	// special depth stencil state: draw where stencil is/isn't set
	ID3D11DepthStencilState *depthStencilStateDrawWhereStencilSet;
	ID3D11DepthStencilState *depthStencilStateDrawWhereStencilClear;

	// Render states for normal and mirror image windows.  The mirror state
	// state changes the normal polygon winding order to counter-clockwise
	// to allow reversing the X or Y coordinate system in the view relative
	// to the world.
	ID3D11RasterizerState *defaultRasterizerState;
	ID3D11RasterizerState *mirrorRasterizerState;

	// current rendering window
	D3DWin *curwin;

	// blend state object
	ID3D11BlendState *blendState;

	// linear sampler states
	ID3D11SamplerState *linearWrapSamplerState;
	ID3D11SamplerState *linearNoWrapSamplerState;

	// Constant buffer for world transform matrix.  Each drawing object
	// needs to transform its local coordinates to world coordinates on
	// each rendering cycle, but it's only needed during the rendering
	// step where we draw a given object, so all objects can share one 
	// instance of the D3D buffer.
	ID3D11Buffer *cbWorld;

	// world matrix
	DirectX::XMMATRIX worldMatrix;

	// special vertex shader to render a full-screen quad
	ID3D11VertexShader *vsFullScreenQuad;
};
