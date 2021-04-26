// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <Setupapi.h>
#include <Hidsdi.h>
#include <dinput.h>
#include "Pointers.h"
#include "Joystick.h"

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

JoystickManager *JoystickManager::inst = 0;
const TCHAR *JoystickManager::cv_RememberJSButtonSource = _T("RememberJSButtonSource");
const GUID JoystickManager::emptyGuid = { 0x00000000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };

bool JoystickManager::Init()
{
	// if we're not initialized, do so now
	if (inst == 0)
	{
		// create the global singleton instance
		inst = new JoystickManager();
	}

	// success
	return true;
}

void JoystickManager::Shutdown()
{
	delete inst;
	inst = 0;
}

JoystickManager::JoystickManager()
{
	// Create our IDirectInput8 interface.  We use this only to obtain
	// Instance GUIDs for the joystick devices we discover.  We need
	// some kind of stable device instance identifier in order to save
	// button/axis settings across sessions, and the lower-level Windows
	// USB and HID layers don't have any good equivalent.  The SetupDI
	// layer comes the closest with its notion of "device instance ID",
	// but that's not as stable as we'd like, in that it can depend upon
	// the device's bus address - so it can change if the user plugs the
	// device into a different port.  I think this exact problem is the
	// whole reason Microsoft created Instance GUIDs - the SDK doc says
	// specifically that applications can use these to save per-device
	// settings.  I just wish they had put them in the lower-level APIs
	// instead of in DirectInput, given that DirectInput has since been
	// deprecated, meaning Microsoft is no longer developing DI and
	// doesn't want us using it in new programs.  Hopefully MSFT intends
	// to at least continue maintaining the Instance GUID functionality,
	// even though they're no longer doing active development on DI.
	if (SUCCEEDED(DirectInput8Create(G_hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<void**>(&idi8), NULL)))
	{
		// load the Instance GUID cache
		UpdateInstanceGuidCache();
	}
	else
	{
		// failed to initialize DI8 - make sure we don't have an
		// interface pointer
		idi8 = nullptr;
	}
}

JoystickManager::~JoystickManager()
{
}

void JoystickManager::AddDevice(HANDLE hDevice, const RID_DEVICE_INFO_HID *rid)
{
	// If the handle is already in our joystick list, do nothing.
	// Windows can call this redundantly by sending device change
	// notifications after startup for devices we've already found
	// via discovery.
	if (physJoysticks.find(hDevice) != physJoysticks.end())
		return;

	// The Raw Input API doesn't provide a friendly name for the
	// device, but we can get the device's USB product string from
	// the HidD API.  All we need for that is a HidD handle for
	// the HID object corresponding to the Raw Input device.  To
	// get the HidD handle, we first retrieve the RIDI_DEVICENAME
	// property for this raw input device.  That gives us a pseudo
	// file system path that we can open with CreateFile() to get
	// the HidD handle.
	TCHAR devname[512];
	UINT sz = countof(devname);
	GetRawInputDeviceInfo(hDevice, RIDI_DEVICENAME, &devname, &sz);
	HANDLE fp = CreateFile(
		devname, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		0, OPEN_EXISTING, 0, 0);
	WCHAR prodname[128] = L"";
	WCHAR serial[128] = L"";
	if (fp != INVALID_HANDLE_VALUE)
	{
		// query the product name
		if (!HidD_GetProductString(fp, prodname, countof(prodname)))
			prodname[0] = 0;

		// query the serial number string
		if (!HidD_GetSerialNumberString(fp, serial, countof(serial)))
			serial[0] = 0;

		// done with the HidD device object handle
		CloseHandle(fp);
	}

	// If we weren't able to get a product name out of HidD, 
	// synthesize a semi-friendly name from the VID/PID codes.
	// It's rare to have more than one device of the same type 
	// in a system, and most devices have hard-coded VID/PID 
	// codes, so this should give us a unique name and stable
	// name that we can use to correlate config records to the
	// same device in a future session even if the "device name"
	// path changes (due to reinstallation, e.g.).
	if (prodname[0] == 0)
	{
		swprintf_s(prodname, L"Joystick %04lx:%04lx",
			rid->dwVendorId, rid->dwProductId);
	}

	// If we didn't get a serial, synthesize a placeholder serial number
	if (serial[0] == 0)
		wcscpy_s(serial, L"00000000");

	// Note the number of logical joysticks currently in our list.
	// This will let us infer whether or not we had to add a new
	// logical joystick for this physical joystick.
	size_t nLogJs = GetLogicalJoystickCount();

	// look up the GUID by device name
	GUID guid = emptyGuid;
	TSTRING pathKey = devname;
	std::transform(pathKey.begin(), pathKey.end(), pathKey.begin(), ::_totlower);
	if (auto it = pathToGuid.find(pathKey); it != pathToGuid.end())
		guid = it->second;

	// add it to the list
	auto it = physJoysticks.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(hDevice),
		std::forward_as_tuple(rid->dwVendorId, rid->dwProductId, prodname, guid, hDevice, devname, serial));

	// retrieve the new physical joystick from the result
	PhysicalJoystick *js = &it.first->second;

	// find out if we added a new logical joystick for this unit
	bool logicalIsNew = GetLogicalJoystickCount() > nLogJs;

	// notify event subscribers
	for (auto r : eventReceivers)
		r->OnJoystickAdded(js, logicalIsNew);
}


