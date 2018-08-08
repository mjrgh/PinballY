// Dialog with saved position

#pragma once
#include "../Utilities/Dialog.h"

class DialogWithSavedPos : public Dialog
{
public:
	DialogWithSavedPos(const TCHAR *configVar) : configVar(configVar) { }

protected:
	// dialog box procedure
	virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam);

	// configuration variable for the saved position information
	TSTRING configVar;
};
