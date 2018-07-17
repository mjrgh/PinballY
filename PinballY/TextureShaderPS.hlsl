// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Texture Shader - pixel shader

Texture2D shaderTexture;
SamplerState SampleType;

cbuffer AlphaBufferType
{
	float alpha;
	float3 padding;
}

struct PixelInputType
{
	float4 position : SV_POSITION;
	float2 tex : TEXCOORD0;
	float3 normal : NORMAL;
};

float4 main(PixelInputType input) : SV_TARGET
{
	// pass through the color from the texture
	float4 textureColor;
	textureColor = shaderTexture.Sample(SampleType, input.tex);

	// apply the global alpha
	textureColor.w *= alpha;

	// Discard fully transparent pixels (w=0 -> 0% transparency).  This allows us to draw objects
	// that use transparency only for cropping purposes in any order.  Since we're not writing
	// anything to a transparent pixel on this object, we won't update the depth stencil for
	// this pixel, hence an object that's drawn after us in drawing order will still get to
	// write to this pixel, even if it's behind us in depth order.  This doesn't work for
	// partial transparency, so we still need to handle that separately, but this is an easy
	// and computationally efficient way to handle full transparency.
	if (textureColor.w == 0)
		discard;

	// return the texture color
	return textureColor;
}