void JoystickManager::RemoveDevice(HANDLE hDevice)
{
	// look up the device in our physical joystick list
	auto it = physJoysticks.find(hDevice);

	// if we found it, remove it from the table
	if (it != physJoysticks.end())
	{
		// notify event subscribers
		for (auto r : eventReceivers)
			r->OnJoystickRemoved(&it->second);

		// remove it from our table
		physJoysticks.erase(it);
	}
}

const TCHAR *JoystickManager::Joystick::valNames[] = {
	_T("X"),
	_T("Y"),
	_T("Z"),
	_T("RX"),
	_T("RY"),
	_T("RZ"),
	_T("Slider"),
	_T("Dial"),
	_T("Wheel"),
	_T("Hat"),
};
const CHAR *JoystickManager::Joystick::valNamesA[] = {
	"X",
	"Y",
	"Z",
	"RX",
	"RY",
	"RZ",
	"Slider",
	"Dial",
	"Wheel",
	"Hat",
};

JoystickManager::Joystick::Joystick(int vendorID, int productID, const TCHAR *prodName, const GUID &instanceGuid) :
	vendorID(vendorID), productID(productID), prodName(prodName), instanceGuid(instanceGuid)
{
}


JoystickManager::PhysicalJoystick::PhysicalJoystick(
	int vendorID, int productID, const TCHAR *prodName, const GUID &instanceGuid,
	HANDLE hRawDevice, const TCHAR *path, const TCHAR *serial)
	: Joystick(vendorID, productID, prodName, instanceGuid),
	hRawDevice(hRawDevice),
	path(path),
	serial(serial)
{
	// assign our logical joystick
	logjs = JoystickManager::GetInstance()->BindPhysicalToLogicalJoystick(this);

	// retrieve the preparsed data size
	UINT ppdSize;
	GetRawInputDeviceInfo(hRawDevice, RIDI_PREPARSEDDATA, 0, &ppdSize);
	if (ppdSize == 0)
		return;

	// allocate space for the preparsed data
	ppData.reset(new (std::nothrow) BYTE[ppdSize]);
	if (ppData.get() == 0)
		return;

	// retrieve the preparsed data
	UINT ppdActual = ppdSize;
	if (GetRawInputDeviceInfo(hRawDevice, RIDI_PREPARSEDDATA, ppData.get(), &ppdActual) != ppdSize)
		return;

	// retrieve the HID capabilities
	PHIDP_PREPARSED_DATA ppd = (PHIDP_PREPARSED_DATA)ppData.get();
	HIDP_CAPS caps;
	if (HidP_GetCaps(ppd, &caps) != HIDP_STATUS_SUCCESS)
		return;

	// allocate space for the button capabilities
	USHORT numBtnCaps = caps.NumberInputButtonCaps;
	std::unique_ptr<HIDP_BUTTON_CAPS> btnCaps(new (std::nothrow) HIDP_BUTTON_CAPS[numBtnCaps]);
	if (btnCaps.get() == 0)
		return;

	// retrieve the button caps
	if (HidP_GetButtonCaps(HidP_Input, btnCaps.get(), &numBtnCaps, ppd) != HIDP_STATUS_SUCCESS)
		return;

	// Interpret the HID_BUTTON_CAPS entries.  Each entry specifies
	// a report ID and one or more buttons included in the report.
	// We need to gather a few pieces of information from the array:
	// 
	// 1. The total number of buttons in the whole device.  Or,
	// more precisely, the highest button number.  We keep track
	// of the button state via an array of BYTE entries, one per
	// button, where array[N] represents button #N.  That means
	// we simply need to allocate an array of size MAX+1, where
	// MAX is the highest button number in the whole device. 
	// 
	// 2. The list of report IDs that contain button states.  We
	// need to set up one ButtonReportGroup object per report ID.
	// (Most joystick devices are relatvely simple and only issue
	// one report ID, so it's probably a bit of overkill to worry
	// about different IDs, but for sake of generality we do.)
	// For each HID_BUTTON_CAPS item, we'll add a new 
	// ButtonReportGroup object to our list if we don't already
	// have one for the report type.
	//
	// 3. The number of buttons in each report ID group.
	//
	int maxButtonIndex = 0;
	HIDP_BUTTON_CAPS *bc = btnCaps.get();;
	for (unsigned int i = 0; i < numBtnCaps; ++i, ++bc)
	{
		// We're only interested in buttons, which are indicated by
		// Usage Page 0x09 ("Buttons Page") from the USB HID spec
		if (bc->UsagePage == 9)
		{
			// find the report group item for this report ID
			ButtonReportGroup *brg = GetButtonReportGroup(bc->ReportID, bc->UsagePage);

			// Check if it's a button range or a single button.
			// (Good god, MSFT, you really have to make things
			// difficult, don't you?  Did it not occur to anyone
			// that "range" is general enough to include "one"
			// without a whole separate type???)
			int nButtons;
			if (bc->IsRange)
			{
				// usage range - use the upper bound
				if (bc->Range.UsageMax > maxButtonIndex)
					maxButtonIndex = bc->Range.UsageMax;

				// count the buttons in the range
				nButtons = bc->Range.UsageMax - bc->Range.UsageMin + 1;
				brg->buttonFirstIndex = bc->Range.UsageMin;
				brg->buttonLastIndex = bc->Range.UsageMax;
			}
			else
			{
				// single usage - use the usage number
				brg->buttonFirstIndex = brg->buttonLastIndex = bc->NotRange.Usage;
				if (bc->NotRange.Usage > maxButtonIndex)
					maxButtonIndex = bc->NotRange.Usage;

				// there's just one button here
				nButtons = 1;
			}

			// count the buttons in this group
			brg->nButtons += nButtons;
		}
	}

	// allocate the ON lists in the button report groups, now that we
	// know how many buttons can be reported in each group
	for (auto& brg : buttonReportGroups)
		brg.AllocOnLists();

	// allocate the button state array, now that we know how many
	// buttons there are overall
	nButtonStates = maxButtonIndex + 1;
	buttonState.reset(new (std::nothrow) ButtonState[nButtonStates]);

	// if our logical joystick doesn't have enough buttons to cover
	// the entries in this physical unit, expand it
	if (logjs->nButtonStates < nButtonStates)
	{
		// save the old list
		std::unique_ptr<ButtonState> oldp(logjs->buttonState.release());

		// allocate a new list
		logjs->buttonState.reset(new (std::nothrow) ButtonState[maxButtonIndex + 1]);
		if (logjs->buttonState.get() == 0)
		{
			// uh-oh - couldn't allocate it; restore the old list
			logjs->buttonState.reset(oldp.release());
		}
		else
		{
			// copy the old list
			if (oldp.get() != 0)
				memcpy(logjs->buttonState.get(), oldp.get(), logjs->nButtonStates * sizeof(ButtonState));

			// remember the new count
			logjs->nButtonStates = nButtonStates;
		}
	}

	// mark the buttons that are present
	for (auto &brg : buttonReportGroups)
	{
		if (brg.nButtons != 0)
		{
			for (int i = brg.buttonFirstIndex; i <= brg.buttonLastIndex && i < logjs->nButtonStates; ++i)
				buttonState.get()[i].present = logjs->buttonState.get()[i].present = true;
		}
	}

	// allocate space for the value caps
	USHORT numValCaps = caps.NumberInputValueCaps;
	std::unique_ptr<HIDP_VALUE_CAPS> valCaps(new (std::nothrow) HIDP_VALUE_CAPS[numValCaps]);
	if (valCaps.get() == 0)
		return;

	// retrieve the value caps
	if (HidP_GetValueCaps(HidP_Input, valCaps.get(), &numValCaps, ppd) != HIDP_STATUS_SUCCESS)
		return;

	// Parse the value caps descriptors
	const HIDP_VALUE_CAPS *v = valCaps.get();
	for (unsigned int i = 0; i < numValCaps; ++i, ++v)
	{
		// ignore anything that's not usage page 0x01, Generic Desktop
		if (v->UsagePage != 0x01)
			continue;

		// Find the report group item for this report type.  All of the
		// descriptors we find apply to this report type, so we'll store
		// them under the report group object.
		ButtonReportGroup *brg = GetButtonReportGroup(v->ReportID, v->UsagePage);

		// visit each usage mentioned
		USAGE lastUsage = v->IsRange ? v->Range.UsageMax : v->NotRange.Usage;
		for (USAGE usage = v->Range.UsageMin; usage <= lastUsage; ++usage)
		{
			// check if it's one we're interested in
			if (usage >= iValFirst && usage <= iValLast)
			{
				// add the entry to the report group
				brg->usageVal.emplace_back(v->UsagePage, usage);

				// mark it as present
				int index = static_cast<int>(usage - iValFirst);
				val[index].present = logjs->val[index].present = true;

				// remember the physical and logical ranges
				val[index].logMin = logjs->val[index].logMin = v->LogicalMin;
				val[index].logMax = logjs->val[index].logMax = v->LogicalMax;
				val[index].physMin = logjs->val[index].physMin = v->PhysicalMin;
				val[index].physMax = logjs->val[index].physMax = v->PhysicalMax;
			}
		}
	}
}

