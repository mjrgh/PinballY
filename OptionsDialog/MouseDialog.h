// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class MouseDialog : public OptionsPage
{
	DECLARE_DYNAMIC(MouseDialog)

public:
	MouseDialog(int dialogId);
	virtual ~MouseDialog();

	virtual void InitVarMap() override;

	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;

protected:
	DECLARE_MESSAGE_MAP()

	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd *pWnd);

	// capturing mouse off-screen hiding coordinates
	bool capturingCoords = false;
};

