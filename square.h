#pragma once
#include <cstdint>
#include <vector>

struct Pixel
{
    uint8_t b, g, r, a;
};

static std::vector<Pixel> g_pixels;
static UINT g_side = 0;
static HWND g_hwndSquare = nullptr;

void SetPixelsAndSide(const std::vector<Pixel>& pixels, UINT side);
void DisplaySquareDialog(HWND parent);
void RegisterSquareDialogClass(HINSTANCE hInst);