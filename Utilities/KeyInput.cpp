// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Key Input Manager

#include "stdafx.h"
#include <ctype.h>
#include "KeyInput.h"
#include "Joystick.h"

KeyInput *KeyInput::inst = 0;

const KeyInput::KeyLabel KeyInput::keyName[] =
{
	// internal name,     friendly name,       js event.key,        js event.code,        which, loc, UI sort key // vk#  - description
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   60000  },   // 0x00 - no key assigned - sort to end
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50001  },   // 0x01 - VK_LBUTTON - Left mouse button
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50002  },   // 0x02 - VK_RBUTTON - Right mouse button
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50003  },   // 0x03 - VK_CANCEL - Control-break processing
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50004  },   // 0x04 - VK_MBUTTON - Middle mouse button (three-button mouse)
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50005  },   // 0x05 - VK_XBUTTON1 - X1 mouse button
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50006  },   // 0x06 - VK_XBUTTON2 - X2 mouse button
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50007  },   // 0x07 - Undefined
	{ _T("BACK"),         _T("Backspace"),     _T("Backspace"),     _T("Backspace"),      0x08,  0,   300    },   // 0x08 - VK_BACK - BACKSPACE key
	{ _T("TAB"),          _T("Tab"),           _T("Tab"),           _T("Tab"),            0x09,  0,   320    },   // 0x09 - VK_TAB - TAB key
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50008  },   // 0x0a - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50009  },   // 0x0b - Reserved
	{ _T("CLEAR"),        _T("Clear"),         _T("Clear"),         _T("Numpad5"),        0x0c,  3,   859    },   // 0x0c - VK_CLEAR - CLEAR key (keypad 5)
	{ _T("RETURN"),       _T("Return"),        _T("Enter"),         _T("Enter"),          0x0d,  0,   310    },   // 0x0d - VK_RETURN - ENTER key
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50010  },   // 0x0e - Undefined
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50011  },   // 0x0f - Undefined
	{ _T("SHIFT"),        _T("Shift"),         _T("Shift"),         _T("Shift"),          0x10,  0,   5012   },   // 0x10 - VK_SHIFT - SHIFT key
	{ _T("CTRL"),         _T("Control"),       _T("Control"),       _T("Control"),        0x11,  0,   5011   },   // 0x11 - VK_CONTROL - CTRL key
	{ _T("ALT"),          _T("Alt"),           _T("Alt"),           _T("Alt"),            0x12,  0,   5010   },   // 0x12 - VK_MENU - ALT key
	{ _T("PAUSE"),        _T("Pause"),         _T("Pause"),         _T("Pause"),          0x13,  0,   440    },   // 0x13 - VK_PAUSE - PAUSE key
	{ _T("CAPITAL"),      _T("Caps Lock"),     _T("CapsLock"),      _T("CapsLock"),       0x14,  0,   460    },   // 0x14 - VK_CAPITAL - CAPS LOCK key
	{ _T("KANA"),         _T("Kana"),          _T("KanaMode"),      _T("KanaMode"),       0x15,  0,   20000  },   // 0x15 - VK_KANA - IME Kana mode/Hangul mode
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50012  },   // 0x16 - Undefined
	{ _T("JUNJA"),        _T("Junja"),         _T("Lang1"),         _T("Lang1"),          0x17,  0,   20001  },   // 0x17 - VK_JUNJA - IME Junja mode
	{ _T("FINAL"),        _T("Final"),         _T("Final"),         _T("Final"),          0x18,  20002  },   // 0x18 - VK_FINAL - IME final mode
	{ _T("KANJI"),        _T("Kanji"),         _T("Lang2"),         _T("Lang2"),          0x19,  0,   20003  },   // 0x19 - VK_HANJA - IME Hanja mode/Kanji mode
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50013  },   // 0x1a -  Undefined
	{ _T("ESCAPE"),       _T("Esc"),           _T("Escape"),        _T("Escape"),         0x1b,  0,   330    },   // 0x1b - VK_ESCAPE - ESC key
	{ _T("CONVERT"),      _T("Convert"),       _T("Convert"),       _T("Convert"),        0x1c,  0,   20004  },   // 0x1c - VK_CONVERT - IME convert
	{ _T("NOCONVERT"),    _T("NoConvert"),     _T("NonConvert"),    _T("NonConvert"),     0x1d,  0,   20005  },   // 0x1d - VK_NONCONVERT - IME nonconvert
	{ _T("ACCEPT"),       _T("Accept"),        _T("Accept"),        _T("Accept"),         0x1e,  0,   20006  },   // 0x1e - VK_ACCEPT - IME accept
	{ _T("MODECHANGE"),   _T("Mode Change"),   _T("ModeChange"),    _T("ModeChange"),     0x1f,  0,   20007  },   // 0x1f - VK_MODECHANGE - IME mode change request
	{ _T("SPACE"),        _T("Space"),         _T(" "),             _T("Space"),          0x20,  0,   150    },   // 0x20 - VK_SPACE - SPACEBAR
	{ _T("PRIOR"),        _T("Page Up"),       _T("PageUp"),        _T("PageUp"),         0x21,  0,   380    },   // 0x21 - VK_PRIOR - PAGE UP key
	{ _T("NEXT"),         _T("Page Down"),     _T("PageDown"),      _T("PageDown"),       0x22,  0,   390    },   // 0x22 - VK_NEXT - PAGE DOWN key
	{ _T("END"),          _T("End"),           _T("End"),           _T("End"),            0x23,  0,   430    },   // 0x23 - VK_END - END key
	{ _T("HOME"),         _T("Home"),          _T("Home"),          _T("Home"),           0x24,  0,   420    },   // 0x24 - VK_HOME - HOME key
	{ _T("LEFT"),         _T("Left"),          _T("ArrowLeft"),     _T("ArrowLeft"),      0x25,  0,   340    },   // 0x25 - VK_LEFT - LEFT ARROW key
	{ _T("UP"),           _T("Up"),            _T("ArrowUp"),       _T("ArrowUp"),        0x26,  0,   360    },   // 0x26 - VK_UP - UP ARROW key
	{ _T("RIGHT"),        _T("Right"),         _T("ArrowRight"),    _T("ArrowRight"),     0x27,  0,   350    },   // 0x27 - VK_RIGHT - RIGHT ARROW key
	{ _T("DOWN"),         _T("Down"),          _T("ArrowDown"),     _T("ArrowDown"),      0x28,  0,   370    },   // 0x28 - VK_DOWN - DOWN ARROW key
	{ _T("SELECT"),       _T("Select"),        _T("Select"),        _T("Select"),         0x29,  0,   600    },   // 0x29 - VK_SELECT - SELECT key
	{ _T("PRINT"),        _T("Print"),         _T("Print"),         _T("Print"),          0x2a,  0,   610    },   // 0x2a - VK_PRINT - PRINT key
	{ _T("EXECUTE"),      _T("Execute"),       _T("Execute"),       _T("Execute"),        0x2b,  0,   620    },   // 0x2b - VK_EXECUTE - EXECUTE key
	{ _T("SYSRQ"),        _T("SysRq"),         _T("PrintScreen"),   _T("PrintScreen"),    0x2c,  0,   450    },   // 0x2c - VK_SNAPSHOT - PRINT SCREEN key
	{ _T("INSERT"),       _T("Insert"),        _T("Insert"),        _T("Insert"),         0x2d,  0,   400    },   // 0x2d - VK_INSERT - INS key
	{ _T("DELETE"),       _T("Delete"),        _T("Delete"),        _T("Delete"),         0x2e,  0,   410    },   // 0x2e - VK_DELETE - DEL key
	{ _T("HELP"),         _T("Help"),          _T("Help"),          _T("Help"),           0x2f,  0,   640    },   // 0x2f - VK_HELP - HELP key
	{ _T("0"),            _T("0"),             _T("0|+"),           _T("Digit0"),         0x30,  0,   40     },   // 0x30 - 0 key
	{ _T("1"),            _T("1"),             _T("1|!"),           _T("Digit1"),         0x31,  0,   41     },   // 0x31 - 1 key
	{ _T("2"),            _T("2"),             _T("2|@"),           _T("Digit2"),         0x32,  0,   42     },   // 0x32 - 2 key
	{ _T("3"),            _T("3"),             _T("3|#"),           _T("Digit3"),         0x33,  0,   43     },   // 0x33 - 3 key
	{ _T("4"),            _T("4"),             _T("4|$"),           _T("Digit4"),         0x34,  0,   44     },   // 0x34 - 4 key
	{ _T("5"),            _T("5"),             _T("5|%"),           _T("Digit5"),         0x35,  0,   45     },   // 0x35 - 5 key
	{ _T("6"),            _T("6"),             _T("6|^"),           _T("Digit6"),         0x36,  0,   46     },   // 0x36 - 6 key
	{ _T("7"),            _T("7"),             _T("7|&"),           _T("Digit7"),         0x37,  0,   47     },   // 0x37 - 7 key
	{ _T("8"),            _T("8"),             _T("8|*"),           _T("Digit8"),         0x38,  0,   48     },   // 0x38 - 8 key
	{ _T("9"),            _T("9"),             _T("9|("),           _T("Digit9"),         0x39,  0,   49     },   // 0x39 - 9 key
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50014  },   // 0x3a - Undefined
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50015  },   // 0x3b - Undefined
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50016  },   // 0x3c - Undefined
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50017  },   // 0x3d - Undefined
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50018  },   // 0x3e - Undefined
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50019  },   // 0x3f - Undefined
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50020  },   // 0x40 - Undefined
	{ _T("A"),            _T("A"),             _T("a|A"),           _T("KeyA"),           0x41,  0,   10     },   // 0x41 - A key
	{ _T("B"),            _T("B"),             _T("b|B"),           _T("KeyB"),           0x42,  0,   11     },   // 0x42 - B key
	{ _T("C"),            _T("C"),             _T("c|C"),           _T("KeyC"),           0x43,  0,   12     },   // 0x43 - C key
	{ _T("D"),            _T("D"),             _T("d|D"),           _T("KeyD"),           0x44,  0,   13     },   // 0x44 - D key
	{ _T("E"),            _T("E"),             _T("e|E"),           _T("KeyE"),           0x45,  0,   14     },   // 0x45 - E key
	{ _T("F"),            _T("F"),             _T("f|F"),           _T("KeyF"),           0x46,  0,   15     },   // 0x46 - F key
	{ _T("G"),            _T("G"),             _T("g|G"),           _T("KeyG"),           0x47,  0,   16     },   // 0x47 - G key
	{ _T("H"),            _T("H"),             _T("h|H"),           _T("KeyH"),           0x48,  0,   17     },   // 0x48 - H key
	{ _T("I"),            _T("I"),             _T("i|I"),           _T("KeyI"),           0x49,  0,   18     },   // 0x49 - I key
	{ _T("J"),            _T("J"),             _T("j|J"),           _T("KeyJ"),           0x4a,  0,   19     },   // 0x4a - J key
	{ _T("K"),            _T("K"),             _T("k|K"),           _T("KeyK"),           0x4b,  0,   20     },   // 0x4b - K key
	{ _T("L"),            _T("L"),             _T("l|L"),           _T("KeyL"),           0x4c,  0,   21     },   // 0x4c - L key
	{ _T("M"),            _T("M"),             _T("m|M"),           _T("KeyM"),           0x4d,  0,   22     },   // 0x4d - M key
	{ _T("N"),            _T("N"),             _T("n|N"),           _T("KeyN"),           0x4e,  0,   23     },   // 0x4e - N key
	{ _T("O"),            _T("O"),             _T("o|O"),           _T("KeyO"),           0x4f,  0,   24     },   // 0x4f - O key
	{ _T("P"),            _T("P"),             _T("p|P"),           _T("KeyP"),           0x50,  0,   25     },   // 0x50 - P key
	{ _T("Q"),            _T("Q"),             _T("q|Q"),           _T("KeyQ"),           0x51,  0,   26     },   // 0x51 - Q key
	{ _T("R"),            _T("R"),             _T("r|R"),           _T("KeyR"),           0x52,  0,   27     },   // 0x52 - R key
	{ _T("S"),            _T("S"),             _T("s|S"),           _T("KeyS"),           0x53,  0,   28     },   // 0x53 - S key 
	{ _T("T"),            _T("T"),             _T("t|T"),           _T("KeyT"),           0x54,  0,   29     },   // 0x54 - T key
	{ _T("U"),            _T("U"),             _T("u|U"),           _T("KeyU"),           0x55,  0,   30     },   // 0x55 - U key
	{ _T("V"),            _T("V"),             _T("v|V"),           _T("KeyV"),           0x56,  0,   31     },   // 0x56 - V key
	{ _T("W"),            _T("W"),             _T("w|W"),           _T("KeyW"),           0x57,  0,   32     },   // 0x57 - W key
	{ _T("X"),            _T("X"),             _T("x|X"),           _T("KeyX"),           0x58,  0,   33     },   // 0x58 - X key
	{ _T("Y"),            _T("Y"),             _T("y|Y"),           _T("KeyY"),           0x59,  0,   34     },   // 0x59 - Y key
	{ _T("Z"),            _T("Z"),             _T("z|Z"),           _T("KeyZ"),           0x5a,  0,   35     },   // 0x5a - Z key
	{ _T("LWIN"),         _T("Left Win"),      _T("Meta"),          _T("MetaLeft"),       0x5b,  1,   606    },   // 0x5b - VK_LWIN - Left Windows key
	{ _T("RWIN"),         _T("Right Win"),     _T("Meta"),          _T("MetaRight"),      0x5c,  2,   607    },   // 0x5c - VK_RWIN - Right Windows key
	{ _T("APPS"),         _T("Application"),   _T("ContextMenu"),   _T("ContextMenu"),    0x5d,  0,   608    },   // 0x5d - VK_APPS - Applications key
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50021  },   // 0x5e - Reserved
	{ _T("SLEEP"),        _T("Sleep"),         _T("Sleep"),         _T("Sleep"),          0x5f,  0,   1010   },   // 0x5f - VK_SLEEP - Computer Sleep key
	{ _T("NUMPAD0"),      _T("Keypad 0"),      _T("0|Insert"),      _T("Numpad0"),        0x2d,  3,   800    },   // 0x60 - VK_NUMPAD0 - Numeric keypad 0 key
	{ _T("NUMPAD1"),      _T("Keypad 1"),      _T("1|End"),         _T("Numpad1"),        0x23,  3,   801    },   // 0x61 - VK_NUMPAD1 - Numeric keypad 1 key
	{ _T("NUMPAD2"),      _T("Keypad 2"),      _T("2|ArrowDown"),   _T("Numpad2"),        0x28,  3,   802    },   // 0x62 - VK_NUMPAD2 - Numeric keypad 2 key
	{ _T("NUMPAD3"),      _T("Keypad 3"),      _T("3|PageDown"),    _T("Numpad3"),        0x22,  3,   803    },   // 0x63 - VK_NUMPAD3 - Numeric keypad 3 key
	{ _T("NUMPAD4"),      _T("Keypad 4"),      _T("4|ArrowLeft"),   _T("Numpad4"),        0x25,  3,   804    },   // 0x64 - VK_NUMPAD4 - Numeric keypad 4 key
	{ _T("NUMPAD5"),      _T("Keypad 5"),      _T("5|Clear"),       _T("Numpad5"),        0x0c,  3,   805    },   // 0x65 - VK_NUMPAD5 - Numeric keypad 5 key
	{ _T("NUMPAD6"),      _T("Keypad 6"),      _T("6|ArrowRight"),  _T("Numpad6"),        0x27,  3,   806    },   // 0x66 - VK_NUMPAD6 - Numeric keypad 6 key
	{ _T("NUMPAD7"),      _T("Keypad 7"),      _T("7|Home"),        _T("Numpad7"),        0x24,  3,   807    },   // 0x67 - VK_NUMPAD7 - Numeric keypad 7 key
	{ _T("NUMPAD8"),      _T("Keypad 8"),      _T("8|ArrowUp"),     _T("Numpad8"),        0x26,  3,   808    },   // 0x68 - VK_NUMPAD8 - Numeric keypad 8 key
	{ _T("NUMPAD9"),      _T("Keypad 9"),      _T("9|PageUp"),      _T("Numpad9"),        0x21,  3,   809    },   // 0x69 - VK_NUMPAD9 - Numeric keypad 9 key
	{ _T("MULTIPLY"),     _T("Keypad *"),      _T("*"),             _T("NumpadMultiply"), 0x6a,  3,   820    },   // 0x6a - VK_MULTIPLY - Multiply key
	{ _T("ADD"),          _T("Keypad +"),      _T("+"),             _T("NumpadAdd"),      0x6b,  3,   830    },   // 0x6b - VK_ADD - Add key
	{ _T("SEPARATOR"),    _T("Separator"),     _T(","),             _T("NumpadSeparator"),0x6c,  3,   840    },   // 0x6c - VK_SEPARATOR - Separator (on some non-US keypads)
	{ _T("SUBTRACT"),     _T("Keypad -"),      _T("-"),             _T("NumpadSubtract"), 0x6d,  3,   841    },   // 0x6d - VK_SUBTRACT - Subtract key
	{ _T("DECIMAL"),      _T("Keypad ."),      _T("Del|."),         _T("NumpadDecimal"),  0x2e,  3,   850    },   // 0x6e - VK_DECIMAL - Decimal key
	{ _T("DIVIDE"),       _T("Keypad /"),      _T("/"),             _T("NumpadDivide"),   0x6f,  3,   860    },   // 0x6f - VK_DIVIDE - Divide key
	{ _T("F1"),           _T("F1"),            _T("F1"),            _T("F1"),             0x70,  0,   700    },   // 0x70 - VK_F1 - F1 key
	{ _T("F2"),           _T("F2"),            _T("F2"),            _T("F2"),             0x71,  0,   701    },   // 0x71 - VK_F2 - F2 key
	{ _T("F3"),           _T("F3"),            _T("F3"),            _T("F3"),             0x72,  0,   702    },   // 0x72 - VK_F3 - F3 key
	{ _T("F4"),           _T("F4"),            _T("F4"),            _T("F4"),             0x73,  0,   703    },   // 0x73 - VK_F4 - F4 key
	{ _T("F5"),           _T("F5"),            _T("F5"),            _T("F5"),             0x74,  0,   704    },   // 0x74 - VK_F5 - F5 key
	{ _T("F6"),           _T("F6"),            _T("F6"),            _T("F6"),             0x75,  0,   705    },   // 0x75 - VK_F6 - F6 key
	{ _T("F7"),           _T("F7"),            _T("F7"),            _T("F7"),             0x76,  0,   706    },   // 0x76 - VK_F7 - F7 key
	{ _T("F8"),           _T("F8"),            _T("F8"),            _T("F8"),             0x77,  0,   707    },   // 0x77 - VK_F8 - F8 key
	{ _T("F9"),           _T("F9"),            _T("F9"),            _T("F9"),             0x78,  0,   708    },   // 0x78 - VK_F9 - F9 key
	{ _T("F10"),          _T("F10"),           _T("F10"),           _T("F10"),            0x79,  0,   709    },   // 0x79 - VK_F10 - F10 key
	{ _T("F11"),          _T("F11"),           _T("F11"),           _T("F11"),            0x7a,  0,   710    },   // 0x7a - VK_F11 - F11 key
	{ _T("F12"),          _T("F12"),           _T("F12"),           _T("F12"),            0x7b,  0,   711    },   // 0x7b - VK_F12 - F12 key
	{ _T("F13"),          _T("F13"),           _T("F13"),           _T("F13"),            0x7c,  0,   712    },   // 0x7c - VK_F13 - F13 key
	{ _T("F14"),          _T("F14"),           _T("F14"),           _T("F14"),            0x7d,  0,   713    },   // 0x7d - VK_F14 - F14 key
	{ _T("F15"),          _T("F15"),           _T("F15"),           _T("F15"),            0x7e,  0,   714    },   // 0x7e - VK_F15 - F15 key
	{ _T("F16"),          _T("F16"),           _T("F16"),           _T("F16"),            0x7f,  0,   715    },   // 0x7f - VK_F16 - F16 key
	{ _T("F17"),          _T("F17"),           _T("F17"),           _T("F17"),            0x80,  0,   716    },   // 0x80 - VK_F17 - F17 key
	{ _T("F18"),          _T("F18"),           _T("F18"),           _T("F18"),            0x81,  0,   717    },   // 0x81 - VK_F18 - F18 key
	{ _T("F19"),          _T("F19"),           _T("F19"),           _T("F19"),            0x82,  0,   718    },   // 0x82 - VK_F19 - F19 key
	{ _T("F20"),          _T("F20"),           _T("F20"),           _T("F20"),            0x83,  0,   719    },   // 0x83 - VK_F20 - F20 key
	{ _T("F21"),          _T("F21"),           _T("F21"),           _T("F21"),            0x84,  0,   720    },   // 0x84 - VK_F21 - F21 key
	{ _T("F22"),          _T("F22"),           _T("F22"),           _T("F22"),            0x85,  0,   721    },   // 0x85 - VK_F22 - F22 key
	{ _T("F23"),          _T("F23"),           _T("F23"),           _T("F23"),            0x86,  0,   722    },   // 0x86 - VK_F23 - F23 key
	{ _T("F24"),          _T("F24"),           _T("F24"),           _T("F24"),            0x87,  0,   723    },   // 0x87 - VK_F24 - F24 key
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50022  },   // 0x88 - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50023  },   // 0x89 - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50024  },   // 0x8a - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50025  },   // 0x8b - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50026  },   // 0x8c - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50027  },   // 0x8d - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50028  },   // 0x8e - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50029  },   // 0x8f - Unassigned
	{ _T("NUMLOCK"),      _T("Num Lock"),      _T("NumLock"),       _T("NumLock"),        0,     0,   470    },   // 0x90 - VK_NUMLOCK - NUM LOCK key
	{ _T("SCROLL"),       _T("Scroll Lock"),   _T("ScrollLock"),    _T("ScrollLock"),     0,     0,   480    },   // 0x91 - VK_SCROLL - SCROLL LOCK key
	{ _T("OEM92"),        _T("OEM92"),         _T("OEM92"),         _T("OEM92"),          0x92,  0,   800    },   // 0x92 - OEM specific
	{ _T("OEM93"),        _T("OEM93"),         _T("OEM93"),         _T("OEM93"),          0x93,  0,   801    },   // 0x93 - OEM specific
	{ _T("OEM94"),        _T("OEM94"),         _T("OEM94"),         _T("OEM94"),          0x94,  0,   802    },   // 0x94 - OEM specific
	{ _T("OEM95"),        _T("OEM95"),         _T("OEM95"),         _T("OEM95"),          0x95,  0,   803    },   // 0x95 - OEM specific
	{ _T("OEM96"),        _T("OEM96"),         _T("OEM96"),         _T("OEM96"),          0x96,  0,   804    },   // 0x96 - OEM specific
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50030  },   // 0x97 - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50031  },   // 0x98 - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50032  },   // 0x99 - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50033  },   // 0x9a - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50034  },   // 0x9b - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50035  },   // 0x9c - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50036  },   // 0x9d - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50037  },   // 0x9e - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50038  },   // 0x9f - Unassigned
	{ _T("LSHIFT"),       _T("Left Shift"),    _T("Shift"),         _T("ShiftLeft"),      0x10,  1,   604    },   // 0xa0 - VK_LSHIFT - Left SHIFT key
	{ _T("RSHIFT"),       _T("Right Shift"),   _T("Shift"),         _T("ShiftRight"),     0x10,  2,   605    },   // 0xa1 - VK_RSHIFT - Right SHIFT key
	{ _T("LCONTROL"),     _T("Left Ctrl"),     _T("Control"),       _T("ControlLeft"),    0x11,  1,   602    },   // 0xa2 - VK_LCONTROL - Left CONTROL key
	{ _T("RCONTROL"),     _T("Right Ctrl"),    _T("Control"),       _T("ControlRight"),   0x11,  2,   603    },   // 0xa3 - VK_RCONTROL - Right CONTROL key
	{ _T("LMENU"),        _T("Left Alt"),      _T("Alt"),           _T("AltLeft"),        0x12,  1,   600    },   // 0xa4 - VK_LMENU - Left MENU (Alt) key
	{ _T("RMENU"),        _T("Right Alt"),     _T("Alt"),           _T("AltRight"),       0x12,  2,   601    },   // 0xa5 - VK_RMENU - Right MENU (Alt) key
	{ _T("WEBBACK"),      _T("Web Back"),      _T("BrowserBack"),   _T("BrowserBack"),    0xa6,  0,   8020   },   // 0xa6 - VK_BROWSER_BACK - Browser Back key
	{ _T("WEBFORWARD"),   _T("Web Forward"),   _T("BrowserForward"),_T("BrowserForward"), 0xa7,  0,   8030   },   // 0xa7 - VK_BROWSER_FORWARD - Browser Forward key
	{ _T("WEBREFRESH"),   _T("Web Refresh"),   _T("BrowserRefresh"),_T("BrowserRefresh"), 0xa8,  0,   8010   },   // 0xa8 - VK_BROWSER_REFRESH - Browser Refresh key
	{ _T("WEBSTOP"),      _T("Web Stop"),      _T("BrowserStop"),   _T("BrowserStop"),    0xa9,  0,   8040   },   // 0xa9 - VK_BROWSER_STOP - Browser Stop key
	{ _T("WEBSEARCH"),    _T("Web Search"),    _T("BrowserSearch"), _T("BrowserSearch"),  0xaa,  0,   8050   },   // 0xaa - VK_BROWSER_SEARCH - Browser Search key
	{ _T("WEBFAVORITES"), _T("Web Favorites"), _T("BrowserFavorites"),_T("BrowserFavorites"),0xab,0,  8060   },   // 0xab - VK_BROWSER_FAVORITES - Browser Favorites key
	{ _T("WEBHOME"),      _T("Web Home"),      _T("BrowserHome"),   _T("BrowserHome"),    0xac,  0,   8000   },   // 0xac - VK_BROWSER_HOME - Browser Start and Home key
	{ _T("MUTE"),         _T("Mute"),          _T("AudioVolumeMute"),_T("AudioVolumeMute"),0xad, 0,   6020   },   // 0xad - VK_VOLUME_MUTE - Volume Mute key
	{ _T("VOLUMEDOWN"),   _T("Volume Down"),   _T("AudioVolumeDown"),_T("AudioVolumeDown"),0xae, 0,   6010   },   // 0xae - VK_VOLUME_DOWN - Volume Down key
	{ _T("VOLUMEUP"),     _T("Volume Up"),     _T("AudioVolumeUp"), _T("AudioVolumeUp"),  0xaf,  0,   6000   },   // 0xaf - VK_VOLUME_UP - Volume Up key
	{ _T("NEXTTRACK"),    _T("Next Track"),    _T("MediaTrackNext"),_T("MediaTrackNext"), 0xb0,  0,   6030   },   // 0xb0 - VK_MEDIA_NEXT_TRACK - Next Track key
	{ _T("PREVTRACK"),    _T("Prev Track"),    _T("MediaTrackPrevious"),_T("MediaTrackPrevious"),0xb1,0,6040   },   // 0xb1 - VK_MEDIA_PREV_TRACK - Previous Track key
	{ _T("MEDIASTOP"),    _T("Media Stop"),    _T("MediaStop"),     _T("MediaStop"),      0xb2,  0,   6070   },   // 0xb2 - VK_MEDIA_STOP - Stop Media key
	{ _T("PLAYPAUSE"),    _T("Play/Pause"),    _T("MediaPlayPause"),_T("MediaPlayPause"), 0xb3,  0,   6050   },   // 0xb3 - VK_MEDIA_PLAY_PAUSE - Play/Pause Media key
	{ _T("MAIL"),         _T("Mail"),          _T("LaunchMail"),    _T("LaunchMail"),     0xb4,  0,   7000   },   // 0xb4 - VK_LAUNCH_MAIL - Start Mail key
	{ _T("MEDIASELECT"),  _T("Media Select"),  _T("MediaSelect"),   _T("MediaSelect"),    0xb5,  0,   6990   },   // 0xb5 - VK_LAUNCH_MEDIA_SELECT - Select Media key
	{ _T("STARTAPP1"),    _T("Start App 1"),   _T("LaunchApp1"),    _T("LaunchApp1"),     0xb6,  0,   7100   },   // 0xb6 - VK_LAUNCH_APP1 - Start Application 1 key
	{ _T("STARTAPP2"),    _T("Start App 2"),   _T("LaunchApp2"),    _T("LaunchApp2"),     0xb7,  0,   6101   },   // 0xb7 - VK_LAUNCH_APP2 - Start Application 2 key
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50039  },   // 0xb8 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50040  },   // 0xb9 - Reserved
	{ _T("SEMICOLON"),    _T(";"),             _T(";|:"),           _T("Seimcolon"),      0xba,  0,   82     },   // 0xba - VK_OEM_1 - :;
	{ _T("EQUALS"),       _T("="),             _T("=|+"),           _T("Equal"),          0xbb,  0,   88     },   // 0xbb - VK_OEM_PLUS
	{ _T("COMMA"),        _T(","),             _T(",|<"),           _T("Comma"),          0xbc,  0,   80     },   // 0xbc - VK_OEM_COMMA
	{ _T("MINUS"),        _T("-"),             _T("-|_"),           _T("Minus"),          0xbd,  0,   84     },   // 0xbd - VK_OEM_MINUS
	{ _T("PERIOD"),       _T("."),             _T(".|>"),           _T("Period"),         0xbe,  0,   81     },   // 0xbe - VK_OEM_PERIOD
	{ _T("SLASH"),        _T("/"),             _T("/|?"),           _T("Slash"),          0xbf,  0,   85     },   // 0xbf - VK_OEM_2 '/?' (US)
	{ _T("GRAVE"),        _T("`"),             _T("`|~"),           _T("Backquote"),      0xc0,  0,   87     },   // 0xc0 - VK_OEM_3 - '`~' (US)
	{ _T("ABNT_C1"),      _T("ABNT_C1"),       _T("ABNT_C1"),       _T("ABNT_C1"),        0xc1,  0,   20100  },   // 0xc1 - 0xC1
	{ _T("ABNT_C2"),      _T("ABNT_C2"),       _T("ABNT_C2"),       _T("ABNT_C2"),        0xc2,  0,   20200  },   // 0xc2 - 0xC2
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50043  },   // 0xc3 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50044  },   // 0xc4 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50045  },   // 0xc5 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50046  },   // 0xc6 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50047  },   // 0xc7 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50048  },   // 0xc8 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50049  },   // 0xc9 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50050  },   // 0xca - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50051  },   // 0xcb - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50052  },   // 0xcc - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50053  },   // 0xcd - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50054  },   // 0xce - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50055  },   // 0xcf - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50056  },   // 0xd0 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50057  },   // 0xd1 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50058  },   // 0xd2 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50059  },   // 0xd3 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50060  },   // 0xd4 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50061  },   // 0xd5 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50062  },   // 0xd6 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50063  },   // 0xd7 - Reserved
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50064  },   // 0xd8 - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50065  },   // 0xd9 - Unassigned
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50066  },   // 0xda - Unassigned
	{ _T("LBRACKET"),     _T("["),             _T("[|{"),           _T("BracketLeft"),    0xdb,  0,   90     },   // 0xdb - VK_OEM_4 - '[{' (US)
	{ _T("BACKSLASH2B"),  _T("\\"),            _T("\\||"),          _T("Backslash"),      0xdc,  0,   92     },   // 0xdc - VK_OEM_5 - '\|' (US)
	{ _T("RBRACKET"),     _T("]"),             _T("]|}"),           _T("BracketRight"),   0xdd,  0,   91     },   // 0xdd - VK_OEM_6 - ']}' (US)
	{ _T("APOSTROPHE"),   _T("'"),             _T("'|\\\""),        _T("Quote"),          0xde,  0,   86     },   // 0xde - VK_OEM_7 - '" (US) key
	{ _T("OEM8"),         _T("OEM8"),          _T("OEM8"),          _T("OEM8"),           0xdf,  0,   91     },   // 0xdf - VK_OEM_8 - varies by keyboard
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50067  },   // 0xe0 - Reserved
	{ _T("BACKSLASH2B"),  _T("\\"),            _T("\\"),            _T("Backslash"),      0xe1,  0,   92     },   // 0xe1 - OEM specific
	{ _T("BACKSLASH102"), _T("\\"),            _T("\\"),            _T("Backslash"),      0xe2,  0,   93     },   // 0xe2 - VK_OEM_102 - '\'
	{ _T("OEME3"),        _T("OEM E3"),        _T("OEM3"),          _T("OEM3"),           0xe3,  0,   94     },   // 0xe3 - OEM specific
	{ _T("AT"),           _T("@"),             _T("@"),             _T("AtSign"),         0xe4,  0,   95     },   // 0xe4 - OEM specific
	{ _T("PROCESS"),      _T("Process"),       _T("Process"),       _T("Process"),        0xe5,  0,   2008   },   // 0xe5 - IME PROCESS key
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50067  },   // 0xe6 - Reserved
	{ _T("PACKET"),       _T("Packet"),        _T("Packet"),        _T("Packet"),         0xe7,  0,   50068  },   // 0xe7 - VK_PACKET
	{ _T("OEME8"),        _T("OEM E8"),        _T("OEME8"),         _T("OEME8"),          0xe8,  0,   50069  },   // 0xe8 - OEM specific
	{ _T("OEME9"),        _T("OEM E9"),        _T("OEME9"),         _T("OEME9"),          0xe9,  0,   50070  },   // 0xe9 - OEM specific
	{ _T("OEMEA"),        _T("OEM EA"),        _T("OEMEA"),         _T("OEMEA"),          0xea,  0,   50071  },   // 0xea - OEM specific
	{ _T("OEMEB"),        _T("OEM EB"),        _T("OEMEB"),         _T("OEMEB"),          0xeb,  0,   50072  },   // 0xeb - OEM specific
	{ _T("OEMEC"),        _T("OEM EC"),        _T("OEMEC"),         _T("OEMEC"),          0xec,  0,   50073  },   // 0xec - OEM specific
	{ _T("OEMED"),        _T("OEM ED"),        _T("OEMED"),         _T("OEMED"),          0xed,  0,   50074  },   // 0xed - OEM specific
	{ _T("OEMEE"),        _T("OEM EE"),        _T("OEMEE"),         _T("OEMEE"),          0xee,  0,   50075  },   // 0xee - OEM specific
	{ _T("OEMEF"),        _T("OEM EF"),        _T("OEMEF"),         _T("OEMEF"),          0xef,  0,   50076  },   // 0xef - OEM specific
	{ _T("OEMF0"),        _T("OEM F0"),        _T("OEMF0"),         _T("OEMF0"),          0xf0,  0,   50077  },   // 0xf0 - OEM specific
	{ _T("OEMF1"),        _T("OEM F1"),        _T("OEMF1"),         _T("OEMF1"),          0xf1,  0,   50078  },   // 0xf1 - OEM specific
	{ _T("OEMF2"),        _T("OEM F2"),        _T("OEMF2"),         _T("OEMF2"),          0xf2,  0,   50079  },   // 0xf2 - OEM specific
	{ _T("OEMF3"),        _T("OEM F3"),        _T("OEMF3"),         _T("OEMF3"),          0xf3,  0,   50080  },   // 0xf3 - OEM specific
	{ _T("OEMF4"),        _T("OEM F4"),        _T("OEMF4"),         _T("OEMF4"),          0xf4,  0,   50081  },   // 0xf4 - OEM specific
	{ _T("OEMF5"),        _T("OEM F5"),        _T("OEMF5"),         _T("OEMF5"),          0xf5,  0,   50082  },   // 0xf5 - OEM specific
	{ _T("ATTN"),         _T("ATTN"),          _T("Attn"),          _T("Attn"),           0xf6,  0,   850    },   // 0xf6 - VK_ATTN - Attn key
	{ _T("CRSEL"),        _T("CrSel"),         _T("CrSel"),         _T("CrSel"),          0xf7,  0,   851    },   // 0xf7 - VK_CRSEL - CrSel key
	{ _T("EXSEL"),        _T("ExSel"),         _T("ExSel"),         _T("ExSel"),          0xf8,  0,   852    },   // 0xf8 - VK_EXSEL - ExSel key
	{ _T("EREOF"),        _T("Erase EOF"),     _T("EraseEOF"),      _T("EraseEOF"),       0xf9,  0,   853    },   // 0xf9 - VK_EREOF - Erase EOF key
	{ _T("PLAY"),         _T("Play"),          _T("MediaPlay"),     _T("MediaPlay"),      0xfa,  0,   854    },   // 0xfa - VK_PLAY - Play key
	{ _T("ZOOM"),         _T("Zoom"),          _T("Zoom"),          _T("Zoom"),           0xfb,  0,   855    },   // 0xfb - VK_ZOOM - Zoom key
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   856    },   // 0xfc - VK_NONAME - Reserved
	{ _T("PA1"),          _T("PA1"),           _T("PA1"),           _T("PA1"),            0xfd,  0,   857    },   // 0xfd - VK_PA1 - PA1 key
	{ _T("OEMCLEAR"),     _T("Clear"),         _T("Clear"),         _T("Clear"),          0xfe,  0,   858    },   // 0xfe - VK_OEM_CLEAR - Clear key
	{ 0,                  0,                   _T("Unidentified"),  _T("Unidentified"),   0,     0,   50083  },   // 0xff - undefined
	{ _T("NUMPADENTER"),  _T("Keypad Enter"),  _T("Enter"),         _T("NumpadEnter"),    0x0d,  3,   880    },   // 0x100 - VKE_NUMPAD_ENTER - Keypad Enter
	{ _T("NUMPADEQUALS"), _T("Keypad ="),      _T("="),             _T("NumpadEqual"),    0xbb,  3,   870    },   // 0x101 - VKE_NUMPAD_EQUALS - Keypad '='
	{ _T("NUMPADCOMMA"),  _T("Keypad ,"),      _T(","),             _T("NumpadComma"),    0xbc,  3,   890    },   // 0x102 - VKE_NUMPAD_COMMA - Keypad comma
	{ _T("YEN"),          _T("Yen"),           _T("\xA5"),          _T("IntlYen"),        0x103, 0,   96     },   // 0x103 - VKE_YEN - Yen sign
	{ _T("COLON"),        _T(":"),             _T(":"),             _T("Colon"),          0x104, 0,   83     },   // 0x104 - VKE_COLON - Colon key
	{ _T("UNDERLINE"),    _T("Underline"),     _T("_"),             _T("Underline"),      0x105, 0,   89     },   // 0x105 - VKE_UNDERLINE - Underline key
	{ _T("STOP"),         _T("Stop"),          _T("Stop"),          _T("Stop"),           0x106, 0,   6060   },   // 0x106 - VKE_STOP - Stop key
	{ _T("UNLABELED"),    _T("Unlabeled"),     _T("Unlabeled"),     _T("Unlabeled"),      0x107, 0,   20021  },   // 0x107 - VKE_UNLABELED - Unlabeled key
	{ _T("CALCULATOR"),   _T("Calculator"),    _T("LaunchCalculator"),_T("LaunchCalculator"),0x108,0, 7020   },   // 0x108 - VKE_CALCULATOR - Calculator key
	{ _T("MYCOMPUTER"),   _T("My Computer"),   _T("LaunchMyComputer"),_T("LaunchMyComputer"),0x109,0, 7010   },   // 0x109 - VKE_MYCOMPUTER - My Computer key
	{ _T("POWER"),        _T("Power"),         _T("Power"),         _T("Power"),          0x10a, 0,   1000   },   // 0x10a - VKE_POWER - Power button
	{ _T("WAKE"),         _T("Wake"),          _T("Wake"),          _T("Wake"),           0x10b, 0,   1020   },   // 0x10b - VKE_WAKE - Wake button
	{ _T("AX"),           _T("AX"),            _T("AX"),            _T("AX"),             0x10c, 0,   20020  },   // 0x10c - VKE_AX - AX key
};

