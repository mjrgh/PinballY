// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Input Manager
// 
// This class defines the abstract input model for the keyboard and
// joystick.  We use the services of the KeyInput and JoystickManager
// classes for the hardware specifics.

#pragma once
#include "Joystick.h"


class InputManager
{
	friend class JoystickManager;

public:
	// Create and initialize the global singleton instance.  The
	// caller can provide a singleton if desired, which can be a
	// custom subclass.  If this is null, we'll simply create
	// a new instance of the base InputManager.
	static bool Init(InputManager *singleton = nullptr);

	// destroy the global singleton on program exit
	static void Shutdown();

	// get the global singleton
	static InputManager *GetInstance() { return inst; }

	// Get the Windows keyboard auto-repeat parameters
	struct KbAutoRepeat
	{
		// delay between key-press and first repeat, in milliseconds
		int delay;

		// interval between auto-repeats, in milliseconds
		int interval;
	};
	static KbAutoRepeat GetKeyboardAutoRepeatSettings();

	// Initialize the Raw Input subsystem.  The main window
	// must call this during program startup.  We use Raw
	// Input to handle joystick input.  (Keyboard and mouse
	// are handled through regular Windows events.)
	bool InitRawInput(HWND hwnd);

	// Unitialize raw input
	void UninitRawInput();

	// Is raw input initialized?
	bool IsRawInputInitialized() const { return rawInputHWnd != 0; }

	// Process raw input.  The main window calls this on
	// receiving a WM_INPUT message to process the input.
	// (Note that the arguments are directly from the WM_INPUT
	// message parameters: rimType is the WPARAM, hRawInput
	// is the LPARAM.)  The caller must always call the
	// DefWindowProc after calling this, since that performs
	// required cleanup on the input buffer data.
	void ProcessRawInput(UINT rimType, HRAWINPUT hRawInput);

	// Process a device change notification.  The main window
	// calls this on receiving a WM_INPUT_DEVICE_CHANGE message.
	void ProcessDeviceChange(USHORT what, HANDLE hDevice);

	// Raw input subscriber.  A class that wants to process
	// raw input events can implement this interface and then
	// subscribe for events as needed.
	class RawInputReceiver
	{
	public:
		virtual ~RawInputReceiver()
		{
			// make sure we're unsubscribed before the object is destroyed
			InputManager::GetInstance()->UnsubscribeRawInput(this);
		}

		// Handle a raw input event.  Returns true if the
		// subscriber fully handles the event; this prevents
		// the event from being passed to other subscribers
		// in the list.  Returns false to forward the event
		// to the next subscriber.
		virtual bool OnRawInputEvent(UINT rawInputCode, RAWINPUT *raw, DWORD dwSize) = 0;

		// Keyboard auto-repeat event flag
		//
		// For raw input keyboard events, we set this special,
		// private bit to the RAWKEYBOARD::Flags element, to represent
		// the auto-repeat state of the key.
		//
		// This bit is chosen so that it doesn't overlap any of the 
		// bits currently defined in the Windows headers, so it doesn't
		// conflict with any information that Windows is passing us
		// in the RAWKEYBOARD Flags.  We always overwrite this bit
		// with the auto-repeat status, so even if a future Windows
		// version defines this same bit for some new purposes, it
		// won't create a conflict, *unless* we want to access the
		// new information represented by the new Windows bits.  If
		// that ever happens, we can redefine this to some other,
		// still unused bit (or, if no unused bits are available, to
		// some other Windows-defined bit that we never need to access
		// in this application).
		static const USHORT RI_KEY_AUTOREPEAT = 0x0800;
	};

	// Subscribe to raw input events.  This adds the given
	// object at the head of the subscription list, so it will
	// be first in line to receive events.
	void SubscribeRawInput(RawInputReceiver *receiver);

	// Unsubscribe to raw input events
	void UnsubscribeRawInput(RawInputReceiver *receiver);

	// Translate virtual key codes in a raw input keyboard event:
	//
	// - Translate VK_SHIFT into VK_RSHIFT or VK_LSHIFT
	// - Translate VK_CONTROL into VK_LCONTROL or VK_RCONTROL
	// - Translate VK_MENU into VK_LMENU or VK_RMENU
	// - Translate keypad Enter into VKE_KEYPAD_ENTER 
	// - Translate keypad '+' into VKE_KEYPAD_PLUS
	// - Translate keypad ',' into VKE_KEYPAD_COMMA
	//
	USHORT TranslateVKey(const RAWINPUT *raw) const;

	// Translate a raw input keyboard scan code (from the "MakeCode"
	// field of the RAWKEYBOARD struct) from the hardware scan code
	// to the "soft" scan code that Windows uses.  This applies the
	// scan code mapping from the registry (HKEY_LOCAL_MACHINE\SYSTEM\
	// CurrentControlSet\Control\Keyboard Layout[Scancode Map]), which
	// some users use to remap selected keyboard keys.  Raw input
	// reports scan codes using the hardware scan codes without any
	// translation, which is useful if you want to know the true key
	// pressed, but doesn't always correspond to the way Windows will
	// interpret the key in WM_KEYxxx messages.  This does the same
	// mapping that Windows will do to get the soft key that the 
	// regular WM_KEYxxx messages report.
	//
	// If the key uses an extended scan code with E0 or E1 prefix,
	// the prefix is returned in the high byte of the return value.
	// E.g., "right control" return 0xE01D.
	USHORT TranslateScanCode(const RAWINPUT *raw) const;

