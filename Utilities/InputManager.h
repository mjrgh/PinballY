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
	};

	// Subscribe to raw input events.  This adds the given
	// object at the head of the subscription list, so it will
	// be first in line to receive events.
	void SubscribeRawInput(RawInputReceiver *receiver);

	// Unsubscribe to raw input events
	void UnsubscribeRawInput(RawInputReceiver *receiver);

	// Key/button object.  This represents one input device
	// button, which can be either a key on the keyboard or a
	// button on a joystick.
	struct Button
	{
		// Source device type
		enum DevType
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
};

