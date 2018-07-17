// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <regex>
#include <stdlib.h>
#include <memory.h>
#include <SetupAPI.h>
#include <hidsdi.h>
#include "PinscapeDevice.h"

#pragma comment(lib, "setupapi.lib")

void PinscapeDevice::FindDevices(std::list<PinscapeDevice> &devices)
{
	// get the list of devices matching the HID class GUID
	GUID hidGuid;
	HidD_GetHidGuid(&hidGuid);
	HDEVINFO hDevInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_DEVICEINTERFACE);

	// Mark each device already in the list as "not present".  We'll
	// change this back to "present" if we find it on our new search.
	for (auto &d : devices)
		d.isPresent = false;

	// iterate over the devices in the list
	SP_DEVICE_INTERFACE_DATA did;
	did.cbSize = sizeof(did);
	for (UINT i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &did); ++i)
	{
		// get the size of the detail structure
		DWORD detailSize = 0;
		SetupDiGetDeviceInterfaceDetail(hDevInfo, &did, NULL, 0, &detailSize, NULL);

		// allocate the detail struct
		std::unique_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> detailData(
			(PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(detailSize));

		// read it
		detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		SP_DEVINFO_DATA devinfoData;
		devinfoData.cbSize = sizeof(devinfoData);
		if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &did, detailData.get(), detailSize, &detailSize, &devinfoData))
		{
			// open the device as a file
			HandleHolder fp(CreateFile(
				detailData->DevicePath,
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL, OPEN_EXISTING, 0, NULL));

			if (fp == 0 || fp == INVALID_HANDLE_VALUE)
				continue;

			// read the attributes
			HIDD_ATTRIBUTES attrs;
			if (!HidD_GetAttributes(fp, &attrs))
				continue;

			// read the product name
			TCHAR name[256];
			if (!HidD_GetProductString(fp, name, countof(name)))
				continue;

			// check for a match to our product name pattern
			std::basic_regex<TCHAR> pspat(_T(".*\\bpinscape controller\\b.*"), std::regex_constants::icase);
			if (std::regex_match(name, pspat))
			{
				// It's a Pinscape device.  Search the existing list to see
				// if it's already there.
				bool found = false;
				for (auto &d : devices)
				{
					// match on the Windows device path
					if (d.devPath == detailData->DevicePath)
					{
						// got it - mark the existing entry as still present
						d.isPresent = true;

						// note that we found it and stop searching
						found = true;
						break;
					}
				}

				// if we didn't find it, add a new entry for it
				if (!found)
				{
					// add the new entry
					devices.emplace_front(detailData->DevicePath, name,
						attrs.VendorID, attrs.ProductID, attrs.VersionNumber);

					// Check the newly created device to make sure it responded
					// properly to the setup queries.  Since we initially match on
					// the product string, it's possible (although unlikely) that
					// we could have false positives for non-Pinscape devices. 
					// Other devices won't use the same USB protocol, though, so
					// we'll reduce the chances of another device making it into
					// our list by checking for proper protocol responses.
					if (!devices.front().IsValid())
						devices.erase(devices.begin());
				}
			}
		}
	}

	// Go back through the list and remove any entries that we
	// didn't find on this search.  USB devices can be added and 
	// removed at any time, so a device we found on an earlier
	// search might have been removed since we found it.
	for (auto it = devices.begin(); it != devices.end();)
	{
		// Get the next device, in case we delete the current
		// one.  Deleting the current one will make the current
		// item iterator invalid, so we have to get the next
		// ahead of any possible deletion.
		auto next = it;
		++next;

		// if this item is no longer present, remove it
		if (!it->isPresent)
			devices.erase(it);

		// move on to the next item (the one we saved earlier)
		it = next;
	}
}

