// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "InputManager.h"
#include "KeyInput.h"
#include "Joystick.h"
#include "WinUtil.h"

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
		{ _T("Rotate Monitor"),		_T("RotateMonitor"),  4300,  VK_MULTIPLY },
		{ _T("Pause Game"),         _T("PauseGame"),      4400,  0 },
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

	// load the scancode map from the system registry
	HKEYHolder hkey;
	if (RegOpenKey(HKEY_LOCAL_MACHINE, _T("SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout"), &hkey) == ERROR_SUCCESS)
	{
		DWORD typ;
		DWORD cbData = 0;
		if (RegQueryValueEx(hkey, _T("Scancode Map"), NULL, &typ, NULL, &cbData) == ERROR_SUCCESS)
		{
			std::unique_ptr<BYTE> data(new BYTE[cbData]);
			if (RegQueryValueEx(hkey, _T("Scancode Map"), NULL, &typ, data.get(), &cbData) == ERROR_SUCCESS)
			{
				// the scancode map is a binary struct, arranged as follows:
				//
				//  offset  type   description
				//      0   DWORD  header version, always 0
				//      4   DWORD  header flags, always 0
				//      8   DWORD  number of mapping entries
				//     12   WORD   entry 0 "to" scan code (soft key used in Windows when "from" key is pressed)
				//     14   WORD   entry 0 "from" scan code (original hardware scan code of key being remapped)
				//     <repeat pairs of 16-bit WORD entries with from/to pairs>
				//
				DWORD n = *(CONST DWORD *)(data.get() + 8);
				const UINT16 *p = (CONST UINT16 *)(data.get() + 12);
				for (DWORD i = 0; i < n - 1; ++i)
				{
					USHORT to = *(UINT16*)p++;
					USHORT from = *(UINT16*)p++;
					scancodeMap[from] = to;
				}
			}
		}
	}

	// zero the key status maps
	ZeroMemory(keyDown, sizeof(keyDown));
	ZeroMemory(extKeyDown, sizeof(extKeyDown));
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

	// If this is a keyboard event, determine if it's an auto-repeat event.
	// The Raw Input subsystem doesn't track this, so we have to do so
	// explicitly by keeping track of make/break pairs that we see.  Don't
	// track keys with the E1 prefix, as those are a few oddball keys that
	// don't send "break" codes and thus can't meaningfully be tracked for
	// up/down status.
	if (raw->header.dwType == RIM_TYPEKEYBOARD && (raw->data.keyboard.Flags & RI_KEY_E1) == 0)
	{
		// get the key map according to the code type
		auto &rawkb = raw->data.keyboard;
		BYTE *pKeyDown = (rawkb.Flags & RI_KEY_E0) != 0 ? extKeyDown : keyDown;

		// get the scan code, truncating to 8 bits (it should always fit
		// within 8 bytes anyway, but explicitly truncate it just to be
		// certain, since we're going to use it as an array index into
		// a 256-element array)
		USHORT scanCode = rawkb.MakeCode & 0xFF;

		// Clear our private "repeat" bit in the raw input data.  This is
		// meant to be a bit that no version of Windows defines, hence Windows
		// should never set it - but a future version of Windows could define
		// it.  So clear it.  Note that it's still okay if a future Windows
		// version does use this bit, since we always set the bit to the 
		// repeat state and thus it can always be interpreted correctly as
		// the repeat state.  The only downside is that we won't be able
		// to use that future Windows meaning of the bit (should one ever
		// be added), and thus won't be able to access whatever new
		// functionality or information it represents, because we're 
		// overwriting it with the repeat status.  If such a bit is ever
		// added and we actually want to use it, we can do that by moving
		// our private hijacked bit to a new value, assuming Windows 
		// doesn't eventually take over all 16 bits of the Flags element.
		rawkb.Flags &= ~RawInputReceiver::RI_KEY_AUTOREPEAT;

		// Check if this is a "make" or "break".  Note that the RI_KEY_MAKE
		// flag isn't really a bit mask - it's defined in the Windows headers
		// as 0 - so don't try to test for it with "&".  Test for the BREAK
		// bit instead; its absence indicates a "make" event.
		if ((rawkb.Flags & RI_KEY_BREAK) != 0)
		{
			// "break" event - the key is now up
			pKeyDown[scanCode] = 0;
		}
		else
		{
			// "Make" event - the key is now down.  If it was already down, this
			// is a repeat event.  Signify this by adding our private bit to the
			// raw input data.
			if (pKeyDown[scanCode])
				rawkb.Flags |= RawInputReceiver::RI_KEY_AUTOREPEAT;

			// mark the key as down for future repeat events
			pKeyDown[scanCode] = 1;
		}
	}

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

USHORT InputManager::TranslateVKey(const RAWINPUT *raw) const
{
	// note the extended key bit
	bool E0 = ((raw->data.keyboard.Flags & RI_KEY_E0) != 0);

	// check the base vkey
	switch (auto vkey = raw->data.keyboard.VKey)
	{
	case VK_SHIFT:
		// left and right shift keys have distinct scan codes
		return TranslateScanCode(raw) == 0x36 ? VK_RSHIFT : VK_LSHIFT;

	case VK_CONTROL:
		// left and right control keys are distinguished by the E0 bit
		return E0 ? VK_RCONTROL : VK_LCONTROL;

	case VK_MENU:
		// left and right alt keys are distinguished by the E0 bit
		return E0 ? VK_RMENU : VK_LMENU;

	case VK_RETURN:
		// keyboard and keypad Enter keys are distinguished by the E0 bit
		return E0 ? VKE_NUMPAD_ENTER : vkey;

	case VK_OEM_COMMA:
		// keyboard and keypad comma are distinguished by the E0 bit
		return E0 ? VKE_NUMPAD_COMMA : vkey;

	case VK_OEM_PLUS:
		// keyboard and keypad '+' are distinguished by the E0 bit
		return E0 ? VKE_NUMPAD_EQUALS : vkey;

	default:
		return vkey;
	}
}

USHORT InputManager::TranslateScanCode(const RAWINPUT *raw) const
{
	// get the hardware scan code
	USHORT scanCode = raw->data.keyboard.MakeCode;

	// If it's an extended scan code, encode the E0/E1 prefix in the
	// high byte of the scan code.
	if ((raw->data.keyboard.Flags & RI_KEY_E0) != 0)
		scanCode |= 0xE000;
	else if ((raw->data.keyboard.Flags & RI_KEY_E1) != 0)
		scanCode |= 0xE100;

	// if there's a key mapping in the system scancode map, apply it
	if (auto it = scancodeMap.find(scanCode); it != scancodeMap.end())
		scanCode = it->second;

	// return the result
	return scanCode;
}
