// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Joysticks
// 
// We use Raw Input in combination with the HidP API to read joystick
// input.  This is what Microsoft currently recommends.  (The main other
// Microsoft joystick API, DirectInput, has been deprecated, so we avoid
// it.  Microsoft deprecated DirectInput because it was perceived as
// having performance limitations inherent in its architecture.)
//
// Code that wants to handle joystick events can do so by subscribing
// for event notifications.  See SubscribeJoystickEvent().  This offers
// a simple, high-level API to button change and axis value change
// events.
//
// Within the Joystick Manager, we have two kinds of joystick object:
// "physical" and "logical".  A physical joystick represents an actual
// device in the system, discovered in a HID device scan.  A logical
// joystick represents a notional device for configuration purposes.  A
// logical joystick might or might not correspond to any current physical
// joysticks, because the device referenced might have been disconnected
// since the configuration was created.  Logical joysticks are related to
// to physical joysticks by the "product string" in the USB descriptor 
// data, so multiple physical devices present in the system at the same
// time might map to the same logical joystick.  This is described in
// more detail in the descriptions of the classes below.
//
// The basic flow of Raw Input data looks like this:
//
// 1. Main app window receives WM_INPUT through its message loop
// 2. Main app window calls the InputManager to process the input
// 3. InputManager calls JoystickManager if the input is HID data
// 4. JoystickManager calls the PhysicalJoystick matching the handle,
//    and sends it to the PhysicalJoystick if it finds a match
// 5. PhysicalJoystick decodes the packet and applies state changes,
//    then calls JoystickManager with event notifications
// 6. JoystickManager calls each subscriber with the event data
//
// The Raw Input/HidP APIs for reading the joystick are very low-level,
// which makes the decoding process rather complex.  Raw Input takes its
// name pretty literally.  The basic idea is that RI passes us the actual
// byte data that the joystick sends across the USB wire, and leaves it
// up to us to figure out what the heck the bytes mean.  The USB message
// structure is well-defined under the USB HID protocol, but USB HID is
// very complex.  There's no such thing as a "joystick" report format;
// instead, each device defines its own unique, ad hoc format, and uses
// a USB HID "report descriptor" to tell the host how to interpret the
// bytes in its reports.  The "report descriptor" is basically like a
// C struct definition, telling the host how the bytes are arranged
// into fields and what each field means.  There are two levels of
// parsing required: we have to parse the report descriptor language
// in order to understand how to parse the reports.
//
// Fortunately, we're not entirely on our own to parse the USB report
// descriptor language.  Windows provides an API, HidP, that includes
// functions that decode the report descriptor language and decode the
// report packets accordingly.  This API exposes data at the level of 
// joystick abstractions, such as which buttons are pressed and what 
// the various joystick axes are reading.  Even with the help of HidP,
// the decoding process is still pretty complex, but much less so than
// if we had to deal with all of the raw USB bytes directly.
// 
// A note on performance.  When you look at our code that parses the USB
// packets, you might be struck by how much work it seems to be doing,
// and you might pine for the old DirectInput days where the client could
// get at a joystick button state or axis value with relatively little
// code.  And our parsing code is indeed rather complex.  But the complex
// work it's doing is work that *someone* has to do.  DirectInput is in
// fact built on top of the same Raw Input and HidP layers we use, so it
// had to do all of that same parsing work we do.  So this approach,
// building directly on Raw Input and HidP ourselves, doesn't actually
// add overhead; it just takes the same decoding cost that was hidden 
// inside DI and moves it into the open.  In concrete terms, the packet
// parser is actually very fast.  On my 2013/4th generation i7, with 
// this code compiled in release mode, each packet from my Pinscape 
// Controller (which reports button and accelerometer data in joystick 
// format) takes about 700 nanoseconds to go through the parser.  At 
// about 10ms between packets, that's about .007% overhead.  That's on
// top of the WM_INPUT message dispatch and decoding time, but the
// WM_INPUT time applies to ANY path to the input, be it Raw Input,
// Direct Input, or anything else, as everything goes through the raw
// input layer first.
//
// So the bottom line is that even though it looks like we have more
// code here than a DirectInput version would have, we actually have 
// less overall, because the code here would have been in DirectInput
// anyway, and DirectInput had a bunch of other layers that we don't
// need.  Those other layers are the reason Microsoft deprecated DI in
// the first place: they decided that the threading and buffering model
// that DI used to provide a higher-level interface was too duplicative
// of threading and buffering that applications had to do anyway, even
// with DI's layers included, so removing DI lets applications use
// their own purpose-built (and thus presumably more efficient) models
// for that part of it.  It would have been nice if they'd come up
// with some meet-in-the-middle approach that discarded the DI threading
// and buffering but retained its packet decoders, but alas, I guess 
// this is one of those be-careful-what-you-ask-for deals: "So, you game
// developers want more bare-metal access?  Let's see how you like ALL
// THE BARE METAL IN THE WORLD?!?!?  BWHA HA HA HA HA HA!"


