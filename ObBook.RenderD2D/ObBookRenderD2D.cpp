#include "ObBookRenderD2D.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

#pragma comment(lib, "d3d9.lib")

namespace
{
    static std::string StripMarkup(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        bool inTag = false;

        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '<')
            {
                inTag = true;
                if (i + 3 < s.size())
                {
                    char c1 = static_cast<char>(::tolower(static_cast<unsigned char>(s[i + 1])));
                    char c2 = static_cast<char>(::tolower(static_cast<unsigned char>(s[i + 2])));
                    if (c1 == 'b' && c2 == 'r') out += "\r\n";
                }
                continue;
            }
            if (s[i] == '>')
            {
                inTag = false;
                continue;
            }
            if (!inTag) out.push_back(s[i]);
        }
        return out;
    }

    static bool TryDirect3D9Background(uint32_t width, uint32_t height, std::vector<uint8_t>& outBgra)
    {
        IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) return false;

        HWND hwnd = GetDesktopWindow();
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = hwnd;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferWidth = width;
        pp.BackBufferHeight = height;

        IDirect3DDevice9* device = nullptr;
        HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT,
            &pp, &device);
        if (FAILED(hr))
        {
            d3d->Release();
            return false;
        }

        IDirect3DSurface9* surface = nullptr;
        hr = device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &surface, nullptr);
        if (FAILED(hr))
        {
            device->Release();
            d3d->Release();
            return false;
        }

        D3DLOCKED_RECT lr{};
        hr = surface->LockRect(&lr, nullptr, 0);
        if (SUCCEEDED(hr))
        {
            for (uint32_t y = 0; y < height; ++y)
            {
                auto* row = reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
                for (uint32_t x = 0; x < width; ++x)
                {
                    const bool border = (x < 2 || y < 2 || x + 2 >= width || y + 2 >= height);
                    row[x * 4 + 0] = border ? 0x80 : 0xF5;
                    row[x * 4 + 1] = border ? 0x80 : 0xF0;
                    row[x * 4 + 2] = border ? 0x80 : 0xE7;
                    row[x * 4 + 3] = 0xFF;
                }
            }
            surface->UnlockRect();

            outBgra.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
            hr = surface->LockRect(&lr, nullptr, D3DLOCK_READONLY);
            if (SUCCEEDED(hr))
            {
                for (uint32_t y = 0; y < height; ++y)
                {
                    std::memcpy(&outBgra[static_cast<size_t>(y) * width * 4],
                        static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch,
                        static_cast<size_t>(width) * 4);
                }
                surface->UnlockRect();
            }
        }

        surface->Release();
        device->Release();
        d3d->Release();
        return !outBgra.empty();
    }

    static void DrawTextSoftware(uint32_t width, uint32_t height, const std::string& text, std::vector<uint8_t>& bgra)
    {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(width);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* dibBits = nullptr;
        HDC hdc = CreateCompatibleDC(nullptr);
        HBITMAP dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
        HGDIOBJ oldBmp = SelectObject(hdc, dib);

        if (!bgra.empty() && dibBits)
            std::memcpy(dibBits, bgra.data(), bgra.size());

        SetTextColor(hdc, RGB(36, 36, 36));
        SetBkMode(hdc, TRANSPARENT);
        HFONT font = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FF_DONTCARE, L"Times New Roman");
        HGDIOBJ oldFont = SelectObject(hdc, font);

        RECT rc{ 48, 42, static_cast<LONG>(width) - 48, static_cast<LONG>(height) - 42 };
        std::wstring wtext(text.begin(), text.end());
        DrawTextW(hdc, wtext.c_str(), -1, &rc, DT_WORDBREAK | DT_TOP | DT_LEFT);

        if (dibBits && bgra.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
            std::memcpy(bgra.data(), dibBits, bgra.size());

        SelectObject(hdc, oldFont);
        DeleteObject(font);
        SelectObject(hdc, oldBmp);
        DeleteObject(dib);
        DeleteDC(hdc);
    }
}

bool obbook::RenderPreviewBgra(const RenderParams& p, const std::string& sourceUtf8, std::vector<uint8_t>& outBgra, std::string& outError)
{
    outError.clear();

    if (p.width == 0 || p.height == 0)
    {
        outError = "Invalid render target size.";
        return false;
    }

    if (!TryDirect3D9Background(p.width, p.height, outBgra))
    {
        const size_t stride = static_cast<size_t>(p.width) * 4;
        outBgra.assign(stride * static_cast<size_t>(p.height), 0xFF);
        for (uint32_t y = 0; y < p.height; ++y)
        {
            for (uint32_t x = 0; x < p.width; ++x)
            {
                uint8_t* px = &outBgra[(static_cast<size_t>(y) * p.width + x) * 4];
                px[0] = 0xF5;
                px[1] = 0xF0;
                px[2] = 0xE7;
                px[3] = 0xFF;
            }
        }
    }

    DrawTextSoftware(p.width, p.height, StripMarkup(sourceUtf8), outBgra);
    return true;
}
