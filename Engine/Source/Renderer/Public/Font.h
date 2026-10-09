#pragma once

/**
 * DRX Engine - Font atlas (TrueType rasterised with stb_truetype)
 * CPU-only: produces an R8 coverage atlas plus glyph metrics for text layout.
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"

namespace DRX::Renderer {

/**
 * Screen-space quad for one glyph (pixels, y down) with atlas UVs
 */
struct GlyphQuad {
    float32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float32 u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

class FontAtlas {
public:
    FontAtlas();
    ~FontAtlas();

    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    /**
     * Rasterises the font at each pixel size (Basic Latin + Latin-1).
     * The font data must stay valid only during this call.
     */
    bool Build(const uint8* ttf_data, size_t ttf_size, const Array<float32>& pixel_sizes, uint32 atlas_size = 1024);

    bool IsValid() const { return !pixels_.empty(); }
    uint32 GetWidth() const { return width_; }
    uint32 GetHeight() const { return height_; }
    const Array<uint8>& GetPixels() const { return pixels_; }

    // UV of a fully covered texel, used for solid fills sharing the atlas texture
    Math::Vector2 GetWhiteUV() const { return white_uv_; }

    float32 GetLineHeight(float32 pixel_size) const;
    float32 MeasureText(const String& utf8_text, float32 pixel_size) const;

    // Lays out one line of text; (x, y) is the top-left corner of the line
    void LayoutText(const String& utf8_text, float32 x, float32 y, float32 pixel_size,
                    Array<GlyphQuad>& out_quads) const;

    // Decodes one UTF-8 code point starting at `index` and advances it (invalid bytes yield U+FFFD)
    static uint32 DecodeUtf8(const String& text, size_t& index);

private:
    struct Glyph {
        float32 x0, y0, x1, y1;     // offsets from the pen position (pixels at baked size)
        float32 u0, v0, u1, v1;
        float32 advance;
    };

    struct BakedSize {
        float32 pixel_size = 0;
        float32 ascent = 0;         // pixels above the baseline
        float32 line_height = 0;
        HashMap<uint32, Glyph> glyphs;
    };

    const BakedSize* SelectSize(float32 pixel_size) const;
    const Glyph* FindGlyph(const BakedSize& baked, uint32 codepoint) const;

    Array<BakedSize> sizes_;
    Array<uint8> pixels_;
    uint32 width_ = 0;
    uint32 height_ = 0;
    Math::Vector2 white_uv_;
};

} // namespace DRX::Renderer
