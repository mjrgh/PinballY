// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include "Config.h"
#include "../Utilities/InputManager.h"

class InputManagerWithConfig : public InputManager, public ConfigManager::Subscriber
{
public:
	InputManagerWithConfig();

	static bool Init() { return InputManager::Init(new InputManagerWithConfig()); }

	// Load/store the settings from/to the config manager.
	// These only read/write the in-memory config data; if you
	// want to update the on-disk file, use Save() in the 
	// config manager.
	void LoadConfig();
	void StoreConfig();

	// On config file reloads, reload our configuration
	virtual void OnConfigReload() override { LoadConfig(); }
};
