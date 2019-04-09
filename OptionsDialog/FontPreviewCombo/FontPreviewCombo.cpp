/*********************************************************************

   This is a customized version Font Preview Combo by Chris Losinger 
   and Dave Schumann.  This is a modified version tailored to
   PinballY's font options dialog.

   The original copyright notice for Font Preview Combo follows.

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

#include "stdafx.h"
#include <algorithm>
#include "../resource.h"
#include "FontPreviewCombo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define SPACING      10
#define GLYPH_WIDTH  15

/////////////////////////////////////////////////////////////////////////////
// CFontPreviewCombo

CFontPreviewCombo::CFontPreviewCombo()
{
	m_clrSample = GetSysColor(COLOR_WINDOWTEXT);
}

CFontPreviewCombo::~CFontPreviewCombo()
{
}


BEGIN_MESSAGE_MAP(CFontPreviewCombo, CComboBox)
	//{{AFX_MSG_MAP(CFontPreviewCombo)
	ON_WM_MEASUREITEM()
	ON_CONTROL_REFLECT(CBN_DROPDOWN, OnDropdown)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CFontPreviewCombo message handlers


/////////////////////////////////////////////////////////////////////////////

static int CALLBACK FontEnumProc(ENUMLOGFONT *lplf, NEWTEXTMETRIC *lptm, DWORD dwType, LPARAM lpData)
{
	// skip "@" fonts - these are for writing sideways (rotated 90 degrees)
	if (lplf->elfLogFont.lfFaceName[0] == '@')
		return TRUE;

	// get the font list object
	auto fonts = reinterpret_cast<CFontPreviewCombo::Fonts*>(lpData);

	// get the name
	auto const faceName = lplf->elfLogFont.lfFaceName;

	// figure our internal flags for the item data
	DWORD flags = 0;
	if ((dwType & TRUETYPE_FONTTYPE) != 0)
		flags |= CFontPreviewCombo::ffTrueType;
	if (lplf->elfLogFont.lfCharSet == SYMBOL_CHARSET)
		flags |= CFontPreviewCombo::ffSymbol;

	// set up a logical font
	CFont cf;
	if (!cf.CreateFont(fonts->fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, 
		faceName))
	{
		ASSERT(0);
		return TRUE;
	}

	LOGFONT lf;
	cf.GetLogFont(&lf);

	CClientDC dc(fonts->cwndPar);

	// measure font name in GUI font
	HFONT hFont = ((HFONT)GetStockObject(DEFAULT_GUI_FONT));
	HFONT hf = (HFONT)dc.SelectObject(hFont);
	CSize sz = dc.GetTextExtent(faceName);
	LONG nameWidth = sz.cx;
	fonts->maxNameWidth = max(fonts->maxNameWidth, nameWidth);
	dc.SelectObject(hf);

	// measure sample in cur font
	hf = (HFONT)dc.SelectObject(cf);
	if (hf)
	{
		sz = dc.GetTextExtent(fonts->sample);
		LONG cappedSampleWidth = min(sz.cx, static_cast<LONG>(nameWidth * 1.25f));
		fonts->maxSampleWidth = max(fonts->maxSampleWidth, cappedSampleWidth);
		dc.SelectObject(hf);
	}

	// add it to the font vector
	fonts->fonts.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(faceName),
		std::forward_as_tuple(faceName, flags, lf.lfHeight));

	// continue the iteration
	return TRUE;
}

void CFontPreviewCombo::InitFonts(Fonts &fonts, CWnd *parent, int fontHeight, const TCHAR *sampleText)
{
	// enumerate system fonts
	fonts.fontHeight = fontHeight;
	fonts.sample = sampleText;
	fonts.cwndPar = parent;
	EnumFontFamilies(parent->GetDC()->m_hDC, NULL, reinterpret_cast<FONTENUMPROC>(FontEnumProc), reinterpret_cast<LPARAM>(&fonts));

	// create a vector of the fonts, for sorting
	fonts.byName.reserve(fonts.fonts.size() + 1);
	for (auto &f : fonts.fonts)
		fonts.byName.emplace_back(&f.second);

	// sort by name
	std::sort(fonts.byName.begin(), fonts.byName.end(), [](const FontInfo* const &a, const FontInfo* const &b) {
		return lstrcmpi(a->name.GetString(), b->name.GetString()) < 0; });
}

void CFontPreviewCombo::Init(Fonts *fonts)
{
	// save the font map
	m_fonts = fonts;

	// load the image list
	m_img.Create(IDB_TTF_BMP, GLYPH_WIDTH, 1, RGB(255, 255, 255));

	// turn off sorting while loading the fonts, so that we don't waste time
	// doing the insertion sort at every step
	bool sorted = (GetStyle() & CBS_SORT) != 0;
	ModifyStyle(CBS_SORT, 0);

	// likewise, turn off drawing
	SetRedraw(false);

	// reset the list
	ResetContent();

	// allocate space for the list
	UINT nBytes = 128;
	for (auto &f : fonts->fonts)
		nBytes += static_cast<UINT>((f.first.length() + 1)*sizeof(TCHAR) + 8);
	InitStorage(static_cast<int>(fonts->fonts.size() + 1), nBytes);

	// add an entry for the default
	AddComboItem(_T("*"), 0);

	// add a combo item for each font
	for (auto &f : fonts->byName)
		AddComboItem(f->name, f->flags);

	// restore drawing
	SetRedraw(true);

	// reenable sorting
	if (sorted)
		ModifyStyle(0, CBS_SORT);
}

/////////////////////////////////////////////////////////////////////////////

void CFontPreviewCombo::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	ASSERT(lpDIS->CtlType == ODT_COMBOBOX);

	CRect rc = lpDIS->rcItem;

	CDC dc;
	dc.Attach(lpDIS->hDC);

	if (lpDIS->itemState & ODS_FOCUS)
		dc.DrawFocusRect(&rc);

	if (lpDIS->itemID == -1)
		return;

	int nIndexDC = dc.SaveDC();

	CBrush br;

	COLORREF clrSample = m_clrSample;

	if (lpDIS->itemState & ODS_SELECTED)
	{
		br.CreateSolidBrush(::GetSysColor(COLOR_HIGHLIGHT));
		dc.SetTextColor(::GetSysColor(COLOR_HIGHLIGHTTEXT));
		clrSample = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
	}
	else
	{
		br.CreateSolidBrush(dc.GetBkColor());
	}

	dc.SetBkMode(TRANSPARENT);
	dc.FillRect(&rc, &br);

	// which one are we working on?
	CString csCurFontName;
	GetLBText(lpDIS->itemID, csCurFontName);

	// draw the cute TTF glyph
	DWORD_PTR dwData = GetItemData(lpDIS->itemID);
	if ((dwData & ffTrueType) != 0)
		m_img.Draw(&dc, 0, CPoint(rc.left + 5, rc.top + 4), ILD_TRANSPARENT);

	// advance past the glyph whether it's there or not, so that the font names line up
	rc.left += GLYPH_WIDTH;

	int iOffsetX = SPACING;

	// figure the style
	PreviewStyle style = m_style;
	if (style == NAME_ONLY && (dwData & ffSymbol) != 0)
		style = NAME_THEN_SAMPLE;

	// if we need it, create a font 
	CFont cf;
	BOOL fontCreateResult = FALSE;
	if (style != NAME_GUI_FONT && csCurFontName != _T("*"))
	{
		fontCreateResult = cf.CreateFont(
			m_fonts->fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
			FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, csCurFontName);
	}

	// draw the text
	CSize sz;
	int iPosY = 0;
	HFONT hf = NULL;
	switch (style)
	{
	case NAME_GUI_FONT:
		{
			// font name in GUI font
			sz = dc.GetTextExtent(csCurFontName);
			iPosY = (rc.Height() - sz.cy) / 2;
			dc.TextOut(rc.left+iOffsetX, rc.top + iPosY,csCurFontName);
		}
		break;

	case NAME_ONLY:
		{
			// font name in current font
			if (fontCreateResult)
				hf = (HFONT)dc.SelectObject(cf);

			sz = dc.GetTextExtent(csCurFontName);
			iPosY = (rc.Height() - sz.cy) / 2;
			dc.TextOut(rc.left+iOffsetX, rc.top + iPosY,csCurFontName);

			if (fontCreateResult)
				dc.SelectObject(hf);
		}
		break;

	case NAME_THEN_SAMPLE:
		{
			// font name in GUI font
			sz = dc.GetTextExtent(csCurFontName);
			iPosY = (rc.Height() - sz.cy) / 2;
			dc.TextOut(rc.left+iOffsetX, rc.top + iPosY, csCurFontName);

			// show the sample in the current font, if available
			if (fontCreateResult)
			{
				// condense, for edit
				int iSep = m_fonts->maxNameWidth;
				if ((lpDIS->itemState & ODS_COMBOBOXEDIT) == ODS_COMBOBOXEDIT)
					iSep = sz.cx;

				// sample in current font
				hf = (HFONT)dc.SelectObject(cf);
				sz = dc.GetTextExtent(m_fonts->sample);
				iPosY = (rc.Height() - sz.cy) / 2;
				COLORREF clr = dc.SetTextColor(clrSample);
				dc.TextOut(rc.left + iOffsetX + iSep + iOffsetX, rc.top + iPosY, m_fonts->sample);
				dc.SetTextColor(clr);
				dc.SelectObject(hf);
			}
		}
		break;

	case SAMPLE_THEN_NAME:
		{
			// sample in current font, if available
			if (fontCreateResult)
			{
				hf = (HFONT)dc.SelectObject(cf);

				sz = dc.GetTextExtent(m_fonts->sample);
				iPosY = (rc.Height() - sz.cy) / 2;
				COLORREF clr = dc.SetTextColor(clrSample);
				dc.TextOut(rc.left + iOffsetX, rc.top + iPosY, m_fonts->sample);
				dc.SetTextColor(clr);

				dc.SelectObject(hf);
			}

			// condense, for edit
			int iSep = m_fonts->maxSampleWidth;
			if ((lpDIS->itemState & ODS_COMBOBOXEDIT) == ODS_COMBOBOXEDIT)
	            iSep = sz.cx;

			// font name in GUI font
			sz = dc.GetTextExtent(csCurFontName);
			iPosY = (rc.Height() - sz.cy) / 2;
			dc.TextOut(rc.left + iOffsetX + iSep + iOffsetX, rc.top + iPosY, csCurFontName);
		}
		break;

	case SAMPLE_ONLY:
		{			
			// sample in current font
			if (fontCreateResult)
				hf = (HFONT)dc.SelectObject(cf);

			sz = dc.GetTextExtent(m_fonts->sample);
			iPosY = (rc.Height() - sz.cy) / 2;
			dc.TextOut(rc.left+iOffsetX, rc.top + iPosY, m_fonts->sample);

			if (fontCreateResult)
				dc.SelectObject(hf);
		}
		break;
	}

	dc.RestoreDC(nIndexDC);

	dc.Detach();
}

/////////////////////////////////////////////////////////////////////////////

void CFontPreviewCombo::MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct) 
{
	// get the font name
	CString csFontName;
	GetLBText(lpMeasureItemStruct->itemID, csFontName);

	// look up the item in the font list
	if (auto it = m_fonts->fonts.find(csFontName.GetString()); it != m_fonts->fonts.end())
	{
		// use the height from the font list, with a few extra pixels for padding
		lpMeasureItemStruct->itemHeight = it->second.height + 4;
	}
}

/////////////////////////////////////////////////////////////////////////////

void CFontPreviewCombo::OnDropdown() 
{
   int nScrollWidth = ::GetSystemMetrics(SM_CXVSCROLL);
   int nWidth = nScrollWidth;
   nWidth += GLYPH_WIDTH;

	switch (m_style)
	{
	case NAME_GUI_FONT:
      nWidth += m_fonts->maxNameWidth;
		break;
	case NAME_ONLY:
      nWidth += m_fonts->maxNameWidth;
		break;
	case NAME_THEN_SAMPLE:
      nWidth += m_fonts->maxNameWidth;
      nWidth += m_fonts->maxSampleWidth;
      nWidth += SPACING * 2;
		break;
	case SAMPLE_THEN_NAME:
      nWidth += m_fonts->maxNameWidth;
      nWidth += m_fonts->maxSampleWidth;
      nWidth += SPACING * 2;
		break;
	case SAMPLE_ONLY:
      nWidth += m_fonts->maxSampleWidth;
		break;
	}

   SetDroppedWidth(nWidth);
}

void CFontPreviewCombo::AddComboItem(const TCHAR *faceName, DWORD itemData)
{
	int index = InsertString(this->GetCount(), faceName);
	ASSERT(index != -1);

	int ret = SetItemData(index, itemData);
	ASSERT(ret != -1);
}


int CFontPreviewCombo::GetFontHeight()
{
	return m_fonts->fontHeight;
}

void CFontPreviewCombo::SetPreviewStyle(PreviewStyle style)
{
	if (style == m_style) return;
	m_style = style;
}

int CFontPreviewCombo::GetPreviewStyle()
{
	return m_style;
}

void WINAPI DDX_FontPreviewCombo(CDataExchange* pDX, int nIDC, CString& faceName)
{
    HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
    _ASSERTE (hWndCtrl != NULL);                
    
    CFontPreviewCombo* ctrl = 
		(CFontPreviewCombo*) CWnd::FromHandle(hWndCtrl);
 
	if (pDX->m_bSaveAndValidate) //data FROM control
	{
		//no validation needed when coming FROM control
		int pos = ctrl->GetCurSel();
		if (pos != CB_ERR)
		{
			ctrl->GetLBText(pos, faceName);
		}
		else
			faceName = "";
	}
	else //data TO control
	{
		//need to make sure this fontname is one we have
		//but if it's not we can't use the Fail() DDX mechanism since we're
		//not in save-and-validate mode.  So instead we just set the 
		//selection to the first item, which is the default anyway, if the
		//facename isn't in the box.

		int pos = ctrl->FindString (-1, faceName);
		if (pos != CB_ERR)
		{
			ctrl->SetCurSel (pos);
		}
		else
			ctrl->SetCurSel (0);
	}
}
