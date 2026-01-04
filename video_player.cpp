#include "video_player.h"


IMFMediaEngine* g_pEngine = nullptr;
IMFMediaEngineEx* g_pEngineEx = nullptr;
IMFDXGIDeviceManager* g_pDXGIManager = nullptr;
ID3D11Device* g_pD3DDevice = nullptr;
IDXGISwapChain1* g_pSwapChain = nullptr;

UINT g_resetToken = 0;
UINT g_videoClientW = 640;
UINT g_videoClientH = 360;

UINT g_pendingW = 0, g_pendingH = 0;
BOOL g_bPlaying = FALSE;
BOOL g_bFileLoaded = FALSE;
bool g_bSeeking = false;
BOOL g_bUserDragging = FALSE;

HWND g_hwndVideo = nullptr;
HWND hPlay = nullptr, hPause = nullptr, hStop = nullptr;
HWND hTimeline = nullptr;
HWND g_hwnd = nullptr;

const UINT BUTTON_BAR_HEIGHT = 70;
const UINT BUTTON_WIDTH = 80;
const UINT BUTTON_HEIGHT = 30;
const UINT BUTTON_GAP = 12;
const UINT MAX_VIDEO_WIDTH = 1920;
const UINT MAX_VIDEO_HEIGHT = 1080;
static const int TIMELINE_RESOLUTION = 1000;
wchar_t g_szCurrentFile[MAX_PATH] = { 0 };

bool g_maskDirty = false;

class MediaEngineNotify : public IMFMediaEngineNotify {
	long m_cRef;
public:
	MediaEngineNotify() : m_cRef(1) {}
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (riid == __uuidof(IMFMediaEngineNotify) || riid == __uuidof(IUnknown)) {
			*ppv = static_cast<IMFMediaEngineNotify*>(this);
			AddRef(); return S_OK;
		}
		*ppv = nullptr; return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
	STDMETHODIMP_(ULONG) Release() override {
		LONG cRef = InterlockedDecrement(&m_cRef);
		if (cRef == 0) delete this;
		return cRef;
	}
	STDMETHODIMP EventNotify(DWORD meEvent, DWORD_PTR param1, DWORD) override;
};

MediaEngineNotify* g_pNotify = nullptr;

void UpdateButtonsEnabled(bool enabled) {
	EnableWindow(hPlay, enabled);
	EnableWindow(hPause, enabled);
	EnableWindow(hStop, enabled);
}

void LayoutUI(UINT clientW, UINT videoH)
{
	SetWindowPos(g_hwndVideo, nullptr, 0, 0, clientW, videoH, SWP_NOZORDER);

	UINT barY = videoH;
	UINT barH = BUTTON_BAR_HEIGHT;

	SetWindowPos(
		hTimeline, nullptr,
		10,
		barY + 4,
		clientW - 20,
		20,
		SWP_NOZORDER);

	UINT totalBtnsW = 3 * BUTTON_WIDTH + 2 * BUTTON_GAP;
	int startX = (clientW > totalBtnsW) ? int((clientW - totalBtnsW) / 2) : 0;
	int y = barY + 26;

	SetWindowPos(hPlay, nullptr, startX, y, BUTTON_WIDTH, BUTTON_HEIGHT, SWP_NOZORDER);
	SetWindowPos(hPause, nullptr, startX + BUTTON_WIDTH + BUTTON_GAP, y, BUTTON_WIDTH, BUTTON_HEIGHT, SWP_NOZORDER);
	SetWindowPos(hStop, nullptr, startX + 2 * (BUTTON_WIDTH + BUTTON_GAP), y, BUTTON_WIDTH, BUTTON_HEIGHT, SWP_NOZORDER);
}

