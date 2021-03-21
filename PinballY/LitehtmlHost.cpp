// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Litehtml Host Interface
//

#include "stdafx.h"
#include "LitehtmlHost.h"

// HRGN holder, for automatically deleting regions as they go out of scope
struct HRGNHolder
{
	HRGNHolder() : hrgn(NULL) { }
	HRGNHolder(HRGN hrgn) : hrgn(hrgn) { }
	~HRGNHolder() { Clear(); }

	operator HRGN () { return hrgn; }

	void Clear() {
		if (hrgn != NULL) {
			DeleteObject(hrgn);
			hrgn = NULL;
		}
	}

	HRGN Get() const { return hrgn; }

	void Set(HRGN hrgn) {
		Clear();
		this->hrgn = hrgn;
	}

	HRGN hrgn;
};

// LitehtmlHost constructor
LitehtmlHost::LitehtmlHost()
{
	// set up a string formatter for taking text measurements - use the Generic Typographic
	// style, with the addition of including traiilng whitespace, since litehtml expects that.
	measuringFormat.reset(new Gdiplus::StringFormat(Gdiplus::StringFormat::GenericTypographic()));
	measuringFormat->SetFormatFlags(measuringFormat->GetFormatFlags()
		| Gdiplus::StringFormatFlags::StringFormatFlagsMeasureTrailingSpaces);
}

// Destructor
LitehtmlHost::~LitehtmlHost()
{
	// delete any remaining clip regions
	while (clipRegionStack.size() != 0)
	{
		DeleteObject(clipRegionStack.back());
		clipRegionStack.pop_back();
	}
}


void LitehtmlHost::get_language(litehtml::tstring& language, litehtml::tstring & culture) const
{
	TCHAR buf[LOCALE_NAME_MAX_LENGTH];
	std::basic_regex<TCHAR> pat(_T("([a-zA-Z0-9]+)-([a-zA-Z0-9]+)"));
	std::match_results<const TCHAR*> match;
	if (GetUserDefaultLocaleName(buf, countof(buf)) && std::regex_match(buf, match, pat))
	{
		language = match[1].str();
		culture = match[2].str();
	}
	else
	{
		language = _T("en");
		culture = _T("US");
	}
}

void LitehtmlHost::get_media_features(litehtml::media_features& media) const
{
	media.type = litehtml::media_type::media_type_screen;
	media.width = media.device_width = width;
	media.height = media.device_height = height;
	media.color = 8;
	media.color_index = 0;
	media.monochrome = 0;
	media.resolution = 96;
}

void LitehtmlHost::transform_text(litehtml::tstring& text, litehtml::text_transform tt) 
{
	switch (tt)
	{
	case litehtml::text_transform::text_transform_capitalize:
		if (text.length() > 0)
			CharUpperBuff(text.data(), 1);
		break;

	case litehtml::text_transform::text_transform_uppercase:
		CharUpperBuff(text.data(), static_cast<DWORD>(text.length()));
		break;

	case litehtml::text_transform::text_transform_lowercase:
		CharLowerBuff(text.data(), static_cast<DWORD>(text.length()));
		break;
	}
}

