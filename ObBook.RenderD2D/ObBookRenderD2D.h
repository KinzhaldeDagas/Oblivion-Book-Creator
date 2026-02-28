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

    // Renders a preview BGRA8 buffer from compiled/normalized markup-like source.
    // Attempts a Direct3D9 path first and falls back to software rendering.
    bool RenderPreviewBgra(const RenderParams& p, const std::string& sourceUtf8, std::vector<uint8_t>& outBgra, std::string& outError);
}
