// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Camera

#pragma once

#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "D3D.h"

// constant buffer structs
struct CBProjection
{
	DirectX::XMMATRIX projection;
};
struct CBView
{
	DirectX::XMMATRIX view;
};

class Camera : public Align16
{
public:
	Camera();
	~Camera();

	// initialize; returns true on success, false on failure
	bool Init(int width, int height);

	// udpate the view size
	void SetViewSize(int width, int height);

	// Set the monitor rotation, in degrees CW.  This is usually a cardinal
	// rotation (multiples of 90 degrees), but that's not required.  The typical
	// settings are:
	//
	//   0   = standard portrait mode, with the Y axis facing up
	//   90  = standard landscape mode
	//   180 = inverted portrait mode
	//   270 = CCW landscape mode
	//
	void SetMonitorRotation(int degrees);

	// get the current monitor rotation in degrees
	int GetMonitorRotation() const { return monitorRotation; }

	// Set monitor mirroring in the X or Y direction
	void SetMirrorHorz(bool f);
	void SetMirrorVert(bool f);

	// Get the mirroring state
	bool IsMirrorHorz() const { return mirrorHorz; }
	bool IsMirrorVert() const { return mirrorVert; }

	// Set the orthographic view pixel scaling factor.  
	void SetOrthoScaleFactor(float f);

	// Set the orthographic view pixel scaling factor separately for X and Y
	void SetOrthoScaleFactor(float fx, float fy);

	// set the camera position
	void SetPosition(float x, float y, float z);

	// set the camera rotation (radians)
	//   pitch = X rotation
	//   yaw = Y axis
	//   roll = Z axis
	void SetRotation(float pitch, float yaw, float roll);

	//
	// Set our constant buffers in the shader inputs
	//

	// view -> vertex shader
	inline void VSSetViewConstantBuffer(int index)
		{ D3D::Get()->VSSetConstantBuffers(index, 1, &cbView); }
	
	// view -> pixel shader
	inline void PSSetViewConstantBuffer(int index)
		{ D3D::Get()->PSSetConstantBuffers(index, 1, &cbView);	}
	
	// projection -> vertex shaders - this sets the current ortho projection
	inline void VSSetProjectionConstantBuffer(int index)	
		{ D3D::Get()->VSSetConstantBuffers(index, 1, &cbOrtho); }

	// Text view and projection -> vertex shaders - this sets up for 
	// rendering the text overlay
	inline void VSSetTextViewConstantBuffer(int index)
		{ D3D::Get()->VSSetConstantBuffers(index, 1, &cbViewText); }
	inline void VSSetTextProjectionConstantBuffer(int index)
		{ D3D::Get()->VSSetConstantBuffers(index, 1, &cbProjectionText); }

protected:
	// depth limits for view frustum
	const float NEAR_Z = 0.01f;
	const float FAR_Z = 30 * 12 * 25.4f;  // 30 feet in mllimeters

	// Recalculate the text view and projection.  This has to be updated
	// whenever the screen size or monitor rotation changes.
	void RecalcTextView();

	// Realculate the view.  This has to be updated whenever the camera
	// position or orientation changes.
	void RecalcView();

	// Recalculate the ortho projection matrix.  This has to be updated
	// whenever the orthographic scaling factor or window size changes.
	void RecalcOrthoProjection();

	// Orthographic scale factor: model distance units (mm) per pixel,
	// for the ortho projection.  This is the ortho view analog of zooming
	// in a perspective view.  The X and Y scaling can be set separately
	// if non-square pixels are desired.
	float orthoScaleFactorX, orthoScaleFactorY;

	// view size (size of the window or monitor we're displaying on)
	struct { int width, height; } viewSize;

	// camera position relative to the model
	DirectX::XMVECTOR pos;

	// camera angle
	float yaw, pitch, roll;

	// mirroring state
	bool mirrorHorz, mirrorVert;

	// Reference UP vector.  This represents the actual monitor's
	// rotation:
	//
	//    0,1,0  = standard portrait mode (Y axis points up)
	//    1,0,0  = standard landscape mode (X axis points up, 90 degree CW monitor rotation)
	//    0,-1,0 = inverted portrait mode (180 degree monitor rotation)
	//   -1,0,0  = CCW landscape mode (90 degree CCW monitor rotation)
	DirectX::XMVECTOR up;

	// monitor rotation in degrees - the 'up' vector is always sync'ed with this
	int monitorRotation;

	// constant buffers for the ortho view
	ID3D11Buffer *cbView;
	ID3D11Buffer *cbOrtho;

	// constant buffers for the text overlay view
	ID3D11Buffer *cbViewText;
	ID3D11Buffer *cbProjectionText;

	// projection and view matrices
	DirectX::XMMATRIX projectionMatrix;
	DirectX::XMMATRIX viewMatrix;
};