litehtml::uint_ptr LitehtmlHost::create_font(
	const litehtml::tchar_t* faceName, int size, int weight,
	litehtml::font_style italic,
	unsigned int decoration, litehtml::font_metrics* fm) 
{
	// create the font, using a null HDC to create it for a DIB
	int gpStyleBits = (weight >= 700 ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular);
	if (italic == litehtml::font_style::fontStyleItalic) gpStyleBits |= Gdiplus::FontStyleItalic;
	if ((decoration & litehtml::font_decoration_underline) != 0) gpStyleBits |= Gdiplus::FontStyleUnderline;
	if ((decoration & litehtml::font_decoration_linethrough) != 0) gpStyleBits |= Gdiplus::FontStyleStrikeout;
	auto font = CreateGPFontPixHt(faceName, size, static_cast<Gdiplus::FontStyle>(gpStyleBits), NULL);

	// get the font family information
	Gdiplus::FontFamily family;
	font->GetFamily(&family);
	auto style = font->GetStyle();
	auto pixelsPerDesignUnit = font->GetSize() / family.GetEmHeight(style);

	// Fill in the metrics
	fm->height = static_cast<int>(family.GetLineSpacing(style) * pixelsPerDesignUnit);
	fm->ascent = static_cast<int>(family.GetCellAscent(style) * pixelsPerDesignUnit);
	fm->descent = static_cast<int>(family.GetCellDescent(style) * pixelsPerDesignUnit);

	// Neither GDI nor Gdiplus provide a way to get a font's x-height, so we'll
	// use half of the em height as an approximation.  This isn't technically
	// accurate, because the x-height is properly a design feature of a font,
	// and it can in principle vary quite a bit from half the em-height.  But
	// this approximation tends to be within a few percent for most fonts, and
	// even in extreme cases it's really not possible for this to be *too* far
	// off; the x-height pretty much has to be somewhere between zero and the
	// the em-height, and I'd guess that the most extreme actual examples in
	// the wild will have x-heights between 1/3 and 2/3 of the em-height, which
	// would make our 1/2-em approximation about 50% off at worst.  Not great,
	// but for anything but huge display fonts, this will be a matter of a few
	// pixels.  Even that might be more of a concern if litehtml used the
	// x-height for many purposes, but it actually only uses it for one thing:
	// aligning "vertical-align: middle" objects on the middle of the text.
	// Using half the x-height (above the baseline) as the vertical centering
	// point is sensible, since it should generally yield the center of the
	// average black area of adjacent lower-case text.  But that's always
	// going to be subjective and context-dependent; the center of all-caps
	// text will look more like half the ascender height above the baseline.
	// So it shouldn't be noticeable in most cases that we're using an
	// approximation (or perhaps even in ANY cases, short of counting pixels
	// and comparing against the true font metrics).
	fm->x_height = static_cast<int>(font->GetSize() / 2.0f);

	// cast the Gdiplus::Font pointer to an opaque uint_ptr to pass back to litehtml
	return reinterpret_cast<litehtml::uint_ptr>(font);
}

void LitehtmlHost::delete_font(litehtml::uint_ptr hFont)
{
	// reinterpret the font handle as a Gdiplus::Font pointer, and delete the object
	delete reinterpret_cast<Gdiplus::Font*>(hFont);
}

int LitehtmlHost::text_width(const litehtml::tchar_t* text, litehtml::uint_ptr hFont)
{
	// get the font
	auto pFont = reinterpret_cast<Gdiplus::Font*>(hFont);

	// set up a Gdiplus context on a memory DC
	MemoryDC memdc;
	Gdiplus::Graphics g(memdc);

	// measure the bounding box of the string with origin 0,0
	Gdiplus::PointF origin(0.0f, 0.0f);
	Gdiplus::RectF bbox;
	g.MeasureString(text, static_cast<INT>(_tcslen(text)), pFont, origin, measuringFormat.get(), &bbox);

	// return the width of the bounding box
	return static_cast<int>(bbox.Width);
}

void LitehtmlHost::draw_text(litehtml::uint_ptr hdc, const litehtml::tchar_t* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos)
{
	// "hdc" is the Gdiplus::Graphics object pointer, passed down through litehtml as an opaque uint_ptr
	auto gp = reinterpret_cast<Gdiplus::Graphics*>(hdc);
	WithClip(gp, [this, gp, text, hFont, color, pos]()
	{
		// "hFont" is a Gdiplus::Font object pointer, passed through litehtml an opaque uint_ptr
		auto pFont = reinterpret_cast<Gdiplus::Font*>(hFont);

		// set up a brush of the specified color
		Gdiplus::SolidBrush br(Gdiplus::Color(color.alpha, color.red, color.green, color.blue));

		// draw the text
		Gdiplus::PointF origin{ static_cast<float>(pos.x), static_cast<float>(pos.y) };
		gp->DrawString(text, static_cast<INT>(_tcslen(text)), pFont, origin, this->measuringFormat.get(), &br);
	});
}


