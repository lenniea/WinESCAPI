// winescapi.cpp : Defines the entry point for the application.
//

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>
#include <stdio.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include "resource.h"
#include "escapi.h"

#define WIDTH		320
#define HEIGHT		240
#define MAX_STR		80

// Global Variables:
HINSTANCE hInst;						// current instance
TCHAR szTitle[MAX_STR];					// The title bar text
const TCHAR szWindowClass[] = _T("WINESCAPI");
UINT uDevices;
int width = WIDTH;
int height = HEIGHT;
BOOL bRaw = FALSE;
int iDevice = -1;
int* buf = NULL;
DWORD dwThreadId = 0;
UINT uFrame = 0;
HANDLE hThread = INVALID_HANDLE_VALUE;

void MsgBox(const TCHAR* msg)
{
	MessageBox(NULL, msg, szTitle, MB_OK | MB_ICONINFORMATION);
}

void DrawImage(HDC hdc, LPBITMAPINFOHEADER lpbi, void* bits, const RECT& target_rect)
{
	int height = lpbi->biHeight;
    ::StretchDIBits(
         hdc,                                    // Target device HDC
         target_rect.left,                       // X sink position
         target_rect.top,                        // Y sink position
         target_rect.right - target_rect.left,   // Destination width
         target_rect.bottom - target_rect.top,   // Destination height
         0,                                      // X source position
         height,                                 // Adjusted Y source position
         lpbi->biWidth,                          // Source width
         -height,                                // Source height
         bits,                                   // Image data
         (LPBITMAPINFO)lpbi,                     // DIB header
         DIB_PAL_COLORS,                         // Type of palette
         SRCCOPY);                               // Simple image copy 
}

void ShowFrame(HWND hWnd)
{
	RECT rect;

	HDC hDC = GetDC(hWnd);
	BITMAPINFO bmi;
	memset(&bmi, 0, sizeof(bmi));
	LPBITMAPINFOHEADER pBMI = &bmi.bmiHeader;
	pBMI->biSize = sizeof(BITMAPINFOHEADER);
	pBMI->biWidth = width;
	pBMI->biHeight = height;
	pBMI->biPlanes = 1;
	pBMI->biBitCount = 32;

	rect.left = 0; rect.top = 0;
	rect.right = width; rect.bottom = height;
	DrawImage(hDC, pBMI, buf, rect);
	ReleaseDC(hWnd, hDC);
}

#define SWAP16(x)   ((((x) >> 8) & 0xFF) | (((x) & 0xFF) << 8))
#define SWAP32(x)   (((SWAP16(x >> 16)) & 0xFFFF) | (SWAP16(x) << 16))

#define FLF_MAGIC	0x59A66A95

void WriteFrame(void* buf, short width, short height, int num)
{
	/* Write raw capture buffer to file */
	size_t pixels = width * height;
	TCHAR filename[MAX_STR];
	wsprintf(filename, _T("frame%03u.flf"), num);
	FILE* fp = _tfopen(filename, _T("wb"));
	DWORD flfheader[8];
	flfheader[0] = SWAP32(FLF_MAGIC);
	flfheader[1] = SWAP32(width);
	flfheader[2] = SWAP32(height);
	flfheader[3] = SWAP32(16);
	flfheader[4] = 0;
	flfheader[5] = SWAP32(1);
	flfheader[6] = 0;
	flfheader[7] = 0;

	fwrite(flfheader, 32, 1, fp);
	unsigned short *ptr = (unsigned short*) buf;
	for (int p = 0; p < pixels; ++p)
	{
		unsigned short pixel = *ptr++;
		pixel = (pixel >> 8) | (pixel << 8);
		fwrite(&pixel, 1, sizeof(pixel), fp);
	}
	fclose(fp);
}

DWORD WINAPI CaptureThread(LPVOID pVoid)
{
	HWND hWnd = (HWND) pVoid;
	while (iDevice >= 0)
	{
		/* request a capture */			
		doCapture(iDevice);
		
        DWORD dwStart = GetTickCount();
		while (isCaptureDone(iDevice) == 0 && GetTickCount() - dwStart < 1000)
		{
			/* Wait until capture is done.
			 * Warning: if capture init failed, or if the capture
			 * simply fails (i.e, user unplugs the web camera), this
			 * will be an infinite loop.
			 */		   
		}
		if (bRaw)
		{
			WriteFrame(buf, width, height, uFrame++);
		}
		else
		{
			ShowFrame(hWnd);
		}
	}
	deinitCapture(iDevice);
	hThread = INVALID_HANDLE_VALUE;
	return 0;
}

void OpenDevice(HWND hWnd, UINT device)
{
	HMENU hMenu = GetMenu(hWnd);

	// Check Menu Item for device
	for (UINT id = 0; id < uDevices; ++id)
	{
		UINT uChecked = (id == device) ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED |MF_BYCOMMAND;
		CheckMenuItem(hMenu, IDM_DEVICE_0 + id, uChecked);
	}
	if (iDevice >= 0)
	{
		// Tell capture thread to exit
		iDevice = -1;
		DWORD dwStart = GetTickCount();
		// Wait up to 1 second for device to close
		while (hThread != INVALID_HANDLE_VALUE && GetTickCount() - dwStart < 1000)
		{
			Sleep(100);
		}
	}
	// Set device for CaptureThread
	iDevice = device;

	// free (previous capture buffer)
	if (buf)
	{
		delete buf;
	}
	// Allocate capture buffer
	buf = new int[width * height];

   /* Set up capture parameters.
    * ESCAPI will scale the data received from the camera 
    * (with point sampling) to whatever values you want. 
    * Typically the native resolution is 320*240.
    */
	struct SimpleCapParams capture;
	capture.mWidth = width;
	capture.mHeight = height;
	capture.mTargetBuf = buf;

	DWORD dwOptions = bRaw ? CAPTURE_OPTION_RAWDATA : 0;
	
	if (initCaptureWithOptions(device, &capture, dwOptions) == 0)
	{
		MsgBox(_T("Capture failed - device may already be in use"));
		return;
	}
	if (hThread == INVALID_HANDLE_VALUE)
	{
		hThread = CreateThread(NULL, /*dwStackSize=*/0, CaptureThread, (LPVOID) hWnd, /*dwCreationFlags=*/0, &dwThreadId);
	}
}

