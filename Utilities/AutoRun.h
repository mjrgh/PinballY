// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

class ErrorHandler;

// Set up to launch the program automatically at system startup.  (More
// specifically, at user logon.)
//
// If 'add' is true, we'll add or update the launch setup, otherwise we'll
// remove it.  
//
// We use Task Scheduler to effect the auto-launch, creating a task named
// "<desc> Startup Task" that launches the given program when the current
// user logs on.
//
bool SetUpAutoRun(bool add, const TCHAR *desc, 
	const TCHAR *exe, const TCHAR *params, bool adminMode, 
	ErrorHandler &eh);

// Get the auto-launch state in Task Scheduler.  Returns true on success,
// false on failure.
bool GetAutoRunState(const TCHAR *desc, bool &exists, 
	TSTRING &exe, TSTRING &params, bool &adminMode, 
	ErrorHandler &eh);


