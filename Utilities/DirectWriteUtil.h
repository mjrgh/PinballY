// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//

#include <dwrite.h>
#include <d2d1.h>
#include <wincodec.h>
#include "Pointers.h"
#include "ComUtil.h"
#include "LogError.h"

// device-independent pixels per printer's point unit
const float dipsPerPoint = 0.0138889f * 96.0f;

class DirectWriteUtils
{
public:
	static DirectWriteUtils *Get() { return inst; }
	
	static void Init(ErrorHandler &eh);
	static void Terminate();

	// get the DirectWrite factory
	IDWriteFactory *GetDWriteFactory() const { return dwFactory; }

	// get the WIC factory
	IWICImagingFactory *GetWICFactory() const { return wicFactory; }

	// draw formatted text
	void Draw(Gdiplus::Graphics &g, const TCHAR *txt, Gdiplus::Font *font, Gdiplus::RectF &rc, ErrorHandler &eh);
	
	// Base class for custom inline objects
	class InlineObject : public IDWriteInlineObject, public RefCounted
	{
	public:
		// basic IUnknown implementation
		ULONG AddRef() override { return RefCounted::AddRef(); }
		ULONG Release() override { return RefCounted::Release(); }
		HRESULT QueryInterface(IID const &riid, void **ppvObject)
		{
			if (ppvObject == nullptr)
				return E_POINTER;

			if (riid == __uuidof(IDWriteInlineObject)
				|| riid == __uuidof(IUnknown))
			{
				AddRef();
				*ppvObject = this;
				return S_OK;
			}
			*ppvObject = nullptr;
			return E_FAIL;
		}

		// set/clear the render target
		virtual void SetRenderTarget(ID2D1RenderTarget *target) { renderTarget = target; }

	protected:
		// Render target.  The renderer sets this before each rendering pass,
		// and clears it after the rendering pass.  (You'd think that the D2D
		// framework would automatically include this information in Draw()
		// calls, 
		RefPtr<ID2D1RenderTarget> renderTarget;
	};

	// Inline image object vertical alignment
	enum ImageVAlign {
		Top,
		Center,
		Bottom,
		Baseline
	};

	// Template struct for rectangle-like objects, with top, bottom, 
	// left, right elements: padding, margins, borders
	template<class Ele> struct RectLike
	{
		Ele top;
		Ele bottom;
		Ele left;
		Ele right;

		void SetAll(Ele val) { top = bottom = left = right = val; }
	};

	// border styles
	enum BorderStyle {
		None,
		Hidden,
		Dotted,
		Dashed,
		Solid,
		Double,
		Groove,
		Ridge,
		Inset,
		Outset,
		Initial,
		Inherit
	};

	// borders
	struct BorderEle
	{
		UINT32 color = 0;
		BorderStyle style = BorderStyle::None;
		float width = 0.0f;

		// visible -> style is not None or Hidden AND non-zero width AND not completely transparent
		bool IsVisible() const { return style != None && style != Hidden && (color & 0xFF000000) != 0 && width != 0.0f; }

		// affects layout -> style is not None or Hidden AND non-zero width
		bool AffectsLayout() const { return style != None && style != Hidden && width != 0.0f; }

		// layout width
		float LayoutWidth() const { return AffectsLayout() ? width : 0.0f; }

		// apply the parent style
		void ApplyParentStyle(const BorderEle &parent) 
		{ 
			if (style == Inherit)
				*this = parent;
			else if (style == Initial)
				style = None;
		}

		// clear the border
		void Clear() {
			color = 0;
			style = BorderStyle::None;
			width = 0.0f;
		}
	};
	struct Borders : RectLike<BorderEle>
	{
		bool IsVisible() const {
			return left.IsVisible() || right.IsVisible() || top.IsVisible() || bottom.IsVisible();
		}
		bool AffectsLayout() const {
			return left.AffectsLayout() || right.AffectsLayout() || top.AffectsLayout() || bottom.AffectsLayout();
		}
		void ApplyParentStyle(const Borders &parent)
		{
			left.ApplyParentStyle(parent.left); right.ApplyParentStyle(parent.right); 
			top.ApplyParentStyle(parent.top); bottom.ApplyParentStyle(parent.bottom);
		}
		void Clear() {
			left.Clear(); right.Clear(); top.Clear(); bottom.Clear();
		}
	};


	// Styled text.  This encapsulates a series of inline text spans, each with
	// its own styling data, that can be assembled into an IDWriteTextLayout for
	// measurement and display.  This object corresponds roughly to a "block"
	// object in the HTML box layout model, such as a DIV or paragraph.
	struct StyledText
	{
		// Inline text span style
		//
		// Note: Colors are in D2D UINT32 RGB format, which has the
		// bytes in opposite order of the Win32 COLORREF format.  The
		// bits are arranged 00000000.rrrrrrrr.gggggggg.bbbbbbbb in this
		// format (most significant to least).  This happens to be the same
		// byte ordering that HTML uses for #RRGGBB values, which actually
		// makes these easier to work with than COLORREFs, but it's worth
		// noting since it's opposite the usual Win32 conventions.
		struct SpanStyle
		{
			WSTRING face = L"Arial";
			float size = 10.0f * dipsPerPoint;
			DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
			DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
			DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
			bool underline = false;
			bool strikethrough = false;

