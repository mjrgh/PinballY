// This file is part of PinballY
// Copyright 2019 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// YUVA 4:2:0 (VLC FOURCC 'I40A') and YUVA 4:4:4 shaders ('YUVA')
//
// This shader takes Y:U:V:A data as four separate textures, one per 
// plane.  The Y plane provides 8 bits of luma data per pixel; the U 
// and V planes each provide 8 bits of chroma data per 2x2 pixel block;
// and the A plane provides 8 bits of alpha (transparency) data per
// pixel.
//
// This shader also works with YUVA (4:4:4) data with no changes.  YUVA
// uses 8 bits per image pixel every plane, so the only difference
// from 4:2:0 is that the U and V planes have more pixels.  This is
// transparent to the shader because we use normalized texture coords.
// Simply bind the supersized textures the same way as for I420.
//
// For libvlc, I420 is the most direct and efficient (in terms of CPU 
// load) output format for most of the the decoders, so this is our 
// preferred format.  Libvlc is capable of performing color space
// conversions from YUV to RGB itself, but doing the conversions in
// libvlc is extremely CPU intensive, to such a degree that libvlc
// can't keep up with real-time playback of more than one or two
// streams, even on a fast CPU.  Moving the color space conversion 
// to the shader shifts the load from the CPU to the GPU.  GPUs are
// inherently more efficient at this kind of work.  A mid-range GPU
// can easily outperform a high-end CPU at this, so we'll get good
// playback on a wider range of machines using this approach.
//
// The DXGI layer in D3D11.1 has native support for a number of YUV
// formats, but this shader doesn't use any of that.  This is important
// because DXGI 11.1 only exists on Windows 8 and later, and pre-11.1 
// DXGI *doesn't* have any YUV support.  Using native DXGI support for
// YUV would therefore make us incompatible with Windows 7, which is
// still popular on pin cabs.  This shader intentionally doesn't use 
// any native DXGI YUV formats in order to maintain Windows 7 and 
// Vista compatibility.
// 
// Use this shader by preparing the Y, U, and V planes as three separate
// textures of type DXGI_FORMAT_R8_UNORM.  The Y texture has one byte 
// per pixel of the final image; the U and V textures each have one 
// 8-bit element for each 2x2 pixel block in the final image, so the U 
// and V textures are half the width and height of the Y texture.  
//
// This arrangement matches the raw byte layout that libvlc's decoders
// produce as output when set to I420 format.  In I420 output mode, the
// libvlc decoders produce the Y-U-V data for each frame in separate 
// Y-U-V memory buffers.  You simply take those three memory buffers 
// from libvlc and create a D3D DXGI_FORMAT_R8_UNORM texture (and 
// a matching shader resource view) for each one.  Bind these three 
// resource views to this shader (in order Y, U, V), and render.  We
// sample the Y-U-V data from the three inputs, convert to RGB, and
// output the RGB pixel for use on the render surface.

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
	float Y = 1.164f * ((YTexture.Sample(SampleType, input.tex).r * 255.0f) - 16.0f);
	float U = (UTexture.Sample(SampleType, input.tex).r * 255.0f) - 128.0f;
	float V = (VTexture.Sample(SampleType, input.tex).r * 255.0f) - 128.0f;

	// Get the A (alpha) value.  No conversion is necessary, as YUVA alpha
	// is interpreted the same way as RGBA alpha.
	float A = ATexture.Sample(SampleType, input.tex).r;

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