#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <Hidsdi.h>

// Joystick manager.
class JoystickManager
{
	friend class InputManager;

public:
	// forward declarations
	struct Joystick;
	struct LogicalJoystick;
	struct PhysicalJoystick;

	// config variable: remember the joystick associated with each button
	static const TCHAR *cv_RememberJSButtonSource;

	// initialize the global singleton instance
	static bool Init();

	// shut down - delete the global singleton
	static void Shutdown();

	// get the global singleton instance
	static JoystickManager *GetInstance() { return inst; }

	// Joystick event subscriber interface.  Implement this if
	// you wish to subscribe to joystick events.
	class JoystickEventReceiver
	{
	public:
		virtual ~JoystickEventReceiver()
		{
			if (JoystickManager::GetInstance() != 0)
				JoystickManager::GetInstance()->UnsubscribeJoystickEvents(this);
		}

		// Joystick button state change.  Returns true if the event
		// is fully consumed; this prevents other subscribers from
		// receiving the event.
		virtual bool OnJoystickButtonChange(PhysicalJoystick *js, 
			int button, bool pressed, bool foreground) 
		    { return false; }

		// Joystick added.  Called when a physical joystick is added to
		// the system (which usually means that the user plugged it in).
		// 'logicalIsNew' is true if this also represents a new logical
		// device.
		virtual void OnJoystickAdded(PhysicalJoystick *js, bool logicalIsNew) { }

		// Joystick removed.
		virtual void OnJoystickRemoved(PhysicalJoystick *js) { }
	};

	// Subscribe/unsubscribe for joystick events.  Subscribing
	// adds the receiver at the head of the list, so the latest
	// subscriber is first in line for event dispatch.
	void SubscribeJoystickEvents(JoystickEventReceiver *receiver);
	void UnsubscribeJoystickEvents(JoystickEventReceiver *receiver);

	// Get the logical joystick object for a given logical unit number
	LogicalJoystick *GetLogicalJoystick(int index);

	// Get the number of logical joysticks currently in the system
	size_t GetLogicalJoystickCount() const { return logicalJoysticks.size(); }

	// Add a logical joystick representing a physical joystick
	// type.  If there's already an entry that matches, we'll
	// return it, otherwise we'll create a new entry.
	LogicalJoystick *AddLogicalJoystick(int vendorID, int productID, const TCHAR *prodName);
	LogicalJoystick *AddLogicalJoystick(const PhysicalJoystick *jsPhys);

	// Joystick description.  This is the base class for physical
	// and logical joystick records.  (A physical joystick object
	// represents an actual device found attached to the system.
	// A logical joystick object represents a joystick with
	// assigned commands in the configuration.)
	struct Joystick
	{
		Joystick(int vendorID, int productID, const TCHAR *prodName)
			: vendorID(vendorID), productID(productID), prodName(prodName)
		{
			nButtons = 0;
			for (size_t i = 0; i < countof(val); ++i)
				val[i] = 0;
		}

		~Joystick() { }

		// Product name.  This is the product name string that the 
		// device reports in its HID descriptor, if available.
		TSTRING prodName;

