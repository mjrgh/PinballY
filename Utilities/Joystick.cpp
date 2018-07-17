// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <Setupapi.h>
#include <Hidsdi.h>
#include "Joystick.h"

#pragma comment(lib, "hid.lib")

JoystickManager *JoystickManager::inst = 0;
const TCHAR *JoystickManager::cv_RememberJSButtonSource = _T("RememberJSButtonSource");

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

	// Retrieve the joystick's Raw Input device name.
	TCHAR devname[512];
	UINT sz = countof(devname);
	GetRawInputDeviceInfo(hDevice, RIDI_DEVICENAME, &devname, &sz);

	// The Raw Input API doesn't provide a friendly name for the
	// device, but we can get the device's USB product name from
	// the HidD API.  All we need to do is get a HidD handle for
	// the HID device corresponding to the Raw Input device.
	//
	// Fortunately, there's an easy way to do this.  The "device
	// name" that Raw Input reports via RIDI_DEVICENAME is actually
	// the file system path for the device for HidD purposes.  This
	// path can be handed to CreateFile() to create a HidD handle
	// to the object.
	//
	// Hack alert!  The correspondence between RIDI_DEVICENAME
	// and HidD CreateFile() path isn't offically documented in
	// any Microsoft material, as far as I can tell, which makes
	// it an implementation detail, which makes relying upon it
	// a design flaw.  This code would break if MSFT ever changed
	// the implementation, which in principle they could do at any 
	// time given that there's no documentation committing them
	// to it.  However, I'm relying on it anyway, for four reasons.  
	// 1) I need the information, and it seems to be the only way 
	// to get it.  2) The correspondence is widely mentioned on
	// the Internet as THE way to get HidD data for a Raw Input 
	// device.  3) I've found several open-source products and 
	// libraries that make the same assumption for the same 
	// reason, so Microsoft would clearly break lots of other 
	// products as well if they ever changed it.  4) It seems to 
	// be consistent across all existing Windows versions from XP
	// to Win 10.  This is all enough to convince me that Microsoft
	// must consider it a de facto feature of the public API, even
	// though they won't commit to it through documentation, and 
	// that they're stuck with it indefinitely.  Given how many
	// third-party products seem to rely on it, I'd be surprised
	// if Microsoft didn't also have plenty of their own code that
	// relies on it, so that's probably enough by itself to make
	// them carry it forward it indefinitely.
	HANDLE fp = CreateFile(
		devname, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		0, OPEN_EXISTING, 0, 0);
	WCHAR prodname[128] = L"";
	if (fp != 0)
	{
		// query the product name
		if (!HidD_GetProductString(fp, prodname, countof(prodname)))
			prodname[0] = 0;

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

	// Note the number of logical joysticks currently in our list.
	// This will let us infer whether or not we had to add a new
	// logical joystick for this physical joystick.
	size_t nLogJs = GetLogicalJoystickCount();

	// add it to the list
	auto it = physJoysticks.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(hDevice),
		std::forward_as_tuple(hDevice, devname, rid->dwVendorId, rid->dwProductId, prodname));

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

