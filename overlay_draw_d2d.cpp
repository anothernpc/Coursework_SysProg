#include "overlay_draw_d2d.h"

#include <d2d1_1.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <vector>
#include <windows.h>

#pragma comment(lib, "d2d1.lib")

using Microsoft::WRL::ComPtr;
ID3D11DeviceContext* g_pD3DContext = nullptr;

bool OverlayD2D_Init(ID3D11Device* d3dDevice)
{
    if (!d3dDevice)
        return false;

    HRESULT hr;
    D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        &options,
        reinterpret_cast<void**>(g_factory.GetAddressOf()));

    if (FAILED(hr))
        return false;
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr))
        return false;
    hr = g_factory->CreateDevice(dxgiDevice.Get(), &g_device);
    if (FAILED(hr))
        return false;
    hr = g_device->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        &g_context);
    if (FAILED(hr))
        return false;
    hr = g_context->CreateSolidColorBrush(
        D2D1::ColorF(0.f, 1.f, 0.f, 1.f),
        &g_brush);

    return SUCCEEDED(hr);
}

void OverlayD2D_Shutdown()
{
    g_targetBitmap.Reset();
    g_brush.Reset();
    g_context.Reset();
    g_device.Reset();
    g_factory.Reset();
}

void OverlayD2D_SetTarget(ID3D11Texture2D* overlayTexture, HWND hwnd)
{
    if (!overlayTexture || !g_context || !hwnd)
        return;

    g_targetBitmap.Reset();

    ComPtr<IDXGISurface> surface;
    if (FAILED(overlayTexture->QueryInterface(IID_PPV_ARGS(&surface))))
        return;

    UINT dpi = GetDpiForWindow(hwnd);
    FLOAT dpiX = (FLOAT)dpi;
    FLOAT dpiY = (FLOAT)dpi;

    D2D1_BITMAP_PROPERTIES1 props =
        D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_IGNORE), 
            dpiX,
            dpiY);

    HRESULT hr = g_context->CreateBitmapFromDxgiSurface(
        surface.Get(),
        &props,
        &g_targetBitmap);

    if (FAILED(hr))
        return;

    g_context->SetTarget(g_targetBitmap.Get());
}

void OverlayD2D_Start() { 
    g_enabled = true; 
    g_drawing = false;

    g_points.clear();
    g_polygonCPU.clear();
    g_polygonFinal = false;
}
void OverlayD2D_Stop() { 
    g_enabled = false; g_drawing = false; 
    g_polygonCPU.clear();
    for (const auto& p : g_points)
        g_polygonCPU.push_back({ (LONG)p.x, (LONG)p.y });

    g_polygonFinal = (g_polygonCPU.size() >= 3);

    IR_DrawPolygonMaskGPU(g_pD3DContext, OverlayD2D_GetPolygonCPU());
}

void OverlayD2D_Clear() { g_points.clear(); }


bool OverlayD2D_IsPolygonFinal()
{
    return g_polygonFinal;
}

const std::vector<POINT>& OverlayD2D_GetPolygonCPU()
{
    return g_polygonCPU;
}


void OverlayD2D_OnMouseDown(float x, float y)
{
    if (!g_enabled)
        return;

    g_points.clear();
    g_points.push_back({ x, y });
    g_drawing = true;
}

void OverlayD2D_OnMouseMove(float x, float y, bool dragging)
{
    if (!g_enabled || !g_drawing || !dragging)
        return;

    g_points.push_back({ x, y });
}

void OverlayD2D_OnMouseUp()
{
    g_drawing = false;
}

void OverlayD2D_BeginDraw()
{
    if (!g_context || !g_targetBitmap)
        return;

    g_context->BeginDraw();
}

void OverlayD2D_Draw()
{
    if (!g_brush || g_points.size() < 2)
        return;

    for (size_t i = 1; i < g_points.size(); ++i)
    {
        g_context->DrawLine(
            g_points[i - 1],
            g_points[i],
            g_brush.Get(),
            3.0f);
    }
}

void OverlayD2D_EndDraw()
{
    if (!g_context)
        return;

    HRESULT hr = g_context->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET)
    {
        g_targetBitmap.Reset();
    }
}