STDMETHODIMP MediaEngineNotify::EventNotify(DWORD meEvent, DWORD_PTR param1, DWORD) {
	if (meEvent == MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE) {
		SetEvent(reinterpret_cast<HANDLE>(param1));
		return S_OK;
	}
	switch (meEvent) {
	case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
		g_bFileLoaded = TRUE;
		break;
	case MF_MEDIA_ENGINE_EVENT_CANPLAY: {
		DWORD vw = 0, vh = 0;
		SetTimer(g_hwnd, 1, 16, NULL);

		if (g_pEngine && SUCCEEDED(g_pEngine->GetNativeVideoSize(&vw, &vh)) && vw && vh) {
			g_pendingW = vw;
			g_pendingH = vh;
			PostMessageW(g_hwnd, WM_APP_RESIZE_VIDEO, 0, 0);
		}
		double duration = g_pEngine->GetDuration();
		if (duration > 0.0)
		{
			SendMessageW(hTimeline, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
			SendMessageW(hTimeline, TBM_SETPAGESIZE, 0, 10);
			SendMessageW(hTimeline, TBM_SETPOS, TRUE, 0);
			EnableWindow(hTimeline, TRUE);
		}

		UpdateButtonsEnabled(true);
		break;
	}
	case MF_MEDIA_ENGINE_EVENT_PLAY:
		g_bPlaying = TRUE;
		break;
	case MF_MEDIA_ENGINE_EVENT_PAUSE:
		g_bPlaying = FALSE;
		break;
	case MF_MEDIA_ENGINE_EVENT_ENDED:
		g_bPlaying = FALSE;
		if (g_pEngine) g_pEngine->SetCurrentTime(0.0);
		break;
	case MF_MEDIA_ENGINE_EVENT_ERROR: {
		wchar_t buf[256];
		swprintf_s(buf, L"Media Engine Error: 0x%08X", (DWORD)param1);
		MessageBoxW(g_hwnd, buf, L"Error", MB_OK | MB_ICONERROR);
		UpdateButtonsEnabled(false);
		break;
	}
	}
	return S_OK;
}

HRESULT CreateSwapChain(HWND hwndVideo) {
	if (!g_pD3DDevice || !g_pD3DContext || !hwndVideo) return E_FAIL;

	IDXGIDevice2* pDXGIDevice = nullptr;
	HRESULT hr = g_pD3DDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
	if (FAILED(hr)) return hr;

	IDXGIAdapter* pAdapter = nullptr;
	hr = pDXGIDevice->GetParent(IID_PPV_ARGS(&pAdapter));
	if (FAILED(hr)) { pDXGIDevice->Release(); return hr; }

	IDXGIFactory2* pFactory = nullptr;
	hr = pAdapter->GetParent(IID_PPV_ARGS(&pFactory));
	if (FAILED(hr)) { pAdapter->Release(); pDXGIDevice->Release(); return hr; }

	if (g_pSwapChain) {
		g_pSwapChain->Release();
		g_pSwapChain = nullptr;
	}

	UINT width = uimax(1u, g_videoClientW);
	UINT height = uimax(1u, g_videoClientH);

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 2;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.Scaling = DXGI_SCALING_NONE;

	hr = pFactory->CreateSwapChainForHwnd(g_pD3DDevice, hwndVideo, &desc, nullptr, nullptr, &g_pSwapChain);

	IR_Init(g_pD3DDevice, desc.Width, desc.Height);

	if (SUCCEEDED(hr)) {
		IDXGIDevice2* pDev2 = nullptr;
		if (SUCCEEDED(g_pD3DDevice->QueryInterface(IID_PPV_ARGS(&pDev2)))) {
			pDev2->SetMaximumFrameLatency(1);
			pDev2->Release();
		}
	}

	pFactory->Release();
	pAdapter->Release();
	pDXGIDevice->Release();
	return hr; 
}

void RenderVideo()
{
	if (!g_pEngine || !g_pSwapChain)
		return;

	LONGLONG pts = 0;
	HRESULT hr = g_pEngine->OnVideoStreamTick(&pts); 

	if (hr != S_OK)
		return;

	ComPtr<ID3D11Texture2D> backBuffer;
	hr = g_pSwapChain->GetBuffer(
		0, IID_PPV_ARGS(&backBuffer));

	if (FAILED(hr))
		return;

	DXGI_SWAP_CHAIN_DESC1 desc{};
	g_pSwapChain->GetDesc1(&desc);

	RECT rc{ 0, 0, (LONG)desc.Width, (LONG)desc.Height };
	MFARGB bg{ 0, 0, 0, 255 };

	g_pEngine->TransferVideoFrame(backBuffer.Get(), nullptr, &rc, &bg);

	OverlayD2D_SetTarget(backBuffer.Get(), g_hwndVideo);

	OverlayD2D_BeginDraw();
	OverlayD2D_Draw();
	OverlayD2D_EndDraw();

	if (OverlayD2D_IsPolygonFinal() && g_maskDirty)
	{
		IR_DrawPolygonMaskGPU(g_pD3DContext, OverlayD2D_GetPolygonCPU());
		g_maskDirty = false;
	}
	if (OverlayD2D_IsPolygonFinal())
	{
		std::vector<Pixel> pixels =
			GetPixelsInPolygon(
				g_pD3DContext,
				backBuffer.Get());

		if (!pixels.empty())
		{
			uint32_t side = 0;
			auto square = IR_PackPixelsToSquare(pixels, side);
			SetPixelsAndSide(square, side);
		}
	}


	g_pSwapChain->Present(1, 0);

	if (g_bPlaying && !g_bSeeking)
	{
		double t = g_pEngine->GetCurrentTime();
		SendMessage(
			hTimeline,
			TBM_SETPOS,
			TRUE,
			(int)(t * TIMELINE_RESOLUTION)
		);
	}

	backBuffer->Release();
}

void CreateVideoWindowAndButtons(HWND hwnd) {
	g_hwndVideo = CreateWindowExW(0, L"STATIC", L"",
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
		0, 0, g_videoClientW, g_videoClientH,
		hwnd, NULL, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

	hPlay = CreateWindowW(
		L"BUTTON", L"Play",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		0, 0, BUTTON_WIDTH, BUTTON_HEIGHT,
		hwnd, (HMENU)ID_BTN_PLAY,
		(HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
		nullptr);

	hPause = CreateWindowW(
		L"BUTTON", L"Pause",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		0, 0, BUTTON_WIDTH, BUTTON_HEIGHT,
		hwnd, (HMENU)ID_BTN_PAUSE,
		(HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
		nullptr);

	hStop = CreateWindowW(
		L"BUTTON", L"Stop",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		0, 0, BUTTON_WIDTH, BUTTON_HEIGHT,
		hwnd, (HMENU)ID_BTN_STOP,
		(HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
		nullptr);
}

void CreateTimeline(HWND hwnd) {
	hTimeline = CreateWindowExW(
		0,
		TRACKBAR_CLASSW,
		nullptr,
		WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
		0, 0, 100, 20,
		hwnd,
		(HMENU)ID_TIMELINE,
		(HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
		nullptr);

	SendMessage(hTimeline, TBM_SETRANGE, TRUE, MAKELPARAM(0, 0));
	EnableWindow(hTimeline, FALSE);
}

LRESULT CreateD3D11Device(HRESULT hr, HWND hwnd) {
	D3D_FEATURE_LEVEL levels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0
	};
	D3D_FEATURE_LEVEL fl;
	hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
		&g_pD3DDevice, &fl, &g_pD3DContext);
	if (FAILED(hr)) { MessageBoxW(hwnd, L"Failed to create D3D11 device", L"Error", MB_OK); return -1; }
	{
		ID3D10Multithread* pMT = nullptr;
		if (SUCCEEDED(g_pD3DDevice->QueryInterface(IID_PPV_ARGS(&pMT)))) {
			pMT->SetMultithreadProtected(TRUE);
			pMT->Release();
		}
	}
}

LRESULT CreateMediaEngine(HRESULT hr, HWND hwnd) {
	hr = MFCreateDXGIDeviceManager(&g_resetToken, &g_pDXGIManager);
	if (FAILED(hr)) return -1;
	hr = g_pDXGIManager->ResetDevice(g_pD3DDevice, g_resetToken);
	if (FAILED(hr)) return -1;

	CreateSwapChain(g_hwndVideo);

	g_pNotify = new MediaEngineNotify();
	IMFMediaEngineClassFactory* pFactory = nullptr;
	hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
	if (FAILED(hr)) return -1;

	IMFAttributes* pAttr = nullptr;
	hr = MFCreateAttributes(&pAttr, 3);
	if (FAILED(hr)) { pFactory->Release(); return -1; }

	pAttr->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, g_pDXGIManager);
	pAttr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, g_pNotify);
	pAttr->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);

	hr = pFactory->CreateInstance(MF_MEDIA_ENGINE_WAITFORSTABLE_STATE, pAttr, &g_pEngine);
	pFactory->Release(); pAttr->Release();
	if (FAILED(hr)) { MessageBoxW(hwnd, L"CreateInstance failed", L"Error", MB_OK); return -1; }

	g_pEngine->QueryInterface(IID_PPV_ARGS(&g_pEngineEx));

	UINT clientW = g_videoClientW;
	UINT clientH = g_videoClientH + BUTTON_BAR_HEIGHT;

	RECT rc = { 0, 0, (LONG)clientW, (LONG)clientH };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE);

	SetWindowPos(
		hwnd,
		nullptr,
		0, 0,
		rc.right - rc.left,
		rc.bottom - rc.top,
		SWP_NOMOVE | SWP_NOZORDER);

	LayoutUI(clientW, g_videoClientH);
	UpdateButtonsEnabled(false);
	CreateTimeline(hwnd);
}