		// USB vendor and product IDs
		int vendorID;
		int productID;

		// Button states.  This is simply indexed by the
		// nominal button number as labeled in the USB HID
		// descriptors, so we have to allocate an array of 
		// size Max Button Index + 1.  Note that the USB
		// spec allows for non-contiguous numbering, so this
		// allocation scheme could conceivably waste a lot
		// of memory if we were to encounter a pathological
		// case where a device had one button labeled '65535'
		// or something like that.  But the HID specs
		// recommend contiguous numbering from 1 even though
		// the descriptor scheme doesn't structurally require
		// it, and I doubt that anyone would do anything else
		// in practice since there's no obvious reason to.
		// So I'm going to keep this easy and efficient and
		// assume everyone plays nice.  If we ever do
		// encounter a device that makes this assumption
		// a problem, we can change this to a map.
		int nButtons;
		std::unique_ptr<BYTE> buttonState;

		// Is a button pressed?
		bool IsButtonPressed(int button) const
		{
			return button >= 0 && button < nButtons && buttonState.get()[button] != 0;
		}

		// Control value usages.  These are the usage IDs
		// in the HID Generic Desktop Page for the joystick
		// controls we're interested in.
		static const USAGE iValFirst = 0x30;		// first in our range
		static const USAGE iX = 0x30;				// X axis
		static const USAGE iY = 0x31;				// Y axis
		static const USAGE iZ = 0x32;				// Z axis
		static const USAGE iRX = 0x33;				// X rotation
		static const USAGE iRY = 0x34;				// Y rotation
		static const USAGE iRZ = 0x35;				// Y rotation
		static const USAGE iSlider = 0x36;			// Slider
		static const USAGE iDial = 0x37;			// Dial
		static const USAGE iWheel = 0x38;			// Wheel
		static const USAGE iHat = 0x39;				// Hat switch
		static const USAGE iValLast = 0x39;			// last in our range

		// Current control values.  These slots contain the
		// latest values reported by the device for the 
		// relevant usages.
		//
		// We take advantage of the way the HID usages for our
		// inputs  of interest are all nicely grouped together
		// starting at 0x30.  To get the value for a particular
		// axis, use val[USAGE - iValFirst], where USAGE is one of
		// the values listed above from iX==0x30 to iHat==0x39.
		LONG val[iValLast - iValFirst + 1];
	};

	// Logical joystick descriptor
	struct LogicalJoystick : Joystick
	{
		LogicalJoystick(int index, int vendorID, int productID, const TCHAR *prodName)
			: Joystick(vendorID, productID, prodName), index(index)
		{
		}

		// Index in the joystick vector.  This serves as a proxy
		// for the GUID for the duration of the session, since vector
		// entries are never removed.
		int index;
	};

	// Physical joystick descriptor.  We create one of these for each 
	// joystick device that Windows reports in a device scan.  (We call
	// these "physical" joysticks because they usually correspond
	// directly to physical devices attached to the system.  That's not 
	// exactly true, though, since what we're really talking about is
	// what Windows thinks of as a physical device, and Windows has
	// several ways to virtualize these supposedly physical interfaces.
	// For example, we could be seeing a virtual device created by a 
	// purely software device driver like vJoy, or we could be seeing
	// one USB HID interface presented by a physical device that has
	// multiple HID interfaces and thus looks like multiple devices.
	// So a better name for this might be "WindowsJoystickDevice" or
	// something like that.  But "physical" is clearer and is pretty
	// close to the truth in most cases.)
	struct PhysicalJoystick : Joystick
	{
		PhysicalJoystick(
			HANDLE hRawDevice, const TCHAR *rawDeviceName,
			int vendorID, int productID, const TCHAR *prodName);

		// Raw device handle 
		HANDLE hRawDevice;

