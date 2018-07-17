// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "InputManager.h"
#include "KeyInput.h"
#include "Joystick.h"

InputManager *InputManager::inst = 0;

bool InputManager::Init(InputManager *singleton)
{
	// wrap the singleton in a unique_ptr<> so that it gets delete
	// if we don't end up assuming ownership
	auto deleter = [](InputManager *p) { delete p; };
	std::unique_ptr<InputManager, decltype(deleter)> pSingleton(singleton, deleter);

	// if there's already an instance, simply return success
	if (inst != nullptr)
		return true;

	// initialize the keyboard input manager
	if (!KeyInput::Init())
		return false;

	// initialize the joystick input manager
	if (!JoystickManager::Init())
		return false;

	// If the caller provided an instance, use it, assuming ownership
	// of the pointer.  If not, create a new instance of the base class.
	inst = pSingleton != nullptr ? pSingleton.release() : new InputManager();

	// do initial device discover
	inst->DiscoverRawInputDevices();

	// success
	return true;
}

void InputManager::Shutdown()
{
	// shut down the individual input subsystems
	KeyInput::Shutdown();
	JoystickManager::Shutdown();

	// delete our instance
	delete inst;
	inst = 0;
}

InputManager::InputManager()
{
	// Raw input isn't yet initialized
	rawInputHWnd = 0;

	// Command list.  This defines the set of commands that can be
	// activated with keys and joystick buttons.  
	//
	// IMPORTANT:  The order of items in this list has dependencies,
	// so don't change it unless you also change the dependent items:
	//
	// - The player button icon image list in the Designer (used in 
	//   the button mapping dialog UI) has to be arranged in the same
	//   order as the commands here.
	//
	// If you only want to change the order of the items displayed
	// in the button mapping dialog list, you can do that by changing
	// the uiSortOrder element.  That establishes the default sorting
	// order independently of the array order.  The uiSortOrder values
	// are only meaningful as far as their relative order, so those
	// can be changed freely.  The sorting order given here is purely
	// for ease of navigation in the dialog UI.  We try to group 
	// buttons by function and put related buttons in an intuitive 
	// order.
	struct cmd
	{
		const TCHAR *name;
		const TCHAR *configID;
		int uiSortOrder;
		int defaultKey;
	};
	static const cmd c[] =
	{
		{ _T("Select/Enter"),       _T("Select"),          100,  '1' },
		{ _T("Cancel/Escape"),      _T("Exit"),            200,  VK_ESCAPE },
		{ _T("Exit Game"),          _T("ExitGame"),        300,  VK_ESCAPE },
		{ _T("Next"),               _T("Next"),            400,  VK_RSHIFT },
		{ _T("Previous"),           _T("Prev"),            500,  VK_LSHIFT },
		{ _T("Next Page"),          _T("NextPage"),        600,  VK_RCONTROL },
		{ _T("Previous Page"),      _T("PrevPage"),        700,  VK_LCONTROL },
		{ _T("Launch"),             _T("Launch"),          800,  VK_RETURN },
		{ _T("Information"),        _T("Information"),     900,  '2' },
		{ _T("Instructions"),       _T("Instructions"),   1000,  0 },
		{ _T("Coin 1"),             _T("Coin1"),          2000,  '3' },
		{ _T("Coin 2"),             _T("Coin2"),          2100,  '4' },
		{ _T("Coin 3"),             _T("Coin3"),          2200,  '5' },
		{ _T("Coin 4"),             _T("Coin4"),          2300,  '6' },
		{ _T("Coin Door"),          _T("CoinDoor"),       3000,  VK_END },
		{ _T("Service 1/Escape"),   _T("Service1"),       3100,  '7' },
		{ _T("Service 2/Down"),     _T("Service2"),       3200,  '8' },
		{ _T("Service 3/Up"),       _T("Service3"),       3300,  '9' },
		{ _T("Service 4/Enter"),    _T("Service4"),       3400,  '0' },
		{ _T("Frame Counter"),      _T("FrameCounter"),   4000,  VK_F11 },
		{ _T("Full Screen Toggle"), _T("FullScreen"),     4100,  VK_F12 },
		{ _T("Settings"),           _T("Settings"),       4200,  'O' },
		{ _T("Rotate Monitor"),		_T("RotateMonitor"),  4300,  VK_MULTIPLY }
	};

	// add the commands to the list
	for (int i = 0; i < countof(c); ++i)
	{
		// add the command
		commands.emplace_back(i, c[i].name, c[i].configID, c[i].uiSortOrder, c[i].defaultKey);

		// add the default key mapping
		if (c[i].defaultKey != 0)
			commands.back().buttons.emplace_back(Button::TypeKB, 0, c[i].defaultKey);
	}
}

