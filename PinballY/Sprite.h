// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Sprite.  This implements simple 2D drawing object that shows
// a static bitmap mapped onto a rectangle.  The rectangle is
// actually a D3D mesh consisting of a pair of triangles covering
// the rectangle area.  The sprite can be scaled, translated, and
// rotated just like any D3D mesh.
//
// The bitmap can be created by loading a file (in one of the
// supported WIC formats: PNG, JPEG, BMP), by using an existing
// HBITMAP object (e.g., loaded from a resource or created in
// memory), by using an existing DIB section, or by drawing into
// a GDI device context (DC) via a callback function.  The dynamic 
// GDI drawing mechanism provides an easy way to create dynamic 
// content without the usual hassle of managing all of the Windows
// resources involved in off-screen drawing.
//
// Once created, a Sprite object can be rendered by the TextureShader
// via the usual two-step process:
//
// - Invoke PSSetShaderResources() to pass our 2D texture buffer
//   to the TextureShader pixel shader
//
// - Invoke Render() to draw the mesh
//

#pragma once
#include "D3D.h"

class Camera;
class FlashClientSite;
class Shader;

class Sprite: public RefCounted
{
public:
	Sprite();

	// Load a texture file.  The normalized size is in terms of our
	// normalized screen dimensions, where 1.0 is the window height;
	// this is used for the layout of the 3D mesh object.  The pixel
	// size is used to determine the rasterization size for vector 
	// graphic media (e.g., Flash objects).  It's ignored for raster
	// images (e.g., JPEG, PNG), which are loaded at the native size
	// for the media.
	bool Load(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh);

	// Load from a Shockwave Flash file.  The regular Load(filename,...) method
	// calls this when it detects Flash content, so you don't have to call this
	// explicitly unless you know for certain that a file contains Flash data
	// and doesn't need to be checked for other content types.
	bool LoadSWF(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh);

	// Load from an HBITMAP
	bool Load(HDC hdc, HBITMAP hbitmap, ErrorHandler &eh, const TCHAR *descForErrors);

	// load from a device-independent bitmap pixel array
	bool Load(const BITMAPINFO &bmi, const void *dibits, ErrorHandler &eh, const TCHAR *descForErrors);

	// Load by drawing into an off-screen HDC.  This allows dynamic content
	// to be created via GDI or GDI+ and then displayed through a sprite.
	// The off-screen bitmap for drawing is created with the given pixel
	// width and height; we scale the sprite to our normalized screen 
	// dimensions (1920-pixel screen height).
	bool Load(int pixWidth, int pixHeight, std::function<void(HDC, HBITMAP)> drawingFunc,
		ErrorHandler &eh, const TCHAR *descForErrors);

	// Render the sprite
	virtual void Render(Camera *camera);

	// Do the basic mesh rendering.  This renders the mesh using whatever
	// shader resource view is currently loaded.
	void RenderMesh();

	// image load size, in normalized coordinates (window height = 1.0)
	POINTF loadSize;

	// common data structure for 3D spatial data
	struct XYZ
	{
		FLOAT x, y, z;
	};

	// spatial position, rotation, and scale
	XYZ offset;
	XYZ rotation;
	XYZ scale;

	// global alpha transparency
	float alpha;

	// Start a fade
	void StartFade(int dir, DWORD milliseconds);

	// update the fade for the current time
	float UpdateFade();

	// is a fade in progress?
	bool IsFading() const { return fadeDir != 0; }

	// has the last fade completed?
	bool IsFadeDone(bool reset = FALSE);

	// update our world transform for a change in offset, rotation, or scale
	void UpdateWorld();

	// Advise the sprite of the window size.  This adjusts the texture
	// rasterization to match the sprite size, if appropriate.  This is
	// only necessary if the underlying texture comes from vector graphics
	// media, such as a Flash object.
	void AdviseWindowSize(SIZE szLayout);

protected:
	virtual ~Sprite();

	// detach the Flash object, if present
	void DetachFlash();

	// create the texture and resource view from a bitmap
	bool CreateTextureFromBitmap(const BITMAPINFO &bmi, const void *dibits, ErrorHandler &eh, const TCHAR *descForErrors);

	// create the mesh
	bool CreateMesh(POINTF normalizedSize, ErrorHandler &eh, const TCHAR *descForErrors);

	// create the staging texture
	bool CreateStagingTexture(int pixWidth, int pixHeight, ErrorHandler &eh);

	// Alpha fade parameters.  A sprite can manage a fade in/out when
	// rendering.  The caller simply provides the total fade time and
	// direction.  fadeDir is positive for a fade-in, negative for a
	// fade-out, and zero if no fade is in progress.  The times are in
	// milliseconds, using GetTickCount().
	int fadeDir;
	DWORD fadeStartTime;
	DWORD fadeDuration;

	// the last fade has completed
	bool fadeDone;

	// Vertex and index lists.  Our sprites are always rectangular, 
	// so they consist of four vertices and two triangles.
	CommonVertex vertex[4];
	WORD index[6];

	// Vertex and index buffers
	RefPtr<ID3D11Buffer> vertexBuffer;
	RefPtr<ID3D11Buffer> indexBuffer;

	// Flash client site, for SWF objects
	RefPtr<FlashClientSite> flashSite;

	// Get my shader.  Most sprites use the basic Texture Shader, but 
	// special sprites can use a different shader as needed.
	virtual Shader *GetShader() const;

	// our texture, and its shader resource view
	RefPtr<ID3D11Resource> texture;
	RefPtr<ID3D11ShaderResourceView> rv;
	
	// staging texture - used only for Flash objects
	RefPtr<ID3D11Texture2D> stagingTexture;

	// world transform matrix
	DirectX::XMMATRIX world;

	// transposed world matrix, for passing to the shader
	DirectX::XMMATRIX worldT;
};
