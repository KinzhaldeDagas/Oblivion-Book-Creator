#include "ObBookRenderD2D.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool obbook::RenderStubBgra(const RenderParams& p, std::vector<uint8_t>& outBgra, std::string& outError)
{
    outError.clear();

    if (p.width == 0 || p.height == 0)
    {
        outError = "Invalid render target size.";
        return false;
    }

    const size_t stride = static_cast<size_t>(p.width) * 4;
    outBgra.assign(stride * static_cast<size_t>(p.height), 0);

    // Simple checkerboard + border so UI plumbing is testable on day one.
    for (uint32_t y = 0; y < p.height; y++)
    {
        for (uint32_t x = 0; x < p.width; x++)
        {
            uint8_t* px = &outBgra[(static_cast<size_t>(y) * p.width + x) * 4];
            const bool border = (x < 2 || y < 2 || x + 2 >= p.width || y + 2 >= p.height);
            const bool check = (((x >> 5) ^ (y >> 5)) & 1) != 0;
            uint8_t v = border ? 0x00 : (check ? 0xE6 : 0xF4);

            // BGRA
            px[0] = v;
            px[1] = v;
            px[2] = v;
            px[3] = 0xFF;
        }
    }

    return true;
}
