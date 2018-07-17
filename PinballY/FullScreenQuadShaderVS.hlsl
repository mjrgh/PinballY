// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
struct PixelInputType
{
	float4 position : SV_POSITION;
	float2 tex : TEXCOORD0;
};

PixelInputType VS(uint vI : SV_VERTEXID)
{
	PixelInputType output;

	output.tex = float2(vI & 1, vI >> 1); 
	output.position = float4((output.tex.x - 0.5f) * 2, - (output.tex.y - 0.5f) * 2, 0, 1);
	return output;
}