JoystickManager::PhysicalJoystick::ButtonReportGroup*
    JoystickManager::PhysicalJoystick::GetButtonReportGroup(int reportID, USAGE usagePage)
{

	// Check if we already have a ButtonReportGroup item 
	// this report ID.
	ButtonReportGroup *brg = findifex(buttonReportGroups, [reportID](ButtonReportGroup &b) {
		return b.reportId == reportID; });

	// if not, create a new one
	if (brg == 0)
	{ 
		buttonReportGroups.emplace_back(reportID, usagePage);
		brg = &buttonReportGroups.back();
	}

	// return what we found
	return brg;
}

void JoystickManager::ProcessRawInput(UINT rawInputCode, HANDLE hRawInput, RAWINPUT *raw)
{
	// only process HID events
	if (raw->header.dwType == RIM_TYPEHID)
	{
		// look up the joystick
		auto it = physJoysticks.find(hRawInput);
		if (it != physJoysticks.end())
			it->second.ProcessRawInput(rawInputCode, raw);
	}
}

void JoystickManager::PhysicalJoystick::ProcessRawInput(UINT rawInputCode, RAWINPUT *raw)
{
	// note if the event happened in the foreground or background
	bool foreground = rawInputCode == RIM_INPUT;

	// get the preparsed data
	PHIDP_PREPARSED_DATA pp = (PHIDP_PREPARSED_DATA)ppData.get();

	// get the button state array
	ButtonState *bs = buttonState.get();

	// process each input report
	BYTE *pRawData = raw->data.hid.bRawData;
	DWORD dwSizeHid = raw->data.hid.dwSizeHid;
	for (unsigned rptno = 0; rptno < raw->data.hid.dwCount; ++rptno, pRawData += dwSizeHid)
	{
		// Per Windows HID conventions, the first byte of every 
		// HID report is the report ID from the device.
		BYTE reportId = pRawData[0];

		// find the Button Report Group for this report ID
		for (auto brgit = buttonReportGroups.begin(); brgit != buttonReportGroups.end(); ++brgit)
		{
			// check for a match on the report ID
			if (brgit->reportId == reportId)
			{
				// get the direct pointer
				ButtonReportGroup *brg = &*brgit;

				// Figure the NEXT and LAST on list indices
				int lastOnIndex = brg->lastOnIndex;
				int nextOnIndex = lastOnIndex ^ 1;

				// Get the NEXT and LAST on list pointers
				int nLastOn = brg->on[lastOnIndex].nOn;
				USAGE *lastOn = brg->on[lastOnIndex].usage.get();
				USAGE *nextOn = brg->on[nextOnIndex].usage.get();

				// Get the Usages from the report for our button group's
				// usage page.  A "Usage" in the case of a button is simply
				// the button number, and for this particular API, the
				// reported Usage list consists of all of the ON buttons
				// in the report.  Retrieve the report into the NEXT OnList
				// in the button group object.  The OnList has nButtonStates
				// elements allocated.
				ULONG usageLen = brg->nButtons;
				if (HidP_GetUsages(HidP_Input, brg->usagePage, 0, nextOn, &usageLen, pp,
					(PCHAR)raw->data.hid.bRawData, raw->data.hid.dwSizeHid) == HIDP_STATUS_SUCCESS)
				{
					// nextOn[] now contains usageLen Usages, i.e., button
					// numbers for the ON buttons.  'OR' an 0x02 bit into
					// each button.  This combines with the previous state
					// of 0x00 for OFF or 0x01 for ON to give us our new
					// state:
					//
					//   - If the button was previously OFF, it changes
					//     from 0x00 to 0x02
					//
					//   - If the button was previously ON, it changes
					//     from 0x01 to 0x03
					//
					// And note the effect on buttons that were previously
					// ON but are now off, so aren't included in the nextOn
					// list:
					//
					//   - If the button was previously ON and now OFF,
					//     it stays at 0x01
					//
					// So we can tell the effect of this message on each
					// of the buttons in the nextOn and lastOn lists just
					// by looking at the updated button state, without
					// having to cross-search either list:
					//
					//   0x01 -> was ON, now OFF -> OFF EVENT
					//   0x02 -> was OFF, now ON -> ON EVENT
					//   0x03 -> was ON, now ON -> no change
					//
					for (unsigned int i = 0; i < usageLen; ++i)
					{
						int button = nextOn[i];
						if ((bs[button].state |= 0x02) == 0x02)
						{
							// this button is newly on - fire an event
							JoystickManager::GetInstance()->SendButtonEvent(
								this, button, true, foreground);
						}
					}

					// Now visit each button in the PREVIOUS On list.
					// That came from exactly the same report type as the
					// current On list, so it covers exactly the same set
					// of buttons.  Therefore, any button that was ON in
					// the OLD list but wasn't mentioned as ON in the new
					// list must have just turned OFF.  
					//
					// As explained above, our first pass over the nextOn[]
					// list updated our buttonState[] array in such a way
					// that we can tell if a button in the lastOn[] list
					// is still on, without any need to search for it in 
					// the nextOn[] list.  If X is a button in the lastOn[]
					// list, and buttonState[X] is 0x01, that button just
					// switched off; if its state is 0x03, it's still on
					// (therefore unchanged).
					for (int i = 0; i < nLastOn; ++i)
					{
						int button = lastOn[i];
						if (bs[button].state == 0x01)
						{
							// this button is now off - fire an event
							JoystickManager::GetInstance()->SendButtonEvent(
								this, button, false, foreground);

							// set its state to OFF (0)
							bs[button].state = 0;

							// copy it to the logical joystick state as well
							if (button < logjs->nButtonStates)
								logjs->buttonState.get()[button].state = 0;
						}
					}

					// Clean up the button states for next time, by
					// setting all of the ON button states to 0x01.
					for (unsigned int i = 0; i < usageLen; ++i)
					{
						// set this button state to ON (1)
						int button = nextOn[i];
						bs[button].state = 1;

						// copy it to the logical joystick state as well
						if (button <= logjs->nButtonStates)
							logjs->buttonState.get()[button].state = 1;
					}

					// And finally, the new ON list now becomes the prior
					// ON list for the next event.
					brg->lastOnIndex = nextOnIndex;
					brg->on[nextOnIndex].nOn = usageLen;
				}

				// Read the axis value updates
				std::list<ValueChange> valueChanges;
				for (auto const& v : brg->usageVal)
				{
					// parse the value from the report
					LONG newVal;
					USAGE usage = v.usage;
					if (HidP_GetScaledUsageValue(HidP_Input, v.usagePage, 0, usage, &newVal, pp,
						(PCHAR)raw->data.hid.bRawData, raw->data.hid.dwSizeHid) == HIDP_STATUS_SUCCESS)
					{
						// if the value has changed, update it here and in our logical device
						int iVal = usage - iValFirst;
						if (val[iVal].cur != newVal)
						{
							// store the new value
							val[iVal].cur = newVal;
							logjs->val[iVal].cur = newVal;

							// add it to the value change list
							valueChanges.emplace_back(usage, newVal);
						}
					}
				}

				// if any values changed, fire an event
				if (valueChanges.size() != 0)
					JoystickManager::GetInstance()->SendValueChangeEvent(this, valueChanges, foreground);

				// Report Group items are unique per report ID, so there's no 
				// need to look any further.
				break;
			}
		}
	}
}