bool KeyInput::IsValidKeyCode(int vk)
{
	// The code is valid if it's within range of our table and
	// it has a name entry in the table.
	return vk > 0 && vk < countof(keyName) && keyName[vk].keyID != 0;
}

bool KeyInput::Init()
{
	// if we're not already initiazed, do so no
	if (inst == 0)
	{
		// create the global singleton instance
		inst = new KeyInput();
	}

	// success
	return true;
}

void KeyInput::Shutdown()
{
	delete inst;
	inst = 0;
}

KeyInput::KeyInput()
{
	// populate the key name lookup table
	for (int i = 0; i < countof(keyName); ++i)
	{
		if (keyName[i].keyID != 0)
			keyIdMap.emplace(keyName[i].keyID, i);
	}
}

KeyInput::~KeyInput()
{
}

int KeyInput::KeyByID(const TCHAR *name)
{
	// keyNameMap maps name => virtual key code - look it up and return
	// the result, or -1 if not found
	auto it = keyIdMap.find(name);
	return it == keyIdMap.end() ? -1 : it->second;
}


int KeyInput::TranslateExtKeys(UINT msg, WPARAM wParam, LPARAM lParam)
{
	UINT scancode = (lParam & 0x00ff0000) >> 16;
	UINT extended = (lParam & 0x01000000) != 0;
	int vk = (int)wParam;

	// check for special scan codes
	switch (scancode)
	{
	case 0x7d: return VKE_YEN;
	case 0x92: return VKE_COLON;
	case 0x93: return VKE_UNDERLINE;
	case 0x95: return VKE_STOP;
	case 0x96: return VKE_AX;
	case 0x97: return VKE_UNLABELED;
	case 0xa1: return VKE_CALCULATOR;
	case 0xeb: return VKE_MYCOMPUTER;
	case 0xde: return VKE_POWER;
	case 0xe3: return VKE_WAKE;
	}

	switch (vk)
	{
	case VK_SHIFT:
		// for the shift key, we can differentiate left and right
		// by mapping the scan code to the extended virtual key
		return MapVirtualKey(scancode, MAPVK_VSC_TO_VK_EX);

	case VK_CONTROL:
		// the right control key has the 'extended' flag
		return extended ? VK_RCONTROL : VK_LCONTROL;

	case VK_MENU:
		// the right Alt key has the 'extended' flag
		return extended ? VK_RMENU : VK_LMENU;

	case VK_RETURN:
		// keypad enter has the 'extended' flag
		return extended ? VKE_NUMPAD_ENTER : VK_RETURN;

	case VK_OEM_PLUS:
		// It's called VK_OEM_PLUS because it's the +/= key on the regular 
		// keyboard, but it's mapped to numpad '=' as well.  The keypad "=" 
		// has the 'extended' flag.
		return extended ? VKE_NUMPAD_EQUALS : VK_OEM_PLUS;

	case VK_OEM_COMMA:
		// keypad comma has the 'extended' flag
		return extended ? VKE_NUMPAD_COMMA : VK_OEM_COMMA;

	// For keypad .0-9, these come in as the cursor keys (Ins, Del, etc)
	// if Num Lock is off, but they're distinguishable from the separate
	// cursor keys by the LACK of 'extended' bit.  (The separate cursor
	// keys weren't in the original PC keyboard, so they count as
	// extended.)  For consistency in game mode, we don't want Num Lock
	// to affect key interpretation, so map the cursor keys to the keypad
	// keys instead.
	case VK_DELETE:
		return extended ? VK_DELETE : VK_DECIMAL;
	case VK_INSERT:
		return extended ? VK_INSERT : VK_NUMPAD0;
	case VK_END:
		return extended ? VK_END : VK_NUMPAD1;
	case VK_DOWN:
		return extended ? VK_DOWN : VK_NUMPAD2;
	case VK_NEXT:
		return extended ? VK_NEXT : VK_NUMPAD3;
	case VK_LEFT:
		return extended ? VK_LEFT : VK_NUMPAD4;
	case VK_CLEAR:
		return extended ? VK_CLEAR : VK_NUMPAD5;
	case VK_RIGHT:
		return extended ? VK_RIGHT : VK_NUMPAD6;
	case VK_HOME:
		return extended ? VK_HOME : VK_NUMPAD7;
	case VK_UP:
		return extended ? VK_UP : VK_NUMPAD8;
	case VK_PRIOR:
		return extended ? VK_PRIOR : VK_NUMPAD9;

	default:
		// for other keys, use the virtual key code from the message
		return vk;
	}
}
