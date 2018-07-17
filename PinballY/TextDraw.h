// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Text Drawing.  This is designed for drawing a 2D text overlaid
// on the 3D view, for informational displays.
//
// The coordinate system for text items mimics normal window
// coordinates.  The origin is at the top left of the window, +X
// is to the right, and +Y is down.

#pragma once

#include "stdafx.h"
#include <vector>
#include <unordered_map>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "camera.h"
#include "Shader.h"
#include "d3d.h"
#include "TextShader.h"

// text object vertex type
struct TextVertexType
{
	DirectX::XMFLOAT4 position;
	DirectX::XMFLOAT2 texCoord;
};

// Font object - loads a DirectXTK formatted font file
class TextDrawFont
{
public:
	TextDrawFont();
	~TextDrawFont();

	// Load a font.  Returns true on success, false on failure.
	bool Load(const TCHAR *filename, ErrorHandler &handler);

	// Get a reference to my shader resource, adding a ref count
	ID3D11ShaderResourceView *GetShaderResourceView()
	{
		if (shaderResourceView != 0)
			shaderResourceView->AddRef();
		return shaderResourceView;
	}

	// Create a drawing list for a string
	HRESULT CreateBuffers(
		const TCHAR *text,
		ID3D11Buffer **vertexBuffer, ID3D11Buffer **indexBuffer, size_t *indexCount);

	// get the line height
	float GetLineHeight() const { return lineSpacing; }

	// measure text
	POINTF MeasureText(const TCHAR *text) const;

protected:
	// Glyph descriptor.  This matches the byte layout of the objects
	// in a font file created by MakeSpriteFont in the DirectXTK library.
	struct Glyph
	{
		uint32_t charCode;
		RECT subrect;
		float xOffset;
		float yOffset;
		float xAdvance;
	};

	// texture dimensions
	struct { uint32_t width, height;	} textureSize;

	// glyph data
	uint32_t nGlyphs;
	Glyph *glyphs;

	// glyph hash
	std::unordered_map<uint32_t, Glyph *> glyphMap;

	// default character
	Glyph *defaultGlyph;

	// line height
	float lineSpacing;

	// shader resource view
	ID3D11ShaderResourceView *shaderResourceView;
};

// Text item.  This is a D3D triangle list for a string of text.
class TextDrawItem : public Align16
{
public:
	TextDrawItem();

	// reference counting
	void AddRef() { ++refCnt; }
	void Release() { if (--refCnt == 0) delete this; }

	// Create from a font and text string
	HRESULT Load(
		const TCHAR *text,
		TextDrawFont *font, DirectX::XMFLOAT4 color,
		float x, float y, float rotation);

	// Set the location
	void SetLoc(float x, float y)
	{
		pos.x = x;
		pos.y = y;
		RecalcWorld();
	}

	// set the rotation
	void SetRotation(float r)
	{
		rotation = r;
		RecalcWorld();
	}

	// set the color
	void SetColor(DirectX::XMFLOAT4 color)
	{
		this->color = color;
	}

	void Render(TextShader *shader);

protected:
	// reference count
	int refCnt;

	// position - upper left corner
	struct { float x, y; } pos;

	// rotation in radians, clockwise from horizontal left-to-right
	float rotation;

	// color
	DirectX::XMFLOAT4 color;

	// our destructor is protected because life cycle is handled by 
	// reference counting
	~TextDrawItem();

	// recalculate the world matrix for a change in position or rotation
	void RecalcWorld();

	// vertex buffer
	ID3D11Buffer *vertexBuffer;

	// index buffer
	ID3D11Buffer *indexBuffer;
	size_t indexCount;

	// font texture
	ID3D11ShaderResourceView *shaderResourceView;

	// world transform matrix
	DirectX::XMMATRIX world;
};

// TextDraw - create an instance of this to manage a collection of
// text to display.
class TextDraw
{
public:
	TextDraw();
	~TextDraw();

	// initialize
	bool Init();

	// Clear all drawing items
	void Clear();

	// add a string
	void Add(
		const TCHAR *text, 
		TextDrawFont *font, DirectX::XMFLOAT4 color,
		float x, float y, float rotation);

	// Add a string from an existing item
	void Add(TextDrawItem *item) 
	{
		item->AddRef();
		items.push_back(item); 
	}

	// Render
	void Render(Camera *camera);

	// Look up a font, loading it into our cache if it's not already present
	TextDrawFont *GetFont(const TCHAR *filename, ErrorHandler &handler);

protected:
	// Shader
	TextShader *shader;

	// font cache
	std::unordered_map<TSTRING, TextDrawFont *> fontCache;

	// active text item list
	std::vector<TextDrawItem *> items;
};
