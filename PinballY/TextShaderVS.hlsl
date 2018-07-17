// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
cbuffer MatrixBuffer
{
	matrix viewMatrix;
}
cbuffer MatrixBuffer
{
	matrix projectionMatrix;
}
cbuffer WorldBuffer
{
	matrix worldMatrix;
}

struct VertexInputType
{
	float4 position: POSITION;
	float2 texCoord: TEXCOORD;
};

struct PixelInputType
{
	float4 position: SV_POSITION;
	float2 texCoord: TEXCOOR0;
};

PixelInputType VS(VertexInputType input)
{
	PixelInputType output;

	input.position.w = 1.0f;

	output.position = mul(input.position, worldMatrix);
	output.position = mul(output.position, viewMatrix);
	output.position = mul(output.position, projectionMatrix);

	output.texCoord = input.texCoord;

	return output;
}
