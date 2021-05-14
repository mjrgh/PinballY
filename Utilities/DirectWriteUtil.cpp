// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <stdarg.h>
#include <gdiplus.h>
#include <ObjIdl.h>
#include <dwrite.h>
#include <d2d1.h>
#include <wincodec.h>
#include "GraphicsUtil.h"
#include "DirectWriteUtil.h"
#include "ComUtil.h"
#include "StringUtil.h"
#include "WinUtil.h"
#include "LogError.h"


#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "D2d1.lib")

// ------------------------------------------------------------------------
//
// Inline object for image display
//
class ImageObject : public DirectWriteUtils::InlineObject
{
public:
	ImageObject(const WCHAR *filename, size_t filenameLen, 
		float layoutWidth, float layoutHeight, 
		DirectWriteUtils::ImageVAlign align, float ascent, float descent,
		ErrorHandler &eh) :
		layoutWidth(layoutWidth),
		layoutHeight(layoutHeight),
		align(align), ascent(ascent), descent(descent)
	{
		// store the filename
		this->filename.assign(filename, filenameLen);

		// proceed only if we have a WIC factory to work with
		if (auto wic = DirectWriteUtils::Get()->GetWICFactory(); wic != nullptr)
		{
			HRESULT hr;
			auto HRError = [&hr, &eh](const TCHAR *where) {
				eh.SysError(_T("Error loading image file in StyledText"), MsgFmt(_T("%s failed, HRESULT=%lx"), where, hr));
			};

			// create a WIC decoder to load the image file
			RefPtr<IWICBitmapDecoder> decoder;
			if (!SUCCEEDED(hr = wic->CreateDecoderFromFilename(
				this->filename.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
				{ HRError(_T("Creating WIC decoder")); return; }

			// decode the first frame
			RefPtr<IWICBitmapFrameDecode> frameDecode;
			if (!SUCCEEDED(hr = decoder->GetFrame(0, &frameDecode)))
				{ HRError(_T("Decoding first frame")); return; }

			// set up the format converter to 32bppPBGRA
			if (!SUCCEEDED(hr = wic->CreateFormatConverter(&wicConverter)))
				{ HRError(_T("Creating format converter")); return; }
			if (!SUCCEEDED(hr = wicConverter->Initialize(frameDecode, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0f, WICBitmapPaletteTypeMedianCut)))
				{ HRError(_T("Initializing format converter")); return; }

			// get the image size
			wicConverter->GetSize(&imageWidth, &imageHeight);

			// if the layout size wasn't specified, or has a free dimension, figure
			// the layout size from the natural size
			if (layoutWidth < 0.0f && layoutWidth < 0.0f)
			{
				// no layout dimensions specified - use the natural size
				this->layoutWidth = static_cast<float>(imageWidth);
				this->layoutHeight = static_cast<float>(imageHeight);
			}
			else if (layoutWidth < 0.0f)
			{
				// height is specified; figure the width to maintain the aspect ratio
				this->layoutWidth = static_cast<float>(imageWidth) / static_cast<float>(imageHeight) * layoutHeight;
			}
			else if (layoutHeight < 0.0f)
			{
				// width is specified; figure the height to maintain the aspect ratio
				this->layoutHeight = static_cast<float>(imageHeight) / static_cast<float>(imageWidth) * layoutWidth;
			}

			// if the font ascent and descent are unknown, use defaults
			if (ascent == 0.0f && descent == 0.0f)
				this->ascent = 16.0f;
		}
	}

	HRESULT STDMETHODCALLTYPE Draw(
		__maybenull void* clientDrawingContext,
		IDWriteTextRenderer* renderer,
		FLOAT originX,
		FLOAT originY,
		BOOL isSideways,
		BOOL isRightToLeft,
		IUnknown* clientDrawingEffect)
	{
		// make sure the render target and WIC converter are valid
		if (renderTarget == nullptr || wicConverter == nullptr)
			return E_POINTER;

		// create the device-dependent ID2D1 bitmap from the device-independent WIC bitmap
		HRESULT hr;
		RefPtr<ID2D1Bitmap> bitmap;
		D2D1_RECT_F rcSrc{ 0.0f, 0.0f, static_cast<float>(imageWidth), static_cast<float>(imageHeight) };
		D2D1_RECT_F rcDst{ originX, originY, originX + layoutWidth, originY + layoutHeight };
		if (!SUCCEEDED(hr = renderTarget->CreateBitmapFromWicBitmap(wicConverter, &bitmap)))
			return hr;

		// render the bitmap
		renderTarget->DrawBitmap(bitmap, rcDst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, rcSrc);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetMetrics(__out DWRITE_INLINE_OBJECT_METRICS* metrics)
	{
		metrics->width = layoutWidth;
		metrics->height = layoutHeight;
		metrics->supportsSideways = FALSE;

		// metrics->baseline is the distance from the top of the
		// object to the text baseline, with positive numbers
		// placing the object above the baseline.  The zero
		// point in this coordinate system is the text baseline.
		switch (align)
		{
		case DirectWriteUtils::ImageVAlign::Top:
			// top = text height above baseline
			metrics->baseline = ascent;
			break;

		case DirectWriteUtils::ImageVAlign::Center:
			// top = (text center point) + (image height)/2
			//     = (text bottom + text top)/2 + (image height)/2
			//     = (0 - baseline offset + text height + image height)/2
			metrics->baseline = (-descent + ascent + layoutHeight) / 2.0f;
			break;

		case DirectWriteUtils::ImageVAlign::Bottom:
			// top = text bottom + image height = 0 - baseline offset + image hieght
			metrics->baseline = -descent + layoutHeight;
			break;

		case DirectWriteUtils::ImageVAlign::Baseline:
			// top = 0 + image height
			metrics->baseline = layoutHeight;
			break;
		}

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetOverhangMetrics(__out DWRITE_OVERHANG_METRICS* overhangs)
	{
		overhangs->left = 0;
		overhangs->top = 0;
		overhangs->right = 0;
		overhangs->bottom = 0;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetBreakConditions(
		__out DWRITE_BREAK_CONDITION* breakConditionBefore,
		__out DWRITE_BREAK_CONDITION* breakConditionAfter)
	{
		// defer to adjacent elements to decide on line breaks
		*breakConditionBefore = DWRITE_BREAK_CONDITION_NEUTRAL;
		*breakConditionAfter = DWRITE_BREAK_CONDITION_NEUTRAL;
		return S_OK;
	}

protected:
	// WIC resources.  We have WIC load the image in device-independent
	// format at load time, then create the D2D device-dependent bitmap
	// at render time.
	RefPtr<IWICFormatConverter> wicConverter;

	// image filename
	WSTRING filename;

	// Layout width and height.  Zero means automatic: if both are
	// zero, we use the natural pixel size of the image; if one is
	// zero and the other isn't, the auto dimension is chosen to
	// maintain the natural aspect ratio based on the other fixed
	// dimension.
	float layoutWidth, layoutHeight;

	// Native image size
	UINT imageWidth, imageHeight;

	// vertical alignment relative to adjacent text
	DirectWriteUtils::ImageVAlign align;

	// font metrics, if known; all are zeroed if unknown
	float ascent, descent;
};


// ------------------------------------------------------------------------
//
// Non-breaking space.  This is an inline object that looks visually like
// a space, but prevents line breaks on both sides.
//

class NonBreakingSpace : public DirectWriteUtils::InlineObject
{
public:
	enum ImageVAlign {
		Top,
		Center,
		Bottom,
		Baseline
	};

	NonBreakingSpace(float width) : width(width)
	{
	}

	HRESULT STDMETHODCALLTYPE Draw(
		__maybenull void* clientDrawingContext,
		IDWriteTextRenderer* renderer,
		FLOAT originX,
		FLOAT originY,
		BOOL isSideways,
		BOOL isRightToLeft,
		IUnknown* clientDrawingEffect)
	{
		// there's nothing to draw for a space; our only job is to
		// consume horizontal space as reflected in our metrics
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetMetrics(__out DWRITE_INLINE_OBJECT_METRICS* metrics)
	{
		// consume our specified width, and some nominal height; our height
		// shouldn't matter, but just to avoid any edge cases in the renderer,
		// use a non-zero height so that our bounding box isn't empty
		metrics->width = width;
		metrics->height = 1.0f;
		metrics->supportsSideways = TRUE;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetOverhangMetrics(__out DWRITE_OVERHANG_METRICS* overhangs)
	{
		overhangs->left = 0.0f;
		overhangs->top = 0;
		overhangs->right = 0.0f;
		overhangs->bottom = 0;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetBreakConditions(
		__out DWRITE_BREAK_CONDITION* breakConditionBefore,
		__out DWRITE_BREAK_CONDITION* breakConditionAfter)
	{
		// our whole point is to not allow breaking on either side
		*breakConditionBefore = DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
		*breakConditionAfter = DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
		return S_OK;
	}

protected:
	// space width
	float width;
};


// ------------------------------------------------------------------------
//
// Direct Write Utilities
//

// singleton instance
DirectWriteUtils *DirectWriteUtils::inst = nullptr;

void DirectWriteUtils::Init(ErrorHandler &eh)
{
	if (inst == nullptr)
		inst = new DirectWriteUtils(eh);
}

void DirectWriteUtils::Terminate()
{
	delete inst;
	inst = nullptr;
}


DirectWriteUtils::StyledText::SpanStyle::FontMetrics DirectWriteUtils::StyledText::SpanStyle::GetFontMetrics() const
{
	RefPtr<IDWriteFontCollection> fontColl;
	RefPtr<IDWriteFontFamily> family;
	RefPtr<IDWriteFont> font;
	UINT32 fontIndex;
	BOOL exists;
	FontMetrics result;
	auto factory = DirectWriteUtils::Get()->GetDWriteFactory();
	if (factory != nullptr
		&& SUCCEEDED(factory->GetSystemFontCollection(&fontColl, FALSE))
		&& SUCCEEDED(fontColl->FindFamilyName(face.c_str(), &fontIndex, &exists))
		&& SUCCEEDED(fontColl->GetFontFamily(fontIndex, &family))
		&& SUCCEEDED(family->GetFirstMatchingFont(weight, stretch, style, &font)))
	{
		DWRITE_FONT_METRICS fm;
		font->GetMetrics(&fm);
		float conv = size / fm.designUnitsPerEm;
		result.ascent = fm.ascent * conv;
		result.descent = fm.descent * conv;
	}
	else
	{
		result.ascent = 0.0f;
		result.descent = 0.0f;
	}

	// return the results
	return result;
}


HRESULT DirectWriteUtils::StyledText::SpanStyle::MeasureText(
	DWRITE_TEXT_METRICS &tm, const WCHAR *str, size_t len, float maxWidth, float maxHeight) const
{
	// get the factory
	auto factory = DirectWriteUtils::Get()->GetDWriteFactory();
	auto locale = DirectWriteUtils::Get()->locale;
	if (factory == nullptr)
		return E_FAIL;

	// create a text format
	HRESULT hr;
	RefPtr<IDWriteTextFormat> format;
	if (!SUCCEEDED(hr = factory->CreateTextFormat(face.c_str(), nullptr, weight, style, stretch, size, locale, &format)))
		return hr;

	// create a text layout
	RefPtr<IDWriteTextLayout> layout;
	if (!SUCCEEDED(hr = factory->CreateTextLayout(str, static_cast<UINT32>(len), format, maxWidth, maxHeight, &layout)))
		return hr;

	// measure the layout
	return layout->GetMetrics(&tm);
};

void DirectWriteUtils::StyledText::AddSpan(const TCHAR *txt, size_t len, const SpanStyle &style)
{
	if (len == 0)
		return;

	// set the starting position to the current end of text
	DWRITE_TEXT_RANGE range = {
		static_cast<UINT32>(plainText.length()),
		static_cast<UINT32>(len)
	};

	// append the new text
	plainText.append(txt, len);

	// add the style range entry
	spans.emplace_back(style, nullptr, range);

	// Invalidate the layout, if one exists.  The layout contains an immutable
	// copy of the plain text, so we have to re-create the layout each time
	// we add new text.  There's no need to create a new layout immediately;
	// any code that needs it will create it on demand.  It's more efficient
	// to wait to create the layout until we actually need it, since the
	// caller might be in the process of adding a batch of new text.
	layout = nullptr;
}

void DirectWriteUtils::StyledText::AddNBSP(const SpanStyle &style)
{
	// Add a U+00A0 character to the plaintext.  This isn't actually used or
	// displayed by the IDWriteLayout; it's just a dummy character so that the
	// text range is non-empty.  IDWriteLayout requires a non-empty range for
	// every inline object, just as a placeholder.  The choice of character
	// for the placeholder doesn't matter to DWwrite, so we use U+00A0 (the
	// standard Unicode non-breaking space character, or "&nbsp;" in HTML)
	// for the sake of anyone looking at the plain text in the debugger.
	DWRITE_TEXT_RANGE range = { static_cast<UINT32>(plainText.length()), 1 };
	plainText.append(1, 0x00A0);

	// figure the size of a space in the specified font
	DWRITE_TEXT_METRICS tm;
	style.MeasureText(tm, L" ", 1, 1000.0f, 1000.0f);

	// create the image NBSP object
	RefPtr<NonBreakingSpace> nbsp(new NonBreakingSpace(tm.widthIncludingTrailingWhitespace));

	// add the span
	spans.emplace_back(style, nbsp, range);
}

void DirectWriteUtils::StyledText::AddImage(
	const TCHAR *filename, const SpanStyle &style,
	ImageVAlign valign, float layoutWidth, float layoutHeight,
	ErrorHandler &eh)
{
	// Add the plain text "<IMG>" to serve as the placeholder text that
	// IDWriteLayout requires.  The choice of text doesn't matter to
	// DWrite, so we use something that might at least be recognizable
	// if someone happens to look at the text stream in the debugger.
	DWRITE_TEXT_RANGE range = { static_cast<UINT32>(plainText.length()), 5 };
	plainText.append(L"<IMG>");

	// create the image object
	auto fm = style.GetFontMetrics();
	RefPtr<ImageObject> image(new ImageObject(filename, _tcslen(filename), layoutWidth, layoutHeight, valign, fm.ascent, fm.descent, eh));
}

HRESULT DirectWriteUtils::StyledText::Span::CreateTextFormat()
{
	if (format != nullptr)
		return S_OK;

	auto factory = DirectWriteUtils::Get()->GetDWriteFactory();
	auto locale = DirectWriteUtils::Get()->locale;
	if (factory == nullptr)
		return E_FAIL;

	return factory->CreateTextFormat(
		style.face.c_str(), nullptr, style.weight, style.style,
		style.stretch, style.size, locale, &format);
}

void DirectWriteUtils::StyledText::CreateTextLayout(ErrorHandler &eh)
{
	// if there's already a layout object, just keep it
	if (layout != nullptr)
		return;

	// we only need to proceed if we have text to format
	if (plainText.length() == 0)
		return;

	// get the DirectWrite factory
	auto dw = DirectWriteUtils::Get();
	auto dwFactory = dw->GetDWriteFactory();

	// create the Text Format for the initial run's format
	SpanStyle defaultStyle;
	RefPtr<IDWriteTextFormat> format;
	HRESULT hr;
	auto &s0 = spans.size() != 0 ? spans.front().style : defaultStyle;
	if (!SUCCEEDED(hr = dwFactory->CreateTextFormat(s0.face.c_str(), NULL,
		s0.weight, s0.style, s0.stretch, s0.size, dw->locale, &format)))
	{
		eh.SysError(_T("Error creating styled text layout"), MsgFmt(_T("CreateTextFormat, HRESULT=%lx"), hr));
		return;
	}

	// Create the layout with the format for the first range.  Note that
	// there's no need to separately set the font style information that's
	// part of the Text Format, since that initially applies to the entire
	// text range,  including the first run.
	if (!SUCCEEDED(hr = dwFactory->CreateTextLayout(
		plainText.c_str(), static_cast<UINT32>(plainText.length()),
		format, 1000.0f, 1000.0f, &layout)))
	{
		eh.SysError(_T("Error creating styled text layout"), MsgFmt(_T("CreateTextLayout, HRESULT=%lx"), hr));
		return;
	}

	// set the horizontal alignment
	layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	layout->SetTextAlignment(hAlign);

	// Add each text span.  The baseline for the whole layout is s0, so
	// we only need to add the styles that differ from s0.
	for (auto &s : spans)
	{
		// apply format changes relative to s0
		auto l = layout.Get();
		if (s.style.face != s0.face)
			l->SetFontFamilyName(s.style.face.c_str(), s.range);
		if (s.style.size != s0.size)
			l->SetFontSize(s.style.size, s.range);
		if (s.style.style != s0.style)
			l->SetFontStyle(s.style.style, s.range);
		if (s.style.weight != s0.weight)
			l->SetFontWeight(s.style.weight, s.range);
		if (s.style.stretch != s0.stretch)
			l->SetFontStretch(s.style.stretch, s.range);

		// every section needs underline/strikethrough set, since those
		// aren't stored in the base format
		if (s.style.underline)
			l->SetUnderline(TRUE, s.range);
		if (s.style.strikethrough)
			l->SetStrikethrough(TRUE, s.range);

		// if this color run has a background color, create a formatter for
		// use in the drawing routine, for measuring the text overhang of 
		// each box
		if ((s.style.bgColor & 0xFF000000) != 0)
			s.CreateTextFormat();
	}
}


void DirectWriteUtils::RenderStyledText(Gdiplus::Graphics &g, StyledText *txt,
	const Gdiplus::RectF &rcLayout, const Gdiplus::RectF &rcClip, ErrorHandler &eh)
{
	// HRESULT error handler - log it and return
	HRESULT hr = S_OK;
	auto HRError = [&hr, &eh](const TCHAR *where) -> void {
		eh.SysError(_T("DirectWrite error drawing formatted text"), MsgFmt(_T("%s, HRESULT=%lx"), where, hr));
	};

	// get the GDI+ DC
	struct GPHDC
	{
		GPHDC(Gdiplus::Graphics &g) : g(g)
		{
			g.Flush();
			hdc = g.GetHDC();
		}

		~GPHDC() { ReleaseDC(); }

		operator HDC () { return hdc; }

		void ReleaseDC()
		{
			if (hdc != NULL)
			{
				g.ReleaseHDC(hdc);
				hdc = NULL;
			}
		}

		Gdiplus::Graphics &g;
		HDC hdc;
	};
	GPHDC gphdc(g);

	// create a DC render target
	RefPtr<ID2D1DCRenderTarget> target;
	D2D1_RENDER_TARGET_PROPERTIES targetProps = {
		D2D1_RENDER_TARGET_TYPE_DEFAULT, { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED },
		0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
	};
	if (!SUCCEEDED(hr = d2dFactory->CreateDCRenderTarget(&targetProps, &target)))
		return HRError(_T("CreateDCRenderTarget"));

	// Bind the DC to the render target
	// graphics area, to allow for text that overhangs the layout rectangle.
	RECT rcBind{ static_cast<LONG>(rcClip.X), static_cast<LONG>(rcClip.Y), static_cast<LONG>(rcClip.GetRight()), static_cast<LONG>(rcClip.GetBottom()) };
	if (!SUCCEEDED(target->BindDC(gphdc, &rcBind)))
		return HRError(_T("BindDC"));

	// create the default brush
	RefPtr<ID2D1SolidColorBrush> br;
	if (!SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Yellow, 1.0f), &br)))
		return HRError(_T("Create default brush"));

	// open the drawing in the render target
	target->BeginDraw();

	// create the text layout if we haven't already done so
	txt->CreateTextLayout(eh);
	if (txt->layout != nullptr)
	{
		// set the layout to the available width and height
		txt->layout->SetMaxWidth(rcLayout.Width - txt->padding.left - txt->padding.right);
		txt->layout->SetMaxHeight(rcLayout.Height - txt->padding.top - txt->padding.bottom);

		// measure the div's layout area
		DWRITE_TEXT_METRICS tm;
		txt->layout->GetMetrics(&tm);

		// if there's a non-transparent background, fill it
		if ((txt->bgColor & 0xFF000000) != 0)
		{
			// get the layout content area relative to our padding box
			D2D1_ROUNDED_RECT rc{
				{ 
					rcLayout.X + tm.left,
					rcLayout.Y + tm.top,
					rcLayout.X + txt->padding.left + tm.left + tm.width + txt->padding.right,
					rcLayout.Y + txt->padding.top + tm.top + tm.height + txt->padding.bottom
				}, 
				txt->cornerRadius, 
				txt->cornerRadius
			};

			// fill it with the background color
			RefPtr<ID2D1SolidColorBrush> bgbr;
			if (SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(txt->bgColor), &bgbr)))
				target->FillRoundedRectangle(rc, bgbr);
		}

		// figure the text origin within the layout space
		D2D1_POINT_2F origin{ rcLayout.X + txt->padding.left, rcLayout.Y + txt->padding.top };

		// set up the renderer in all of the images
		for (auto &span : txt->spans)
		{
			if (span.inlineObject != nullptr)
				span.inlineObject->SetRenderTarget(target);
		}

		// Before drawing, we need to create the text color brushes for
		// all of the runs in the layout, and fill the background for any
		// runs with non-transparent backgrounds.  Text brush creation has
		// to be deferred until drawing because brushes are device-specific
		// resources.
		for (auto &span : txt->spans)
		{
			// set the drawing effect to a solid color brush for the text color
			RefPtr<ID2D1SolidColorBrush> txtbr;
			if (SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(span.style.textColor), &txtbr)))
				txt->layout->SetDrawingEffect(txtbr.Get(), span.range);

			// If there's a non-transparent background color, we'll need to draw 
			// the background fill before drawing the text, so that the text
			// appears on top of the fill.
			if ((span.style.bgColor & 0xFF000000) != 0)
			{
				// get the metrics for the range covered by the color run
				DWRITE_HIT_TEST_METRICS hit[50];
				UINT32 nHit;
				if (SUCCEEDED(txt->layout->HitTestTextRange(span.range.startPosition, span.range.length, origin.x, origin.y, hit, countof(hit), &nHit)))
				{
					// process each layout box included in the range
					for (UINT i = 0; i < nHit; ++i)
					{
						// get this item
						auto &h = hit[i];

						// If the color run has a Text Format available, measure the "overhang"
						// of the box.  The overhang is the amount of extra space around the
						// nominal layout box where "ink" from the glyphs is drawn.  The hit
						// test metrics only tell us the bounds of the area used by the
						// "positioning" boxes of the glyphs, but glyphs in some fonts can
						// draw outside of the positioning box; the extra space consumed is
						// the overhang.  This is common with italic and oblique fonts, where
						// the slanted edge tends to overhang the positioning box horizontally
						// on the side in the direction of the slant.  It's also common with
						// decorative fonts with exaggerated serifs and swashes that can draw
						// outside of the positioning box.
						DWRITE_OVERHANG_METRICS om = { 0.0f, 0.0f, 0.0f, 0.0f };
						if (span.format != nullptr)
						{
							// set up a layout for the text range
							RefPtr<IDWriteTextLayout> boxLayout;
							if (!SUCCEEDED(dwFactory->CreateTextLayout(txt->plainText.c_str() + h.textPosition, h.length, span.format, h.width, h.height, &boxLayout))
								|| !SUCCEEDED(boxLayout->GetOverhangMetrics(&om)))
								om = { 0.0f, 0.0f, 0.0f, 0.0f };
						}

						// Set up a rect with the positioning box plus the overhang.
						//
						// Note that we need to add a pixel at the right and bottom to fill the
						// background seamlessly when two text runs are juxtaposed, since FillRect
						// only fills inside the rect at those edges.  This is independent of the
						// overhang; it's just a difference between the way FillRect treats the
						// rect edges compared to the DWrite text metrics.
						D2D1_RECT_F rc{
							h.left - fmaxf(om.left, 0.0f) - rcClip.X,
							h.top - fmaxf(om.top, 0.0f) - rcClip.Y,
							h.left + h.width + fmaxf(om.right, 0.0f) + 1.0f - rcClip.X,
							h.top + h.height + fmaxf(om.bottom, 0.0f) + 1.0f - rcClip.Y
						};

						// if there's a non-transparent background color, fill it
						if ((span.style.bgColor & 0xFF000000) != 0)
						{
							// create the brush and fill the box
							RefPtr<ID2D1SolidColorBrush> bgbr;
							if (SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(span.style.bgColor), &bgbr)))
								target->FillRectangle(rc, bgbr);
						}
					}
				}
			}
		}

		// draw the vbox's layout
		D2D1_POINT_2F drawOrigin{ origin.x - rcClip.X, origin.y - rcClip.Y };
		target->DrawTextLayout(drawOrigin, txt->layout, br, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

		// remove the render target references from the images
		for (auto &span : txt->spans)
		{
			if (span.inlineObject != nullptr)
				span.inlineObject->SetRenderTarget(nullptr);
		}
	}

	// close drawing in the render target
	if (!SUCCEEDED(target->EndDraw()))
		return HRError(_T("EndDraw"));
}

// measure the bounding box of styled text
bool DirectWriteUtils::MeasureStyledText(Gdiplus::RectF &rcPositioning, Gdiplus::RectF &rcInk, StyledText *txt, const Gdiplus::RectF &rc, ErrorHandler &eh)
{
	// start with empty result boxes
	rcPositioning = { rc.X, rc.Y, 0.0f, 0.0f };
	rcInk = { rc.X, rc.Y, 0.0f, 0.0f };
	float y = rc.Y;

	// create the text layout if it doesn't already exist
	txt->CreateTextLayout(eh);
	if (txt->layout != nullptr)
	{
		// set the layout to the available width and height
		txt->layout->SetMaxWidth(rc.Width - txt->padding.left - txt->padding.right);
		txt->layout->SetMaxHeight(rc.Height - txt->padding.top - txt->padding.bottom);

		// get the bounding box
		DWRITE_TEXT_METRICS tm;
		if (!SUCCEEDED(txt->layout->GetMetrics(&tm)))
			return false;

		// Get the overhang outside the calculated metrics box.  The overhang in
		// the current metrics reflect the overhang relative to the caller's
		// layout box, not relative to the metrics box.  So to find the overhang
		// relative to the metrics box, we have to do a second pass where we
		// specify the metrics box as the layout box.
		DWRITE_OVERHANG_METRICS om;
		txt->layout->SetMaxWidth(tm.width);
		txt->layout->SetMaxHeight(tm.height);
		if (!SUCCEEDED(txt->layout->GetOverhangMetrics(&om)))
			return false;

		// Figure the positioning box, relative to the layout rc.  The layout
		// from the caller's perspective includes padding, so add that back in.
		Gdiplus::RectF rcDivPos{
			rc.X + tm.left,
			rc.Y + tm.top,
			tm.width + txt->padding.left + txt->padding.right,
			tm.height + txt->padding.top + txt->padding.bottom
		};

		// Figure the ink box, by adjusting the positioning box by the overhang.
		// So far we're just including the overhang for the glyphs, so this
		// doesn't include any padding.
		Gdiplus::RectF rcDivInk{
			rc.X + tm.left - om.left,
			rc.Y + tm.top - om.top,
			tm.width + om.left + om.right,
			tm.height + om.top + om.bottom
		};

		// If we're drawing a non-transparent background fill color, the fill
		// counts as part of the ink.  The fill rect is the same as the basic
		// positioning rect rcDivPos.
		if ((txt->bgColor & 0xFF000000) != 0)
			Gdiplus::RectF::Union(rcDivInk, rcDivInk, rcDivPos);

		// figure the origin within the layout space
		D2D1_POINT_2F origin{ rc.X + txt->padding.left, y + txt->padding.top };

		// extend the ink boxes to include background fill
		for (auto &span : txt->spans)
		{
			// If there's a non-transparent background color, include the
			// background fill area in the ink box calculation
			if ((span.style.bgColor & 0xFF000000) != 0)
			{
				// get the metrics for the range covered by the color run
				DWRITE_HIT_TEST_METRICS hit[50];
				UINT32 nHit;
				if (SUCCEEDED(txt->layout->HitTestTextRange(span.range.startPosition, span.range.length, origin.x, origin.y, hit, countof(hit), &nHit)))
				{
					// process each layout box included in the range
					for (UINT i = 0; i < nHit; ++i)
					{
						// get this item
						auto &h = hit[i];

						// measure the overhang
						DWRITE_OVERHANG_METRICS om;
						if (span.format != nullptr)
						{
							// set up a layout for the text range
							RefPtr<IDWriteTextLayout> boxLayout;
							if (!SUCCEEDED(dwFactory->CreateTextLayout(txt->plainText.c_str() + h.textPosition, h.length, span.format, h.width, h.height, &boxLayout))
								|| !SUCCEEDED(boxLayout->GetOverhangMetrics(&om)))
								om = { 0.0f, 0.0f, 0.0f, 0.0f };
						}

						// Set up a rect for the fill area, including overhang.  Ignore
						// underhang (negative overhang values), as we're going to fill
						// at least out to the edges of the layout box.  Underhang just
						// means that the glyphs don't have pixels out to the very edges
						// of the layout box.
						Gdiplus::RectF rcFill{
							h.left - fmaxf(om.left, 0.0f),
							h.top - fmaxf(om.top, 0.0f),
							h.width + fmaxf(om.right, 0.0f) + 1.0f,
							h.height + fmaxf(om.bottom, 0.0f) + 1.0f
						};

						// Union this into the ink box
						Gdiplus::RectF::Union(rcDivInk, rcDivInk, rcFill);
					}
				}
			}
		}

		// set the return boxes
		rcPositioning = rcDivPos;
		rcInk = rcDivInk;
	}

	return true;
}

void DirectWriteUtils::Draw(Gdiplus::Graphics &g, const TCHAR *txt, Gdiplus::Font *font, Gdiplus::RectF &rc, ErrorHandler &eh)
{
	// if DWrite setup failed, fall back on plain text formatting
	if (dwFactory == nullptr)
	{
		// set up a string draw context on the desired target area
		GPDrawString gds(g, rc);

		// draw with a default font and brush
		Gdiplus::SolidBrush br(Gdiplus::Color(0, 0, 0));
		gds.DrawString(txt, font, &br);

		// done
		return;
	}

	// HRESULT error handler - log it and return
	HRESULT hr = S_OK;
	auto HRError = [&hr, &eh](const TCHAR *where) -> void {
		eh.SysError(_T("DirectWrite error drawing formatted text"), MsgFmt(_T("%s, HRESULT=%lx"), where, hr));
	};

	// get the GDI+ HDC
	struct GPHDC
	{
		GPHDC(Gdiplus::Graphics &g) : g(g)
		{
			g.Flush();
			hdc = g.GetHDC();
		}

		~GPHDC() { ReleaseDC(); }

		operator HDC () { return hdc; }

		void ReleaseDC()
		{
			if (hdc != NULL)
			{
				g.ReleaseHDC(hdc);
				hdc = NULL;
			}
		}

		Gdiplus::Graphics &g;
		HDC hdc;
	};
	GPHDC gphdc(g);

	Gdiplus::FontFamily family;
	font->GetFamily(&family);
	WCHAR fontName[LF_FACESIZE];
	family.GetFamilyName(fontName);
	float fontSz = font->GetSize();

	// set up a default text format
	RefPtr<IDWriteTextFormat> baseFormat;
	if (!SUCCEEDED(hr = dwFactory->CreateTextFormat(fontName, NULL,
		DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
		fontSz * 96.0f / static_cast<float>(GetDeviceCaps(gphdc, LOGPIXELSY)),
		locale, &baseFormat)))
		return HRError(_T("CreateTextFormat (default base format)"));

	// set default alignment
	baseFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	baseFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// set up a text layout
	RefPtr<IDWriteTextLayout> layout;
	if (!SUCCEEDED(hr = dwFactory->CreateTextLayout(
		TCHARToWCHAR(txt), static_cast<UINT32>(_tcslen(txt)),
		baseFormat, rc.Width, rc.Height, &layout)))
		return HRError(_T("CreateTextLayout"));

	// create a DC render target
	RefPtr<ID2D1DCRenderTarget> target;
	D2D1_RENDER_TARGET_PROPERTIES targetProps = {
		D2D1_RENDER_TARGET_TYPE_DEFAULT, { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED },
		0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
	};
	if (!SUCCEEDED(hr = d2dFactory->CreateDCRenderTarget(&targetProps, &target)))
		return HRError(_T("CreateDCRenderTarget"));

	// bind the DC to the render target
	RECT rc2{ static_cast<int>(rc.X), static_cast<int>(rc.Y), static_cast<int>(rc.GetRight()), static_cast<int>(rc.GetBottom()) };
	if (!SUCCEEDED(target->BindDC(gphdc, &rc2)))
		return HRError(_T("BindDC"));

	// create the default brush
	RefPtr<ID2D1SolidColorBrush> br;
	if (!SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Yellow, 1.0f), &br)))
		return HRError(_T("Create default brush"));

	// draw
	target->BeginDraw();
	target->DrawTextLayout({ 0.0f, 0.0f }, layout, br, D2D1_DRAW_TEXT_OPTIONS_NONE);
	if (!SUCCEEDED(target->EndDraw()))
		return HRError(_T("EndDraw"));
}

DirectWriteUtils::DirectWriteUtils(ErrorHandler &eh)
{
	HRESULT hr = S_OK;
	auto HRError = [this, &eh, &hr](const TCHAR *where)
	{
		eh.SysError(_T("An error occurred initializing the DirectWrite subsystem. Formatted text functions in Javascript won't operate during this session."),
			MsgFmt(_T("%s failed, HRESULT=%lx"), where, hr));

		// release any resources we created
		this->dwFactory = nullptr;
		this->d2dFactory = nullptr;
	};

	// create the DWrite fatory
	if (!SUCCEEDED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwFactory))))
		{ HRError(_T("DWriteCreateFactory")); return; }

	// create the D2D factory
	if (!SUCCEEDED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory)))
		{ HRError(_T("D2D1CreateFactory")); return; }

	// retrieve the system default locale name
	if (GetSystemDefaultLocaleName(locale, countof(locale)) == 0)
		wcscpy_s(locale, L"en-US");

	// initialize a WIC factory
	if (!SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory))))
		{ HRError(_T("CoCreateInstance(WICImagingFatory)")); return; }
}

DirectWriteUtils::~DirectWriteUtils()
{
}
