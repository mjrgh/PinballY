// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "InputManagerWithConfig.h"
#include "../Utilities/KeyInput.h"
#include "../Utilities/Joystick.h"

// Name of the JoystickName array variable in the config
static const TCHAR *joystickConfigArray = _T("JoystickDevice");

InputManagerWithConfig::InputManagerWithConfig()
{
	ConfigManager::GetInstance()->Subscribe(this);
}

void InputManagerWithConfig::LoadConfig()
{
	// get the config manager instance
	ConfigManager *config = ConfigManager::GetInstance();

	// keep track of the keyboard keys we assign
	bool keyAssigned[VKE_LAST + 1];
	memset(keyAssigned, 0, sizeof(keyAssigned));

	// get the joystick manager instance
	JoystickManager *jsman = JoystickManager::GetInstance();

	// update the DirectInput instance GUID cache, so that we
	// can look up saved GUIDs
	jsman->UpdateInstanceGuidCache();

	// Set up a map that finds the Joystick object for a given
	// joystick in the config.  The config gives each joystick
	// a local index that's meaningful only in the file, so we
	// need to map these to the local logical joystick index.
	std::unordered_map<int, int> jsMap;

	// Load the joystick list.  This provides device detail on
	// the joysticks mentioned in button assignments, so that we
	// can connect the button assignments to the same devices in
	// a new session.  These are stored in an array indexed by 
	// an arbitrary integer ID; each joystick button assignment
	// refers to its joystick record by the ID number.  The entry
	// looks like this:
	//
	//   {GUID}:VID:PID:product name
	//
	// The GUID might not be present.  We added the GUID in v1.1
	// Beta 1 so that we could distinguish among multiple instances
	// of the same device type.  Before that, we could only match
	// on device type - which is fine as long as there's only one
	// of a given joystick in the system.  If this GUID is missing,
	// we'll revert to the VID/PID/name matching when looking up
	// the logical unit.
	config->EnumArray(joystickConfigArray, [this, &jsman, config, &jsMap](
		const TCHAR *val, const TCHAR *fileIndex, const TCHAR *fullName)
	{
		// original format:  VID:PID:product name
		static const std::basic_regex<TCHAR> devpat1(
			_T("\\s*(\\{[0-9a-f]{8}-[0-9a-f]{4}[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\}:)?([0-9a-f]{1,4}):([0-9a-f]{1,4}):\\s*(.+)\\s*"),
			std::regex_constants::icase);

		// fields
		GUID guid = JoystickManager::emptyGuid;
		int vid, pid;
		TSTRING prodName;

		// try parsing with each pattern
		std::match_results<const TCHAR *> m;
		if (std::regex_match(val, m, devpat1))
		{
			// check for a GUID
			if (m[1].matched)
				ParseGuid(m[1].str().c_str(), guid);

			// pull out the remaining fields
			vid = _tcstol(m[2].str().c_str(), 0, 16);
			pid = _tcstol(m[3].str().c_str(), 0, 16);
			prodName = m[4].str();
		}
		else
		{
			// no pattern matched - ignore this entry
			return;
		}

		// add it to the logical joystick list
		auto jsLog = jsman->FindOrAddLogicalJoystick(vid, pid, prodName.c_str(), guid);

		// add the mapping from the file device ID numbering to our
		// local ID numbering
		jsMap[_ttoi(fileIndex)] = jsLog->index;
	});

	// regular expressions for button and key mapping entries in the file
	std::basic_regex<TCHAR> jspat(
		_T("\\s*joystick\\s+(\\d+|\\*)\\s+(\\d+)\\s*,?(.*)"),
		std::regex_constants::icase);
	std::basic_regex<TCHAR> kbpat(
		_T("\\s*keyboard\\s+(\\w+)\\s*,?(.*)"),
		std::regex_constants::icase);
	std::basic_regex<TCHAR> nonepat(
		_T("\\s*none\\s*,?(.*)"),
		std::regex_constants::icase);

	// look up each command entry in the file
	for (auto& cmd : commands)
	{
		// clear old keys assignments
		cmd.buttons.clear();

		// find the command's config entry
		const TCHAR *txt = config->Get(cmd.GetConfigID().c_str());
		while (txt != 0 && *txt != 0)
		{
			// A config command entry consists of a comma-delimited
			// list of key/button assignments, each consisting of one
			// of these formats:
			//
			//   joystick <unit number> <button number>
			//      The <unit number> is the joystick's local index in
			//      the file, referring to an entry in our joystick
			//      data array (parsed above).  The special unit "*"
			//      means that the button matches input from any
			//      joystick.  The button numbering starts at 1 per
			//      the USB HID spec.
			//
			//   keyboard <keyname>
			//      The <keyname> is the internal 'keyID' string
			//      from the key name array in KeyInput.cpp.
			//
			//   none
			//      This signifies that the user has explicitly removed
			//      all key assignments from this command.  This is
			//      different from a missing config entry, which implies
			//      that we should use the default key for the command.

			// try the various patterns
			std::match_results<const TCHAR *> m;
			if (std::regex_match(txt, m, jspat))
			{
				// Joystick button.  Get the button number.
				int buttonNum = _ttoi(m[2].str().c_str());

				// Get the unit number.  This can be "*" if the button
				// matches a press of the button on any unit, or can be
				// a specific unit number, referring to one of the 
				// joystick device records in the config by index.
				int unitNum = -1;
				if (m[1].str() != _T("*"))
				{
					// The button is tied to a specific joystick device.
					// Look up the unit number.
					auto jsit = jsMap.find(_ttoi(m[1].str().c_str()));

					// Make sure we found the entry.  It shouldn't be possible
					// to have a missing entry in a well-formed file, but the
					// file could have been damaged (e.g., by user editing).
					// If the joystick index is invalid, simply ignore this 
					// button mapping.
					if (jsit != jsMap.end())
						unitNum = jsit->second;
				}

				// Add the button
				cmd.buttons.emplace_back(Button::TypeJS, unitNum, buttonNum);

				// Get the rest of the string
				txt = m[3].first;
			}
			else if (std::regex_match(txt, m, kbpat))
			{
				// look up the key name in all caps
				TSTRING keyname = m[1].str().c_str();
				std::transform(keyname.begin(), keyname.end(), keyname.begin(), ::_totupper);
				int vk = KeyInput::GetInstance()->KeyByID(keyname.c_str());
				if (vk != -1)
				{
					// Add a key item for (type=keyboard, unit=0, value=VK_xxx);
					cmd.buttons.emplace_back(Button::TypeKB, 0, vk);

					// Claim the key assignment.  This prevents reassigning
					// the key to its default command if the default command
					// doesn't have any other keys assigned.  We want explicit
					// assignments in the config to override defaults.
					keyAssigned[vk] = true;
				}

				// get the rest of the string
				txt = m[2].first;
			}
			else if (std::regex_match(txt, m, nonepat))
			{
				// no assignment - add a key item for (type=none, unit=0, value=0)
				cmd.buttons.emplace_back(Button::TypeNone, 0, 0);

				// get the rest of the string
				txt = m[1].first;
			}
			else
			{
				// bad pattern - skip the rest
				break;
			}
		}
	}

	// Go back through the commands and assign default keys for any commands
	// with no key assignments.
	for (auto& cmd : commands)
	{
		// If there's nothing assigned to the button (not even an explicit
		// "none" entry), and the default key for the command hasn't been
		// claimed by another command, assign the default key.
		if (cmd.buttons.size() == 0 && cmd.defaultKey != 0 && !keyAssigned[cmd.defaultKey])
		{
			// assign the key as (type=keyboard, unit=0, value=cmd.defaultKey)
			cmd.buttons.emplace_back(Button::TypeKB, 0, cmd.defaultKey);

			// claim the key
			keyAssigned[cmd.defaultKey] = true;
		}
	}
}

