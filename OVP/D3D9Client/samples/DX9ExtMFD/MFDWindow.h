// ==============================================================          
// Copyright (C) 2006 Martin Schweiger
// Licensed under the MIT License
// ==============================================================

#ifndef __MFDWINDOW_H
#define __MFDWINDOW_H

#define STRICT 1
#include <windows.h>
#include "spacexpansesdk.h"
#include "gcCoreAPI.h"

class MFDWindow: public ExternMFD {
public:
	MFDWindow (HINSTANCE _hInst, const MFDSPEC &spec);
	~MFDWindow ();
	void Initialise (HWND _hDlg);
	void SetVessel (OBJHANDLE hV);
	void SetTitle ();
	void Resize();
	void CheckAspect (LPRECT, DWORD);
	void RepaintButton (HWND hWnd);
	void RepaintDisplay(HWND hWnd);
	void ProcessButton (int bt, int event);
	void StickToVessel (bool stick);

	void clbkRefreshDisplay (SURFHANDLE);
	void clbkRefreshButtons ();
	void clbkFocusChanged (OBJHANDLE hFocus);

private:
	RECT wr;
	HSWAP hSwap;
	HINSTANCE hInst;  // instance handle
	HWND hDlg, hDsp;  // dialog and MFD display handles
	HFONT hBtnFnt;    // button font
	int BW, BH, ds;   // button width and height, display size
	int gap;          // geometry parameters
	int fnth;         // button font height
	bool vstick;      // stick to vessel
	bool bFailed;
};

#endif // !__MFDWINDOW_H
