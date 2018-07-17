// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD vertex shader
//
// This shader is used to simulate the DMD pixel structure in a
// video window.

cbuffer MatrixBuffer
{
	matrix viewMatrix;
}
cbuffer MatrixBuffer
{
	matrix projectionMatrix;
}
cbuffer MatrixBuffer
{
	matrix worldMatrix;
};


struct VertexInputType
{
	float4 position : POSITION;
	float2 tex : TEXCOORD;
	float3 normal : NORMAL;
};

struct PixelInputType
{
	float4 position : SV_POSITION;
	float2 tex : TEXCOORD0;
	float3 normal : NORMAL;
};


PixelInputType main(VertexInputType input)
{
	PixelInputType output;

	// Change the position vector to be 4 units for proper matrix calculations
	input.position.w = 1.0f;

	// Calculate the position of the vertex against the world, view, and projection matrices
	output.position = mul(input.position, worldMatrix);
	output.position = mul(output.position, viewMatrix);
	output.position = mul(output.position, projectionMatrix);

	// Store the texture coordinates for the pixel shader
	output.tex = input.tex;

	// Calculate the normal vector against the world matrix only
	output.normal = normalize(mul(input.normal, (float3x3)worldMatrix));

	return output;
}
