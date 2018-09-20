// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// PinballX utilities

#pragma once

// Get the PinballX install path.  If 'refresh' is true, we'll search the
// registry again even if we've checked in the past; otherwise, we'll use
// cached information from the last search, if available.
const TCHAR *GetPinballXPath(bool refresh = false);