JoystickManager::PhysicalJoystick::PhysicalJoystick(
	HANDLE hRawDevice, const TCHAR *rawDeviceName,
	int vendorID, int productID, const TCHAR *prodName)
	: Joystick(vendorID, productID, prodName),
	hRawDevice(hRawDevice), rawDeviceName(rawDeviceName)
{
	// assign our logical joystick
	logjs = JoystickManager::GetInstance()->AddLogicalJoystick(this);

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
			}
			else
			{
				// single usage - use the usage number
				if (bc->NotRange.Usage > maxButtonIndex)
					maxButtonIndex = bc->NotRange.Usage;

				// there's just one button here
				nButtons = 1;
			}

			// find the report group item for this report ID
			ButtonReportGroup *brg = GetButtonReportGroup(bc->ReportID, bc->UsagePage);

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
	nButtons = maxButtonIndex + 1;
	buttonState.reset(new (std::nothrow) BYTE[nButtons]);

	// Set all initial button states to "off".  For most joysticks,
	// this will automatically update to the current actual physical
	// state as soon as we get our first report from the device,
	// since joysticks typically send all button states in every
	// input report (and typically send reports at regular intervals
	// even when nothing is changing).
	if (buttonState.get() != 0)
		memset(buttonState.get(), 0, nButtons);

	// if our logical joystick doesn't have enough buttons to cover
	// the entries in this physical unit, expand it
	if (logjs->nButtons < nButtons)
	{
		// save the old list
		std::unique_ptr<BYTE> oldp(logjs->buttonState.release());

		// allocate a new list
		logjs->buttonState.reset(new (std::nothrow) BYTE[maxButtonIndex + 1]);
		if (logjs->buttonState.get() == 0)
		{
			// uh-oh - couldn't allocate it; restore the old list
			logjs->buttonState.reset(oldp.release());
		}
		else
		{
			// copy the old list
			if (oldp.get() != 0)
				memcpy(logjs->buttonState.get(), oldp.get(), logjs->nButtons);

			// zero the new items
			memset(logjs->buttonState.get() + logjs->nButtons, 0,
				nButtons - logjs->nButtons);

			// remember the new count
			logjs->nButtons = nButtons;
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
	BYTE *bs = buttonState.get();

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
				// in the button group object.  The OnList has nButtons
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
						// shift a '1' into the low-order bit
						int button = nextOn[i];
						if ((bs[button] |= 0x02) == 0x02)
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
						if (bs[button] == 0x01)
						{
							// this button is now off - fire an event
							JoystickManager::GetInstance()->SendButtonEvent(
								this, button, false, foreground);

							// set its state to OFF (0)
							bs[button] = 0;

							// copy it to the logical joystick state as well
							if (button < logjs->nButtons)
								logjs->buttonState.get()[button] = 0;
						}
					}

					// Clean up the button states for next time, by
					// setting all of the ON button states to 0x01.
					for (unsigned int i = 0; i < usageLen; ++i)
					{
						// set this button state to ON (1)
						int button = nextOn[i];
						bs[button] = 1;

						// copy it to the logical joystick state as well
						if (button <= logjs->nButtons)
							logjs->buttonState.get()[button] = 1;
					}

					// And finally, the new ON list now becomes the prior
					// ON list for the next event.
					brg->lastOnIndex = nextOnIndex;
					brg->on[nextOnIndex].nOn = usageLen;
				}

				// Read the axis value updates
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
						if (val[iVal] != newVal)
						{
							val[iVal] = newVal;
							logjs->val[iVal] = newVal;
						}
					}
				}

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

void JoystickManager::SendButtonEvent(PhysicalJoystick *js, 
	int button, bool pressed, bool foreground)
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

JoystickManager::LogicalJoystick *JoystickManager::AddLogicalJoystick(
	int vendorID, int productID, const TCHAR *prodName)
{
	// search for an existing entry
	for (auto& jsLog : logicalJoysticks)
	{
		// check for a match on product name or VID/PID 
		if (jsLog.prodName == prodName
			|| (jsLog.vendorID == vendorID && jsLog.productID == productID))
			return &jsLog;
	}

	// No match - create a new one.  Figure the new item index.
	int localIndex = (int)logicalJoysticks.size();

	// add the item
	logicalJoysticks.emplace_back(localIndex, vendorID, productID, prodName);

	// return it
	return &logicalJoysticks.back();
}

JoystickManager::LogicalJoystick *JoystickManager::AddLogicalJoystick(
	const JoystickManager::PhysicalJoystick *jsPhys)
{
	return AddLogicalJoystick(jsPhys->vendorID, jsPhys->productID, jsPhys->prodName.c_str());
}

JoystickManager::LogicalJoystick *JoystickManager::GetLogicalJoystick(int index)
{
	return findifex(logicalJoysticks, [index](LogicalJoystick &j) { return j.index == index; });
}