void JoystickManager::SubscribeJoystickEvents(JoystickEventReceiver *r)
{
	eventReceivers.push_front(r);
}

void JoystickManager::UnsubscribeJoystickEvents(JoystickEventReceiver *r)
{
	eventReceivers.remove(r);
}

void JoystickManager::SendButtonEvent(
	PhysicalJoystick *js, int button, bool pressed, bool foreground)
{
	// send the event to each receiver
	for (auto r : eventReceivers)
	{
		// send the event; if the handler returns true, it means that
		// it fully consumed the event, so don't send it to any other
		// subscribers
		if (r->OnJoystickButtonChange(js, button, pressed, foreground))
			break;
	}
}

void JoystickManager::SendValueChangeEvent(
	PhysicalJoystick *js, const std::list<ValueChange> &changes, bool foreground)
{
	// send the event to each receiver
	for (auto r : eventReceivers)
	{
		// send the event; if the handler returns true, it means that
		// it fully consumed the event, so don't send it to any other
		// subscribers
		if (r->OnJoystickValueChange(js, changes, foreground))
			break;
	}
}

JoystickManager::LogicalJoystick *JoystickManager::FindOrAddLogicalJoystick(
	int vendorID, int productID, const TCHAR *prodName, const GUID &instanceGuid)
{
	// Search the existing logical joysticks for a matching device
	for (auto &l : logicalJoysticks)
	{
		// Only consider devices that match on type (VID+PID+product name)
		if (l.vendorID == vendorID && l.productID == productID && l.prodName == prodName)
		{
			// If the caller provided an empty GUID, match on VID/PID/name alone
			if (instanceGuid == emptyGuid)
				return &l;

			// The caller provided a GUID, so match if the GUIDs match
			if (instanceGuid == l.instanceGuid)
				return &l;

			// If this device has an empty GUID, match it on VID/PID/name alone.
			// In this case, adopt the caller's GUID as the logical unit's GUID,
			// so that the same unit can't also match another explicit GUID.  If
			// someone else comes along looking for the same VID/PID/name under
			// a different GUID, they'll get a new logical entry for that GUID.
			if (l.instanceGuid == emptyGuid)
			{
				l.instanceGuid = instanceGuid;
				return &l;
			}
		}
	}

	// No existing entry matches.  Create a new entry.
	int localIndex = (int)logicalJoysticks.size();
	auto lj = &logicalJoysticks.emplace_back(localIndex, vendorID, productID, prodName, instanceGuid);

	// add it to the by-index index
	logicalJoysticksByIndex.emplace_back(lj);

	// return the new object
	return lj;
}

