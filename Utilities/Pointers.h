// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Pointers and references
//
// A collection of utilities for reference management.
//
// RefCounted    - base class for reference-counted objects
// RefPtr<T>     - reference-counted pointer to reference-counted class T
// WeakRef<T>    - weak reference to reference-counted class T
//

#pragma once
#include <list>
#include <iterator>
#include "Util.h"

// ------------------------------------------------------------------------
//
// Basic reference-counted object.  This implements a COM-like
// reference count interface with AddRef() and Release() methods.
// Use this as a base class.
//
// Subclasses should generally use protected or private destructors,
// since reference-counted objects should normally only be deleted
// implicitly when the reference count reaches zero in Release().
//
// These objects can be referenced via RefPtr<T> and WeakRef<T>
// pointers.
//
class RefCounted
{
public:
	// Per the COM custom, the constructor counts one reference on
	// behalf of the caller, so 'new' creates an object with one
	// reference already applied.  
	RefCounted()
	{
		// count one reference on behalf of 'new' caller
		refCnt = 1;
	}

	// Add a reference.  Call this when storing a pointer to the
	// object in a new variable.  This is thread-safe.
	ULONG AddRef() { return InterlockedIncrement(&refCnt); }

	// Release a reference.  Call this when a pointer variable is
	// about to go out of scope, or an object containing a reference
	// to this object is about to be deleted, etc.  Releasing the
	// last reference automatically deletes the object.  This is
	// thread-safe.
	ULONG Release() 
	{ 
		ULONG ret = InterlockedDecrement(&refCnt);
		if (ret == 0) 
			delete this; 
		return ret;
	}

protected:
	// Make the destructor protected, since it's not normally called
	// directly by outside code.  The only way we should normally be
	// deleted is by our own Release() method, when the reference
	// count reaches zero.
	virtual ~RefCounted() { }

	// current reference count
	ULONG refCnt;
};

// ------------------------------------------------------------------------
//
// Pointer to reference-counted object.  This automatically handles 
// reference counting on common operations.  
//
// This can be used to create managed pointers to RefCounted objects
// as well as COM objects.  The only requirement of the referenced
// type is that it provides AddRef() and Release() methods.
//
// To use:
//
//    // set up a reference-counted pointer to MyClass
//    RefPtr<MyClass> p;
//
//    // assign, adding a reference to the object
//    p = myClassInstance;
//
//    // Attach - assumes the pointer WITHOUT adding a reference.
//    // Use this for cases where the pointer comes from a function
//    // that has already added a reference on behalf of the caller,
//    // such as a constructor or QueryInterface.
//    p = new MyClass();
//
//    // Assign a new reference.  This automatically adds a reference
//    // to the new object and releases the reference to the previous
//    // object, if any.
//    p = myClassOtherInstace;
//
//    // Assign to null.  This automatically releases any existing
//    // reference in 'p'.
//    p = 0;
//
//    // Detach.  This sets my pointer to null WITHOUT releasing the
//    // reference count.  Use this when some other code will assume
//    // the reference, such as the caller.
//    return p.Detach();
//
template<class T> class RefPtr
{
public:
	// construction - set null reference by default
	RefPtr() { ptr = 0; }

	// Construction from a bare pointer.  This is used to create
	// the initial reference, such as when constructing an object
	// with "new", so it behaves like Attach() - which is to say
	// that the new reference isn't counted.  Ref counted objects
	// by convention set an initial ref in the constructor on 
	// behalf of the creator, so when we're initializing a RefPtr
	// with a newly minted underlying object, that serves as our
	// ref count and we don't want to add another.
	RefPtr(T *t)
	{
		ptr = t;
	}

	// Copy construction.  We disable this for now due to the
	// ambiguity of the counting semantics.  Given that the source
	// is another RefPtr, it seems like we should add a reference;
	// but then again, the T* form above *doesn't* add a reference,
	// which might make it confusing to add one here.  To avoid
	// this confusion, we simply disable the operation entirely.
	// Use an explicit assignment instead, where the semantics are
	// consistent between all source formats.
	RefPtr(const RefPtr &r) = delete;

	// destruction - release any reference
	~RefPtr()
	{
		// if we have a valid object pointer, decrease the reference
		// count to reflect the termination of this referencer
		if (ptr != 0)
			ptr->Release();
	}

	// assign - drop any existing reference and add a reference to
	// the new object
	RefPtr<T>& operator =(T *t)
	{
		// If the new object is non-null, count our new reference.
		// Do this first, before decrementing the reference to our
		// previous object, in case it's the same object - we don't
		// want to trigger spurious destruction by letting the 
		// reference count drop incorrectly to zero before we
		// attach to it internally.
		if (t != 0)
			t->AddRef();

		// Now it's safe to remove the reference to the previous
		// object, if any
		if (ptr != 0)
			ptr->Release();

		// finally, remember the new object
		ptr = t;

		// return a self reference as the result
		return *this;
	}

	// assign - drop any existing reference and add a reference to
	// the new object
	RefPtr<T>& operator=(RefPtr<T> &t)
	{
		// count the new reference, drop the old reference, and 
		// remember the new pointer (in that order)
		if (t.ptr != 0)
			t.ptr->AddRef();
		if (ptr != 0)
			ptr->Release();
		ptr = t.ptr;

		// return a self reference as the result
		return *this;
	}

	// Attach - set a new pointer WITHOUT adding a reference count.
	// Use this for attaching to a pointer obtained from a function
	// that implicitly adds a reference on behalf of its caller, 
	// such as 'new' or QueryInterface.
	RefPtr<T> &Attach(T *t)
	{
		// release any previous object pointer
		if (ptr != 0)
			ptr->Release();

		// set the new pointer without adding a reference
		ptr = t;
		return *this;
	}

	// Detach - clear my internal pointer WITHOUT decrementing the
	// reference count.  Use this to separate the naked object
	// pointer from the RefPtr wrapper, to pass ownership of the
	// the reference to another object or routine.  For example,
	// this is useful when we have to return a naked pointer to
	// the underlying object from a function, with a reference
	// counted on behalf of the caller.  This lets the caller
	// assume our reference to the object.
	T* Detach()
	{
		// remember the object pointer value for return
		T *retval = ptr;

		// clear the internal pointer without counting the reference
		// removal, since our reference count now implicitly belongs
		// to the caller
		ptr = 0;

		// return the naked pointer
		return retval;
	}

	// Get the underlying naked object pointer by taking the
	// "address" of the RefPtr.
	//
	// When using this to pass the address field to a COM method
	// that returns an object via a pointer-to-pointer parameter,
	// be sure to clear the reference by setting the ref ptr to
	// null first, to ensure that the COM method doesn't overwrite
	// an existing pointer without calling Release():
	//
	//   p = 0;  // releases any previous reference in 'p'
	//   foo->QueryInterface(UUID_xxx, &p);
	//
	T** operator&()
	{
		return &ptr;
	}

	// casting to the underlying object gets the naked pointer
	operator T*() const
	{
		return ptr;
	}

	// get the naked pointer explicitly
	T* Get() const { return ptr; }

	// dereference the underlying object pointer
	T* operator ->() { return ptr; }

	const T* operator ->() const { return ptr; }

protected:
	T *ptr;
};