InputManager::~InputManager() 
{
}

// Initialize raw input
bool InputManager::InitRawInput(HWND hwnd)
{
	RAWINPUTDEVICE rd[2];

	// Note: See the USB specification "HID Usage Tables" for the
	// meanings of the Usage Page and Usage codes.  These aren't the
	// usual cryptic numbers assigned by fiat by Microsoft; they're
	// cryptic numbers assigned by fiat by the USB Implementers' 
	// Forum, the industry group that defines the USB standards.

	// Use RIDEV_INPUTSINK so that we receive all input, whether the
	// app is in the foreground or background.  We want background
	// input so that we can monitor for the EXIT key while a table
	// is running in a player process we launch (e.g., VP).  We want
	// background input on both the keyboard and joystick so that 
	// either type of device can be used for the EXIT key.

	// joysticks
	rd[0].usUsagePage = 1;				// "Generic Desktop"
	rd[0].usUsage = 4;					// joysticks
	rd[0].dwFlags = RIDEV_DEVNOTIFY 	// ask for WM_INPUT_DEVICE_CHANGE notifications
		| RIDEV_INPUTSINK;				// get input whether in foreground or background
	rd[0].hwndTarget = hwnd;

	// Keyboard.
	rd[1].usUsagePage = 1;				// "Generic Desktop"
	rd[1].usUsage = 6;					// keyboards
	rd[1].dwFlags = RIDEV_INPUTSINK;	// get input whether in foreground or background
	rd[1].hwndTarget = hwnd;

	// do the registration
	if (!RegisterRawInputDevices(rd, countof(rd), sizeof(rd[0])))
	{
		LogSysError(EIT_Error,
			_T("Unable to set up joystick access.  You might ")
			_T("need to close other programs."),
			MsgFmt(_T("RegisterRawInputDevices failed, system error %ld"), GetLastError()));
		return false;
	}

	// remember the handler window
	rawInputHWnd = hwnd;

	// success
	return true;
}

void InputManager::UninitRawInput()
{
	RAWINPUTDEVICE rd[2];

	// joysticks
	rd[0].usUsagePage = 1;				// "Generic Desktop"
	rd[0].usUsage = 4;					// joysticks
	rd[0].dwFlags = RIDEV_REMOVE;		// stop monitoring input
	rd[0].hwndTarget = NULL;

	// Keyboard.
	rd[1].usUsagePage = 1;				// "Generic Desktop"
	rd[1].usUsage = 6;					// keyboards
	rd[1].dwFlags = RIDEV_REMOVE;		// stop monitoring input
	rd[1].hwndTarget = NULL;

	// do the registration
	RegisterRawInputDevices(rd, countof(rd), sizeof(rd[0]));
}

