// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Utilities

#pragma once
#include <Windows.h>
#include <string>
#include <varargs.h>
#include <vector>
#include <iterator>
#include <functional>

// Conditional code for debugging
#if defined(_DEBUG)
# define IF_DEBUG(code) code
#else
# define IF_DEBUG(code)
#endif

// Conditional code for bitness of the build.
#if defined(_M_IX86)
#define IF_32_64(valFor32Bits, valFor64Bits) valFor32Bits
#elif defined(_M_X64)
#define IF_32_64(valFor32Bits, valFor64Bits) valFor64Bits
#else
#define IF_32_64(valFor32Bits, valFor64Bits) Error_Unknown_Bitness
#endif

// array element count
#define countof(array) (sizeof(array)/sizeof((array)[0]))

// Adjust an index in a circular buffer for wrapping.  This
// works with values that are out of range above or below (that
// is, negative index values), so it can be used to wrap relative
// offsets in either direction.
inline int Wrap(int index, int cnt)
{
	return ((index % cnt) + cnt) % cnt;
}

// 16-byte aligned object.  DirectXMath vector and matrix types
// use __m128xxx types inside, which require 16-byte alignment.
// Any class *containing* such a type must itself be 16-byte aligned.
// Use this base class when needed to ensure proper alignment.  We
// overload operators new and delete to use the special aligned
// malloc routines rather than than the standard malloc, which
// only uses 4- or 8-byte alignment.
class Align16
{
public:
	// align 16-byte aligned
	void *operator new(size_t bytes) 
	    { return _mm_malloc(bytes, 16); }
	void *operator new(size_t bytes, LPCSTR /*sourceFileName*/, int /*sourceLineNum*/) 
	    { return _mm_malloc(bytes, 16); }
	void operator delete(void *ptr) 
	    { _mm_free(ptr); }
};


// Shorthand for find(collection.begin(), collection.end(), value)
template<class Coll, class ValType> typename Coll::const_iterator findex(Coll &list, ValType val)
{
	return std::find(list.begin(), list.end(), val);
}

// Shorthand for find_if(collection.begion(), collection.end(), value); 
// a pointer to the item found, or null if no match is found
template<class Coll> typename Coll::value_type* findifex(Coll &list, std::function<bool(typename Coll::value_type&)> pred)
{
	auto it = std::find_if(list.begin(), list.end(), pred);
	return (it == list.end() ? 0 : &(*it));
}

// Shorthand for finding the index of an item in a vector
template<class Coll> int indexOf(Coll &list, typename Coll::value_type val)
{
	auto it = std::find(list.begin(), list.end(), val);
	return (it == list.end() ? -1 : static_cast<int>(std::distance(list.begin(), it)));
}

// Generic "if null" macro.  For any type that can yield nullptr, returns
// the second argument if the first is null.
template<typename T> T* IfNull(T *val, T *defVal) { return val != nullptr ? val : defVal; }