UINT GetSelectedDevice(HMENU hMenu)
{
	UINT id;
	for (id = 0; id < uDevices; ++id)
	{
		UINT uChecked = GetMenuState(hMenu, IDM_DEVICE_0 + id, MF_BYCOMMAND);
		if (uChecked & MF_CHECKED)
		{
			break;
		}
	}
	return id;
}

// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK       CaptureDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	AboutDlgProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	MSG msg;

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_STR);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow))
	{
		return FALSE;
	}

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINESCAPI));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_WINESCAPI);
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(hInstance, MAKEINTRESOURCE(IDC_WINESCAPI));

	return RegisterClassEx(&wcex);
}

const SIZE GetWindowSize()
{
	const int cxFrame = GetSystemMetrics(SM_CXFRAME);
	const int cyMenu = GetSystemMetrics(SM_CYMENU);
	const int cyCaption = GetSystemMetrics(SM_CYCAPTION);
	const int cyFrame = GetSystemMetrics(SM_CYFRAME);
	SIZE size;
	size.cx = width + cxFrame * 2;
	size.cy = height + cyMenu + cyCaption + cyFrame * 2;
	return size;
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

    const SIZE size = GetWindowSize();

    HWND hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
           CW_USEDEFAULT, 0, size.cx, size.cy, NULL, NULL, hInstance, NULL);

    if (!hWnd)
    {
        return FALSE;
    }
	
	/* Initialize ESCAPI */
	
	uDevices = setupESCAPI();

	if (uDevices == 0)
	{
		MsgBox(_T("ESCAPI initialization failure or no devices found"));
		return FALSE;
	}

	// Enumerate devices for File menu
    HMENU hMenu = GetMenu(hWnd);
	HMENU hFileMenu = GetSubMenu(hMenu, 0);
	for (UINT id = 0; id < uDevices; ++id)
	{
		char szDevice[MAX_STR];
		TCHAR szMenuText[MAX_STR+10];
		int len = wsprintf(szMenuText, _T("&%u "), id);
		// Get device name from ESCAPI
		getCaptureDeviceName(id, szDevice, MAX_STR);
		// Concatenate device name as TCHAR string
		mbstowcs(szMenuText + len, szDevice, MAX_STR - len);
		InsertMenu(hFileMenu, id, MF_BYPOSITION, IDM_DEVICE_0 + id, szMenuText);
	}
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;
	HMENU hMenu = GetMenu(hWnd);

	switch (message)
	{
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, AboutDlgProc);
			break;
		case IDM_CLOSE:
			iDevice = -1;
			break;
		case IDM_DEVICE_0:
		case IDM_DEVICE_1:
		case IDM_DEVICE_2:
		case IDM_DEVICE_3:
		case IDM_DEVICE_4:
		case IDM_DEVICE_5:
		case IDM_DEVICE_6:
		case IDM_DEVICE_7:
		case IDM_DEVICE_8:
		case IDM_DEVICE_9:
			OpenDevice(hWnd, wmId - IDM_DEVICE_0);
			break;
		case IDM_CAPTURE:
			if (DialogBox(hInst, MAKEINTRESOURCE(IDD_CAPTURE), hWnd, CaptureDlgProc) == IDOK)
			{
				// Resize window
				const SIZE size = GetWindowSize();
				SetWindowPos(hWnd, NULL, 0, 0, size.cx, size.cy, SWP_NOZORDER|SWP_NOMOVE);
			}
			break;
			{
				int device = GetSelectedDevice(hMenu);
//				DoCapture(hWnd, device, width, height, buf);
				delete buf;
			}
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		// TODO: Add any drawing code here...
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// Message handler for Capture Dialog box.
BOOL CALLBACK CaptureDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	WORD wId;

	switch (message)
	{
	case WM_INITDIALOG:
		SetDlgItemInt(hDlg, IDC_WIDTH, width, FALSE);
		SetDlgItemInt(hDlg, IDC_HEIGHT, height, FALSE);
		CheckDlgButton(hDlg, IDC_RAW, bRaw);
		return TRUE;

	case WM_COMMAND:
		wId = LOWORD(wParam);
		if (wId == IDOK || wId == IDCANCEL)
		{
			if (wId == IDOK)
			{
				BOOL bOK = FALSE;
				width = GetDlgItemInt(hDlg, IDC_WIDTH, &bOK, FALSE);
				height = GetDlgItemInt(hDlg, IDC_HEIGHT, &bOK, FALSE);
				bRaw = IsDlgButtonChecked(hDlg, IDC_RAW);
			}
			EndDialog(hDlg, wId);
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}


// Message handler for About Dialog box.
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
