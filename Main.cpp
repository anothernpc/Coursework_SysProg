#include <windows.h>
#include <windowsx.h>

#include "Main.h"

#define ID_FILE_OPEN   1001
#define ID_FILE_EXIT   1002
#define ID_HELP_ABOUT  1003
#define ID_DRAW_START   4001
#define ID_DRAW_STOP    4002
#define ID_DRAW_CLEAR   4003

bool g_drawEnabled = false;


void CreateMainMenu(HWND hwnd)
{
    HMENU hMenuBar = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    HMENU hHelp = CreatePopupMenu();
    HMENU hDraw = CreatePopupMenu();

    AppendMenuW(hFile, MF_STRING, ID_FILE_OPEN, L"Open");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, ID_FILE_EXIT, L"Exit");

    AppendMenuW(hHelp, MF_STRING, ID_HELP_ABOUT, L"About");

    AppendMenuW(hDraw, MF_STRING, ID_DRAW_START, L"Start drawing");
    AppendMenuW(hDraw, MF_STRING, ID_DRAW_STOP, L"Stop drawing");
    AppendMenuW(hDraw, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hDraw, MF_STRING, ID_DRAW_CLEAR, L"Clear");

    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFile, L"File");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hHelp, L"Help");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hDraw, L"Draw");

    SetMenu(hwnd, hMenuBar);
}

LRESULT OnCreate(HWND hwnd) {
    g_hwnd = hwnd;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return -1;
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return -1;
    CreateMainMenu(hwnd);
    CreateVideoWindowAndButtons(hwnd);
    CreateD3D11Device(hr, hwnd);
    OverlayD2D_Init(g_pD3DDevice);
    CreateMediaEngine(hr, hwnd);

    return 0;
}


LRESULT OnEraseBkgnd(HWND hwnd, WPARAM wParam)
{
    HDC hdc = (HDC)wParam;
    RECT rc; GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));

    RECT line = { 0, (LONG)g_videoClientH, rc.right, (LONG)g_videoClientH + 1 };
    FillRect(hdc, &line, (HBRUSH)(COLOR_3DSHADOW + 1));
    return 1;
}

LRESULT OnSize(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    if (wParam != SIZE_MINIMIZED)
    {
        UINT clientW = LOWORD(lParam);
        UINT clientH = HIWORD(lParam);

        if (clientH > BUTTON_BAR_HEIGHT)
        {
            g_videoClientW = clientW;
            g_videoClientH = clientH - BUTTON_BAR_HEIGHT;

            if (g_pSwapChain) {
                g_pSwapChain->Release();
                g_pSwapChain = nullptr;
            }
            CreateSwapChain(g_hwndVideo);
            LayoutUI(clientW, g_videoClientH);
        }
    }
    return 0;
}

LRESULT OnCommand(HWND hwnd, WPARAM wParam)
{
    switch (LOWORD(wParam))
    {
    case ID_FILE_OPEN:  OpenMediaFile(hwnd); break;
    case ID_FILE_EXIT:  PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
    case ID_HELP_ABOUT:
        MessageBoxW(hwnd, L"Media Engine Sample", L"About", MB_OK | MB_ICONINFORMATION);
        break;

    case ID_BTN_PLAY:
        if (g_pEngine && g_bFileLoaded)
        {
            if (SUCCEEDED(g_pEngine->Play()))
            {
                g_bPlaying = TRUE;
                SetTimer(hwnd, 1, 16, NULL);
            }
        }
        break;

    case ID_BTN_PAUSE:
        if (g_pEngine && g_bFileLoaded)
        {
            g_pEngine->Pause();
            g_bPlaying = FALSE;
        }
        break;

    case ID_BTN_STOP:
        if (g_pEngine && g_bFileLoaded)
        {
            g_pEngine->SetCurrentTime(0.0);
            g_pEngine->Pause();
            g_bPlaying = FALSE;
        }
        break;
    case ID_DRAW_START:
        g_drawEnabled = true;
        OverlayD2D_Start();
        break;

    case ID_DRAW_STOP:
        g_drawEnabled = false;

        OverlayD2D_Stop();
        g_maskDirty = true;
        DisplaySquareDialog(hwnd);
        SetActiveWindow(hwnd);
        break;

    case ID_DRAW_CLEAR:
        OverlayD2D_Clear();
        break;

    }
    return 0;
}


LRESULT OnDestroy(HWND hwnd)
{
    KillTimer(hwnd, 1);
    VideoPlayerShutdown();
    OverlayD2D_Shutdown();
    MFShutdown();
    CoUninitialize();
    PostQuitMessage(0);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg)
    {
    case WM_CREATE:        return OnCreate(hwnd);
    case WM_SIZE:          return OnSize(hwnd, wParam, lParam);
    case WM_HSCROLL:       return OnHScroll(hwnd, wParam, lParam);
    case WM_COMMAND:       return OnCommand(hwnd, wParam);
    case WM_TIMER:         return OnTimer(hwnd, wParam);
    case WM_ERASEBKGND:    return OnEraseBkgnd(hwnd, wParam);
    case WM_APP_RESIZE_VIDEO: return OnResizeVideo(hwnd);
    case WM_LBUTTONDOWN:
    {
        if (g_drawEnabled)
        {
            SetCapture(hwnd);

            POINT pt{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            ClientToScreen(hwnd, &pt);
            ScreenToClient(g_hwndVideo, &pt);

            OverlayD2D_OnMouseDown(pt.x, pt.y);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (g_drawEnabled && (wParam & MK_LBUTTON))
        {
            POINT pt{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            ClientToScreen(hwnd, &pt);
            ScreenToClient(g_hwndVideo, &pt);


            bool dragging = (wParam & MK_LBUTTON) != 0;
            OverlayD2D_OnMouseMove(pt.x, pt.y, dragging);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (g_drawEnabled)
        {
            ReleaseCapture();

            POINT pt{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            ClientToScreen(hwnd, &pt);
            ScreenToClient(g_hwndVideo, &pt);


            OverlayD2D_OnMouseUp();
        }
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:       return OnDestroy(hwnd);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"MFEngineWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"MFEngineWindow", L"Image Recognition",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInst, NULL);
    RegisterSquareDialogClass(hInst);
    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}