		// Raw Input device name.  This is the device name reported
		// by the Raw Input API during device enumeration.  
		// 
		// The device name also happens to be a pseudo file system 
		// path that can be used to open the device in the HidD API.
		// (This is undocumented but seems to be consistent across all
		// Windows versions, and is widely mentioned on the Internet,
		// so perhaps its not too much of a stretch to consider it a
		// de facto API feature despite being undocumented.)  
		//
		// The name also happens to be unique within the local machine
		// (that's a feature of it being a pseudo file system path),
		// and happens to be stable across sessions and reboots (that's
		// not a feature of the pseudo path aspect, but is a separate
		// detail of the implementation that's consistent across
		// all Windows versions).  That means that we can use it as
		// a persistent identifier in saved config data.
		TSTRING rawDeviceName;

		// Mapped logical joystick.  This is the configuration
		// joystick object that handles command inputs from this
		// physical unit.
		LogicalJoystick *logjs;

		// Preparsed data from the HID descriptors.  We need to hold
		// onto this because we have to pass it to the HidP report
		// parser APIs each time we want to parse a report.
		std::unique_ptr<BYTE> ppData;

		// Button report descriptor list.  This collates all
		// HIDP_BUTTON_CAPS items for a given report ID.  When
		// we receive an input report, we find the ButtonReportGroup
		// object matching the report ID, then we parse the button
		// states in the report against the previous states for
		// the same report type.  
		//
		// We have to group things this curious way because of
		// the way Windows HidP tells us about button states in
		// its input report parser.  HidP only tells us the ON
		// buttons in a given report.  That means that all 
		// buttons NOT mentioned in the HidP data are OFF.  But
		// that doesn't mean ALL other buttons are off - it only
		// means the buttons *covered by the report type* are
		// off.  That's where this structure comes in.  It tells
		// us which buttons are covered by the report type.  
		//
		// The real point of this structure is to make report
		// processing fast.  Windows HidP gives us a list of
		// ON buttons, leaving us to infer the OFF buttons.
		// The obvious but inefficient way to do that is to
		// visit all of the buttons, testing each one to see
		// if it was mentioned in the report.  This is slow
		// because there are usually many buttons in a 
		// joystick but only a very small number that are
		// actually ON at any given time - probably one or
		// two at most.  It's therefore much more efficient
		// if we can limit our scan on each report to only
		// the buttons that either are or were on.  That's
		// waht this structure is for.  For each report type,
		// it keeps track of the list of buttons that were ON
		// ON in the last report of the type.  To figure out
		// which buttons are newly OFF, we need only look at
		// the old On list to see if they're also in the new
		// On list.  So if two buttons were on, we only have 
		// to look at two buttons, not all 24 or 32 or 
		// whatever.  To further reduce overhead, the struct
		// has space for two button lists: the old one and
		// the new one.  On each report, we read the new one
		// into the new slot, then swap the flag that says
		// which slot is old and which is new.  We don't have
		// to do any array copying aside from the unavoidable
		// copy step that the HidP API does.
		//
		struct ButtonReportGroup
		{
			ButtonReportGroup(BYTE reportId, int usagePage)
				: reportId(reportId), usagePage(usagePage)
			{
				nButtons = 0;
				lastOnIndex = 0;
			}

			// USB report ID.  This button report group object
			// contains all of the buttons that will be reported
			// by all HIDP_BUTTON_C
			BYTE reportId;

			// USB HID usage page for the Usage items we handle.
			// Since this handler is explicitly and exclusively
			// for buttons, I'm pretty sure our Usage Page will
			// always be 0x09, "Buttons Page" from the USB HID 
			// spec.  But I'm hedging this assumption by storing 
			// it explicitly, just in case I'm being too
			// blinkered in my reading of the rather voluminous
			// and complex HID spec.
			int usagePage;

			// The number of buttons covered by this report type.
			// This isn't necessarily the same as the total number
			// of buttons in the device, since a given report type
			// could cover only a subset of buttons.
			int nButtons;

			// ON button lists.  At any given time, one of these
			// is for the LAST report and the other is for the
			// NEXT report, as indicated by lastOnList.
			struct OnList
			{
				OnList() { nOn = 0; }

				// number of ON buttons in this list
				int nOn;