	// Key/button object.  This represents one input device
	// button, which can be either a key on the keyboard or a
	// button on a joystick.
	struct Button
	{
		// Source device type
		enum class DevType
		{
			TypeNone = 0,		// no device - placeholder button
			TypeKB = 1,			// keyboard key
			TypeJS = 2			// joystick button
		};
		DevType devType;

		Button(DevType devType, int unit, int code)
			: devType(devType), unit(unit), code(code) { }

		Button(const Button &b) :
			devType(b.devType), unit(b.unit), code(b.code) { }

		// Unit number:
		//
		// - For keyboards, this is always 0 (and is ignored anyway),
		//   since we don't distinguish among keyboards.  We use the
		//   basic Windows handling, which merges all keyboard input
		//   into one logical keyboard.
		//
		// - For joysticks, this is the ID of a LogicalJoystick
		//   object.  -1 means that the button isn't assigned to
		//   a particular joystick, so it'll match a button press
		//   of the given button number on any joystick.
		//
		int unit;

		// Key/button code:
		//
		// - For keyboards, this is a VK_xxx or VKE_xxx code
		//
		// - For joysticks, this is the button number from the 
		//   joystick's HID report.  The HID report buttons use a
		//   zero-based index, so note that we add one to the HID
		//   index when displaying the button number in the UI,
		//   since that's the convention that Windows itself uses
		//   when referring to the buttons in the UI.
		//
		int code;
	};

	// Command object.  This represents an operation that can
	// be assigned to a keyboard key or joystick button.
	struct Command
	{
		Command(int idx, const TCHAR *name, const TCHAR *configID, int uiSortOrder, int defaultKey)
			: idx(idx), name(name), configID(configID), 
			uiSortOrder(uiSortOrder), defaultKey(defaultKey)
		{ 
		}

		// Command index.  We number the commands contiguously
		// from 0, so this can be used as an index into separate
		// arrays containing information related to commands.
		int idx;

		// Name of the command, for display purposes in the UI
		// (e.g., for the keyboard preferences dialog)
		const TCHAR *name;

		// Configuration ID
		const TCHAR *configID;

		// Get the full config ID.  This adds our config ID prefix.
		TSTRING GetConfigID() const
		{
			TSTRING s(_T("Buttons."));
			s.append(configID);
			return s;
		}

		// UI sort order.  This specifies the relative order of items
		// for display purposes, such as in key assignment dialogs.
		// This value is meaningful only for comparison with other
		// elements; it's otherwise arbitrary.  To sort in the order
		// specified here, simply sort the list in ascending order
		// of uiSortOrder values.
		int uiSortOrder;

		// Default key assignment for the command, as a VK_xxx code
		int defaultKey;

		// The keys/buttons associated with the command
		std::list<Button> buttons;
	};

	// Enumerate the commands
	void EnumCommands(std::function<void(const Command &cmd)> callback);

	// Enumerate the key assignments
	void EnumButtons(std::function<void(const Command &cmd, const Button &btn)> callback);

	// Clear all key/button assignments for a command
	void ClearCommandKeys(int commandIndex);

	// Add a key/button assignment for a command.  This adds
	// the given key to the set for the given command.
	void AddCommandKey(int commandIndex, Button &button);

protected:
	// global singleton instance
	static InputManager *inst;

	// external callers use the singleton instance, so the
	// constructor and destructor are only called internally
	// in our Init/Shutdown methods
	InputManager();
	virtual ~InputManager();

	// Perform device discovery
	void DiscoverRawInputDevices();

	// Add a device.  This is called during discovery for
	// each handle in the Raw Input device list, and again
	// when we receive a WM_INPUT_DEVICE_CHANGE with the
	// GIDC_ARRIVAL code.
	void AddRawInputDevice(HANDLE hRawInputDevice);

	// Remove a raw input device.  This is called when we
	// get a WM_INPUT_DEVICE_CHANGE with the GIDC_REMOVAL
	// code.
	void RemoveRawInputDevice(HANDLE hRawInputDevice);

	// Command list
	std::vector<Command> commands;

	// Raw input receiver list
	std::list<RawInputReceiver *> rawInputSubscribers;

	// raw input message handler window
	HWND rawInputHWnd;

	// Scan code map from the system registry.  This contains the data from
	// HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Keyboard Layout\Scancode Map,
	// arranged into a map keyed by hardware scan code and yielding the soft
	// scan code that Windows will use for the key.
	//
	// We need this to properly translate raw input keyboard events, because
	// the raw input data reports the original hardware scan code, and we
	// want to respect the user's soft key mappings.
	std::unordered_map<USHORT, USHORT> scancodeMap;

	// Map of keys that are currently down.  We use this in the raw input
	// processor to determine if a "make" code is an auto-repeat key.  This
	// is called during background processing while a game is running, since
	// we have to intercept keystrokes in the background to monitor the
	// keyboard for the Exit Game key, so it's important to have extremely
	// low overhead.  We therefore use an array rather than a map.  More
	// specifically, we use two arrays: one for regular keys, and one for
	// keys with the 0xE0 prefix.  We don't track keys with 0xE1 prefix,
	// since those are a few unusual keys that don't ever send "break" 
	// codes when released, and thus can't be meaningfully tracked for
	// up/down status.
	BYTE keyDown[256];           // regular keys (no E0 prefix)
	BYTE extKeyDown[256];        // extended keys (E0 prefix)
};

