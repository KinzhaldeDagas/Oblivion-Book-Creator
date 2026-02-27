#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace obbook
{
    struct RenderParams
    {
        uint32_t width = 1024;
        uint32_t height = 768;
        float dpi = 96.0f;
    };

    // v1 stub: returns a BGRA8 buffer.
    // Later: consume PageModel output and draw real glyph runs + images.
    bool RenderStubBgra(const RenderParams& p, std::vector<uint8_t>& outBgra, std::string& outError);
}
