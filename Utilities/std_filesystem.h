// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Sense which version of std::filesystem to include:
//
//    C++17 and later -> std::filesystem
//    earlier         -> std::experimental::filesystem
//
// This header attempts to sense which version the compiler supports
// based on feature macros provided by the compiler environment.
// Based on a StackOverflow posting by BrainStone,
// https://stackoverflow.com/questions/53365538/how-to-determine-whether-to-use-filesystem-or-experimental-filesystem
//
// The automatic sensing is inherently heuristic, since the C++ standards
// didn't anticipate the problem when the 'exprimental' version was
// introduced and so didn't prescribe compiler-defined macros that
// future software could use for such sensing.  So it might not work on
// every compiler.  If it doesn't pick the right version on your compiler,
// you can fall back on purely manual selection.  In your build system
// (Makefile or equivalent), add the appropriate option to your compiler
// command line to define the macro INCLUDE_STD_FILESYSTEM_EXPERIMENTAL
// to 0 (to use the newer std::filesystem) or 1 (to use the older
// experimental version).  For 'cc' command lines, this is typically
// of the form -DINCLUDE_STD_FILESYSTEM_EXPERIMENTAL=0 or =1.

#pragma once

// 
// Step 1 - sense which version to include
//

// If the automatic sensing doesn't work, you can pre-define this on
// your compiler command line to skip the automatic sensing entirely
// and use a pre-determined result.
#ifndef INCLUDE_STD_FILESYSTEM_EXPERIMENTAL

// Check for the compiler feature test macro for <filesystem>
# if defined(__cpp_lib_filesystem)
#  define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 0

// Check for the copmiler feature test macro for <experimental/filesystem>
# elif defined(__cpp_lib_experimental_filesystem)
#  define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 1

// Check if the compiler lets us test for header file existence via
// __has_include().  If so, we can figure it out that way.  If that's
// NOT supported, it's probably an older compiler, so we'll assume
// 'experimental'.
# elif !defined(__has_include)
#  define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 1

// We can do header existence tests - check if the header "<filesystem>" exists
# elif __has_include(<filesystem>)

// If we're compiling on Visual Studio and are NOT compiling with C++17, 
// we need to use experimental even if <filesystem> is present
#  ifdef _MSC_VER

// Check and include header that defines "_HAS_CXX17"
#   if __has_include(<yvals_core.h>)
#    include <yvals_core.h>

// Check for enabled C++17 support.  If so, use the newer std:: version.
#    if defined(_HAS_CXX17) && _HAS_CXX17
#      define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 0
#    endif
#   endif

// If we still haven't decided, that means any of the other VS specific c
// hecks failed, so we need to use experimental
#   ifndef INCLUDE_STD_FILESYSTEM_EXPERIMENTAL
#    define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 1
#   endif

// If we're not on Visual Studio, use the std:: version by default
#  else // defined(_MSC_VER)
#   define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 0
#  endif // defined(_MSC_VER)

// If <experimental/filesystem> exists, use std::experimental::filesystem
# elif __has_include(<experimental/filesystem>)
#  define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 1

// Apparently neither header is available - the subsystem isn't supported at all!
# else
#  error Could not find system header "<filesystem>" or "<experimental/filesystem>"
# endif

#endif // defined(INCLUDE_STD_FILESYSTEM_EXPERIMENTAL)


//
// Step 2 - include the selected version
//

#if INCLUDE_STD_FILESYSTEM_EXPERIMENTAL
//
// Use the experimental version, and create an alias so that we can refer
// to it as std::filesystem, to make the difference transparent to the
// rest of the code.
//
#include <experimental/filesystem>
namespace std {	namespace filesystem = experimental::filesystem; }

#else
//
// Use the newer standard version
//
#include <filesystem>

#endif