void LitehtmlHost::set_clip(const litehtml::position& pos, const litehtml::border_radiuses& br, bool /*valid_x*/, bool /*valid_y*/)
{
	// add a region with the caller's specs to the clipping region stack
	clipRegionStack.push_back(CreateRoundRectRgnDeluxe(pos, br));
}

void LitehtmlHost::del_clip()
{
	if (clipRegionStack.size() != 0)
	{
		DeleteObject(clipRegionStack.back());
		clipRegionStack.pop_back();
	}
}

void LitehtmlHost::load_image(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, bool /*redraw_on_ready*/)
{
	// Look up the cache entry.  If there's already an entry, there's nothing else
	// to do right now; the caller is simply advising us that the image has been
	// found in a parse tree, to give us a chance to initiate loading the image
	// now, to expedite its readiness for future drawing.  The design is meant to
	// allow asynchronous downloading of remote images, so that the download wait
	// time can be more efficiently overlapped with any additional parsing and
	// rendering work the caller has left to do.  That's not important for us,
	// since we only load local file; any reasonably sized local image loads so
	// quickly on a modern system that there's little benefit in doing the work
	// asynchronously.  But caching is stil worthwhile, since it's likely that
	// the same images will be used repeatedly in HTML formatting.  So we'll
	// take this opportunity to load the image into our cache if it's not there
	// already.
	FindOrLoadImage(src, baseurl);
}

void LitehtmlHost::get_image_size(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, litehtml::size& sz)
{
	ImageFileDesc desc;
	if (GetImageFileInfo(src, desc, true))
	{
		sz.width = desc.dispSize.cx;
		sz.height = desc.dispSize.cy;
	}
	else
	{
		// use a default "broken link" image
		sz.width = sz.height = 16;
	}
}