JoystickManager::LogicalJoystick *JoystickManager::BindPhysicalToLogicalJoystick(
	const JoystickManager::PhysicalJoystick *p)
{
	return FindOrAddLogicalJoystick(p->vendorID, p->productID, p->prodName.c_str(), p->instanceGuid);
}


JoystickManager::PhysicalJoystick *JoystickManager::GetPhysicalJoystick(const LogicalJoystick *js)
{
	// if the logical joystick is null, so is the physical joystick
	if (js == nullptr)
		return nullptr;

	// scan the physical joystick list for a match
	for (auto &p : physJoysticks)
	{
		if (p.second.logjs == js)
			return &p.second;
	}

	// no match
	return nullptr;
}

JoystickManager::LogicalJoystick *JoystickManager::GetLogicalJoystick(int unitNum)
{
	return (unitNum >= 0 && unitNum < logicalJoysticks.size() ? logicalJoysticksByIndex[unitNum] : nullptr);
}

void JoystickManager::EnumLogicalJoysticks(std::function<void(const LogicalJoystick*)> func)
{
	for (auto &l : logicalJoysticks)
		func(&l);
}

void JoystickManager::UpdateInstanceGuidCache()
{
	// we can proceed only if we have a Direct Input interface
	if (idi8 != nullptr)
	{
		// clear the old mapping tables and start fresh
		guidToPath.clear();
		pathToGuid.clear();

		// enumerate game controller devices
		struct CallbackContext
		{
			CallbackContext(JoystickManager *jm, IDirectInput8 *idi8) : jm(jm), idi8(idi8) { }
			JoystickManager *jm;
			IDirectInput8 *idi8;
		}
		ctx(this, idi8);
		auto cb = [](LPCDIDEVICEINSTANCE ddi, LPVOID pvRef) -> BOOL
		{
			// open the device and retrieve its device path
			auto ctx = static_cast<CallbackContext*>(pvRef);
			RefPtr<IDirectInputDevice8> idev;
			DIPROPGUIDANDPATH gp{ sizeof(gp), sizeof(DIPROPHEADER), 0, DIPH_DEVICE };
			if (SUCCEEDED(ctx->idi8->CreateDevice(ddi->guidInstance, &idev, NULL))
				&& SUCCEEDED(idev->GetProperty(DIPROP_GUIDANDPATH, &gp.diph)))
			{
				// add the cache entry - canonicalize to lower-case for the key
				_tcslwr_s(gp.wszPath);
				ctx->jm->guidToPath.emplace(FormatGuid(ddi->guidInstance), gp.wszPath);
				ctx->jm->pathToGuid.emplace(gp.wszPath, ddi->guidInstance);
			}

			// continue the enumeration
			return DIENUM_CONTINUE;
		};
		idi8->EnumDevices(DI8DEVCLASS_GAMECTRL, cb, &ctx, DIEDFL_ALLDEVICES);
	}
}
