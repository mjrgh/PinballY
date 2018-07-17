// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Keyboard & joystick input manager
//
// This class contains the mapping between buttons on input
// devices and the player functions ("commands") they're connected
// to.  The point of this mapping layer is to allow users to
// customize eyboard/joystick assignments.

#pragma once

// Extended virtual keys.  The standard Windows VK_ codes 
// include most of the basic keys, but some of the extended
// keys can't be distinguished from their basic counterparts.
// For example, "Keypad Enter" is VK_RETURN, which is the same
// as the regular Return key.  To distinguish these additional
// keys, we define our own VKE_ codes.  These are all mapped
// into the space above the VK_ set.
const int VKE_NUMPAD_ENTER  = 0x100;	// keypad Enter
const int VKE_NUMPAD_EQUALS = 0x101;	// keypad '='
const int VKE_NUMPAD_COMMA  = 0x102;    // keypad comma
const int VKE_YEN           = 0x103;	// Yen sign (Japanese keyboard, scan code 0x7d)
const int VKE_COLON         = 0x104;    // colon key (scan code 0x92)
const int VKE_UNDERLINE     = 0x105;	// underline key (scan code 0x93)
const int VKE_STOP          = 0x106;    // STOP key (scan code 0x95)
const int VKE_UNLABELED     = 0x107;	// UNLABELED key (scan code 0x97)
const int VKE_CALCULATOR    = 0x108;    // Calculator key (scan code 0xa1)
const int VKE_MYCOMPUTER    = 0x109;    // My Computer key (scan code 0xeb)
const int VKE_POWER         = 0x10A;    // Power key (scan code 0xde)
const int VKE_WAKE          = 0x10B;    // Wake button (scan code 0xe3)
const int VKE_AX            = 0x10C;    // AX button (scan code 0x96)
const int VKE_LAST          = 0x10C;	// last VKE_ code

// Aliases for some of the more obscurely named VK keys
const int VK_SLASH = VK_OEM_2;


class ConfigManager;

class KeyInput
{
public:
	// Initialize.  This creates the global singleton instance.
	// Call once at application startup.  Returns true on success, 
	// false on failure.
	static bool Init();

	// Shut down.  Call before exiting the application to clean
	// up the global singleton instance.
	static void Shutdown();

	// get the global singleton
	static KeyInput *GetInstance() { return inst; }

	// is the given VK_xxx (or VKE_xxx) key code valid?
	static bool IsValidKeyCode(int vk);

	// Process the parameters of a keyboard event (WM_KEYDOWN,
	// WM_SYSKEYDOWN, etc) to identify extended keys.  This
	// changes the modifier keys from the generic form to the
	// left/right differentiated form (VK_LSHIFT, etc), and
	// changes other special keys to VKE_ format (e.g., keypad
	// enter -> VKE_NUMPAD_ENTER).  Returns the translated key
	// code.
	static int TranslateExtKeys(UINT msg, WPARAM wParam, LPARAM lParam);

	// Key names.  This is an array of printable key cap names,
	// indexed by VK_xxx/VKE_xxx codes.
	struct KeyLabel
	{
		// Key ID.  This is a unique and parseing-friendly string
		// identifying the key.  This consists only of alphanumeric
		// characters, so it's suitable parsed contexts like config
		// files.
		const TCHAR *keyID;

		// Friendly name.  This is the familiar name of the key cap
		// for presentation in the UI (e.g., "=" or "Page Up").
		const TCHAR *friendlyName;

		// UI sort key.  This is an integer that can be compared to
		// the sort key for another key label entry to determine the
		// relative order of the entries.  The ordering isn't simply
		// alphabetic or by key code; instead, it groups the keys
		// into logical groups, and orders the keys within the
		// groups.  The main groups are the alphabetic keys, numbers,
		// punctuation marks, cursor keys, "F" keys, numeric keypad,
		// shift keys, and miscellaneous system keys.  Within the
		// groups, the keys are sorted by function if some kind of
		// intuitive ordering applies (e.g., volume up, volume down, 
		// mute; A-Z; 0-9; F1-F15).  (The value is meaningless other
		// than for sorting; it's not related to any of the standard
		// Windows key code schemes or any character set encoding.)
		int sortKey;
	};
	static const KeyLabel keyName[];

	// Look up a key by ID (keyID from the keyName table).  This 
	// returns the virtual key code (VK_xxx or VKE_xxx) for one of 
	// our  key names.  Returns -1 if the name isn't found.
	int KeyByID(const TCHAR *name);

protected:
	KeyInput();
	~KeyInput();

	// global singleton instance
	static KeyInput *inst;

	// Map of internal names from the keyName[] array.  This
	// allows fast lookup of the key names, for parsing config
	// files.
	std::unordered_map<TSTRING, int> keyIdMap;
};
