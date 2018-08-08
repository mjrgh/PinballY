#include "DialogWithSavedPos.h"
#include "Application.h"
#include "../Utilities/Config.h"

INT_PTR DialogWithSavedPos::Proc(UINT message, WPARAM wParam, LPARAM lParam)
{
	// on destroy, save our position
	if (message == WM_DESTROY)
		Application::Get()->SaveDialogPos(hDlg, configVar.c_str());

	// do the base class work 
	INT_PTR result = __super::Proc(message, wParam, lParam);

	// on dialog initialization, restore the saved position if possible
	if (message == WM_INITDIALOG)
		Application::Get()->InitDialogPos(hDlg, configVar.c_str());

	// return the base class result
	return result;
}
