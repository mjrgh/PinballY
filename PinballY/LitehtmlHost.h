// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Litehtml Host Interface
//
// The litehtml system defines a class called document_container that serves
// as the host application abstraction layer.  The host is responsible for
// implementing all interaction with the UI and I/O environment, including
// drawing graphics, receiving user input, and file access.
//
// PinballY uses litehtml purely for static HTML rendering, so we don't
// implement any of the functionality related to user interaction.  We just
// need to provide the graphics functions to draw into Gdiplus contexts, and
// the file handling related to loading images.

#pragma once

#include "../litehtml/include/litehtml.h"

class LitehtmlHost : public litehtml::document_container
{
protected:

public:
	LitehtmlHost();
	virtual ~LitehtmlHost();

	// Incorporate a litehtml context object as part of the container.  The
	// context object contains application-wide state for the browsing session;
	// at the moment, it simply contains the system default CSS source.
	litehtml::context litehtmlContext;

	// set the surface size
	void SetSurfaceSize(int width, int height)
	{
		this->width = width;
		this->height = height;
	}

	virtual void get_language(litehtml::tstring& language, litehtml::tstring & culture) const override;

	virtual void get_media_features(litehtml::media_features& media) const override;

	virtual void get_client_rect(litehtml::position& client) const
	{
		client.x = 0;
		client.y = 0;
		client.width = width;
		client.height = height;
	}

	virtual	void transform_text(litehtml::tstring& text, litehtml::text_transform tt) override;

	virtual int pt_to_px(int pt) override { return static_cast<int>(roundf(static_cast<float>(pt) * 0.0138889f * 96.0f)); }
	virtual int get_default_font_size() const override { return 10; }
	virtual const litehtml::tchar_t *get_default_font_name() const override { return _T("Arial"); }

	virtual litehtml::uint_ptr create_font(
		const litehtml::tchar_t* faceName, int size, int weight,
		litehtml::font_style italic,
		unsigned int decoration, litehtml::font_metrics* fm) override;

	virtual void delete_font(litehtml::uint_ptr hFont) override;

	virtual int text_width(const litehtml::tchar_t* text, litehtml::uint_ptr hFont) override;

	virtual void draw_text(litehtml::uint_ptr hdc, const litehtml::tchar_t* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;

	virtual void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& br, bool valid_x, bool valid_y) override;
	virtual void del_clip() override;

	virtual void load_image(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, bool redraw_on_ready) override;
	virtual void get_image_size(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, litehtml::size& sz) override;

	virtual void draw_background(litehtml::uint_ptr hdc, const litehtml::background_paint& bg) override;

	virtual void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) override;

	virtual void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker);

	//
	// The following all relate to full browser functionality that we don't support.
	// We're only interested in using the HTML engine for its layout capabilities,
	// so we don't care about mouse interaction, remote URLs, or external CSS.
	//
	virtual	void set_caption(const litehtml::tchar_t*) override { /* ignore <TITLE> */ }
	virtual void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override { /* ignore <LINK> tags */ }
	virtual	void set_base_url(const litehtml::tchar_t*) override { /* ignore <BASEURL> tags */ }
	virtual void import_css(litehtml::tstring&, const litehtml::tstring&, litehtml::tstring&) { /* external CSS not supported */ }
	virtual void on_anchor_click(const litehtml::tchar_t*, const litehtml::element::ptr&) override { /* ignore clicks */ }
	virtual	void set_cursor(const litehtml::tchar_t*) override { /* ignore mouse interaction */ }
	virtual std::shared_ptr<litehtml::element> create_element(
		const litehtml::tchar_t *, const litehtml::string_map &, const std::shared_ptr<litehtml::document> &) override
	{
		// we don't currently have any custom elements
		return nullptr;
	}

