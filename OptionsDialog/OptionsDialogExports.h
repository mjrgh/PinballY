// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Options dialog exports.  This defines the public interface exported
// from the DLL.
//
#pragma once

typedef std::function<void()> ConfigSaveCallback;
extern "C" void WINAPI ShowOptionsDialog(ConfigSaveCallback configSaveCallback);
