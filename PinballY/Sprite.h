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
struct DIBitmap;

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

	// Load from an HBITMAP
	bool Load(HDC hdc, HBITMAP hbitmap, ErrorHandler &eh, const TCHAR *descForErrors);

	// load from a device-independent bitmap pixel array
	bool Load(const BITMAPINFO &bmi, const void *dibits, ErrorHandler &eh, const TCHAR *descForErrors);
	bool Load(const DIBitmap &dib, ErrorHandler &eh, const TCHAR *descForErrors);

	// Load by drawing into an off-screen HDC.  This allows dynamic content
	// to be created via GDI or GDI+ and then displayed through a sprite.
	// The off-screen bitmap for drawing is created with the given pixel
	// width and height; we scale the sprite to our normalized screen 
	// dimensions (1920-pixel screen height).
	bool Load(int pixWidth, int pixHeight, std::function<void(HDC, HBITMAP)> drawingFunc,
		ErrorHandler &eh, const TCHAR *descForErrors);

	// Load by drawing into an off-screen Gdiplus::Graphics contxt
	bool Load(int pixWidth, int pixHeight, std::function<void(Gdiplus::Graphics &g)> drawingFunc,
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

	// re-create the mesh
	void ReCreateMesh();

	// Clear the sprite.  This frees any exeternal resources currently 
	// in use, such as video playback streams.
	virtual void Clear();

protected:
	virtual ~Sprite();

	// detach the Flash object, if present
	void DetachFlash();

	// Load from a Shockwave Flash file.  The regular Load(filename,...)
	// method calls this when it detects Flash content.
	bool LoadSWF(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh);

	// Load a GIF image file.  The regular Load(filename,...) method calls
	// this when it detects GIF contents.
	bool LoadGIF(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh);

	// Load a texture from an image file using WIC.  This does a direct
	// WIC load, which handles the common image formats (JPEG, PNG, GIF),
	// but doesn't have support for orientation metadata or multi-frame
	// animated GIFs.
	bool LoadWICTexture(const WCHAR *filename, POINTF normalizedSize, ErrorHandler &eh);

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

	// Deferred loader context.  Loading images can take a noticable
	// amount of time - enough to cause visible rendering glitches, 
	// if done on the foreground thread.  To mitigate this, we allow
	// for loading via a background thread.  To make it easy to
	// manage the resources, we create a loader context object,
	// which we share with the loader.  This is particularly useful
	// if the foreground thread happens to re-load a new image at
	// some point before the background thread is finished, in
	// which case we just discard the loader context and set up a
	// new one.  The background thread finishes its loading and
	// happily updates its context, which we no longer care about.
	// The context is harmlessly deleted when the loader releases
	// its last reference.
	struct LoadContext : RefCounted
	{
		// Is the object ready?  The render won't use the resources
		// until this is true, so the loader lets us know that it's
		// done by setting this flag.  Note that no heavier-weight
		// thread synchronization is needed, since this can only be
		// written by the loader thread.
		//
		// Note that we initialize this to true by default, because
		// most of our loading is just done inline on the foreground
		// thread.  We only need to set this to false when we're
		// kicking off an async thread to do the loading.
		bool ready = true;

		// our texture, and its shader resource view
		RefPtr<ID3D11Resource> texture;
		RefPtr<ID3D11ShaderResourceView> rv;
	};

	// current loading context
	RefPtr<LoadContext> loadContext;

	// staging texture - used only for Flash objects
	RefPtr<ID3D11Texture2D> stagingTexture;

	// Animation frame 
	struct AnimFrame
	{
		// time to display this frame, in milliseconds
		DWORD dt;

		// text and shader resource view for the frame
		RefPtr<ID3D11Resource> texture;
		RefPtr<ID3D11ShaderResourceView> rv;
	};

	// animation frame list
	std::vector<std::unique_ptr<AnimFrame>> animFrames;

	// is an animation active?
	bool isAnimation = false;

	// current animation frame index
	int curAnimFrame = 0;

	// ending time of the current frame, in system ticks
	UINT64 curAnimFrameEndTime = 0;

	// world transform matrix
	DirectX::XMMATRIX world;

	// transposed world matrix, for passing to the shader
	DirectX::XMMATRIX worldT;


	// Animated GIF incremental frame reader.  Loading a large
	// multi-frame GIF can take a noticable amount of time.  The
	// individual frame decoding is pretty fast, on the order of
	// a few milliseconds, but this can easily add up to a 
	// perceptible delay (as much as a second or two) for a 
	// GIF with dozens of frames.  One way to mitigate this 
	// would be to do the decoding in a separate thread.  But
	// threading always adds some complexity, and in this case
	// it's not really needed.  A simpler approach that works 
	// well for our purposes is to decode the frames one at a 
	// time on demand, as we actually need to render them.  
	// GIF frames are typically played back no faster than 
	// the video refresh rate, and individual frame decoding
	// is much faster than the render cycle, so we can easily
	// fit one frame's worth of decoding into the time slice
	// we get during a render cycle without adding any extra
	// delay.  Doing it this way naturally distributes the
	// load time over the playback time in such a way that
	// it becomes invisible.  It also has the virtue of 
	// practically zero latency for the first frame, which
	// would be more difficult to accomplish with threaded
	// decoding, since we'd have to synchronize with the
	// thread at single-frame granularity to make that work.
	//
	// The naive way to write a GIF decoder is as a loop that
	// loads all of the frames.  We don't want to sit in a
	// loop, though; we basically want to do one iteration 
	// at a time instead.  So we need to take the state we'd
	// normally put into local variables controlling a loop
	// and put them into a struct.  That's what this struct
	// is about.
	struct GIFLoaderState
	{
		// initialize
		void Init(IWICImagingFactory *pWIC, IWICBitmapDecoder *decoder, 
			UINT width, UINT height, UINT nFrames, WICColor bgColor, const WCHAR *filename)
		{
			this->pWIC = pWIC;
			this->decoder = decoder;
			this->rcFull = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			this->nFrames = nFrames;
			this->bgColor = bgColor;
			this->filename = filename;

			this->frames.reserve(nFrames);
		}

		// clear - releases resources when we're done
		void Clear()
		{
			iFrame = nFrames = 0;
			filename = _T("");
			frames.clear();
			pWIC = nullptr;
			decoder = nullptr;
		}

		// WIC factory
		RefPtr<IWICImagingFactory> pWIC;

		// file decoder
		RefPtr<IWICBitmapDecoder> decoder;

		// sprite file name, for error reporting
		WSTRING filename;

		// background color
		WICColor bgColor;

		// total number of frames
		UINT nFrames = 0;

		// current frame number
		UINT iFrame = 0;

		// "Previous" frame number, for frame disposal purposes.
		// One of the disposal codes is "revert to previous"; this
		// keeps track of the frame that refers to.
		UINT prevFrame = 0;

		// Image frame history.  GIF specifies each frame
		// as a difference from a previous frame, so we 
		// need to keep a history of recent frames.  We
		// actually only need the current frame and one
		// prior frame, so we could make this more
		// efficient, but for now we'll keep all frames.
		std::vector<std::unique_ptr<DirectX::ScratchImage>> frames;

		// GIF "Disposal" code for the prior frame
		enum disposal_t {
			DM_UNDEFINED = 0,
			DM_NONE = 1,         // keep this frame, draw next frame on top of it
			DM_BACKGROUND = 2,   // clear the frame with the background color
			DM_PREVIOUS = 3      // revert to previous frame
		} disposal = DM_UNDEFINED;

		// full-frame rectangle for the overall image
		RECT rcFull;

		// sub-frame rectangle for the current frame
		RECT rcSub = { 0, 0, 0, 0 };

		// Decode the next GIF frame
		void DecodeFrame(Sprite *sprite);
	};
	GIFLoaderState gifLoaderState;
};