protected:
	// current rendering surface size
	int width = 16;
	int height = 16;

	// StringFormat for measuring text
	std::unique_ptr<Gdiplus::StringFormat> measuringFormat;

	// Do we have any non-zero corner radii in a border_radiuses descriptor?
	static bool IsNonZeroCornerRadii(const litehtml::border_radiuses &br)
	{
		return (br.top_left_x != 0 || br.top_left_y != 0
			|| br.top_right_x != 0 || br.top_right_y != 0
			|| br.bottom_left_x != 0 || br.bottom_left_y != 0
			|| br.bottom_right_x != 0 || br.bottom_right_y != 0);
	}

	// Do we have uniform corner radii in a border_radiuses descriptor?
	static bool IsUniformCornerRadii(const litehtml::border_radiuses &br)
	{
		return br.top_left_x == br.top_right_x
			&& br.top_right_x == br.bottom_left_x
			&& br.bottom_left_x == br.bottom_right_x
			&& br.top_left_y == br.top_right_y
			&& br.top_right_y == br.bottom_left_y
			&& br.bottom_left_y == br.bottom_right_y;
	}

	// Create a region representing a rounded rectangle with heterogeneous corner radii
	static HRGN CreateRoundRectRgnDeluxe(const litehtml::position& pos, const litehtml::border_radiuses& br);

	// clip stack
	std::list<HRGN> clipRegionStack;

	// perform an operation with a clip region
	inline void WithClip(Gdiplus::Graphics *g, std::function<void()> func)
	{
		Gdiplus::Region oldClip;
		if (clipRegionStack.size() != 0)
		{
			g->GetClip(&oldClip);
			g->SetClip(clipRegionStack.back());
		}

		func();

		if (clipRegionStack.size() != 0)
			g->SetClip(&oldClip);
	}

	// Image cache.  Litehtml does all of its image work in terms of URLs, leaving
	// it up to the host to do any desired caching.  When we're called upon to load
	// an image, we'll load it into a Gdiplus bitmap and keep a reference in a map
	// keyed by the name, so that we can reuse the bitmap for repeated drawing.
	// We'll also keep track of the time of last use, so that we can discard old
	// cache entries that haven't been used in a while if we decide we need to
	// trim memory use.
	//
	// The cache key is the absolute filename path, converted to all lower-case.
	// Windows filenames are case-insensitive, so we'll use lower-case as the
	// canonical form for keys, so that we don't needlessly create multiple entries
	// for the same file if the HTML source happens to refer to it with different
	// mixtures of case.  (It wouldn't be harmful to create multiple cache entries
	// beyond the unnecessary memory use, since a cache entry doesn't affect the
	// underlying file object.)
	struct ImageCacheEntry
	{
		ImageCacheEntry(Gdiplus::Image *image) :
			image(image),
			accessTime(GetTickCount64())
		{
			// remember the pixel size
			pixelSize = image->GetWidth() * image->GetHeight();
		}

		// Gdiplus image object.  We own the image object, so it'll be deleted
		// when the cache entry is discarded.
		std::unique_ptr<Gdiplus::Image> image;

		// pixel size
		size_t pixelSize;

		// Last access time, as a GetTickCount64() system tick count (milliseconds
		// since system startup).
		ULONGLONG accessTime;
	};
	std::map<TSTRING, ImageCacheEntry> imageCache;

	// Total pixel size of cached images.  We use this as an approximation of
	// the overall memory usage of the cache.
	size_t imageCacheTotalPixelSize = 0;

	// Generate the canonical cache key given a filename
	template<size_t keyLen> static void GetImageCacheKey(TCHAR(&key)[keyLen], const TCHAR *url, const TCHAR *baseUrl);

	// Look up an image cache entry by source URL.  Automatically updates the
	// access timestamp on the entry if found.
	ImageCacheEntry *GetImageCacheEntry(const TCHAR *url, const TCHAR *baseUrl);

	// Look up an image cache entry by key.  Automatically updates the
	// access timestamp on the entry if found.
	ImageCacheEntry *GetImageCacheEntryByKey(const TCHAR *key);

	// Load an image if it's not already cached
	ImageCacheEntry *FindOrLoadImage(const TCHAR *url, const TCHAR *baseUrl);
};


