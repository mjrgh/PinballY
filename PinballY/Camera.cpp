// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Camera

#include "stdafx.h"
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "Resource.h"
#include "camera.h"
#include "d3d.h"

using namespace DirectX;

Camera::Camera()
{
	// clear interfaces
	cbView = 0;
	cbOrtho = 0;
	cbViewText = 0;
	cbProjectionText = 0;

	// set the initial default camera position
	pos = XMVectorSet(0, 0, 0, 0);

	// set the initial view direction to straight ahead
	yaw = pitch = roll = 0.0f;

	// no mirroring by default
	mirrorHorz = mirrorVert = false;

	// Set the initial ortho scale factor to 1:1
	orthoScaleFactorX = orthoScaleFactorY = 1.0f;

	// set a default screen size
	viewSize.width = 1920;
	viewSize.height = 1280;

	// start with standard landscape mode
	up = XMVectorSet(0, 1, 0, 0);
	monitorRotation = 0;
}

Camera::~Camera()
{
	// delete interfaces
	if (cbView != 0) cbView->Release();
	if (cbOrtho != 0) cbOrtho->Release();
	if (cbViewText != 0) cbViewText->Release();
	if (cbProjectionText != 0) cbProjectionText->Release();
}

bool Camera::Init(int width, int height)
{
	D3D *d3d = D3D::Get();
	HRESULT hr;
	auto GenErr = [hr](const TCHAR *details) {
		LogSysError(EIT_Error, LoadStringT(IDS_ERR_GENERICD3DINIT), MsgFmt(details, hr));
		return false;
	};

	// store the screen size
	viewSize.width = width > 1 ? width : 1;
	viewSize.height = height > 1 ? height : 1;

	// create the constant buffers
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.ByteWidth = sizeof(CBView);
	if (FAILED(hr = d3d->CreateBuffer(&bd, &cbView, "Camera::cbView")))
		return GenErr(_T("CreateBuffer(view matrix), error %lx"));

	// create the text view matrix buffer
	if (FAILED(hr = d3d->CreateBuffer(&bd, &cbViewText, "Camera::cbViewText")))
		return GenErr(_T("CreateBuffer(text view matrix), error %lx"));

	// create the ortho matrix buffer
	bd.ByteWidth = sizeof(CBOrtho);
	hr = d3d->CreateBuffer(&bd, &cbOrtho, "Camera::cbOrtho");
	if (FAILED(hr))
		return GenErr(_T("CreateBuffer(ortho projection matrix), error %lx"));

	// create the text projection matrix buffer
	hr = d3d->CreateBuffer(&bd, &cbProjectionText, "Camera::cbProjectionText");
	if (FAILED(hr))
		return GenErr(_T("CreateBuffer(text projection), error %lx"));

	// calculate the initial view and projection matrices
	RecalcView();
	RecalcOrthoProjection();
	RecalcTextView();

	// success
	return true;
}

void Camera::SetViewSize(int width, int height)
{
	viewSize.width = width > 1 ? width : 1;
	viewSize.height = height > 1 ? height : 1;
	RecalcView();
	RecalcOrthoProjection();
	RecalcTextView();
}

void Camera::SetMonitorRotation(int degrees)
{
	// canonicalize the setting (0-359)
	degrees = (degrees < 0 ? (degrees % 360) + 360 : degrees % 360);

	// remember the new setting
	monitorRotation = degrees;

	// Calculate the new 'up vector.  The rotation is typically one
	// of the cardinal directions, so use exact values for these,
	// but calculate it if needed.
	switch (degrees)
	{
	case 0:
		up = XMVectorSet(0, 1, 0, 0);
		break;

	case 90:
		up = XMVectorSet(1, 0, 0, 0);
		break;

	case 180:
		up = XMVectorSet(0, -1, 0, 0);
		break;

	case 270:
		up = XMVectorSet(-1, 0, 0, 0);
		break;

	default:
		up = XMVector3TransformCoord(
			XMVectorSet(0, 1, 0, 0), 
			XMMatrixRotationZ(-degrees * XM_PI / 180));
		break;
	}

	// recalculate the view
	RecalcView();
	RecalcOrthoProjection();
	RecalcTextView();
}

void Camera::SetMirrorHorz(bool f)
{
	mirrorHorz = f;
	RecalcView();
	RecalcTextView();
}

void Camera::SetMirrorVert(bool f)
{
	mirrorVert = f;
	RecalcView();
	RecalcTextView();
}

