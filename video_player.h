#pragma once
#include <windows.h>
#include <mfmediaengine.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <oleauto.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <tchar.h>
#include "overlay_draw_d2d.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

#define WM_APP_RESIZE_VIDEO (WM_APP + 1)
#define ID_BTN_PLAY    2001
#define ID_BTN_PAUSE   2002
#define ID_BTN_STOP    2003
#define ID_TIMELINE  3001

extern IMFMediaEngine* g_pEngine;
extern IMFMediaEngineEx* g_pEngineEx;
extern IMFDXGIDeviceManager* g_pDXGIManager;
extern ID3D11Device* g_pD3DDevice;
extern ID3D11DeviceContext* g_pD3DContext;
extern IDXGISwapChain1* g_pSwapChain;

extern UINT g_resetToken;
extern UINT g_videoClientW;
extern UINT g_videoClientH;

extern UINT g_pendingW, g_pendingH;
extern BOOL g_bPlaying;
extern BOOL g_bFileLoaded;
extern bool g_bSeeking;
extern BOOL g_bUserDragging;

extern HWND g_hwndVideo;
extern HWND hPlay, hPause, hStop;
extern HWND hTimeline;
extern HWND g_hwnd;

extern const UINT BUTTON_BAR_HEIGHT;
extern const UINT BUTTON_WIDTH;
extern const UINT BUTTON_HEIGHT;
extern const UINT BUTTON_GAP;
extern const UINT MAX_VIDEO_WIDTH;
extern const UINT MAX_VIDEO_HEIGHT;
extern wchar_t g_szCurrentFile[MAX_PATH];
extern bool g_maskDirty;

void LayoutUI(UINT clientW, UINT videoH);
LRESULT CreateD3D11Device(HRESULT hr, HWND hwnd);
LRESULT CreateMediaEngine(HRESULT hr, HWND hwnd);
HRESULT OpenMediaFile(HWND hwnd);
void CreateVideoWindowAndButtons(HWND hwnd);
HRESULT CreateSwapChain(HWND hwndVideo);
LRESULT OnHScroll(HWND hwnd, WPARAM wParam, LPARAM lParam);
LRESULT OnTimer(HWND hwnd, WPARAM wParam);
LRESULT OnResizeVideo(HWND hwnd);
void VideoPlayerShutdown();

inline UINT uimax(UINT a, UINT b) { return (a > b) ? a : b; }