PinscapeDevice::PinscapeDevice(
	const TCHAR *devPath, const TCHAR *productString,
	USHORT vendorId, USHORT productId, USHORT versionNum) :
	devPath(devPath),
	productString(productString),
	vendorId(vendorId),
	productId(productId),
	versionNum(versionNum),
	isValid(false),
	isPresent(true),
	inputReportLength(65),
	outputReportLength(65),
	joystickEnabled(false),
	plungerEnabled(false),
	ledWizUnitNo(0)
{
	// open the file
	fp = OpenHandle();

	// create our overlapped I/O event object
	ovEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	// If we're using an LedWiz VID/PID, note the LedWiz unit
	// number.  The nominal unit number is from 1 to 16, based
	// on the PID.
	if (vendorId == 0xFAFA && (productId >= 0x00F0 && productId <= 0x00FF))
		ledWizUnitNo = productId - 0x00F0 + 1;

	// Check the HID interface to see if the HID usage type is
	// type 4, for Joystick.  If so, the joystick interface on
	// the device is enabled and it uses that to send its status
	// reports.  If not, it's not emulating a joystick, and uses 
	// our private status interface instead.
	PHIDP_PREPARSED_DATA ppData;
	if (HidD_GetPreparsedData(fp, &ppData))
	{
		HIDP_CAPS caps;
		if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS)
		{
			// Check the usage.  If the joystick is enabled, the usage
			// will be Usage Page 1 ("generic desktop"), Usage 4 
			// ("joystick").  If not, the usage is 0 (undefined),
			// indicating our private status interface.
			joystickEnabled = caps.UsagePage == 1 && caps.Usage == 4;
			bool isPrivate = caps.UsagePage == 1 && caps.Usage == 0;

			// It's valid if we found one of our expected interfaces,
			// and the output (PC-to-device) report length is nonzero
			if ((joystickEnabled || isPrivate) && caps.OutputReportByteLength > 0)
				isValid = true;

			// remember the input and output report lengths
			inputReportLength = caps.InputReportByteLength;
			outputReportLength = caps.OutputReportByteLength;
		}

		// free the preparsed data
		HidD_FreePreparsedData(ppData);
	}

	// if the device looks valid so far, try reading a status report
	if (isValid)
	{
		// read a report
		std::unique_ptr<BYTE> buf(ReadStatusReport());
		if (buf != nullptr)
		{
			// note the plunger status
			plungerEnabled = buf.get()[1] & 0x01;
		}
		else
		{
			// couldn't read a report - mark the device as invalid
			isValid = false;
		}
	}

	// if it's still looking good, query some more data from the device
	if (isValid)
	{
		// query the CPUID
		QueryCpuId(cpuId);
		QueryOpenSdaId(openSdaId);

		// query the firmware build ID
		QueryBuildId(firmwareVersion.date, firmwareVersion.time, firmwareVersion.s);
	}
}

PinscapeDevice::~PinscapeDevice()
{
}

bool PinscapeDevice::IsNightMode()
{
	// Read a status report
	std::unique_ptr<BYTE> buf(ReadStatusReport());
	if (buf == nullptr)
		return false;

	// Night Mode is indicated by bit 0x02 of the first byte
	// in the status report packet
	return buf.get()[1] & 0x02;
}

void PinscapeDevice::SetNightMode(bool f)
{
	// send the Set Night Mode request (special request 8)
	SpecialRequest(8, f ? 1 : 0);
}

bool PinscapeDevice::QueryDeviceIdString(TSTRING &s, int n)
{
	// send the device ID query and await the response (0x00 0x90 <index> ...)
	std::unique_ptr<BYTE> r(SpecialRequest(7, (BYTE)n, [n](const BYTE *r) {
		return r[1] == 0x00 && r[2] == 0x90 && r[3] == n;
	}));

	// if that failed, return failure
	if (r == nullptr)
		return false;

	// decode the response
	const BYTE *buf = r.get();
	s = MsgFmt(_T("%02x%02x-%02x%02x%02x%02x-%02x%02x%02x%02x"),
		buf[4], buf[5],
		buf[6], buf[7], buf[8], buf[9],
		buf[10], buf[11], buf[12], buf[13]);

	// success
	return true;
}

bool PinscapeDevice::QueryBuildId(DWORD &dd, DWORD &tt, TSTRING &s)
{
	// send the build ID request and await the response (0x00 0xA0 ...)
	std::unique_ptr<BYTE> r(SpecialRequest(10, [](const BYTE *r) {
		return r[1] == 0x00 && r[2] == 0xA0;
	}));

	// if that failed, return failure
	if (r == nullptr)
		return false;

	// decode the response into the date and time values, each
	// of which is expressed as a 32-bit LE value
	const BYTE *b = r.get();
	dd = b[3] | (b[4] << 8) | (b[5] << 16) | (b[6] << 24);
	tt = b[7] | (b[8] << 8) | (b[9] << 16) | (b[10] << 24);

	// format the string as YYYY-MM-DD-HHMM
	s = MsgFmt(_T("%04d-%02d-%02d-%02d%02d"),
		dd/10000 % 10000, dd/100 % 100, dd % 100,
		tt/10000 % 100, tt/100 % 100);

	// success
	return true;
}

