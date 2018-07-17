// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Memory Leak Debugging with Visual Leak Detector
//
// Quick setup instructions, if you've already installed VLD:
//
// - Uncomment the #define line below
//
// - In the project properties, under Linker > Debugging, set "Generate
//   Debug Info" to /DEBUG:FULL
//
// - Do a full rebuild of the DEBUG configuration
//
// - Run the program, perform the steps to reproduce memory leak you're
//   trying to debug, exit the program.  VLD should display its report 
//   in the Visual Studio "Output" window.
//
//
// See below for detailed instructions, including how to install VLD.
//
//#define INCLUDE_VISUAL_LEAK_DETECTOR


// Detailed Instructions
//
// This file OPTIONALLY configures the build to include Visual Leak
// Detector in the build to help debug memory leaks.  VLD is disabled
// by default, so that you can compile the system without installing
// VLD, and also because the leak detection adds some overhead while
// running.
//
// If you want to enable VLD, follow the instructions below to install
// VLD and enable it.
//
// 
// What a "memory leak" is and why it should be fixed
//
// A memory leak occurs when the program allocates a block of memory
// for some purpose, and then doesn't free the memory when it's no
// longer needed.
//
// This is bad because the operation that triggered the memory
// allocation might be repeated, and every time it's repeated, some
// more memory will be consumed that will never be released.  Repeat
// the operation enough and the system will run out of memory entirely,
// causing the program to crash.  Even if it never gets to the point
// of completely using up all memory, the unnecessary memory usage
// can degrade performance.  Programs that seem to get slower and
// slower the longer they keep running are probably leaking memory.
//
// 
// How memory leaks are detected
//
// The Microsoft C++ run-time library has some very basic memory leak
// detection built-in.  This is enabled whenever you run a DEBUG build
// of the program within the Visual Studio debugger.  The basic library
// memory detection works by keeping a list of every memory block
// allocated throughout the program's execution.  Each time a block is
// allocated, it's added to the list; each time a block is freed, it's
// crossed off the list.  At the very end of the run, when you close
// the program and return to the debugger, the library looks to see if
// there's anything left in the list.  A correctly written program will
// precisely match every single memory allocation with a corresponding
// free, so the list should be completely empty when the program exits.
// If there's anything in the list, it represents a memory leak.  So 
// the C++ library displays a warning message in the Visual Studio 
// "Output" window listing each unfreed block.
//
// Whenever you run a debug session during program development, it's
// a good idea to check the "Output" window at the end of the run to
// see if there are any of these leak warnings.  If so, you should 
// fix them.  They're bugs.
//
//
// What VLD brings to the table
//
// The built-in memory detection in the compiler is great for
// determining that you have a leak, especially since it's so
// nicely automatic.  It's always there being quietly vigilant.
// But it's not very helpful for actually fixing the leaks it
// detect, because it gives you very little information on 
// where the problematic memory blocks are being allocated.
//
// That's where VLD comes in.  VLD uses the same basic technique
// as the basic C++ library to detect leaks, but it adds a complete
// record of the source code location of each allocation. Most 
// leaks can be solved pretty easily once you know where the memory 
// involved was allocated, and VLD gives you the full details.  VLD 
// gives you not only the source code location where each leaked
// block was allocated, but also a full stack trace at the point
// where the allocation occurred.
//
// Note that you probably won't want to run with VLD enabled 
// routinely, because all of the extra information gathering does
// add some execution overhead that slows the program down a bit.
// The recommended way to operate is to leave VLD turned off most
// of the time, relying on the basic C++ library to detect leaks.
// When the C++ library detects a leak, turn on VLD so that you
// can track it down.  You'll have to run the program again and
// repeat the steps that triggered the leak to get the VLD report.
//
//
// How to install VLD
//
// 1.  Download VLD and install it on your build machine.  You can
// find the VLD installer on the project site on codeplex.com:
//
//    https://vld.codeplex.com/
//
// VLD is open-source, so you can build it from source code instead of
// installing the binary, if you prefer.  The code repos and build 
// instructions can be found on the VLD project site above.  If the 
// codeplex link above isn't working when you read this, you should be
// able to find the code on other open-source repos sites such as github;
// search for "VLD visual leak detector".
//
// 2. Follow the setup instructions in the VLD project documentation in the
// section "Using Visual Leak Detector".  The instructions for VS 2010-13
// apply to 2015-2017 as well.  Briefly, you have to add the VLD include
// path and library path to your User Property Pages in Visual Studio
// (the macros $(VC_IncludePath) and $(VC_LibraryPath)).  In the Visual 
// Studio IDE, select View > Other Windows > Property Manager.  Select any
// of your C++ projects in the tree, open it to view its subitems in the
// tree, and double-click on "Microsoft.Cpp.Win32.user".  Go to C++ >
// General; add <path to VLD>\include to "Additional Include Diretories".
// Go to Linker > General; add <path to VLD>\lib\Win32 to "Additional
// Library Directories".  Repeat for "Microsoft.Cpp.Win64.user", but use
// lib\x64 for the library path.
//
// 3. In the Solution Explorer view, right-click the subproject where you
// want to use VLD and select Properties.  In the Linker > Debugging section,
// under "Generate Debug Info", make sure the "optimized for sharing and
// publishing (/DEBUG:FULL)" is selected.
//
// 4. Uncomment the #define INCLUDE_VISUAL_LEAK_DETECTOR line at the top
// of this file.
//
// 5. Do a full DEBUG rebuild of the project.  Note that VLD is suppressed
// in release builds, EVEN WHEN THE SYMBOL ABOVE IS DEFINED, to help prevent 
// accidental inclusion in a public release.  That should rarely even be
// noticeable, because you'll usually want to investigate these things in
// the debug build anyway.  But if you encounter an oddball leak that only
// reproduces in the release build, you'll need to edit the #if test at the
// bottom of the file.  Please be sure to set it back when you're done!
// 
// 6. Run the program as normal.  Do whatever it is that you did before that
// triggered the known or suspected memory leak.  Exit the program.  In the
// Visual Studio "Output" pane, VLD should display an extensive report 
// detailing the memory leaks it caught, including a stack trace with source
// file locations at the point where each leaked block was allocated.  Knowing
// exactly which memory blocks are leaked is often enough to figure out why
// they weren't cleaned up.  When it's not, it's at least a good starting point
// for tracing the leaked object's lifecycle in the debugger to observe why
// it's not deleted when it should be.
//
// 7. IF YOU GET AN EMPTY STACK TRACE in the VLD reports: edit VLD.INI (in the
// VLD program folder); search for ReportEncoding; change the setting from 
// "ascii" to "unicode".
//
// 8. After you track down the leak, remember to disable VLD again by 
// commenting out the #define line at the top of this file and seting the
// linker /DEBUG option in back to the normal setting (probably FASTLINK).
// 

#pragma once

#if defined(INCLUDE_VISUAL_LEAK_DETECTOR) && defined(_DEBUG)
#include <vld.h>
#endif
