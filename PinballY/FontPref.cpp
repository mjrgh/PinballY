#include "stdafx.h"
#include "../Utilities/Config.h"
#include "FontPref.h"

void FontPref::Parse(const TCHAR *text, const TCHAR *globalDefaultFamily, bool useDefaults)
{
	// try matching the standard format: <size> <weight> <name>
	static const std::basic_regex<TCHAR> pat(_T("\\s*(\\d+(pt)?|\\*)\\s+(\\S+)\\s+(.*)"), std::regex_constants::icase);
	std::match_results<const TCHAR*> m;
	if (std::regex_match(text, m, pat))
	{
		// read the size
		ptSize = defaultPtSize;
		auto ptSizeStr = m[1].str();
		if (int n = _ttoi(ptSizeStr.c_str()); n > 0)
		{
			// non-zero point size specified - use that
			ptSize = n;
		}

		// read the weight and style
		weight = defaultWeight;
		italic = defaultItalic;
		auto weightStyleStr = m[3].str();
		static const std::basic_regex<TCHAR> weightStylePat(_T("([^/]+)(?:/(.+))?"));
		std::match_results<TSTRING::const_iterator> mw;
		if (weightStyleStr.length() != 0 && weightStyleStr != _T("*") && std::regex_match(weightStyleStr, mw, weightStylePat))
		{
			// check what kind of weight spec we have
			auto weightStr = mw[1].str();
			if (int n = _ttoi(weightStr.c_str()); n >= 100 && n <= 900)
			{
				// numeric weight value, 100 to 900 scale
				weight = n;
			}
			else if (weightStr.length() != 0 && weightStr != _T("*"))
			{
				// try a standard weight keyword
				static const struct
				{
					const TCHAR *name;
					int weight;
				}
				names[] = {
					{ _T("thin"), 100 },
					{ _T("hairline"), 100 },
					{ _T("xlight"), 200 },
					{ _T("extralight"), 200 },
					{ _T("extra-light"), 200 },
					{ _T("ultralight"), 200 },
					{ _T("ultra-light"), 200 },
					{ _T("light"), 300 },
					{ _T("normal"), 400 },
					{ _T("medium"), 500 },
					{ _T("semibold"), 600 },
					{ _T("semi-bold"), 600 },
					{ _T("bold"), 700 },
					{ _T("extrabold"), 800 },
					{ _T("extra-bold"), 800 },
					{ _T("xbold"), 800 },
					{ _T("black"), 900 },
					{ _T("heavy"), 900 },
				};
				bool matched = false;
				for (size_t i = 0; i < countof(names); ++i)
				{
					if (_tcsicmp(weightStr.c_str(), names[i].name) == 0)
					{
						weight = names[i].weight;
						matched = true;
						break;
					}
				}

				// If we didn't match a weight name, check for a style name,
				// in case they used a style without specifying a weight.
				// This is treated as equivalent to "*/style", meaning that
				// the default weight is inherited.
				if (!matched)
				{
					if (_tcsicmp(weightStr.c_str(), _T("italic")) == 0)
						italic = true;
					else if (_tcsicmp(weightStr.c_str(), _T("regular")) == 0)
						italic = false;
				}
			}

			// Check for a style spec, for "regular" or "italic"
			if (mw[2].matched)
			{
				auto styleStr = mw[2].str();
				if (_tcsicmp(styleStr.c_str(), _T("italic")) == 0)
					italic = true;
				else if (_tcsicmp(styleStr.c_str(), _T("regular")) == 0)
					italic = false;
			}
		}

		// Get the family.  If a family name is specified in the settings string, use that;
		// otherwise, if there's a local default family for the preference item, use that;
		// otherwise use the global default.
		auto familyStr = m[4].str();
		if (familyStr.length() != 0 && familyStr != _T("*"))
			family = familyStr;
		else if (defaultFamily != nullptr)
			family = defaultFamily;
		else
			family = globalDefaultFamily;

		// clear any cached font object
		font.reset();
	}
	else if (useDefaults)
	{
		// it's not in the standard format, and the caller directed us to
		// apply defaults in this case, so apply the defaults
		ptSize = defaultPtSize;
		weight = defaultWeight;
		if (defaultFamily != nullptr)
			family = defaultFamily;
		else
			family = globalDefaultFamily;

		// clear any cached font object
		font.reset();
	}
}

void FontPref::ParseConfig(const TCHAR *varname, const TCHAR *globalDefaultFamily)
{
	// Parse the config variable value.  If it's not defined, just parse an
	// empty string, which will set the defaults for the font.
	Parse(ConfigManager::GetInstance()->Get(varname, _T("")), globalDefaultFamily);
}

Gdiplus::Font* FontPref::Get()
{
	if (font == nullptr)
		font.reset(CreateGPFont(family.c_str(), ptSize, weight, italic));

	return font.get();
}

