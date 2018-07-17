// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Text Drawing

#include "stdafx.h"
#include <ctype.h>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "camera.h"
#include "d3d.h"
#include "TextDraw.h"
#include "TextShader.h"
#include "Resource.h"

using namespace DirectX;

// -------------------------------------------------------------------------
//
// TextDraw - text drawing handler
//

TextDraw::TextDraw()
{
	shader = 0;
}

TextDraw::~TextDraw()
{
	// clear the text item list
	Clear();

	// clear the font cache
	for (auto it = fontCache.begin(); it != fontCache.end(); ++it)
		delete it->second;
	fontCache.clear();

	// delete our text shader
	delete shader;
}

bool TextDraw::Init()
{
	// create and initialize our shader
	shader = new TextShader();
	if (!shader->Init())
		return false;

	// success
	return true;
}

void TextDraw::Render(Camera *camera)
{
	D3D *d3d = D3D::Get();

	// turn off the depth stencil
	d3d->SetUseDepthStencil(false);

	// set up rendering the shader
	shader->PrepareForRendering(camera);

	// draw each item in the list
	for (auto it = items.begin(); it != items.end(); ++it)
		(*it)->Render(shader);
}

void TextDraw::Clear()
{
	// discard the meshes in our list
	while (items.size() != 0)
	{
		items.back()->Release();
		items.pop_back();
	}
}

void TextDraw::Add(
	const TCHAR *text, 
	TextDrawFont *font, XMFLOAT4 color,
	float x, float y, float rotation)
{
	// create an item
	TextDrawItem *item = new TextDrawItem();

	// load it
	if (SUCCEEDED(item->Load(text, font, color, x, y, rotation)))
		Add(item);

	// remove our local reference
	item->Release();
}

TextDrawFont *TextDraw::GetFont(const TCHAR *filename, ErrorHandler &handler)
{
	// check the cache
	auto it = fontCache.find(filename);
	if (it != fontCache.end())
		return it->second;

	// it's not in the cache - load it
	TextDrawFont *font = new TextDrawFont();
	if (!font->Load(filename, handler))
	{
		delete font;
		return 0;
	}

	// add it to the cache and return it
	fontCache.emplace(std::make_pair(filename, font));
	return font;
}

// -------------------------------------------------------------------------
//
// TextDrawItem - one TextDraw string
//

TextDrawItem::TextDrawItem()
{
	// set a reference on behalf of the caller
	refCnt = 1;

	// clear pointers
	vertexBuffer = 0;
	indexBuffer = 0;
	indexCount = 0;
	shaderResourceView = 0;
}

TextDrawItem::~TextDrawItem()
{
	if (vertexBuffer != 0) vertexBuffer->Release();
	if (indexBuffer != 0) indexBuffer->Release();
	if (shaderResourceView != 0)
		shaderResourceView->Release();
}

HRESULT TextDrawItem::Load(
	const TCHAR *text,
	TextDrawFont *font, XMFLOAT4 color,
	float x, float y, float rotation)
{
	// set our position, rotation, and color
	pos.x = x;
	pos.y = y;
	this->rotation = rotation;
	this->color = color;

	// update the world matrix
	RecalcWorld();

	// remember the new font texture
	ID3D11ShaderResourceView *oldsrv = shaderResourceView;
	shaderResourceView = font->GetShaderResourceView();

	// release old resources
	if (indexBuffer != 0)
	{
		indexBuffer->Release();
		indexBuffer = 0;
	}
	if (vertexBuffer != 0)
	{
		vertexBuffer->Release();
		vertexBuffer = 0;
	}
	if (oldsrv != 0)
		oldsrv->Release();

	// load the buffer via the font
	return font->CreateBuffers(text, &vertexBuffer, &indexBuffer, &indexCount);
}

void TextDrawItem::Render(TextShader *shader)
{
	// set the font texture
	D3D *d3d = D3D::Get();
	d3d->PSSetShaderResources(0, 1, &shaderResourceView);

	// set our color in the shader
	shader->SetColor(color);

	// load our world transform for the vertex shader
	d3d->UpdateWorldTransform(world);

	// load our vertex and index buffers
	d3d->SetTriangleTopology();
	d3d->IASetVertexBuffer(vertexBuffer, sizeof(TextVertexType));
	d3d->IASetIndexBuffer(indexBuffer);

	// draw
	d3d->DrawIndexed((INT)indexCount);
}