BYTE *PinscapeDevice::ReadStatusReport()
{
	// flush any buffered reports so that we get the real-time status
	FlushUSBInput();

	// Read reports until we get a status report.  The device sends
	// status reports at regular intervals when we're not making 
	// other requests, so we just have to read until we get the
	// status report type.
	for (int i = 0; i < 32; ++i)
	{
		// read a report; fail if we can't read it
		std::unique_ptr<BYTE> buf(ReadUSB());
		if (buf == nullptr)
			return nullptr;

		// if it's a status report, the high bit of byte 2 will be clear
		if ((buf.get()[2] & 0x80) == 0x00)
			return buf.release();
	}

	// we didn't get a status report within the retry limit - fail
	return nullptr;
}

BYTE *PinscapeDevice::SpecialRequest(BYTE requestId, SpecialRequestFilter filter)
{
	std::unique_ptr<BYTE> buf(new BYTE[outputReportLength]);
	buf.get()[0] = CMD_REPORT_ID;
	buf.get()[1] = 0x41;		// Pinscape special request
	buf.get()[2] = requestId;	// request ID
	ZeroMemory(buf.get() + 3, outputReportLength - 3);
	return SpecialRequest(buf.get(), filter);
}

BYTE *PinscapeDevice::SpecialRequest(BYTE requestId, BYTE param0, SpecialRequestFilter filter)
{
	std::unique_ptr<BYTE> buf(new BYTE[outputReportLength]);
	buf.get()[0] = CMD_REPORT_ID;
	buf.get()[1] = 0x41;		// Pinscape special request
	buf.get()[2] = requestId;	// request ID
	buf.get()[3] = param0;
	ZeroMemory(buf.get() + 4, outputReportLength - 4);
	return SpecialRequest(buf.get(), filter);
}

BYTE *PinscapeDevice::SpecialRequest(BYTE requestId, BYTE param0, BYTE param1, SpecialRequestFilter filter)
{
	std::unique_ptr<BYTE> buf(new BYTE[outputReportLength]);
	buf.get()[0] = CMD_REPORT_ID;
	buf.get()[1] = 0x41;		// Pinscape special request
	buf.get()[2] = requestId;	// request ID
	buf.get()[3] = param0;
	buf.get()[4] = param1;
	ZeroMemory(buf.get() + 5, outputReportLength - 5);
	return SpecialRequest(buf.get(), filter);
}

BYTE *PinscapeDevice::SpecialRequest(const BYTE *request, SpecialRequestFilter filter)
{
	// if the caller provided a filter, they want the reply, so
	// flush any pending input first, as the response can't
	// already be in the buffer
	if (filter != nullptr)
		FlushUSBInput();

	// send the request packet to the device
	if (!WriteUSB(request))
		return nullptr;

	// if a filter wasn't provided, the caller doesn't care about
	// a response, so we're done
	if (filter == nullptr)
		return nullptr;

	// await the reply
	for (int tries = 0; tries < 16; ++tries)
	{
		// read a reply
		std::unique_ptr<BYTE> reply(ReadUSB());

		// check it against the filter - if it passes, return it to the caller
		if (reply != nullptr && filter(reply.get()))
			return reply.release();
	}

	// we didn't get a matching response within the retry limit
	return nullptr;
}

