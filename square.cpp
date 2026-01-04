#include <Windows.h>
#include "square.h"

#define SQUARE_DIALOG_SIZE 400

void SetPixelsAndSide(const std::vector<Pixel>& pixels, UINT side)
{
	g_pixels = pixels;
	g_side = side;

	if (g_hwndSquare)
		InvalidateRect(g_hwndSquare, nullptr, FALSE);
}


void DisplaySquareDialog(HWND parent)
{
	if (g_hwndSquare) return;

	HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE);
	RegisterSquareDialogClass(hInst);

	g_hwndSquare = CreateWindowExW(
		WS_EX_CLIENTEDGE,
		L"SquareDialodWndClass",
		L"Square",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		400, 400,
		SQUARE_DIALOG_SIZE,
		SQUARE_DIALOG_SIZE,
		parent,
		nullptr,
		hInst,
		nullptr);
}



LRESULT CALLBACK SquareDialodMainProcedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_COMMAND:
			break;

		case WM_DESTROY:
			g_hwndSquare = nullptr;
			g_pixels.clear();
			g_side = 0;
			break;

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			if (!g_pixels.empty() && g_side > 0)
			{
				BITMAPINFO bmi{};
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = g_side;
				bmi.bmiHeader.biHeight = -((LONG)g_side);
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biBitCount = 32;
				bmi.bmiHeader.biCompression = BI_RGB;

				RECT rc;
				GetClientRect(hwnd, &rc);

				StretchDIBits(
					hdc,
					0, 0, rc.right, rc.bottom,
					0, 0, g_side, g_side,
					g_pixels.data(),
					&bmi,
					DIB_RGB_COLORS,
					SRCCOPY);
			}

			EndPaint(hwnd, &ps);
			return 0;
		}
		default: return DefWindowProc(hwnd, msg, wp, lp);
	}
}
void RegisterSquareDialogClass(HINSTANCE hInst)
{
	static bool registered = false;
	if (registered)
		return;

	WNDCLASS wc{};
	wc.lpfnWndProc = SquareDialodMainProcedure;
	wc.hInstance = hInst;
	wc.lpszClassName = L"SquareDialodWndClass";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

	RegisterClassW(&wc);
	registered = true;
}