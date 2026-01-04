#pragma once
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>
#include <cmath>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include "square.h"

#pragma comment(lib, "d3dcompiler.lib")


static UINT g_w = 0;
static UINT g_h = 0;

static UINT g_maskW = 0;
static UINT g_maskH = 0;

static bool g_stagingReady = false;

static Microsoft::WRL::ComPtr<ID3D11RenderTargetView> g_maskRTV;

static Microsoft::WRL::ComPtr<ID3D11VertexShader> g_vs;
static Microsoft::WRL::ComPtr<ID3D11PixelShader>  g_ps;
static Microsoft::WRL::ComPtr<ID3D11InputLayout>  g_layout;
static Microsoft::WRL::ComPtr<ID3D11Buffer>       g_vb;


static Microsoft::WRL::ComPtr<ID3D11Texture2D> g_videoStaging;
static Microsoft::WRL::ComPtr<ID3D11Texture2D> g_maskTexture; 
static Microsoft::WRL::ComPtr<ID3D11Texture2D> g_maskStaging;

static Microsoft::WRL::ComPtr<ID2D1Factory1>        g_d2dFactory;
static Microsoft::WRL::ComPtr<ID2D1Device>          g_d2dDevice;
static Microsoft::WRL::ComPtr<ID2D1DeviceContext>   g_d2dCtx;

bool IR_Init(ID3D11Device* device, UINT w, UINT h);
std::vector<Pixel> GetPixelsInPolygon(
    ID3D11DeviceContext* ctx,
    ID3D11Texture2D* backBuffer);
std::vector<Pixel> IR_PackPixelsToSquare(
    const std::vector<Pixel>& pixels,
    uint32_t& outSide);

bool IR_DrawPolygonMaskGPU(ID3D11DeviceContext* ctx, const std::vector<POINT>& poly);
void IR_Shutdown();
bool IR_Resize(ID3D11Device* device, UINT w, UINT h);