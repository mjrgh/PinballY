// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Base class for shaders.
//
// A shader encapsulates the way an object's surfaces reflect light:
// how diffuse or specular the reflections are, for example.  A shader 
// basically represents a broad class of materials, such as "shiny
// metallic" or "rough".  Shaders *don't* represent the image drawn
// onto an object's surfaces; that's handled separately through texture 
// graphics.
//
// The usual setup for 3D rendering is to have a small number of 
// shaders, and a large number of drawing objects, with each shader
// used by many drawing objects.  It works out this way because of the
// way a shader represents a broad class of materials; there are only
// so many different materials in a typical model, and many objects
// are built out of the same or similar types of materials, similar
// enough to share shaders.
//
// This arrangement with many objects sharing a few shaders makes it
// most efficient to organize our rendering process such that we draw
// all of the objects sharing a given shader as a group.  This is
// efficient because it lets us load all of the shader parameters into
// the GPU once, then keep reusing the same parameters as we iterate
// over the drawing objects.  As such, each shader keeps a list of its
// drawing objects.
//
// Direct3D separates things into vertex and pixel shaders, but that's
// really an implementation detail.  Conceptually, a shader is really 
// a unit consisting of a vertex shader part and a pixel shader part.  
// So our class hierarchy simply has a single Shader that combines the 
// two facets into one object.  Under the covers, of course, we have to 
// create separate D3D objects for the two functions, but we expose
// these as a matched set.
//
// A given shader subclass is always mated to a pair of .hsls files,
// one for the vertex shader function and one for the pixel shader
// function.  These files provide the *video card* code for the
// shader: this code gets loaded into the GPU for execution at run
// time.  Our convention is to name each shader subclass XxxShader,
// with associated files XxxShaderPS.hlsl and XxxShaderVS.hlsl, for
// the pixel and vertex shader functions, respectively.

#pragma once

#include "stdafx.h"
#include <list>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "D3D.h"


class Sprite;
class Camera;

class Shader : public Align16, public WeakRefable<Shader>
{
public:
	Shader();
	virtual ~Shader();

	// Shader ID.  This is a permanent, external identifier for the
	// shader type.  This is stored in saved game files to associate
	// drawing objects with their shaders, so it has to be stable
	// across versions.
	virtual const char *ID() const = 0;

	// initialize - override per subclass
	virtual bool Init() = 0;

	// Prepare for drawing via the shader.  This loads our shader
	// programs into the GPU.
	void PrepareForRendering(Camera *camera);

	// set input buffers for pixel and vertex shaders
	virtual void SetShaderInputs(Camera *camera) = 0;

	// Set the alpha transparency level for the current rendering, if the
	// shader supports it.
	virtual void SetAlpha(float alpha) = 0;

protected:
	// Create the input layout
	bool CreateInputLayout(
		D3D *d3d,
		D3D11_INPUT_ELEMENT_DESC *layoutDesc, int layoutDescCount,
		const BYTE *shaderByteCode, int shaderByteCodeLen);

	// shaders
	ID3D11VertexShader *vs;
	ID3D11PixelShader *ps;
	ID3D11GeometryShader *gs;

	// input layout
	ID3D11InputLayout *layout;

	// Current prepared shader.  Note:  this is for comparison purposes 
	// only, to detect in PrepareForRendering() if this shader has already 
	// been prepared.  Don't dereference this, as it's not protected against
	// the shader having been destroyed since set here.
	static Shader *currentPreparedShader;
};