HRESULT OpenMediaFile(HWND hwnd) {
	EnableWindow(hTimeline, FALSE);
	SendMessage(hTimeline, TBM_SETPOS, TRUE, 0);

	IFileOpenDialog* pFileOpen = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileOpen));
	if (FAILED(hr)) return hr;

	COMDLG_FILTERSPEC rgSpec[] = {
		{ L"Video Files", L"*.mp4;*.avi;*.mkv;*.mov;*.wmv;*.flv;*.webm;*.m4v" },
		{ L"All Files",   L"*.*" }
	};
	pFileOpen->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
	pFileOpen->SetFileTypeIndex(1);
	pFileOpen->SetDefaultExtension(L"mp4");

	hr = pFileOpen->Show(hwnd);
	if (SUCCEEDED(hr)) {
		IShellItem* pItem = nullptr;
		hr = pFileOpen->GetResult(&pItem);
		if (SUCCEEDED(hr)) {
			PWSTR pszFilePath = nullptr;
			if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
				wcscpy_s(g_szCurrentFile, pszFilePath);
				wchar_t szURL[MAX_PATH * 3] = L"file:///";
				for (int i = 0; pszFilePath[i]; ++i) {
					if (pszFilePath[i] == L'\\') wcscat_s(szURL, L"/");
					else if (pszFilePath[i] == L' ') wcscat_s(szURL, L"%20");
					else {
						wchar_t ch[2] = { pszFilePath[i], 0 };
						wcscat_s(szURL, ch);
					}
				}

				if (g_pEngine) {
					BSTR bstrURL = SysAllocString(szURL);
					hr = g_pEngine->SetSource(bstrURL);
					SysFreeString(bstrURL);
					if (FAILED(hr)) {
						wchar_t buf[256];
						swprintf_s(buf, L"SetSource failed: 0x%08X", hr);
						MessageBoxW(hwnd, buf, L"Error", MB_OK | MB_ICONERROR);
					}
					if (g_bFileLoaded) {
						KillTimer(g_hwnd, 1);
					}
					g_bFileLoaded = FALSE;
					g_bPlaying = FALSE; 
					UpdateButtonsEnabled(false); 
				}
				CoTaskMemFree(pszFilePath);
			}
			pItem->Release();
		}
	}
	EnableWindow(hTimeline, TRUE);
	pFileOpen->Release();
	return hr;
}

