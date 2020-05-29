// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Font Preference


#pragma once
#include <gdiplus.h>

// Font preference option.  This encapsulates parsing for font specifications
// in the settings file.
//
// There are two levels of default font families for each font preference item.
// First, there's the "usage-specific" default.  This is the default for this
// specific preference item, such as "popup title font".  Some usages have
// these defaults and others don't.  Second, there's the "global default".
// The global default is the fallback if there's no usage-specific default.
// 
// When setting up a FontPref item, the defaultFamily construct argument is the
// usage-specific default.  Passing a null pointer means that there's no usage-
// specific default, so the global default will be used.
struct FontPref
{
	FontPref(int defaultPtSize, const TCHAR *defaultFamily = nullptr,
		int defaultWeight = 400, bool defaultItalic = false) :
		defaultPtSize(defaultPtSize),
		defaultFamily(defaultFamily),
		defaultWeight(defaultWeight),
		defaultItalic(defaultItalic)
	{ }

	FontPref& operator =(const FontPref &src)
	{
		defaultFamily = src.defaultFamily;
		defaultPtSize = src.defaultPtSize;
		defaultWeight = src.defaultWeight;
		defaultItalic = src.defaultItalic;

		family = src.family;
		ptSize = src.ptSize;
		weight = src.weight;
		italic = src.italic;

		return *this;
	}

	// font description
	TSTRING family;
	int ptSize = 0;
	int weight = 0;
	bool italic = false;

	// Defaults for the font.  defaultName can be null, in which case the
	// global DefaultFontFamily preference is used.
	const TCHAR *defaultFamily;
	int defaultPtSize;
	int defaultWeight;
	bool defaultItalic;

	// Parse a font option string.  If the string doesn't match the
	// standard format, we'll apply defaults if useDefault is true,
	// otherwise we'll leave the font settings unchanged.
	void Parse(const TCHAR *text, const TCHAR *globalDefaultFamily, bool useDefaults = true);

	// parse the config setting; applies defaults automatically if
	// the config variable is missing or isn't formatted correctly
	void ParseConfig(const TCHAR *varname, const TCHAR *gloablDefaultFamily);

	// get the font from this descriptor, creating the cached font if needed
	Gdiplus::Font *Get();

	operator Gdiplus::Font*() { return Get(); }
	Gdiplus::Font* operator->() { return Get(); }

	// cached font
	std::unique_ptr<Gdiplus::Font> font;
};
