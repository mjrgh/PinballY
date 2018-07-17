// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Pinscape device interface.  This provides access to the USB HID
// interface for Pinscape Controller units.

#pragma once

class PinscapeDevice
{
public:
	PinscapeDevice(
		const TCHAR *devPath, const TCHAR *productString,
		USHORT vendorId, USHORT productId, USHORT versionNum);

	~PinscapeDevice();

	// Find devices.  This updates the device list with any new devices
	// that aren't already in the list, and removes any devices in the
	// list that are no longer present in the system.
	static void FindDevices(std::list<PinscapeDevice> &devices);

	// Is the device valid?  This returns true if it properly responded
	// to status queries during object creation.
	bool IsValid() const { return isValid; }

	// LedWiz unit number.  If the device is using an LedWiz VID/PID,
	// this is the nominal unit number, 1 to 16.  0 means that the
	// device is using a non-LedWiz VID/PID, so it won't be visible
	// to other software as an LedWiz.
	int LedWizUnitNo() const { return ledWizUnitNo; }

	// Get/set Night Mode for the unit
	bool IsNightMode();
	void SetNightMode(bool f);

protected:
	// query CPU information
	bool QueryCpuId(TSTRING &cpuId) { return QueryDeviceIdString(cpuId, 1); }
	bool QueryOpenSdaId(TSTRING &openSdaId) { return QueryDeviceIdString(openSdaId, 2); }

	// USB report ID for our command packets - always 0
	const BYTE CMD_REPORT_ID = 0;

	// query a device ID string: 
	//   1 = KL25Z CPU ID
	//   2 = OpenSDA TUID
	bool QueryDeviceIdString(TSTRING &s, int n);

	// query the firmware build timestamp
	bool QueryBuildId(DWORD &dd, DWORD &tt, TSTRING &s);

	// send a special request to the device
	typedef std::function<bool(const BYTE *)> SpecialRequestFilter;
	BYTE *SpecialRequest(BYTE requestId, SpecialRequestFilter filter = nullptr);
	BYTE *SpecialRequest(BYTE requestId, BYTE param0, SpecialRequestFilter filter = nullptr);
	BYTE *SpecialRequest(BYTE requestId, BYTE param0, BYTE param1, SpecialRequestFilter filter = nullptr);
	BYTE *SpecialRequest(const BYTE *request, SpecialRequestFilter filter = nullptr);

	// read a status report
	BYTE *ReadStatusReport();

	// read a USB report
	BYTE *ReadUSB();

	// write a USB report
	bool WriteUSB(const BYTE *data);

	// flush the input
	bool FlushUSBInput();

	// open our file handle
	HANDLE OpenHandle();

	// try reopening our device handle
	bool TryReopenHandle();

	// read/write file handle to the device
	HandleHolder fp;
	
	// event object for asynchronous reads
	HandleHolder ovEvent;

	// device path
	TSTRING devPath;

	// USB ID
	USHORT vendorId;
	USHORT productId;

	// LedWiz unit number.  This is the nominal unit number, from 1 
	// to 16.  If the device isn't using an LedWiz VID/PID, this is 0.
	int ledWizUnitNo;

	// product string, from the device
	TSTRING productString;

	// version number, from the device
	USHORT versionNum;

	// CPU and OpenSDA ID from the device
	TSTRING cpuId;
	TSTRING openSdaId;

	// firmware version data
	struct
	{
		DWORD date;		// firwmare date, in YYYYMMDD decimal format
		DWORD time;		// firmware time, in HHMMSS decimal format
		TSTRING s;		// printable YYYY-MM-DD-HHMM format
	} firmwareVersion;

	// Is the device valid?  This is true if the device responded
	// as expected to the status queries when we first found it.
	bool isValid;

	// Is the device still present?  FindDevices() clears this flag
	// for all existing devices until it can re-verify that it's
	// still in the active list.
	bool isPresent;

	// is the joystick interface enabled?
	bool joystickEnabled;

	// is the plunger enabled?
	bool plungerEnabled;

	// input and output report lengths
	int inputReportLength;
	int outputReportLength;
};