void LitehtmlHost::draw_background(litehtml::uint_ptr hdc, const litehtml::background_paint& bg)
{
	// "hdc" is the Gdiplus::Graphics object pointer, passed down through litehtml as an opaque uint_ptr
	auto gp = reinterpret_cast<Gdiplus::Graphics*>(hdc);
	WithClip(gp, [this, gp, bg]()
	{
		// start out with the caller's clip precalculated clip rectangle
		Gdiplus::Rect clipRect(bg.clip_box.x, bg.clip_box.y, bg.clip_box.width, bg.clip_box.height);

		// If this is the root element, we simply fill the whole canvas
		if (bg.is_root)
		{
			// root element - fill the entire canvas
			clipRect = { 0, 0, this->width, this->height };
		}

		// Figure the clipping area.  In the original CSS, this can be the border-box,
		// padding-box, or content-box; litehtml computes the result and just gives us
		// the rectangle bounds.  If there are no rounded corners, we can just use the
		// precomputed rectangle.  However, if the border-box is rounded, we have to
		// apply the rounding to the clip box; litehtml only tells us the border radii,
		// not the clip box radii.  Fortunately, it's not too hard to figure it out
		// the effective corner radii: we start with the border radii, and deduct the
		// inset distance from the border box to the clip box, with a minimum radius
		// of zero.
		HRGNHolder clipRgn;
		if (!bg.is_root && IsNonZeroCornerRadii(bg.border_radius))
		{
			// make the corner deductions as needed
			litehtml::border_radiuses clipBoxRad = bg.border_radius;
			if (bg.clip_box.x > bg.border_box.x)
			{
				int inset = bg.clip_box.x - bg.border_box.x;
				clipBoxRad.top_left_x = max(clipBoxRad.top_left_x - inset, 0);
				clipBoxRad.bottom_left_x = max(clipBoxRad.bottom_left_x - inset, 0);
			}
			if (bg.clip_box.y > bg.border_box.y)
			{
				int inset = bg.clip_box.y - bg.border_box.y;
				clipBoxRad.top_left_y = max(clipBoxRad.top_left_y - inset, 0);
				clipBoxRad.top_right_y = max(clipBoxRad.top_right_y - inset, 0);
			}
			if (bg.clip_box.right() < bg.border_box.right())
			{
				int inset = bg.border_box.right() - bg.clip_box.right();
				clipBoxRad.top_right_x = max(clipBoxRad.top_right_x - inset, 0);
				clipBoxRad.bottom_right_x = max(clipBoxRad.bottom_right_x - inset, 0);
			}
			if (bg.clip_box.bottom() < bg.border_box.bottom())
			{
				int inset = bg.border_box.bottom() - bg.clip_box.bottom();
				clipBoxRad.bottom_left_y = max(clipBoxRad.bottom_left_y - inset, 0);
				clipBoxRad.bottom_right_y = max(clipBoxRad.bottom_right_y - inset, 0);
			}

			// if we still have rounded corners, create a clipping region
			if (IsNonZeroCornerRadii(clipBoxRad))
				clipRgn.Set(CreateRoundRectRgnDeluxe(bg.clip_box, clipBoxRad));
		}

		// if there's a background color, fill the computed background rect or reigon with
		// the specified color
		if (bg.color.alpha != 0)
		{
			// fill the rounded-rect region if we have one, or just the rectangle
			Gdiplus::SolidBrush br(Gdiplus::Color(bg.color.alpha, bg.color.red, bg.color.green, bg.color.blue));
			if (clipRgn != nullptr)
			{
				Gdiplus::Region gpClipRgn(clipRgn);
				gp->FillRegion(&br, &gpClipRgn);
			}
			else
				gp->FillRectangle(&br, clipRect);
		}

		// If there's an image, draw the image.  Note that images and colors aren't
		// mutually exclusive; the image goes over the background fill if both are
		// specified.
		if (bg.image.length() != 0)
		{
			// find or load the image
			auto image = FindOrLoadImage(bg.image.c_str(), nullptr);
			if (image != nullptr)
			{
				// figure the starting position
				int x = bg.position_x;
				int y = bg.position_y;

				// if we're in repeat mode, back up until we're at or outside the clip edge
				if (bg.image_size.width != 0 && (bg.repeat == litehtml::background_repeat_repeat || bg.repeat == litehtml::background_repeat_repeat_x))
				{
					// back up the x coordinate
					while (x > clipRect.X)
						x -= bg.image_size.width;
				}
				if (bg.image_size.height != 0 && (bg.repeat == litehtml::background_repeat_repeat || bg.repeat == litehtml::background_repeat_repeat_y))
				{
					// back up the y coordinate
					while (y > clipRect.Y)
						y -= bg.image_size.height;
				}

				// repeat as needed
				for (int x0 = x, y0 = y; ; )
				{
					// draw the image into the current tile
					Gdiplus::Rect tileRect(x, y, bg.image_size.width, bg.image_size.height);
					gp->DrawImage(image->image.get(), tileRect);

					// if repeating horizontally, advance to the next horizontal tile
					bool doneWithRow = true;
					if (bg.image_size.width != 0 && (bg.repeat == litehtml::background_repeat_repeat || bg.repeat == litehtml::background_repeat_repeat_x))
					{
						x += bg.image_size.width;
						if (x < clipRect.GetRight())
							doneWithRow = false;
					}

					// if we're done with the current row, and we're repeating vertically, advance to the next row
					bool doneWithCol = true;
					if (doneWithRow && bg.image_size.height != 0 && (bg.repeat == litehtml::background_repeat_repeat || bg.repeat == litehtml::background_repeat_repeat_y))
					{
						x = x0;
						y += bg.image_size.height;
						if (y < clipRect.GetBottom())
							doneWithCol = false;
					}

					// if we're done with rows and columns, we're done with the repeat tiling
					if (doneWithRow && doneWithCol)
						break;
				}
			}
		}
	});
}