LRESULT OnHScroll(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
	if ((HWND)lParam != hTimeline)
		return 0;

	switch (LOWORD(wParam))
	{
	case TB_THUMBTRACK:
		g_bUserDragging = TRUE;
		break;

	case TB_THUMBPOSITION:
	case TB_ENDTRACK:
		g_bUserDragging = FALSE;

		int pos = (int)SendMessageW(hTimeline, TBM_GETPOS, 0, 0);
		double duration = g_pEngine->GetDuration();
		if (duration > 0.0)
		{
			double newTime = (pos / 1000.0) * duration;
			BOOL wasPlaying = g_bPlaying;
			g_pEngine->SetCurrentTime(newTime);
			if (wasPlaying) g_pEngine->Play();
		}
		break;
	}
	return 0;
}

LRESULT OnTimer(HWND hwnd, WPARAM wParam)
{
	if (wParam != 1 || !g_pEngine || !g_bFileLoaded)
		return 0;

	RenderVideo();

	if (!g_bUserDragging)
	{
		double duration = g_pEngine->GetDuration();
		if (duration > 0.0)
		{
			double cur = g_pEngine->GetCurrentTime();
			int pos = int((cur / duration) * 1000.0);
			SendMessageW(hTimeline, TBM_SETPOS, TRUE, pos);
		}
	}
	return 0;
}


