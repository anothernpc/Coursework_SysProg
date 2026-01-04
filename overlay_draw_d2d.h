#pragma once
#include <d3d11.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <d2d1helper.h>
#include <vector>
#include <wrl.h>
#include "image_recognition.h"

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d2d1.lib")

bool OverlayD2D_Init(ID3D11Device* d3dDevice);
void OverlayD2D_Shutdown();
void OverlayD2D_SetTarget(ID3D11Texture2D* overlayTexture, HWND hwnd);

void OverlayD2D_Start();
void OverlayD2D_Stop();
void OverlayD2D_Clear();

void OverlayD2D_OnMouseDown(float x, float y);
void OverlayD2D_OnMouseMove(float x, float y, bool dragging);
void OverlayD2D_OnMouseUp();

void OverlayD2D_BeginDraw();
void OverlayD2D_Draw();
void OverlayD2D_EndDraw();

extern ID3D11DeviceContext* g_pD3DContext;
static bool g_enabled = false;
static bool g_drawing = false;
static bool g_polygonFinal = false;

static std::vector<D2D1_POINT_2F> g_points;
static std::vector<POINT> g_polygonCPU;

void OverlayD2D_SetTarget(ID3D11Texture2D* overlayTexture, HWND hwnd);
bool OverlayD2D_IsPolygonFinal();
const std::vector<POINT>& OverlayD2D_GetPolygonCPU();

static Microsoft::WRL::ComPtr<ID2D1Factory1>        g_factory;
static Microsoft::WRL::ComPtr<ID2D1Device>          g_device;
static Microsoft::WRL::ComPtr<ID2D1DeviceContext>   g_context;
static Microsoft::WRL::ComPtr<ID2D1Bitmap1>         g_targetBitmap;
static Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g_brush;