			UINT32 textColor = 0xFF000000;
			UINT32 bgColor = 0x00000000;

			// get the font metrics fore this style
			struct FontMetrics { float ascent; float descent; };
			FontMetrics GetFontMetrics() const;

			// Measure the bounds of text in this style
			HRESULT MeasureText(DWRITE_TEXT_METRICS &tm, const WCHAR *str, size_t len, float maxWidth, float maxHeight) const;
		};

		// horizontal alignment of the text in the IDWriteTextLayout block
		DWRITE_TEXT_ALIGNMENT hAlign = DWRITE_TEXT_ALIGNMENT_LEADING;

		// background fill color
		UINT32 bgColor = 0x00000000;

		// corner radius for background fill
		float cornerRadius = 0.0f;

		// padding
		RectLike<float> padding = { 0.0f, 0.0f, 0.0f, 0.0f };

		// Add a text span
		void AddSpan(const TCHAR *txt, size_t len, const SpanStyle &style);
		void AddSpan(const TCHAR *txt, const SpanStyle &style) { AddSpan(txt, txt != nullptr ? _tcslen(txt) : 0, style); }
		void AddSpan(const TSTRING &txt, const SpanStyle &style) { AddSpan(txt.c_str(), txt.length(), style); }

		// Add a non-breaking space
		void AddNBSP(const SpanStyle &style);

		// Add an inline image
		void AddImage(const TCHAR *filename, const SpanStyle &style,
			ImageVAlign valign, float layoutWidth, float layoutHeight,
			ErrorHandler &eh);

		// plain text
		WSTRING plainText;

		// Span.  This represents one inline text element within a vertical
		// division.  It contains the font style, colors, and borders
		// for a text span within the span.
		struct Span
		{
			Span(SpanStyle style, InlineObject *inlineObject, DWRITE_TEXT_RANGE range) : 
				style(style),
				inlineObject(inlineObject, RefCounted::DoAddRef),
				range(range)
			{
			}

			// style of the text in this span
			SpanStyle style;

			// inline object, if any
			RefPtr<InlineObject> inlineObject;

			// character range covered by the span in the underlying plain 
			// text array
			DWRITE_TEXT_RANGE range;

			// Text Format object for the style within this span
			RefPtr<IDWriteTextFormat> format;

			// create a Text Format representing this style
			HRESULT CreateTextFormat();
		};
		std::list<Span> spans;

		// text layout
		RefPtr<IDWriteTextLayout> layout;

		// Create the text layout object if it doesn't already exist
		void CreateTextLayout(ErrorHandler &eh);
	};

	// render styled text
	void RenderStyledText(Gdiplus::Graphics &g, StyledText *txt, 
		const Gdiplus::RectF &rcLayout, const Gdiplus::RectF &rcClip, ErrorHandler &eh);

	// Measure the bounding box of styled text.  Returns true on success, false
	// on failure.
	//
	// rcPositioning is filled in with the positioning box, which is the minimum
	// bounding box enclosing all of the positioning and alignment boxes of the
	// layout's glyphs and objects. 
	//
	// rcInk is the filled with the "ink" box, which is the minimum bounding box
	// enclosing all of the pixels drawn for all of the glyphs and objects.  The
	// ink box might be larger than the positioning box, because glyphs in some
	// fonts can overhang their positioning/alignment boxes.  This is common for
	// italic and oblique fonts: many glyphs in these fonts overhang their
	// character cell boxes horizontally in the slant direction.  It's also
	// common in decorative fonts, where swashes and exaggerated serifs can
	// extend beyond the character cells.  Note that the ink box can even be
	// smaller than the positioning box, since some glyphs have whitespace
	// around the edges.
	bool MeasureStyledText(Gdiplus::RectF &rcPositioning, Gdiplus::RectF &rcInk,
		StyledText *txt, const Gdiplus::RectF &rc, ErrorHandler &eh);

protected:
	// singleton instance
	static DirectWriteUtils *inst;

	// create/destroy through static Init()/Terminate() methods
	DirectWriteUtils(ErrorHandler &eh);
	~DirectWriteUtils();

	// direct write factory interface
	RefPtr<IDWriteFactory> dwFactory;

	// Direct2D factory
	RefPtr<ID2D1Factory> d2dFactory;

	// WIC factory
	RefPtr<IWICImagingFactory> wicFactory;

	// default system locale name
	WCHAR locale[LOCALE_NAME_MAX_LENGTH];
};