void InputManager::ProcessRawInput(UINT rawInputCode, HRAWINPUT hRawInput)
{
	// assume we'll apply the default processing
	bool callDefProc = true;

	// determine the size of the input buffer
	UINT dwSize;
	GetRawInputData(hRawInput, RID_INPUT, 0, &dwSize, sizeof(RAWINPUTHEADER));

	// allocate a buffer (and ignore the message if we don't save space)
	std::unique_ptr<BYTE> buf(new (std::nothrow) BYTE[dwSize]);
	if (buf.get() == 0)
		return;

	// Read the data.  If it doesn't come back at the expected size, 
	// ignore the message.
	if (GetRawInputData(hRawInput, RID_INPUT, buf.get(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize)
		return;

	// get it as a RAWINPUT struct
	RAWINPUT *raw = (RAWINPUT *)buf.get();

	// if it's a HID input, send it to the joystick manager
	if (raw->header.dwType == RIM_TYPEHID)
		JoystickManager::GetInstance()->ProcessRawInput(rawInputCode, raw->header.hDevice, raw);

	// forward the event to raw input subscribers
	for (auto& sub : rawInputSubscribers)
	{
		// send it to this subscriber
		if (sub->OnRawInputEvent(rawInputCode, raw, dwSize))
		{
			// The subscriber fully handled the message, meaning it
			// doesn't want other subscribers to see it.  Exit the
			// forwarding loop.
			break;
		}
	}
}

void InputManager::DiscoverRawInputDevices()
{
	// Find out how many raw input devices are in the system
	UINT numDevices = 0;
	if (GetRawInputDeviceList(0, &numDevices, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1
		&& GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		return;

	// Allocate space for the list
	std::unique_ptr<RAWINPUTDEVICELIST> buf(new (std::nothrow) RAWINPUTDEVICELIST[numDevices]);
	if (buf.get() == 0)
		return;

	// Retrieve the device list; fail if that doesn't return the expected device count
	UINT numActual = numDevices;
	if (GetRawInputDeviceList(buf.get(), &numActual, sizeof(RAWINPUTDEVICELIST)) != numDevices)
		return;

	// Clear any previous joystick device list
	JoystickManager::GetInstance()->physJoysticks.clear();

	// process the list
	RAWINPUTDEVICELIST *r = buf.get();
	for (UINT i = 0; i < numDevices; ++i, ++r)
		AddRawInputDevice(r->hDevice);
}

void InputManager::ProcessDeviceChange(USHORT what, HANDLE hDevice)
{
	// Check what happened
	switch (what)
	{
	case GIDC_ARRIVAL:
		// add the device
		AddRawInputDevice(hDevice);
		break;

	case GIDC_REMOVAL:
		// remove the device
		RemoveRawInputDevice(hDevice);
		break;
	}
}

void InputManager::AddRawInputDevice(HANDLE hDevice)
{
	// retrieve the device information
	RID_DEVICE_INFO info;
	UINT sz = info.cbSize = sizeof(info);
	if (GetRawInputDeviceInfo(hDevice, RIDI_DEVICEINFO, &info, &sz) != (UINT)-1)
	{
		// check the type
		switch (info.dwType)
		{
		case RIM_TYPEHID:
			// HID.  This is the generic Raw Input type code for 
			// anything that's not a keyboard or mouse.  Check the
			// HID usage codes to see if it's a device type we
			// recognize.  The types we recognize are:
			//
			//   Usage Page 0x01, Usage 0x04 => Joystick
			//
			if (info.hid.usUsagePage == 1 && info.hid.usUsage == 4)
			{
				// It's a joystick.  Add it through the joystick
				// manager.
				JoystickManager::GetInstance()->AddDevice(hDevice, &info.hid);
			}
			break;

		case RIM_TYPEKEYBOARD:
		case RIM_TYPEMOUSE:
			// We don't need to track these devices individually,
			// so ignore the device entry.
			break;
		}
	}
}

void InputManager::RemoveRawInputDevice(HANDLE hDevice)
{
	// Raw Input doesn't let us query device information during
	// removal, so there's no way to determine what type of 
	// device this is.  We just have to try removing it from
	// each internal list that might be storing the handle.
	
	// remove it from the joystick list, if it's there
	JoystickManager::GetInstance()->RemoveDevice(hDevice);
}

void InputManager::SubscribeRawInput(RawInputReceiver *receiver)
{
	rawInputSubscribers.emplace_front(receiver);
}

void InputManager::UnsubscribeRawInput(RawInputReceiver *receiver)
{
	rawInputSubscribers.remove(receiver);
}

void InputManager::EnumCommands(std::function<void(const Command &cmd)> callback)
{
	for (auto const& cmd : commands)
		callback(cmd);
}

void InputManager::EnumButtons(std::function<void(const Command &cmd, const Button &btn)> callback)
{
	EnumCommands([callback](const Command &cmd) 
	{
		for (auto const& btn : cmd.buttons)
			callback(cmd, btn);
	});
}

void InputManager::ClearCommandKeys(int commandIndex)
{
	if (commandIndex >= 0 && commandIndex < (int)commands.size())
		commands[commandIndex].buttons.clear();
}

void InputManager::AddCommandKey(int commandIndex, Button &button)
{
	if (commandIndex >= 0 && commandIndex < (int)commands.size())
		commands[commandIndex].buttons.emplace_back(button);
}