LRESULT OnResizeVideo(HWND hwnd) {
	if (g_pendingW && g_pendingH) {
		DWORD vw = g_pendingW, vh = g_pendingH;
		double scale = 1.0;
		if (vw > MAX_VIDEO_WIDTH || vh > MAX_VIDEO_HEIGHT) {
			double sx = double(MAX_VIDEO_WIDTH) / vw;
			double sy = double(MAX_VIDEO_HEIGHT) / vh;
			scale = (sx < sy) ? sx : sy;
		}
		g_videoClientW = UINT(vw * scale);
		g_videoClientH = UINT(vh * scale);

		UINT clientW = g_videoClientW;
		UINT clientH = g_videoClientH + BUTTON_BAR_HEIGHT;

		RECT rc = { 0,0, LONG(clientW), LONG(clientH) };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE);
		UINT outW = rc.right - rc.left;
		UINT outH = rc.bottom - rc.top;

		SetWindowPos(hwnd, NULL, 0, 0, outW, outH, SWP_NOMOVE | SWP_NOZORDER);
		SetWindowPos(g_hwndVideo, NULL, 0, 0, g_videoClientW, g_videoClientH, SWP_NOZORDER);

		if (g_pSwapChain) {
			g_pSwapChain->Release();
			g_pSwapChain = nullptr;
		}
		CreateSwapChain(g_hwndVideo);

		LayoutUI(clientW, g_videoClientH);
		InvalidateRect(hwnd, NULL, TRUE);
	}

	return 0;
}

void VideoPlayerShutdown() {
	if (g_pEngineEx) { g_pEngineEx->Release();   g_pEngineEx = nullptr; }
	if (g_pEngine) { g_pEngine->Release();     g_pEngine = nullptr; }
	if (g_pNotify) { g_pNotify->Release();     g_pNotify = nullptr; }
	if (g_pSwapChain) { g_pSwapChain->Release();  g_pSwapChain = nullptr; }
	if (g_pDXGIManager) { g_pDXGIManager->Release(); g_pDXGIManager = nullptr; }
	if (g_pD3DContext) { g_pD3DContext->Release(); g_pD3DContext = nullptr; }
	if (g_pD3DDevice) { g_pD3DDevice->Release();  g_pD3DDevice = nullptr; }
}
