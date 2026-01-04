#include "image_recognition.h"

using Microsoft::WRL::ComPtr;

bool IR_Init(ID3D11Device* device, UINT w, UINT h)
{
    if (!device || w == 0 || h == 0)
        return false;

    g_w = w;
    g_h = h;

    D2D1_FACTORY_OPTIONS opt{};
#if _DEBUG
    opt.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        &opt,
        reinterpret_cast<void**>(g_d2dFactory.GetAddressOf()));

    if (FAILED(hr))
        return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
        return false;

    if (FAILED(g_d2dFactory->CreateDevice(dxgiDevice.Get(), &g_d2dDevice)))
        return false;

    if (FAILED(g_d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        &g_d2dCtx)))
        return false;

    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    if (FAILED(device->CreateTexture2D(&d, nullptr, &g_videoStaging)))
        return false;

    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d.CPUAccessFlags = 0;

    if (FAILED(device->CreateTexture2D(&d, nullptr, &g_maskTexture)))
        return false;

    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    if (FAILED(device->CreateTexture2D(&d, nullptr, &g_maskStaging)))
        return false;

    return true;
}


bool IR_DrawPolygonMaskGPU(
    ID3D11DeviceContext* ctx,
    const std::vector<POINT>& poly)
{
    if (!ctx || !g_d2dCtx || !g_d2dFactory || !g_maskTexture)
        return false;

    if (poly.size() < 3)
        return false;

    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    if (FAILED(g_maskTexture->QueryInterface(IID_PPV_ARGS(&surface))))
        return false;

    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target;

    D2D1_BITMAP_PROPERTIES1 props =
        D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED));

    if (FAILED(g_d2dCtx->CreateBitmapFromDxgiSurface(
        surface.Get(), &props, &target)))
        return false;

    g_d2dCtx->SetTarget(target.Get());
    g_d2dCtx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    g_d2dCtx->BeginDraw();

    g_d2dCtx->Clear(D2D1::ColorF(0, 0, 0, 0));

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(g_d2dFactory->CreatePathGeometry(&geo)))
        return false;

    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink)))
        return false;

    sink->BeginFigure(
        D2D1::Point2F(
            (FLOAT)poly[0].x + 0.5f,
            (FLOAT)poly[0].y + 0.5f),
        D2D1_FIGURE_BEGIN_FILLED);

    for (size_t i = 1; i < poly.size(); ++i)
    {
        sink->AddLine(D2D1::Point2F(
            (FLOAT)poly[i].x + 0.5f,
            (FLOAT)poly[i].y + 0.5f));
    }

    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> white;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> black;

    g_d2dCtx->CreateSolidColorBrush(
        D2D1::ColorF(1, 1, 1, 1), &white);

    g_d2dCtx->CreateSolidColorBrush(
        D2D1::ColorF(0, 0, 0, 0), &black);
    g_d2dCtx->FillGeometry(geo.Get(), white.Get());
    g_d2dCtx->DrawGeometry(
        geo.Get(),
        black.Get(),
        1.0f,
        nullptr);

    HRESULT hr = g_d2dCtx->EndDraw();
    g_d2dCtx->SetTarget(nullptr);

    return SUCCEEDED(hr);
}

static void ErodeMask1px_8N(
    uint8_t* data,
    UINT w,
    UINT h,
    UINT pitch)
{
    std::vector<uint8_t> copy(h * pitch);
    memcpy(copy.data(), data, h * pitch);

    auto A = [&](int x, int y) -> uint8_t {
        return copy[y * pitch + x * 4 + 3];
        };

    for (UINT y = 1; y < h - 1; ++y)
    {
        uint8_t* row = data + y * pitch;
        for (UINT x = 1; x < w - 1; ++x)
        {
            uint8_t& a = row[x * 4 + 3];
            if (a == 0)
                continue;
            if (
                A(x - 1, y - 1) == 0 || A(x, y - 1) == 0 || A(x + 1, y - 1) == 0 ||
                A(x - 1, y) == 0 || A(x + 1, y) == 0 ||
                A(x - 1, y + 1) == 0 || A(x, y + 1) == 0 || A(x + 1, y + 1) == 0
                )
            {
                a = 0;
            }
        }
    }
}