void TextDrawItem::RecalcWorld()
{
	// Figure our world translation matrix for our current rotation and
	// position.  Note that the position is set in a window-like coordinate
	// system where +X is right and +Y is down.  The D3D Y axis is the other
	// way around, so we need to use the negative Y value.  The camera view
	// automatically places the coordinate system origin at top left, so we
	// don't need to worry about the view size or orientation here.
	world = XMMatrixIdentity();
	world = XMMatrixMultiply(world, XMMatrixRotationZ(rotation));
	world = XMMatrixMultiply(world, XMMatrixTranslation(pos.x, -pos.y, 0));
	world = XMMatrixTranspose(world);
}


// -------------------------------------------------------------------------
//
// TextDrawFont - font object for TextDraw
//

TextDrawFont::TextDrawFont()
{
	defaultGlyph = 0;
	nGlyphs = 0;
	glyphs = 0;
	shaderResourceView = 0;
}

TextDrawFont::~TextDrawFont()
{
	if (glyphs != 0) delete[] glyphs;
	if (shaderResourceView != 0) shaderResourceView->Release();
}

bool TextDrawFont::Load(const TCHAR *filename, ErrorHandler &handler)
{
	D3D *d3d = D3D::Get();

	// read the file
	BinaryReader r;
	if (!r.Load(filename, handler))
		return false;

	// delete old resources if present
	if (glyphs != 0)
	{
		delete[] glyphs;
		glyphs = 0;
	}
	defaultGlyph = 0;
	glyphMap.clear();

	// check the signature
	static const char sig[] = "DXTKfont";
	char filesig[8];
	if (!r.Read(filesig, 8) || memcmp(filesig, sig, sizeof(filesig)) != 0)
	{
		handler.Error(MsgFmt(IDS_ERR_BADDXTKFONT, filename));
		return false;
	}

    auto EofErr = [&handler, filename]() {
        handler.Error(MsgFmt(IDS_ERR_EOFDXTKFONT, filename));
        return false;
    };
        
	// read the glyph data
	if (!r.Read(nGlyphs))
		return EofErr();

	uint32_t defaultChar;
	glyphs = new Glyph[nGlyphs];
	if (!r.Read(glyphs, nGlyphs)
		|| !r.Read(lineSpacing)
        || !r.Read(defaultChar))
        return EofErr();

	// Texture data description.  This matches the byte layout in
	// MakeSpriteFont files.
	struct TextureInfo
	{
		TextureInfo() { data = 0; }
		~TextureInfo() { if (data != 0) delete[] data; }
		uint32_t width;
		uint32_t height;
		DXGI_FORMAT format;
		uint32_t stride;
		uint32_t nRows;
		UINT8 *data;
	}
	texture;
	
	// read the texture header
	if (!r.Read(texture.width)
		|| !r.Read(texture.height)
		|| !r.Read(texture.format)
		|| !r.Read(texture.stride)
        || !r.Read(texture.nRows))
        return EofErr();

	// remember the texture size
	textureSize = { texture.width, texture.height };

	// read the texture data
	texture.data = new UINT8[texture.stride * texture.nRows];
    if (!r.Read(texture.data, texture.stride * texture.nRows))
        return EofErr();

	// create the D3D texture
	CD3D11_TEXTURE2D_DESC texDesc(
		texture.format, texture.width, texture.height, 1, 1, 
		D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE);
	CD3D11_SHADER_RESOURCE_VIEW_DESC viewDesc(
		D3D11_SRV_DIMENSION_TEXTURE2D, texture.format);
	D3D11_SUBRESOURCE_DATA initData = { texture.data, texture.stride };
	HRESULT hr;
	if (!SUCCEEDED(hr = d3d->CreateTexture2D(&texDesc, &initData, &viewDesc, &shaderResourceView)))
	{
        handler.SysError(LoadStringT(IDS_ERR_FONTINIT),
			MsgFmt(_T("CreateTexture2D failed, error code %lx"), hr));
		return false;
	}

	// build the glyph hash
	for (uint32_t i = 0; i < nGlyphs; ++i)
		glyphMap.emplace(std::make_pair(glyphs[i].charCode, &glyphs[i]));

	// look up the default character
	auto it = glyphMap.find(defaultChar);
	if (it != glyphMap.end())
		defaultGlyph = it->second;

	// success
	return true;
}

