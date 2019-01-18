/*********************************************************************

   Copyright (C) 2002 Smaller Animals Software, Inc.

   This software is provided 'as-is', without any express or implied
   warranty.  In no event will the authors be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.

   3. This notice may not be removed or altered from any source distribution.

   http://www.smalleranimals.com
   smallest@smalleranimals.com

   --------

   This code is based, in part, on:
   "A WTL-based Font preview combo box", Ramon Smits
   http://www.codeproject.com/wtl/rsprevfontcmb.asp

**********************************************************************/

#if !defined(AFX_FONTPREVIEWCOMBO_H__3787F1C9_E55D_4F86_A3F2_2405B523A6DB__INCLUDED_)
#define AFX_FONTPREVIEWCOMBO_H__3787F1C9_E55D_4F86_A3F2_2405B523A6DB__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <unordered_map>
#include <memory>
#include <afxwin.h>
#include <afxtempl.h>


/////////////////////////////////////////////////////////////////////////////
// CFontPreviewCombo window

class CFontPreviewCombo : public CComboBox
{
// Construction
public:
	CFontPreviewCombo();

// Attributes
public:

	// System font listing.  The caller is responsible for creating
	// and deleting this object, so that it can be shared among
	// multiple font controls to avoid having to enumerate fonts
	// for each control.
	struct FontInfo
	{
		FontInfo(const TCHAR *name, DWORD flags, LONG height) : 
			name(name), flags(flags), height(height)
		{ }

		// name
		CString name;

		// font height in pixels
		LONG height;

		// ffXxx flags for the font
		DWORD flags;
	};
	struct Fonts
	{
		// parent window
		CWnd *cwndPar;

		// sample text
		CString sample;

		// font height
		int fontHeight;

		// font map
		std::unordered_map<std::basic_string<TCHAR>, FontInfo> fonts;
		
		// vector of the fonts in the map, sorted by name
		std::vector<FontInfo*> byName;

		// maximum name and sample width
		LONG maxNameWidth = 0;
		LONG maxSampleWidth = 0;
	};

	// Internal font type information.  This is stored in the item data for
	// each combo box item.
	static const DWORD ffTrueType = 0x00000001;  // font is a TrueType font
	static const DWORD ffSymbol = 0x00000002;  // this is a symbol font

	//
	// All of the following options must be set before you call Init() !!
	//

	// choose the sample color (only applies with NAME_THEN_SAMPLE and SAMPLE_THEN_NAME)
	COLORREF m_clrSample;

	// choose how the name and sample are displayed
	typedef enum
	{
		NAME_ONLY = 0,		// font name, drawn in font
		NAME_GUI_FONT,		// font name, drawn in GUI font
		NAME_THEN_SAMPLE,	// font name in GUI font, then sample text in font
		SAMPLE_THEN_NAME,	// sample text in font, then font name in GUI font
		SAMPLE_ONLY			// sample text in font
	} PreviewStyle;

// Operations
public:

	// populate a Fonts object with the system font list
	static void InitFonts(Fonts &fonts, CWnd *parent, int fontHeight = 16, const TCHAR *sampleText = _T("abcABC"));
	
	// call this to load the font strings
	void Init(Fonts *fonts);

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CFontPreviewCombo)
	public:
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	//}}AFX_VIRTUAL

// Implementation
public:
	int GetPreviewStyle();
	void SetPreviewStyle(PreviewStyle style);
	int GetFontHeight();
	virtual ~CFontPreviewCombo();
	
protected:
	CImageList m_img;	
	Fonts *m_fonts = nullptr;

	PreviewStyle m_style = NAME_THEN_SAMPLE;

	void AddComboItem(const TCHAR *faceName, DWORD itemData);

	// Generated message map functions
	//{{AFX_MSG(CFontPreviewCombo)
	afx_msg void OnDropdown();
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
};

void WINAPI DDX_FontPreviewCombo(CDataExchange* pDX, int nIDC, CString& faceName);

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_FONTPREVIEWCOMBO_H__3787F1C9_E55D_4F86_A3F2_2405B523A6DB__INCLUDED_)