void LitehtmlHost::draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool /*root*/)
{
	// "hdc" is the Gdiplus::Graphics object pointer, passed down through litehtml as an opaque uint_ptr
	auto gp = reinterpret_cast<Gdiplus::Graphics*>(hdc);
	WithClip(gp, [gp, borders, draw_pos]()
	{
		// get a border width
		auto GetWidth = [](const litehtml::border &b) { return b.style != litehtml::border_style_hidden && b.style != litehtml::border_style_none ? b.width : 0; };

		// Build a path for one border:
		//
		//   x0, y0, theta0 = starting point and angle of first corner arc
		//   x1, y1,        = ending point and angle of first corner arc
		//   x2, y2         = starting point of second corner arc
		//   x3, y3         = ending point and final angle of second corner arc
		struct Path
		{
			Path(const litehtml::border &b) :
				border(b),
				pen(Gdiplus::Color(b.color.alpha, b.color.red, b.color.green, b.color.blue), static_cast<float>(b.width))
			{
				// set up the pen based on the style
				switch (b.style)
				{
				case litehtml::border_style_dashed:
					{
						static const float dashPat[] = { 1.25f, 1.0f };
						pen.SetDashStyle(Gdiplus::DashStyleDash);
						pen.SetDashPattern(dashPat, 2);
					}
					break;

				case litehtml::border_style_dotted:
					pen.SetDashStyle(Gdiplus::DashStyleDot);
					pen.SetDashCap(Gdiplus::DashCapRound);
					break;

				case litehtml::border_style_double:
				case litehtml::border_style_groove:
				case litehtml::border_style_inset:
				case litehtml::border_style_outset:
				case litehtml::border_style_ridge:
					// not implemented
					break;
				}
			}

			litehtml::border border;
			Gdiplus::GraphicsPath path;
			Gdiplus::Pen pen;
		};
		auto BuildPath = [gp](const litehtml::border &b,
			int x0, int y0, int theta0,
			int x1, int y1,
			int x2, int y2,
			int x3, int y3) -> Path*
		{
			// ignore hidden, 'none', and transparent borders
			if (b.style == litehtml::border_style_hidden
				|| b.style == litehtml::border_style_none
				|| b.color.alpha == 0)
				return nullptr;

			// create a path object
			Path *path = new Path(b);

			// Start at the halfway point along the corner - that is, 45 degrees
			// from the starting angle.  This will make the boundaries between unlike
			// borders at adjoining corners line up on the 45-degree bevel.
			theta0 += 45;

			// if the starting corner is rounded, add an arc
			if (x0 != x1 || y0 != y1)
				path->path.AddArc(min(x0, x1), min(y0, y1), abs(x0 - x1), abs(y0 - y1),
					static_cast<float>(theta0), 45.0f);

			// add the main edge
			path->path.AddLine(x1, y1, x2, y2);

			// if the ending corner is rounded, add an arc
			if (x2 != x3 || y2 != y3)
				path->path.AddArc(min(x2, x3), min(y2, y3), abs(x2 - x3), abs(y2 - y3),
					static_cast<float>(theta0 + 45), 45.0f);

			// return the path object
			return path;
		};

		// The border rectangle is the outside of the border box, whereas GDI 
		// draws paths along the centerline of a path, so we need to inset the
		// rectangle by half of the border width.
		RECT rc = {
			draw_pos.x + borders.left.width / 2 + 1,
			draw_pos.y + borders.top.width / 2 + 1,
			draw_pos.right() - borders.right.width / 2 - 1,
			draw_pos.bottom() - borders.bottom.width / 2 - 1
		};

		// Build the paths, one edge at a time, in case they have unique styles
		std::list<std::unique_ptr<Path>> paths;
		Path *path;

		if ((path = BuildPath(
			borders.top,
			rc.left, rc.top + borders.radius.top_left_y, 180,
			rc.left + borders.radius.top_left_x, rc.top,
			rc.right - borders.radius.top_right_x, rc.top,
			rc.right, rc.top + borders.radius.top_right_y)) != nullptr)
			paths.emplace_back(path);

		if ((path = BuildPath(
			borders.right,
			rc.right - borders.radius.top_right_x, rc.top, 270,
			rc.right, rc.top + borders.radius.top_right_y,
			rc.right, rc.bottom - borders.radius.bottom_right_y,
			rc.right - borders.radius.bottom_right_x, rc.bottom)) != nullptr)
			paths.emplace_back(path);

		if ((path = BuildPath(
			borders.bottom,
			rc.right, rc.bottom - borders.radius.bottom_right_y, 0,
			rc.right - borders.radius.bottom_right_x, rc.bottom,
			rc.left + borders.radius.bottom_left_x, rc.bottom,
			rc.left, rc.bottom - borders.radius.bottom_left_y)) != nullptr)
			paths.emplace_back(path);

		if ((path = BuildPath(
			borders.left,
			rc.left + borders.radius.bottom_left_x, rc.bottom, 90,
			rc.left, rc.bottom - borders.radius.bottom_left_y,
			rc.left, rc.top + borders.radius.top_left_y,
			rc.left + borders.radius.top_left_x, rc.top)) != nullptr)
			paths.emplace_back(path);

		// Combine adjacent paths that have matching styles.  This will produce
		// smoother corner joins than drawing them separately.
		auto MatchStyles = [](const litehtml::border &a, const litehtml::border &b) {
			return a.width == b.width
				&& a.style == b.style
				&& a.color.alpha == b.color.alpha
				&& a.color.red == b.color.red
				&& a.color.green == b.color.green
				&& a.color.blue == b.color.blue;
		};
		size_t initialPathCount = paths.size();
		while (paths.size() > 1)
		{
			// Look for an adjacent pair to collapse.  If we find one, collapse
			// it, and start the search over, since that could then pair with
			// the next one, and so on.
			bool collapsed = false;
			for (auto &cur = paths.begin(); cur != paths.end(); ++cur)
			{
				// get the next item; stop if we've reached the end
				auto nxt = cur;
				++nxt;
				if (nxt == paths.end())
					break;

				// check to see if we can combine these
				if (MatchStyles((*cur)->border, (*nxt)->border))
				{
					// they match - combine the two paths
					(*cur)->path.AddPath(&(*nxt)->path, TRUE);

					// we can delete the second of the pair, since it's now
					// been subsumed into the first
					paths.erase(nxt);

					// stop this pass and start the next one
					collapsed = true;
					break;
				}
			}

			// if we didn't find anything on this pass, we're done
			if (!collapsed)
				break;
		}

		// If we got it down to a single path, and we started with all four
		// separate edges, it means that we have all four borders in a single
		// style.  In this case, there's one more little finishing touch we can
		// do to make the final path more perfect.  Our single path currently
		// has its starting and ending point at the top left corner, on the
		// 45-degree bevel (since that's where we started).  Left to its own
		// devices, Gdiplus will often leave a little gap there if the border
		// width is more than a couple of pixels, because the elliptical arc
		// calculator seems to have a little imprecision at getting the final
		// path angle to exactly match what we asked for.  (Or maybe it's just
		// interpolation at the endpoint; hard to tell exactly what's going
		// on in the big black box.)  But we can tell Gdiplus that this is
		// actually a closed figure, which will make it join the path into
		// a continuous loop without the little gap.
		if (initialPathCount == 4 && paths.size() == 1)
			paths.front()->path.CloseFigure();

		// draw the paths
		for (auto &it : paths)
			gp->DrawPath(&it->pen, &it->path);
	});
}

