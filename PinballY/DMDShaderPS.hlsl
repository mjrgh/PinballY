// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD pixel shader
//
// This shader is used to render the 128x32 pixel structure of a DMD
// in a regular video window simulating a DMD.

Texture2D shaderTexture;
SamplerState SampleType;

cbuffer AlphaBufferType
{
	float alpha;
	float3 padding;
}

cbuffer BkColorBufferType
{
	float4 bkColor;
};

struct PixelInputType
{
	float4 position : SV_POSITION;
	float2 tex : TEXCOORD0;
	float3 normal : NORMAL;
};

float4 main(PixelInputType input) : SV_TARGET
{
	// discard fully transparent pixels
	if (alpha == 0)
		discard;

	// Figure the coordinates in a discrete 128x32 space.  DX11 texel
    // coordinates are centered on the half-pixel positions (0.5, 1.5,
    // etc), so take the floor and add 0.5 to get the nominal texel
    // coordinate that we'll sample.
	float x = input.tex.x * 128.0f;
	float y = input.tex.y * 32.0f;
	float xr = floor(x) + 0.5f;
	float yr = floor(y) + 0.5f;

	// get the color from the discrete grid position
	float4 textureColor = shaderTexture.Sample(SampleType, float2(xr/128.0f, yr/32.0f));

	// apply the global alpha
	textureColor.w *= alpha;

	// Now figure the (square of) the distance between the actual
	// texel coordinate (which is always an integer + 0.5) and the
	// scaled coordinate before pegging it to the texel.  We'll use
	// this to draw the DMD-scale pixel as a circle centered on the
	// texel coord.  This reproduces the visible pixel structure of
	// a real DMD by showing each source pixel as a small circle on
	// the video rendition.
	float xd = x - xr;
	float yd = y - yr;
	float r2 = xd*xd + yd*yd;

	// Roll off opacity and truncate past a certain point.  This
	// gives the pixel we're drawing a soft edge, which makes it 
	// look more like a plasma pixel.  Use alpha so that we can
	// blend against arbitrary background colors.
	// if (r2 > .25f) discard;
	// textureColor.w *= 1.0f - r2/0.25f;
	if (r2 > .25f)
		textureColor = bkColor;
	else
		textureColor = (textureColor * (1.0f - r2 / 0.25f)) + (bkColor * r2 / 0.25f);

	// return the texture color
	return textureColor;
}
