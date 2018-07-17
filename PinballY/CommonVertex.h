// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include <DirectXMath.h>

// Common vertex structure
struct CommonVertex
{
#ifdef __cplusplus

	CommonVertex() { }
	CommonVertex(DirectX::XMFLOAT4 position, DirectX::XMFLOAT2 tex, DirectX::XMFLOAT3 normal)
		: position(position), tex(tex), normal(normal) { }
	CommonVertex(float x, float y, float z, float u, float v, float nx, float ny, float nz)
	{
		position = DirectX::XMFLOAT4(x, y, z, 0);
		tex = DirectX::XMFLOAT2(u, v);
		normal = DirectX::XMFLOAT3(nx, ny, nz);
	}

#endif

	DirectX::XMFLOAT4 position;	// spatial position
	DirectX::XMFLOAT2 tex;		// texture coordinates
	DirectX::XMFLOAT3 normal;	// vertex normal
};
