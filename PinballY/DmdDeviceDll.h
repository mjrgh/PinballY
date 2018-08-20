// VPinMAME dmddevice.dll interface
//
// We access real DMD devices through VPinMAME's DLL interface.  
// DLL implementations exist for all of the common physical DMD
// device types used in pin cabs, so it provides good device 
// independence.  And any pin cab with a DMD will certainly
// have it installed, so we can access the DMD without requiring
// any additional setup or configuration steps.
//
// The VPM DMD DLL interface is defined in a header file in the 
// VPinMAME source tree (ext/dmddevice/dmddevice.h), but we don't
// want to include that header directly because it creates compile-
// time links to the DLL imports.  We don't want to be hard-wired
// to the DLL like that, because we don't want our EXE launch to
// fail if the DLL isn't present on the system, plus we want to
// be able to figure out dynamically at run-time where the DLL 
// is located rather than requiring our own copy or requiring it
// to be on the system PATH.  So we reproduce the necessary
// structures here, and then load the DLL and bind to the 
// exported function entrypoints explicitly at run-time, via
// LoadLibrary() and GetProcAddress().
//
// Note that this will have to be kept in sync with any future 
// changes to the VPM DLL ABI, but the nature of DLLs makes it
// difficult to make incompatible changes without breaking lots
// of user installations, so in practical terms this interface is
// frozen for all time anyway.
//
namespace DMDDevice
{
#ifndef DMDDEVICE_INCLUDED
#define DMDDEVICE_INCLUDED

	// PinMAME options settings struct, to initialize the DLL
	typedef struct tPMoptions {
		int dmd_red, dmd_green, dmd_blue;			// monochrome base color at 100% brightness
		int dmd_perc66, dmd_perc33, dmd_perc0;		// monochrome brightness levels for 4-level display
		int dmd_only, dmd_compact, dmd_antialias;
		int dmd_colorize;							// colorize mode enabled
		int dmd_red66, dmd_green66, dmd_blue66;		// colorized RGB for brightness level 2/66%
		int dmd_red33, dmd_green33, dmd_blue33;		// colorized RGB for brightness level 1/33%
		int dmd_red0, dmd_green0, dmd_blue0;        // colorized RGB for brightness level 0/0%
	} tPMoptions;

	// DMD hardware generation codes
	const UINT64 GEN_WPC95 = 0x0000000000080LL;     // WPC95

	// RGB color
	typedef struct rgb24 {
		unsigned char red;
		unsigned char green;
		unsigned char blue;
	} rgb24;

	// DMD DLL entrypoints.  
	// NOTE: These are just extern declarations that are never actually
	// linked.  DON'T call these directly!  Doing so will cause linker
	// errors, which we DO NOT want to satisfy by binding these to any
	// actual imports, ESPECIALLY NOT DLL imports!  We don't want to
	// statically bind the .exe to DmdDevice.dll because we explicitly
	// want to allow that file to be ENTIRELY MISSING at run-time.  The
	// only reason we include these extern refs at all is for their type
	// declarations, which we use to create the dynamically bound symbols.
#define DMDDEV extern
	DMDDEV int Open();
	DMDDEV bool Close();
	DMDDEV void Set_4_Colors_Palette(rgb24 color0, rgb24 color33, rgb24 color66, rgb24 color100);
	DMDDEV void Set_16_Colors_Palette(rgb24 *color);
	DMDDEV void PM_GameSettings(const char* GameName, UINT64 HardwareGeneration, const tPMoptions &Options);
	DMDDEV void Render_4_Shades(UINT16 width, UINT16 height, UINT8 *currbuffer);
	DMDDEV void Render_16_Shades(UINT16 width, UINT16 height, UINT8 *currbuffer);
	DMDDEV void Render_RGB24(UINT16 width, UINT16 height, rgb24 *currbuffer);
	DMDDEV void Console_Data(UINT8 data);

#endif // DMDDEVICE_INCLUDED

	// Run-time DMD DLL imports
	//
	// We call all DLL entrypoints through function pointers that we
	// bind at run-time when we first load the DLL.  To set up an
	// import, add the following two lines of code:
	//
	// - a DMDDEV_ENTRYPOINT() line in the list immediately below
	//
	// - a DMDDEV_BIND() line in the similar list a little later
	// 
	// When calling these functions, call the Xxx_() version
	// instead of the static extern version defined above.

	// Define static pointers to the DMD DLL entrypoints we use
#ifdef DMDDEVICEDLL_DEFINE_EXTERNS
#define DMDDEVICEEXTERN
#else
#define DMDDEVICEEXTERN extern
#endif
#define DMDDEV_ENTRYPOINT(func) DMDDEVICEEXTERN decltype(DMDDevice::func) *func##_;
	DMDDEV_ENTRYPOINT(Open)
	DMDDEV_ENTRYPOINT(Close)
	DMDDEV_ENTRYPOINT(PM_GameSettings)
	DMDDEV_ENTRYPOINT(Render_4_Shades)
	DMDDEV_ENTRYPOINT(Render_16_Shades)
	DMDDEV_ENTRYPOINT(Render_RGB24)

#undef DMDDEVICEEXTERN
}
