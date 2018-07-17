// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class CoinsDialog : public OptionsPage
{
	DECLARE_DYNAMIC(CoinsDialog)

public:
	CoinsDialog(int dialogId);
	virtual ~CoinsDialog();

    virtual BOOL OnInitDialog() override;
    virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
	virtual void InitVarMap() override;

	// most recent "custom" model, in the display format
	CString lastCustom;

	// pricing model drop list
	CComboBox cbPricing;

	// standard pricing levels, for the "Pricing Model" dropdown
	struct PricingModel
	{
		const TCHAR *name;
		struct Level {
			float value;
			float credits;
		} levels[10];

		// convert this model to a string for the details field
		TSTRING ToString() const;
		void FromString(const TCHAR *str);
	};
	static const PricingModel pricingModels[];

	// find the predefined pricing model matching the pricing field value
	const PricingModel *FindPricingModel();

	// sync the pricing model popup with the current text field contents
	void SyncPricingPopupWithText();

	// Special VarMap subclass for the custom pricing data
	struct PricingVarMap : EditStrMap
	{
		 PricingVarMap(const TCHAR *configVar, int controlID, const TCHAR *defVal) :
			EditStrMap(configVar, controlID, defVal) { }

		virtual TSTRING FromConfig(const TCHAR *str) override;
		virtual TSTRING ToConfig(const TCHAR *str) override;
		virtual bool IsModifiedFromConfig() override;
	};
};

