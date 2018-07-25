#pragma once
#include <propvarutil.h>

// EXCEPINFO subclass with auto initialization and cleanup
struct EXCEPINFOEx : EXCEPINFO
{
	EXCEPINFOEx() { ZeroMemory(this, sizeof(EXCEPINFO)); }
	~EXCEPINFOEx() { Clear(); }

	void Clear()
	{
		if (bstrDescription != nullptr) SysFreeString(bstrDescription);
		if (bstrSource != nullptr) SysFreeString(bstrSource);
		if (bstrHelpFile != nullptr) SysFreeString(bstrHelpFile);
		ZeroMemory(this, sizeof(EXCEPINFO));
	}
};

// VARIANT subclass with auto initialization and cleanup
struct VARIANTEx : VARIANT
{
	VARIANTEx(VARTYPE vt = VT_NULL)
	{
		ZeroMemory(this, sizeof(VARIANT));
		this->vt = vt;
	}

	~VARIANTEx() { Clear(); }

	void Clear() { VariantClear(this); }
};
