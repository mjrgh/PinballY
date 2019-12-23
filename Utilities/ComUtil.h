#pragma once
#include <propvarutil.h>
#include "Pointers.h"

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

// VARIANT argument array
struct VARIANTARGArray
{
	VARIANTARGArray(size_t n) : n(n), v(new VARIANTARG[n])
	{
		for (size_t i = 0; i < n; ++i)
			VariantInit(&v[i]);
	}

	~VARIANTARGArray()
	{
		for (size_t i = 0; i < n; ++i)
			VariantClear(&v[i]);

		delete[] v;
	}

	VARIANTARG *v;
	size_t n;
};

// PROPVARIANT subclass with auto initialization and cleanup
struct PROPVARIANTEx : PROPVARIANT
{
	PROPVARIANTEx() { Clear(); }
	~PROPVARIANTEx() { Clear(); }

	void Clear() { PropVariantClear(this); }
};

// ITypeInfo subobject resource managers
template<typename T, void (STDMETHODCALLTYPE ITypeInfo::*release)(T*)>
class TypeInfoItemHolder
{
public:
	TypeInfoItemHolder(ITypeInfo *typeInfo) : typeInfo(typeInfo, RefCounted::DoAddRef) { }
	~TypeInfoItemHolder() { if (p != nullptr) (typeInfo->*release)(p); }

	T** operator&() { return &p; }
	T* operator->() { return p; }

protected:
	// resource object
	T *p = nullptr;

	// originating ITypeInfo
	RefPtr<ITypeInfo> typeInfo;
};

// TYPEATTR holder
class TYPEATTRHolder : public TypeInfoItemHolder<TYPEATTR, &ITypeInfo::ReleaseTypeAttr> { 
	using TypeInfoItemHolder::TypeInfoItemHolder;
};

// FUNCDESC holder
class FUNCDESCHolder : public TypeInfoItemHolder<FUNCDESC, &ITypeInfo::ReleaseFuncDesc> { 
	using TypeInfoItemHolder::TypeInfoItemHolder;
};

// VARDESC holder
class VARDESCHolder : public TypeInfoItemHolder<VARDESC, &ITypeInfo::ReleaseVarDesc> {
	using TypeInfoItemHolder::TypeInfoItemHolder;
};