void LitehtmlHost::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker)
{
	// "hdc" is the Gdiplus::Graphics object pointer, passed down through litehtml as an opaque uint_ptr
	auto gp = reinterpret_cast<Gdiplus::Graphics*>(hdc);
	WithClip(gp, [this, gp, marker]()
	{
		int top_margin = marker.pos.height / 3;
		if (top_margin < 4)
			top_margin = 0;

		int x = marker.pos.x;
		int y = marker.pos.y + top_margin;
		int width = marker.pos.height - top_margin * 2;
		int height = marker.pos.height - top_margin * 2;
		Gdiplus::Color color(marker.color.alpha, marker.color.red, marker.color.green, marker.color.blue);

		// if there's an image, draw that, otherwise draw the geometrical marker
		if (marker.image.length() != 0)
		{
			if (auto entry = this->GetImageCacheEntry(marker.image.c_str(), nullptr); entry != nullptr)
				gp->DrawImage(entry->image.get(), x, y, width, height);
		}
		else
		{
			switch (marker.marker_type)
			{
			case litehtml::list_style_type_circle:
				{
					Gdiplus::Pen pen(color, 1.0f);
					gp->DrawEllipse(&pen, x, y, width, height);
				}
				break;

			case litehtml::list_style_type_disc:
				{
					Gdiplus::SolidBrush br(color);
					gp->FillEllipse(&br, x, y, width, height);
				}
				break;

			case litehtml::list_style_type_square:
				{
					Gdiplus::SolidBrush br(color);
					gp->FillRectangle(&br, x, y, width, height);
				}
				break;
			}
		}
	});
}