void InputManagerWithConfig::StoreConfig()
{
	// get the config manager instance
	ConfigManager *config = ConfigManager::GetInstance();

	// Set up a list to record which joysticks are mentioned in
	// command button mappings
	JoystickManager *jsman = JoystickManager::GetInstance();
	std::unordered_set<const JoystickManager::LogicalJoystick*> jsRefs;

	// Visit the commands
	for (auto const& cmd : commands)
	{
		// check if we have any mappings
		if (cmd.buttons.size() == 0)
		{
			// There are no mappings, so indicate this explicitly
			// in the config with a "none" entry.
			config->Set(cmd.GetConfigID().c_str(), _T("none"));
		}
		else
		{
			// start with an empty button list for the command
			TSTRING txt;

			// Visit each button in the command
			for (auto const& button : cmd.buttons)
			{
				// check what we have
				TSTRING curTxt;
				switch (button.devType)
				{
				case Button::TypeNone:
					// A "none" entry is only used when there are no other
					// entries.  Include it only if this is the single entry.
					if (cmd.buttons.size() == 1)
						curTxt = _T("none");
					break;

				case Button::TypeJS:
					// Joystick button.  If the unit number is -1, it means
					// that the button will match any joystick device; otherwise,
					// it's tied to a particular unit.
					if (button.unit == -1)
					{
						// the button matches any unit - denote this with "*"
						// in place of the unit number in the config
						curTxt = MsgFmt(_T("joystick * %d"), button.code);
					}
					else
					{
						// The button is tied to a particular device.  Get the
						// logical device descriptor.
						const JoystickManager::LogicalJoystick *ljs = jsman->GetLogicalJoystick(button.unit);

						// format the name: joystick <index> <button number>
						curTxt = MsgFmt(_T("joystick %d %d"), ljs->index, button.code);

						// add this joystick to the list of referenced joysticks
						jsRefs.emplace(ljs);
					}
					break;

				case Button::TypeKB:
					// the text is the key name
					if (button.code > 0 && button.code <= VKE_LAST
						&& KeyInput::keyName[button.code].keyID != 0)
						curTxt = MsgFmt(_T("keyboard %s"), KeyInput::keyName[button.code].keyID);
					break;
				}

				// if we generated any text add it
				if (curTxt.length() != 0)
				{
					// add a "," delimiter if there's previous text
					if (txt.length() > 0)
						txt.append(_T(", "));

					// add the new text
					txt.append(curTxt);
				}
			}

			// If the list is empty, set the config entry to "none" to
			// indicate that the command explicitly has no buttons
			// assigned, to prevent the config loader from trying to
			// add the default button to the command on the next run.
			if (txt.length() == 0)
				txt = _T("none");

			// set the config value
			config->Set(cmd.GetConfigID().c_str(), txt.c_str());
		}
	}

	// Store all referenced logical joysticks.  The new way (as of 1.1 Beta 1)
	// to key joysticks in the settings is by the DirectInput Instance GUID.
	// The old way was to use the combination of VID, PID, and product name.
	// The old method can't distinguish among multiple instances of the same
	// device type, whereas GUIDs can.
	config->DeleteArray(joystickConfigArray);
	for (auto const js : jsRefs)
	{
		config->SetArrayEle(joystickConfigArray, MsgFmt(_T("%d"), js->index),
			MsgFmt(_T("{%s}:%04x:%04x:%s"),
				FormatGuid(js->instanceGuid).c_str(),
				js->vendorID,
				js->productID,
				js->prodName.c_str()));
	}
}
