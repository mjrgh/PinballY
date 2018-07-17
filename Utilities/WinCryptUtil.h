// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include <wincrypt.h>

// Windows Cryptography API Handle Holders.  These are specialized
// handle holders (with RAII semantics a la our regular HandleHolder
// template) for selected crypto handle types.  The crypto handles
// aren't based on the normal Windows HANDLE type; they're separate
// types with their own idiosyncratic semantics.  The particular
// idiosyncracy that concerns us here is that each of these handle
// types has its own unique "destroy" function, so we need a special
// holder class for each crypto handle type in order to provide the
// right destructor semantics.  We can at least define a common base
// class with most of the functionality.
//
template<typename HandleType> class CryptHandleHolder
{
public:
	CryptHandleHolder() : h(0) { }
	CryptHandleHolder(HandleType h) : h(h) { }
	HandleType h;

	operator HandleType() const { return h; }
	HandleType* operator&() { return &h; }
	void operator=(HandleType h)
	{
		if (this->h != 0) Clear();
		this->h = h;
	}

	bool operator==(HandleType h) { return this->h == h; }

	// detach the handle from this holder
	HandleType Detach()
	{
		HandleType ret = h;
		h = 0;
		return ret;
	}

	// Clear the handle.  This is pure virtual beacuse we need to 
	// override it per crypto handle type, so that we can call the
	// "destroy" API function peculiar to the handle type.
	virtual void Clear() = 0;
};

// HCRYPTPROV (cryptography provider handle) holder
class HCRYPTPROVHolder : public CryptHandleHolder<HCRYPTPROV>
{
public:
	~HCRYPTPROVHolder() { if (h != 0) Clear(); }
	virtual void Clear() override { CryptReleaseContext(h, 0); }
};

// HCRYPTHASH (hash handle) holder
class HCRYPTHASHHolder : public CryptHandleHolder<HCRYPTHASH>
{
public:
	~HCRYPTHASHHolder() { if (h != 0) Clear(); }
	virtual void Clear() override { CryptDestroyHash(h); }
};

// HCRYPTKEY (encryption key handle) holder
class HCRYPTKEYHolder : public CryptHandleHolder<HCRYPTKEY>
{
public:
	~HCRYPTKEYHolder() { if (h != 0) Clear(); }
	virtual void Clear() override { CryptDestroyKey(h); }
};