// ------------------------------------------------------------------------
//
// Weak reference to a reference-counted object.  This manages a pointer
// reference that isn't counted in the referenced object, and thus doesn't
// keep the object alive.  Instead, we register with the referenced object
// for notification on deletion, so that we can clear our pointer.
//
// A weak reference requires a WeakRefable as the target.
//
// Since the target object can be deleted while one or more weak references
// to it still exists, callers holding weak refs must always check to see 
// if the reference is still valid each time they use the pointer.
//
// Internally, the weak reference is implemented via a proxy object.  The
// proxy is a reference-counted object that simply keeps a pointer to the
// target object.  The proxy is created by the target when the target is
// created, and the target keeps a counted reference on the proxy.  When
// the target is deleted, its destructor clears the pointer in the proxy
// and releases its reference on the proxy.  Each WeakRef on the target
// also keeps a counted reference to the proxy.  When we wish to 
// dereference a WeakRef, we simply get the pointer from the proxy.  If
// the target object still exists, the proxy will contain the pointer to
// it.  If the target has been deleted, the proxy's pointer will be null.
// The proxy itself is reference-counted, so it'll stay around as long as
// the target object or any WeakRefs to the target exist, and will be
// automatically deleted as soon as there are no such references.
//
template<class T> class WeakRefProxy;
template<class T> class WeakRefable;
template<class T> class WeakRef
{
public:
	// construction - set null reference by default
	WeakRef() { }

	// construction - set up an initial reference
	WeakRef(T *t) { Set(ptr); }

	// copy construction
	WeakRef(const WeakRef &r) { Set(r.ptr); }

	// assign - drop any existing reference and replace it with a new one
	WeakRef<T>& operator =(T *t)
	{
		Set(t);
		return *this;
	}

	// assign from another weak ref pointer
	WeakRef<T>& operator =(WeakRef<T> &t)
	{
		Set(t.ptr);
		return *this;
	}

	// Test if the underlying object still alive.  If so, the pointer is
	// valid and can be dereferenced.
	bool IsAlive() const { return ptr != 0 && (*ptr).target != 0; }

	// Casting to the underlying object gets the naked pointer.  Note
	// that this can be null even if the caller hasn't cleared the pointer,
	// since the underlying object might have been deleted by unrelated code
	// by becoming unreachable from strong (counted) references.
	operator T*() const { return ptr != 0 ? (*ptr).target : 0; }

	// Dereference the underlying pointer.  The caller should always take
	// care to check the validity of the pointer first.
	T* operator ->() { return ptr != 0 ? (*ptr).target : 0; }

protected:
	// Set the pointer
	void Set(T *t)
	{
		ptr = (t != nullptr ? (WeakRefProxy<T> *)static_cast<WeakRefable<T>*>(t)->weakRefProxy : nullptr);
	}

	// Pointer to the target object.  This is actually a counted
	// reference to the target object's proxy.
	RefPtr<WeakRefProxy<T>> ptr;
};

// ------------------------------------------------------------------------
//
// Weak reference-capable object.  Use this as a base class for objects
// that are to be referenceable via WeakRef<T>.
//
// To use this as a base class, use this formulation:
//
//    class Foo: public WeakRefable<Foo> ...
//
// That is, use the class you're defining as the template argument.
//
template<class T> class WeakRefable
{
public:
	WeakRefable()
	{
		weakRefProxy.Attach(new WeakRefProxy<T>(static_cast<T *>(this)));
	}

	virtual ~WeakRefable()
	{
		// clear the proxy pointer
		weakRefProxy->target = 0;
	}

protected:
	friend class WeakRef<T>;
	RefPtr<WeakRefProxy<T>> weakRefProxy;
};

template<class T> class WeakRefProxy : public RefCounted
{
	friend class WeakRef<T>;
	friend class WeakRefable<T>;

	WeakRefProxy(T *target) { this->target = target; }
	T *target;
};