HRESULT TextDrawFont::CreateBuffers(
	const TCHAR *text,
	ID3D11Buffer **vertexBuffer, ID3D11Buffer **indexBuffer, size_t *indexCount)
{
	D3D *d3d = D3D::Get();
	HRESULT hr;

	// start with empty vertex and index lists
	std::vector<TextVertexType> vertices;
	std::vector<WORD> indices;

	// start at the top left corner
	float x = 0, y = 0;

	// no vertices in the list yet
	int nv = 0;

	// add each character
	for (const TCHAR *p = text; *p != 0; ++p)
	{
		// handle newlines specially
		if (*p == '\n')
		{
			x = 0;
			y -= lineSpacing;
			continue;
		}

		// skip carriage returns
		if (*p == '\r')
			continue;

		// look up the glyph
		auto it = glyphMap.find(*p);
		Glyph *g = it != glyphMap.end() ? it->second : defaultGlyph;

		// we must have a glyph to proceed
		if (g == 0)
			return E_FAIL;

		// advance by the offset to get the start position for the character cell
		x += g->xOffset;

		// figure the advance distance for the character
		float advance = g->subrect.right - g->subrect.left + g->xAdvance;

		// build the graphics box for the character unless it's whitespace
		if (!_istspace(*p))
		{
			// figure the character cell bounding box
			float left = x;
			float top = y - g->yOffset;
			float right = left + g->subrect.right - g->subrect.left;
			float bottom = top - (g->subrect.bottom - g->subrect.top);

			// figure the texture coordinates
			float u0 = float(g->subrect.left) / textureSize.width;
			float u1 = float(g->subrect.right) / textureSize.width;
			float v0 = float(g->subrect.top) / textureSize.height;
			float v1 = float(g->subrect.bottom) / textureSize.height;

			// create the box's vertex list
			vertices.push_back({ XMFLOAT4(left, top, 0, 0), XMFLOAT2(u0, v0) });
			vertices.push_back({ XMFLOAT4(right, top, 0, 0), XMFLOAT2(u1, v0) });
			vertices.push_back({ XMFLOAT4(right, bottom, 0, 0), XMFLOAT2(u1, v1) });
			vertices.push_back({ XMFLOAT4(left, bottom, 0, 0), XMFLOAT2(u0, v1) });

			// create the index list
			indices.push_back(nv + 0);
			indices.push_back(nv + 1);
			indices.push_back(nv + 2);
			indices.push_back(nv + 2);
			indices.push_back(nv + 3);
			indices.push_back(nv + 0);

			// advance the vertex counter
			nv += 4;
		}

		// advance by the character width
		x += advance;
	}

	// set up the vertex buffer descriptor
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = (UINT)(sizeof(TextVertexType)*vertices.size());
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	// set up the subresource descriptor
	D3D11_SUBRESOURCE_DATA sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.pSysMem = vertices.data();

	// create the vertex buffer
	if (FAILED(hr = d3d->CreateBuffer(&bd, &sd, vertexBuffer, "TextDraw::vertexBuffer")))
		return hr;

	// set up the index buffer descriptor
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = (UINT)(sizeof(WORD) * indices.size());
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = 0;
	sd.pSysMem = indices.data();
	if (FAILED(hr = d3d->CreateBuffer(&bd, &sd, indexBuffer, "TextDraw::indexBuffer")))
		return hr;

	// remember the index count
	*indexCount = indices.size();

	// success
	return S_OK;
}

POINTF TextDrawFont::MeasureText(const TCHAR *text) const
{
	// start at the top left corner
	float x = 0, y = 0;

	// iterate over the characters
	for (const TCHAR *p = text; *p != 0; ++p)
	{
		// handle newlines specially
		if (*p == '\n')
		{
			x = 0;
			y -= lineSpacing;
			continue;
		}

		// skip carriage returns
		if (*p == '\r')
			continue;

		// look up the glyph
		auto it = glyphMap.find(*p);
		Glyph *g = it != glyphMap.end() ? it->second : defaultGlyph;

		// skip missing characters
		if (g == 0)
			continue;

		// figure the advance width
		x += g->xOffset + (g->subrect.right - g->subrect.left) + g->xAdvance;
	}

	// return the result
	return { x, y };
}