HRGN LitehtmlHost::CreateRoundRectRgnDeluxe(const litehtml::position& pos, const litehtml::border_radiuses& br)
{
	if (IsNonZeroCornerRadii(br))
	{
		// if all corners are the same, this is easy; otherwise we have to
		// combine regions for the separate corners
		if (IsUniformCornerRadii(br))
		{
			// all corners are the same, so we just have to create a rounded rect region
			return CreateRoundRectRgn(pos.x, pos.y, pos.right(), pos.bottom(), br.top_left_x, br.top_left_y);
		}
		else
		{
			// The corners are unique, so we need to do this the hard way.  Start
			// with the plain rectangle, and remove the corners one at a time.
			HRGN rect = CreateRectRgn(pos.x, pos.y, pos.right(), pos.bottom());

			// Create an ellipse for the upper left corner radius, then subtract 
			// it from a rectangle representing just the corner part.  That will
			// leave us with the COMPLEMENT of the rounded corner - that is, the
			// part outside the corner.  We can then subtract that from the whole
			// rectangle to get the correct rounding on that corner.
			HRGNHolder ellipse(CreateEllipticRgn(pos.x, pos.y, pos.x + br.top_left_x * 2, pos.y + br.top_left_y * 2));
			HRGNHolder corner(CreateRectRgn(pos.x, pos.y, pos.x + br.top_left_x, pos.y + br.top_left_y));
			CombineRgn(corner, corner, ellipse, RGN_DIFF);
			CombineRgn(rect, rect, corner, RGN_DIFF);

			// Repeat the process for the upper right corner radius
			ellipse.Set(CreateEllipticRgn(pos.right() + 1 - br.top_right_x * 2, pos.y, pos.right() + 1, pos.y + br.top_right_y * 2));
			corner.Set(CreateRectRgn(pos.right() - br.top_right_x, pos.y, pos.right(), pos.y + br.top_right_y));
			CombineRgn(corner, corner, ellipse, RGN_DIFF);
			CombineRgn(rect, rect, corner, RGN_DIFF);

			// And again for the bottom left
			ellipse.Set(CreateEllipticRgn(pos.x, pos.bottom() + 1 - br.bottom_left_y * 2, pos.x + br.bottom_left_x * 2, pos.bottom() + 1));
			corner.Set(CreateRectRgn(pos.x, pos.bottom() - br.bottom_left_y, pos.x + br.bottom_left_x, pos.bottom()));
			CombineRgn(corner, corner, ellipse, RGN_DIFF);
			CombineRgn(rect, rect, corner, RGN_DIFF);

			// And one more time for the bottom right
			ellipse.Set(CreateEllipticRgn(pos.right() + 1 - br.bottom_right_x * 2, pos.bottom() + 1 - br.bottom_right_y * 2, pos.right() + 1, pos.bottom() + 1));
			corner.Set(CreateRectRgn(pos.right() - br.bottom_right_x, pos.bottom() - br.bottom_right_y, pos.right(), pos.bottom()));
			CombineRgn(corner, corner, ellipse, RGN_DIFF);
			CombineRgn(rect, rect, corner, RGN_DIFF);

			// return the final combined rect region
			return rect;
		}
	}
	else
	{
		// simple square region
		return CreateRectRgn(pos.x, pos.y, pos.right(), pos.bottom());
	}
}

