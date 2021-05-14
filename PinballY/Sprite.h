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
#include <png.h>
#include "D3D.h"

class Camera;
class FlashClientSite;
class Shader;
struct DIBitmap;
class SWFParser;

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
	//
	// msgHwnd is a window that will receive AVPxxx messages related
	// to animation playback, if desired.  This can be null if these
	// messages aren't needed for this image site.
	bool Load(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, HWND msgHwnd, ErrorHandler &eh);

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

	// Play/Stop an image or video.  This has no effect (and is harmless)
	// for still images.
	virtual void Play(ErrorHandler&) { animRunning = true; }
	virtual void Stop(ErrorHandler&) { animRunning = false; }

	// Get/set the looping status
	virtual void SetLooping(bool f) { animLooping = f; }
	virtual bool IsLooping() const { return animLooping; }
	
	// Get my media cookie.  This returns an identifier for the loaded
	// media that's unique over the session, to identify it in event
	// callbacks.  This is a simple global serial number that's 
	// incremented for each media object that uses one.  We use this
	// rather than the object pointer because we have no way to
	// guarantee that an object pointer actually points to the same
	// object over time, since the C++ memory manager can reclaim the
	// memory used by a deleted object and use it for a new object.
	virtual DWORD GetMediaCookie() const { return animCookie; }

	// Service an AVPMsgLoopNeeded message generated from the underlying
	// media.  This message is used by audio and video players that need
	// the main thread to handle looped playback; the player generates
	// the message, sending it to the container window, and the window
	// message queue (which runs on the main UI thread) services it by
	// calling this method.  The base Sprite only deals in still images
	// and animated GIFs, which don't require this service.
	virtual void ServiceLoopNeededMessage(ErrorHandler&) { }

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

	// Load an animated PNG image file.  The regular Load(filename,...) method calls
	// this when it detects PNG contents.
	bool LoadAPNG(const WCHAR *filename, POINTF normalizedSize, SIZE pixSize, ErrorHandler &eh);

	// Load a texture from an image file using WIC.  This does a direct
	// WIC load, which handles the common image formats (JPEG, PNG, GIF),
	// but doesn't have support for orientation metadata or multi-frame
	// animated GIFs.
	bool LoadWICTexture(const WCHAR *filename, POINTF normalizedSize, ErrorHandler &eh);

	// Texture + Shader Resource View.  This pair forms the basic
	// D3D rendering object for a bitmap.
	struct TextureAndView
	{
		RefPtr<ID3D11Resource> texture;
		RefPtr<ID3D11ShaderResourceView> rv;
	};

	// create the texture and resource view from a bitmap, and load it
	// into a new loading context
	bool CreateTextureFromBitmap(const BITMAPINFO &bmi, const void *dibits, ErrorHandler &eh, const TCHAR *descForErrors);

	// create the texture and resource view from a bitmap
	bool CreateTextureFromBitmap(const BITMAPINFO &bmi, const void *dibits, ErrorHandler &eh, const TCHAR *descForErrors,
		TextureAndView *tv);

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
		TextureAndView tv;
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
		TextureAndView tv;
	};

	// animation frame list
	std::vector<std::unique_ptr<AnimFrame>> animFrames;

	// animation handler
	struct Animation
	{
		virtual ~Animation() { }
		virtual void DecodeNext(Sprite *sprite) = 0;
	};
	std::unique_ptr<Animation> animation;

	// is the animation (if any) running?
	bool animRunning = true;

	// is the animation played on a loop?
	bool animLooping = true;

	// current animation frame index
	UINT curAnimFrame = 0;

	// ending time of the current frame, in system ticks
	UINT64 curAnimFrameEndTime = 0;

	// If we have an animated image, we'll allocate a media cookie
	// for it, as though it were using a video or audio player.
	// This lets us generate AVP messages related to the animation
	// playback.
	DWORD animCookie = 0;

	// Message HWND.  This is the target window for any AVPxxx 
	// messages we generate for animated media.
	HWND msgHwnd;

	// world transform matrix
	DirectX::XMMATRIX world;

	// transposed world matrix, for passing to the shader
	DirectX::XMMATRIX worldT;

	// SWF incremental loader.
	struct SWFLoaderState : Animation
	{
		SWFLoaderState(SIZE targetPixSize);
		virtual ~SWFLoaderState();

		// Animation interface implementation
		virtual void DecodeNext(Sprite *sprite) override;

		// release resources
		void Clear() { }

		// create an animation frame from the last decoded SWF frame
		bool CreateAnimFrame(Sprite *sprite);

		// SWF file parser/renderer
		std::unique_ptr<SWFParser> parser;

		// Target pixel size.  An SWF has a native size, but that's
		// usually just advisory, because the graphics are usually
		// stored as vectors and thus scale cleanly to any target
		// size.  Since we need to rasterize the SWF contents, it's
		// far better to do the scaling at the SWF rendering level,
		// while the material is still in vector format.
		SIZE targetPixSize;
	};

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
	struct GIFLoaderState : Animation
	{
		// Animation interface implementation
		virtual ~GIFLoaderState() { Clear(); }
		virtual void DecodeNext(Sprite *sprite) override
		{
			// if we haven't reached the last frame yet, decode the next frame
			if (sprite->animFrames.size() < nFrames)
				DecodeFrame(sprite);
		}

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

	// Animated PNG incremental frame reader.  This is the PNG
	// counterpart of the GIF frame reader: it keeps track of the
	// read position in an open PNG file so that we can read one
	// frame at a time on demand.
	struct APNGLoaderState : Animation
	{
		// Animation interface implementation
		virtual ~APNGLoaderState() { EndProcessing(); }
		virtual void DecodeNext(Sprite *sprite) override;

		// Initialize.  This opens the file and scans for the animated
		// PNG marker chunk.  Returns true if we successfully identify
		// this as an animated PNG, false if not.  On a false return,
		// no errors are generated; the caller should simply fall back
		// on the generic WIC loader, on the assumption that it's a
		// conventional single-frame PNG file, an invalid PNG file, or
		// some other image type - in any of those cases, the WIC loader
		// can determine what to do with the file;
		bool Init(Sprite *sprite, const WCHAR *filename, POINTF normalizedSize, SIZE pixSize);

		// file handle
		FILEPtrHolder fp;

		// sprite file name, for error reporting
		WSTRING filename;

		// total number of frames
		UINT nFrames = 0;

		// current frame number
		UINT iFrame = 0;

		// full-frame rectangle for the overall image
		RECT rcFull;

		// sub-frame rectangle for the current frame
		RECT rcSub = { 0, 0, 0, 0 };

		// PNG chunk
		struct Chunk
		{
			Chunk() : size(0) { }
			Chunk(BYTE *data, UINT size) : data(data), size(size) { }

			std::unique_ptr<BYTE> data;
			UINT size;
			BYTE header[8];

			void Clear() { data.reset(); }
		};

		// APNG frame
		struct APNGFrame
		{
			void Init(UINT width, UINT height, UINT delayNum, UINT delayDen)
			{
				// save the properties
				this->width = width;
				this->height = height;
				this->delayNum = delayNum;
				this->delayDen = delayDen;
					
				// allocate the pixel buffer, 32 bits = 4 bytes per pixel
				UINT bytesPerRow = width * 4;
				data.reset(new BYTE[height * bytesPerRow]);

				// set up the row pointers
				rows.reset(new BYTE*[height]);
				BYTE *rowp = data.get();
				for (UINT row = 0; row < height; ++row, rowp += bytesPerRow)
					rows.get()[row] = rowp;
			}

			// take ownership of another frame's resources
			void Take(APNGFrame &src)
			{
				// take ownership of the image data
				data.reset(src.data.release());
				rows.reset(src.rows.release());

				// copy the properties
				width = src.width;
				height = src.height;
				delayNum = src.delayNum;
				delayDen = src.delayDen;
			}

			// make a copy of another frame's resources
			void Copy(const APNGFrame &src)
			{
				// copy properties and allocate memory
				Init(src.width, src.height, src.delayNum, src.delayDen);

				// Copy the image data.  Note that we DON'T copy the row
				// pointers, as they're set up properly by the memory
				// allocator.
				memcpy(data.get(), src.data.get(), width * height * 4);
			}

			std::unique_ptr<BYTE> data;
			std::unique_ptr<BYTE*> rows;
			UINT width;
			UINT height;
			UINT delayNum;          // delay numerator
			UINT delayDen;          // delay denominator
		};

		// acTL data
		struct
		{
			UINT numFrames;   // number of frames in the animation
			UINT numPlays;    // number of times to loop, where 0 = infinite
		} acTL;

		// fcTL data for the frame under construction
		struct
		{
			UINT x;
			UINT y;
			UINT width;
			UINT height;
			UINT delayNum;       // delay numerator
			UINT delayDen;       // delay denominator
			BYTE dop = DOP_BKG;  // frame disposal operation
			BYTE bop;            // frame blend operation
		} fcTL;

		// Disposal information for the outgoing frame
		struct
		{
			BYTE dop = DOP_NONE; // disposal operation
			UINT x;
			UINT y;
			UINT width;
			UINT height;
		} disposal;

		// Disposal operations
		static const BYTE DOP_NONE = 0;   // leave buffer as-is
		static const BYTE DOP_BKG = 1;    // clear background to transparent black
		static const BYTE DOP_PREV = 2;   // revert to previous frame

		// Blend operations
		static const BYTE BOP_SRC = 0;    // replace frame with source
		static const BYTE BOP_OVER = 1;   // alpha blend with OVER operator as defined in PNG spec

		// Is this an animated PNG?  We set this to true upon encountering
		// an acTL (animation control) chunk, which flags it as animated.
		// Per the spec, the acTL comes before the first image data (IDAT)
		// chunk, so we know for sure whether or not it's animated by the
		// time we reach the IDAT.
		bool isAnimated = false;

		// Raw frame buffer.  This is where we have libpng decode the current
		// frame's sub-stream as we work through the PNG file chunks making
		// up the current frame.
		APNGFrame frameRaw;

		// Current frame buffer.  When ReadThroughNextFrame() returns, this
		// contains the finished, composed current frame.
		APNGFrame frameCur;

		// Previous frame buffer.  When the disposal operation for a frame is
		// PREVIOUS, we save the pre-composed frame buffer here.
		APNGFrame framePrev;

		// Chunk IDs of interest
		static const DWORD ID_IHDR = 0x49484452;
		static const DWORD ID_acTL = 0x6163544c;
		static const DWORD ID_fcTL = 0x6663544c;
		static const DWORD ID_IDAT = 0x49444154;
		static const DWORD ID_fdAT = 0x66644154;
		static const DWORD ID_IEND = 0x49454e44;

		// pnglib callbacks
		static void PNGAPI InfoCallback(png_structp png, png_infop pInfo);
		static void PNGAPI RowCallback(png_structp png, png_bytep pRow, png_uint_32 rowNum, int pass);

		// PNG image data processing.  An APNG file is essentially a series of
		// regular PNG files appended together, but all sharing a common pair
		// of stream-bracketing chunks (IHDR..IEND), and also sharing any other
		// info chunks that appear before the first image pixel data (IDAT).
		// We read this using the regular "static" libpng reader by saving
		// and replaying the common elements for each sequential frame.  This
		// lets the static libpng believe it's decoding a complete PNG file
		// for each frame.
		bool StartProcessing();
		void ProcessChunk(BYTE *p, UINT size);
		bool EndProcessing();

		// Read the file through the next image frame.  Returns true if we
		// successfully found an image frame.
		bool ReadThroughNextFrame();

		// Compose a frame
		void ComposeFrame(BYTE **dst, const BYTE *const *src, UINT bop, UINT x, UINT y, UINT width, UINT height);

		// libpng context
		png_structp png = nullptr;

		// libpng info struct
		png_infop pInfo = nullptr;

		// IHDR chunk
		Chunk IHDR;

		// pre-IDAT info chunks
		std::list<Chunk> infoChunks;

		// do we have the IDAT frame yet?
		bool hasIDAT = false;

		// have we reached EOF?
		bool eof = false;

		// number of fcTL records we've encountered so far
		int fcTLCount = 0;

		// do we have frame data to include in the animation?
		bool frameDataAvail = false;

		// Read a chunk size and ID header, returning the ID
		DWORD ReadChunkSizeAndID(Chunk &chunk);

		// Read/skip the rest of the chunk after the size and ID header
		void ReadChunkContents(Chunk &chunk);
		void SkipChunkContents(Chunk &chunk);

		// Read a PNG chunk, returning the ID
		DWORD ReadChunk(Chunk &chunk);

		// create an animation frame and add it to the sprite's frame list
		bool CreateAnimFrame(Sprite *sprite);
	};
};