void Camera::SetPosition(float x, float y, float z)
{
	pos = XMVectorSet(x, y, z, 0);
	RecalcView();
}

void Camera::SetRotation(float pitch, float yaw, float roll)
{
	this->pitch = pitch;
	this->yaw = yaw;
	this->roll = roll;
	RecalcView();
}

void Camera::SetOrthoScaleFactor(float f)
{
	// store the new factor and update the projection
	orthoScaleFactorX = orthoScaleFactorY = f;
	RecalcOrthoProjection();
}

void Camera::SetOrthoScaleFactor(float fx, float fy)
{
	// store the new factor and update the projection
	orthoScaleFactorX = fx;
	orthoScaleFactorY = fy;
	RecalcOrthoProjection();
}

void Camera::RecalcOrthoProjection()
{
	// set up the ortho matrix
	XMMATRIX orthoMatrix = XMMatrixOrthographicLH(
		viewSize.width * orthoScaleFactorX, viewSize.height * orthoScaleFactorY, 
		NEAR_Z, FAR_Z);

	CBOrtho cbo = { XMMatrixTranspose(orthoMatrix) };
	D3D::Get()->UpdateResource(cbOrtho, &cbo);
}

void Camera::RecalcView()
{
	// start with the reference lookAt and up unit vectors - straight
	// ahead along the Z axis
	XMVECTOR lookAt = XMVectorSet(0, 0, 1, 0);

	// figure the rotation matrix
	XMMATRIX r = XMMatrixRotationRollPitchYaw(pitch, yaw, roll);

	// apply the rotations to the view reference vectors
	lookAt = XMVector3TransformCoord(lookAt, r);
	XMVECTOR rotatedUp = XMVector3TransformCoord(up, r);

	// create the new view matrix
	viewMatrix = XMMatrixLookAtLH(pos, XMVectorAdd(pos, lookAt), rotatedUp);

	// mirror horizontally if desired - this mirrors in the Y-Z plane
	if (mirrorHorz)
		viewMatrix = XMMatrixMultiply(viewMatrix, XMMatrixReflect(XMVectorSet(1, 0, 0, 0)));

	// mirror verticaly if desired - mirror in the X-Z plane
	if (mirrorVert)
		viewMatrix = XMMatrixMultiply(viewMatrix, XMMatrixReflect(XMVectorSet(0, 1, 0, 0)));

	// update the view matrix in the constant buffer with the transposed version
	CBView cbv = { XMMatrixTranspose(viewMatrix) };
	D3D::Get()->UpdateResource(cbView, &cbv);
}

void Camera::RecalcTextView()
{
	// update the 2D projection matrix
	XMMATRIX m = XMMatrixOrthographicLH(
		float(viewSize.width), float(viewSize.height),
		NEAR_Z, FAR_Z);
	CBOrtho cbo = { XMMatrixTranspose(m) };
	D3D::Get()->UpdateResource(cbProjectionText, &cbo);

	// figure the view width and height adjusted for monitor rotation
	float rcos = cosf(monitorRotation*XM_PI / 180.0f);
	float rsin = sinf(monitorRotation*XM_PI / 180.0f);
	float width = fabs(viewSize.width*rcos - viewSize.height*rsin);
	float height = fabs(viewSize.width*rsin + viewSize.height*rcos);

	// Update the 2D view matrix.  This view puts the scene origin
	// at the top left of the monitor, for convenience in arranging
	// 2D objects - we want the drawing surface to act like regular
	// window coordinates.  To accomplish this, translate the camera 
	// position and lookAt target over by half the screen size, so
	// that the D3D scene origin is at the top left edge of the view.
	XMVECTOR pos = XMVectorSet(width / 2.0f, -height / 2.0f, -1, 0);
	XMVECTOR lookAt = XMVectorSet(width/2.0f, -height/2.0f, 1, 0);
	m = XMMatrixLookAtLH(pos, lookAt, up);

	// mirror horizontally if desired - this mirrors in the Y-Z plane
	if (mirrorHorz)
		m = XMMatrixMultiply(m, XMMatrixReflect(XMVectorSet(1, 0, 0, 0)));

	// mirror verticaly if desired - mirror in the X-Z plane
	if (mirrorVert)
		m = XMMatrixMultiply(m, XMMatrixReflect(XMVectorSet(0, 1, 0, 0)));

	// set the new text view matrix
	cbo = { XMMatrixTranspose(m) };
	D3D::Get()->UpdateResource(cbViewText, &cbo);
}