// Generate the canonical cache key given a filename
template<size_t keyLen> void LitehtmlHost::GetImageCacheKey(TCHAR(&key)[keyLen], const TCHAR *url, const TCHAR * /*baseUrl*/)
{
	// generate the hash key: a lower-case version of the full absolute path
	GetFullPathName(url, keyLen, key, nullptr);
	std::transform(key, key + keyLen, key, ::_totlower);

	// convert slashes to backslashes
	for (TCHAR *p = key; *p != 0; ++p)
	{
		if (*p == '/') *p = '\\';
	}
}

// Look up an image cache entry by source URL.  Automatically updates the
// access timestamp on the entry if found.
LitehtmlHost::ImageCacheEntry *LitehtmlHost::GetImageCacheEntry(const TCHAR *url, const TCHAR *baseUrl)
{
	// generate the key
	TCHAR key[MAX_PATH];
	GetImageCacheKey(key, url, baseUrl);

	// look it up by key
	return GetImageCacheEntryByKey(key);
}

// Look up an image cache entry by key.  Automatically updates the
// access timestamp on the entry if found.
LitehtmlHost::ImageCacheEntry *LitehtmlHost::GetImageCacheEntryByKey(const TCHAR *key)
{
	// look up the entry
	if (auto &it = imageCache.find(key); it != imageCache.end())
	{
		// update the access time to "now"
		it->second.accessTime = GetTickCount64();

		// return the cache object
		return &it->second;
	}
	else
	{
		// not found
		return nullptr;
	}
}

// Load an image if it's not already cached
LitehtmlHost::ImageCacheEntry *LitehtmlHost::FindOrLoadImage(const TCHAR *url, const TCHAR *baseUrl)
{
	// Generate the cache key
	TCHAR key[MAX_PATH];
	GetImageCacheKey(key, url, baseUrl);

	// look up the entry - if it already exists, simply return it
	auto entry = GetImageCacheEntryByKey(key);
	if (entry != nullptr)
		return entry;

	// The image isn't there yet - load it, using the canonical filename
	auto image = Gdiplus::Image::FromFile(key);

	// if we successfully loaded an image, add a cache entry for it
	if (image != nullptr)
	{
		// add the map entry
		auto it = imageCache.emplace(std::piecewise_construct,
			std::make_tuple(key),
			std::make_tuple(image));

		// retrieve the new cache entry
		entry = &it.first->second;

		// count it against the total cache size
		imageCacheTotalPixelSize += entry->pixelSize;

		// If the image cache is over a threshold, trim old images that
		// haven't been accessed in a while
		const size_t sizeThreshold = 0;
		const size_t ageThreshold = 10000;
		if (imageCacheTotalPixelSize > sizeThreshold)
		{
			// scan for old images until we remove enough to get below the threshold
			size_t removedSoFar = 0;
			std::list<const TCHAR*> removalList;
			ULONGLONG oldestAccessTime = GetTickCount64() - ageThreshold;
			for (auto &it : imageCache)
			{
				// if this hasn't been accessed since the time cutoff, mark it for deletion
				if (it.second.accessTime < oldestAccessTime)
				{
					// add the key to the removal list
					removalList.emplace_back(it.first.c_str());

					// count it
					removedSoFar += it.second.pixelSize;

					// if we've reached the quota, stop scanning
					if (imageCacheTotalPixelSize - removedSoFar <= sizeThreshold)
						break;
				}
			}

			// remove the items marked for deletion
			for (auto &it : removalList)
				imageCache.erase(it);
		}
	}

	// return the entry, if we managed to create one
	return entry;
}