				// "Usages" for the ON buttons in this list.  This
				// list is ALLOCATED at size 'nButtons', since we
				// assume that a given report can contain at most
				// one entry for each button in the report type.
				// Only 'nOn' entries are current in use, though.
				//
				// A "usage" is simply the button number of an ON
				// button.  So if nOn == 2, usage[0] == 6, and
				// usage[1] == 9, buttons 6 and 9 are ON and all
				// other buttons covered by this report type are
				// OFF.
				std::unique_ptr<USAGE> usage;
			};
			OnList on[2];

			// The LAST OnList: 
			//    0 -> on[0] is LAST and on[1] is NEXT
			//    1 -> on[1] is LAST and on[0] is NEXT
			int lastOnIndex;

			// allocate the ON lists
			void AllocOnLists()
			{
				on[0].usage.reset(new USAGE[nButtons]);
				on[1].usage.reset(new USAGE[nButtons]);
			}

			// Usage value descriptor.  For each usage value that
			// appears under this report ID, we create a value
			// here.  When we receive a report of this type, we
			// iterate over the descriptors to retrieve the values
			// from the report and update our internal value slots.
			struct UsageValueDesc
			{
				UsageValueDesc(USAGE usagePage, USAGE usage)
					: usagePage(usagePage), usage(usage) { }

				USAGE usagePage;
				USAGE usage;
			};
			std::vector<UsageValueDesc> usageVal;
		};

		// Button report groups.  There's one per report ID listed 
		// in the button capabilities lists.
		std::list<ButtonReportGroup> buttonReportGroups;

		// get the button report group object for a given report ID,
		// creating a new one if necessary
		ButtonReportGroup *GetButtonReportGroup(int reportID, USAGE usagePage);

		// Process raw input data.  The main joystick manager
		// calls this when it receives a raw input message
		// that matches our Raw Input device handle.  We parse
		// the message, update our button and axis states, and
		// call the main joystick manager to fire off any
		// resulting joystick state change events to event
		// subscribers.
		void ProcessRawInput(UINT rawInputCode, RAWINPUT *raw);
	};

	// Process a raw input event.  The main Input Manager raw
	// input handler calls this when it receives a packet that
	// looks like it could be a joystick message.  We look for
	// a physical joystick in our list that matches the handle, 
	// and if we find one, we forward the message there for
	// parsing and processing.
	void ProcessRawInput(UINT rawInputCode, HANDLE hRawInput, RAWINPUT *raw);

protected:
	JoystickManager();
	~JoystickManager();

	// Add a new physical device.  This is called during device
	// discovery for each joystick found in the system, and any
	// time a WM_INPUT_DEVICE_CHANGE notifies us of a new joystick
	// being attached dynamically.
	void AddDevice(HANDLE hDevice, const RID_DEVICE_INFO_HID *ridHidInfo);

	// Remove a joystick from the system.  This is called when a
	// WM_INPUT_DEVICE_CHANGE event notifies us that an existing
	// joystick has been removed.  Note that no device information
	// is available during removal, so we have to check every
	// removal against the joystick list whether the device being
	// removed was actually a joystick or not.  This routine can
	// therefore be called with handles that refer to other types
	// of devices, so it has to simply ignore handles that aren't
	// in its current list.
	void RemoveDevice(HANDLE hDevice);

	// singleton instance
	static JoystickManager *inst;

	// Attached joysticks, keyed by raw input handle
	std::unordered_map<HANDLE, PhysicalJoystick> physJoysticks;

	// Send a button press/release event to subscribers.  'foreground' is 
	// true if the event occurred while the application was in the foreground;
	// otherwise it means the button change happened while we were in the 
	// background.
	void SendButtonEvent(PhysicalJoystick *js, int button, bool pressed, bool foreground);

	// joystick event subscribers
	std::list<JoystickEventReceiver *> eventReceivers;

	// Logical Joystick list.  This contains logical versions of
	// the current set of physical joysticks, plus entries for
	// any joysticks referenced in the configuration that aren't
	// currently attached.
	//
	// Each logical joystick's index value is the index in this
	// list.
	std::list<LogicalJoystick> logicalJoysticks;
};

