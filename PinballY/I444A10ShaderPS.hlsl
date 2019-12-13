// This file is part of PinballY
// Copyright 2019 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// YUVA 4:4:4 10-bit-per-pixel little-endian shader ('YAOL')
//
// This is a variation on the YUVA shader that works with 10-bit pixels
// instead of 8-bit pixels.  This is all pretty convoluted:
//
//  - In the initial buffer that libvlc sends to us, libvlc unpacks
//    the 10-bit pixels into 16-bit little-endian byte pairs, so each
//    pixel is represented by 2 bytes in each plane buffer
//
//  - DXGI doesn't have a native 10-bit single-element format.  The
//    closest thing is the R16_UNORM format - 16-bit normalized
//    elements.  "Normalized" means that each byte pair will appear
//    to the shader as effectively static_cast<float>(pix)/65535.0f,
//    where 'pix' is the raw byte pair unpacked by libvlc.
//
//  - Each byte pair in the buffer actually came from 10-bit data, so
//    the denominator in the normalization step *should* be 1023.0f.
//    Again, DXGI doesn't have a 10-bit format, so we can't get it
//    to use the right normalization.  Ergo, we have to compensate
//    in the shader, by multiplying each normalized float value by
//    64.0f.
//
// Apart from the renormalization, this shader works just like the
// YUVA shader.

Texture2D<float> YTexture;
Texture2D<float> UTexture;
Texture2D<float> VTexture;
Texture2D<float> ATexture;
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
	// The standard YUV to RGB conversion formula, for 8-bit (0..255)
	// component values:
	//
	//  Y' = 1.164*(Y-16)
	//  U' = U - 128
	//  V' = V - 128
	//
	//  R = Y' + 1.596*V'
	//  G = Y' - 0.813*V' - 0.391*U'
	//  B = Y' + 2.018*U'
	//  
	// The final R, G, and B values must be clamped to the 0..255 range.

	// Get Y', U', V', converting from normalized 0..1 range to 0..255.
	// The three planes are represented as separate shader resource views,
	// so we sample one value from each view.  The texture coordinates are
	// the same in each view, even though the U and V planes are half the
	// width and height of the Y plane, because input.tex uses normalized
	// 0..1 coordinates that are independent of the texture dimensions.
	//
	// Remember that we have to renormalize the samples from the 65535.0f
	// normalization demoninator that DXGI used when passing us the buffer
	// to the 1023.0f (10 bit) scale that the original video data uses.
	float Y = 1.164f * ((YTexture.Sample(SampleType, input.tex).r * 255.0f * 64.0f) - 16.0f);
	float U = (UTexture.Sample(SampleType, input.tex).r * 255.0f * 64.0f) - 128.0f;
	float V = (VTexture.Sample(SampleType, input.tex).r * 255.0f * 64.0f) - 128.0f;

	// Get the A (alpha) value.  No conversion is necessary, other than the 
	// same renormalization we apply to the color channels.
	float A = ATexture.Sample(SampleType, input.tex).r * 64.0f;

	// Figure the RGB conversion, converting the final result back to
	// the normalized 0..1 range.
	float4 RGBA = float4(
		clamp(Y + 1.596f*V, 0, 255.0f) / 255.0f,
		clamp(Y - 0.813f*V - 0.391f*U, 0, 255.0f) / 255.0f,
		clamp(Y + 2.018f*U, 0, 255.0f) / 255.0f,
		clamp(A * alpha, 0, 1));

	// return the RGB result
	return RGBA;
}
