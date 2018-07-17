// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Passkey - create class-specific friend methods
//
// This implements the C++ "passkey" idiom, which is a way to define
// member functions in a class that can be accessed only by designated
// outside callers.  
//
// This is a refinement of the native C++ "friend" mechanism that allows
// fine-grained access control.  A friend class can access ALL of the 
// private member functions and data of the grantor class.  In contrast,
// the passkey idiom allows granting access at the individual function
// level: we can create a member function in the grantor class that can
// be called only from one specific outside class.
//
// The trick is to use an intermediary class called the passkey.  The
// passkey class doesn't have any data or functionality; it exists only
// to mediate the grantor/grantee relationship.  It accomplishes this
// in three steps.  First, the passkey class is defined with a private
// constructor, so that no outside code can create instances of the
// passkey.  Second, the passkey declares the grantee class as a friend,
// overriding the private constructor for the grantee.  This allows code
// within the grantee class's methods to create instances of the passkey
// class.  Third, in the grantor class, we define the method that we 
// want to expose to this one grantee as public, and we define its
// signature such that it takes an instance to the passkey class as a 
// parameter.  The 'public' declaration allows any caller to call it,
// BUT any code that wants to call it needs an instance of the passkey
// class.  And the only way to get an instance of the passkey class is
// to create it, which only the grantee class can do.
//
// In the grantor:
// public:
//     void OnlyCallableByFoo(Passkey<Foo>, otherParams...);
//
// In the grantee:
//     grantor.OnlyCallableByFoo(Passkey<Foo>(), otherArgs...);
//
// Code outside of the grantee class Foo will fail to compile because
// if won't have access to the Passkey<Foo> constructor.

#pragma once

template <typename T> class Passkey
{
private:
	friend T;
	Passkey() {}
	Passkey(const Passkey&) {}
	Passkey& operator=(const Passkey&) = delete;
};