BYTE *PinscapeDevice::ReadUSB()
{
	// set up a buffer for the response
	std::unique_ptr<BYTE> buf(new BYTE[inputReportLength]);

	// retry a few times if necessary, in case the connection is interrupted
	for (int tries = 0; tries < 3; ++tries)
	{
		// set up a non-blocking read
		OVERLAPPED ov;
		ZeroMemory(&ov, sizeof(ov));
		ov.hEvent = ovEvent;
		buf.get()[0] = 0;
		ReadFile(fp, buf.get(), inputReportLength, NULL, &ov);

		// Wait briefly for the read to complete
		if (WaitForSingleObject(ovEvent, 100) == WAIT_OBJECT_0)
		{
			// the read completed successfully - get the result
			DWORD readLen;
			if (!GetOverlappedResult(fp, &ov, &readLen, FALSE))
			{
				// the read failed - try re-opening the file handle
				if (TryReopenHandle())
					continue;

				// failed to reopen the handle - return failure
				return nullptr;
			}
			else if (readLen != inputReportLength)
			{
				// the read length didn't match what we expected - fail
				return nullptr;
			}
			else
			{
				// success - detach the buffer from the unique_ptr and return it
				return buf.release();
			}
		}
		else
		{
			// The read timed out, or the wait failed.  Cancel the
			// async I/O request.
			CancelIo(fp);

			// try reopening the handle, in case the connection was dropped
			if (TryReopenHandle())
				continue;

			// failed
			return nullptr;
		}
	}

	// return failure if we exceeded the retry limit
	return nullptr;
}

// flush the input
bool PinscapeDevice::FlushUSBInput()
{
	// note the starting time
	DWORD now = GetTickCount();

	// set up a buffer for our reads
	std::unique_ptr<BYTE> buf(new BYTE[inputReportLength]);

	// wait until a read would block, or we exceed a maximum delay
	while (GetTickCount() - now < 100)
	{
		// set up a non-blocking read
		OVERLAPPED ov;
		ZeroMemory(&ov, sizeof(ov));
		ov.hEvent = ovEvent;
		buf.get()[0] = 0;
		ReadFile(fp, buf.get(), inputReportLength, NULL, &ov);

		// check to see if it's ready immediately
		if (WaitForSingleObject(ovEvent, 0) == WAIT_OBJECT_0)
		{
			// it's ready - complete the read
			DWORD readLen;
			GetOverlappedResult(fp, &ov, &readLen, FALSE);
		}
		else
		{
			// Not ready - we've have to wait to do a read, so the USB
			// input buffer must be empty, meaning we've accomplished
			// the input flush.  Cancel the read and return.
			CancelIo(fp);
			return true;
		}
	}

	// we timed out before emptying the input - return failure
	return false;
}

// write a USB report
bool PinscapeDevice::WriteUSB(const BYTE *data)
{
	// retry a few times in case the connection is momentarily dropped
	for (int tries = 0; tries < 3; ++tries)
	{
		// set up a non-blocking write
		OVERLAPPED ov;
		ZeroMemory(&ov, sizeof(ov));
		ov.hEvent = ovEvent;
		WriteFile(fp, data, outputReportLength, NULL, &ov);

		// wait briefly for completion
		if (WaitForSingleObject(ovEvent, 250) == WAIT_OBJECT_0)
		{
			// the write completed - get the result
			DWORD bytesWritten;
			if (!GetOverlappedResult(fp, &ov, &bytesWritten, FALSE))
			{
				// write failed - try re-opening the handle and retrying
				if (TryReopenHandle())
					continue;

				// couldn't reopen it - return failure
				return false;
			}
			else if (bytesWritten != outputReportLength)
			{
				// wrong length - fail
				return false;
			}
			else
			{
				// success
				return true;
			}
		}
		else
		{
			// The write timed out.  Cancel the I/O and try reopening
			// the handle.
			CancelIo(fp);
			if (TryReopenHandle())
				continue;

			// couldn't reopen it - fail
			return false;
		}
	}

	// we exceed the maximum number of retries - fail
	return false;
}

HANDLE PinscapeDevice::OpenHandle()
{
	return CreateFile(
		devPath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
}

bool PinscapeDevice::TryReopenHandle()
{
	// If the last error is 6 ("invalid handle") or 1167 ("device not
	// connected"), the problem could be that the USB connection was
	// momentarily interrupted, in which case we can usually cure it
	// by opening a new handle to the device.
	DWORD err = GetLastError();
	if (err == 6 || err == 1167)
	{
		// try opening a new handle
		HANDLE newfp = OpenHandle();
		
		// if that succeeded, replace the old handle and return success
		if (newfp != NULL && newfp != INVALID_HANDLE_VALUE)
		{
			fp = newfp;
			return true;
		}
	}

	// We either decided there was no point in trying a new handle,
	// or we failed to open one.  In either case there's no point in
	// retrying whatever the caller was trying.
	return false;
}