bool ValidateInputs(ID3D11DeviceContext* ctx, ID3D11Texture2D* backBuffer) {
    return ctx && backBuffer && g_videoStaging && g_maskTexture && g_maskStaging;
}

void CopyResources(ID3D11DeviceContext* ctx, ID3D11Texture2D* backBuffer) {
    ctx->CopyResource(g_videoStaging.Get(), backBuffer);
    ctx->CopyResource(g_maskStaging.Get(), g_maskTexture.Get());
    ctx->Flush();
}

bool MapTextures(ID3D11DeviceContext* ctx,
    D3D11_MAPPED_SUBRESOURCE& vmap,
    D3D11_MAPPED_SUBRESOURCE& mmap) {
    if (FAILED(ctx->Map(g_videoStaging.Get(), 0, D3D11_MAP_READ, 0, &vmap)))
        return false;

    if (FAILED(ctx->Map(g_maskStaging.Get(), 0, D3D11_MAP_READ, 0, &mmap))) {
        ctx->Unmap(g_videoStaging.Get(), 0);
        return false;
    }
    return true;
}

void ProcessMask(D3D11_MAPPED_SUBRESOURCE& mmap) {
    ErodeMask1px_8N((uint8_t*)mmap.pData, g_w, g_h, mmap.RowPitch);
    ErodeMask1px_8N((uint8_t*)mmap.pData, g_w, g_h, mmap.RowPitch);
}

std::vector<Pixel> ExtractPixels(const D3D11_MAPPED_SUBRESOURCE& vmap,
    const D3D11_MAPPED_SUBRESOURCE& mmap) {
    std::vector<Pixel> out;
    for (UINT y = 0; y < g_h; ++y) {
        uint8_t* maskRow = (uint8_t*)mmap.pData + y * mmap.RowPitch;
        uint8_t* videoRow = (uint8_t*)vmap.pData + y * vmap.RowPitch;

        for (UINT x = 0; x < g_w; ++x) {
            if (maskRow[x * 4 + 3] == 0)
                continue;
            out.push_back(*(Pixel*)(videoRow + x * 4));
        }
    }
    return out;
}

void UnmapTextures(ID3D11DeviceContext* ctx) {
    ctx->Unmap(g_maskStaging.Get(), 0);
    ctx->Unmap(g_videoStaging.Get(), 0);
}

std::vector<Pixel> GetPixelsInPolygon(ID3D11DeviceContext* ctx,
    ID3D11Texture2D* backBuffer) {
    std::vector<Pixel> out;

    if (!ValidateInputs(ctx, backBuffer))
        return out;

    CopyResources(ctx, backBuffer);

    D3D11_MAPPED_SUBRESOURCE vmap{}, mmap{};
    if (!MapTextures(ctx, vmap, mmap))
        return out;

    ProcessMask(mmap);
    out = ExtractPixels(vmap, mmap);

    UnmapTextures(ctx);

    return out;
}

std::vector<Pixel> IR_PackPixelsToSquare( const std::vector<Pixel>& pixels, uint32_t& outSide) { 
    outSide = 0; 
    if (pixels.empty()) return {}; 
    size_t count = pixels.size(); 
    outSide = (uint32_t)std::ceil(std::sqrt((double)count)); 
    size_t total = (size_t)outSide * outSide; 
    std::vector<Pixel> square; square.reserve(total); 
    for (auto& p : pixels) 
        square.push_back(p); 
    Pixel zero{ 0,0,0,0 }; 
    while (square.size() < total) 
        square.push_back(zero); 
    return square; 
}

void IR_Shutdown()
{
    g_d2dCtx.Reset();
    g_d2dDevice.Reset();
    g_d2dFactory.Reset();

    g_videoStaging.Reset();
    g_maskTexture.Reset();
    g_maskStaging.Reset();

    g_w = g_h = 0;
}

bool IR_Resize(ID3D11Device* device, UINT w, UINT h)
{
    if (w == g_w && h == g_h)
        return true;

    IR_Shutdown();
    return IR_Init(device, w, h);
}
