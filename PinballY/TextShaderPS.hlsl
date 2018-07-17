// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
Texture2D Texture;
sampler TextureSampler;

cbuffer ColorBufferType
{
	float4 color;
}

struct PixelInputType
{
	float4 position: SV_POSITION;
	float2 texCoord: TEXCOOR0;
};

float4 PS(PixelInputType input) : SV_TARGET
{
	return Texture.Sample(TextureSampler, input.texCoord) * color;
}
