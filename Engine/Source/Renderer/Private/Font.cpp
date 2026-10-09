/**
 * DRX Engine - Font atlas implementation
 */

#include "Renderer/Public/Font.h"
#include "Core/Public/Log.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <cmath>

namespace DRX::Renderer {

namespace {

constexpr uint32 kWhiteBlockRows = 4;
constexpr uint32 kReplacementChar = 0xFFFD;

struct CodepointRange {
    uint32 first;
    uint32 count;
};

// Basic Latin and Latin-1 Supplement (covers German umlauts, ß, °, ©, ...)
constexpr CodepointRange kRanges[] = {
    {32, 95},
    {160, 96},
};

} // namespace

FontAtlas::FontAtlas() = default;
FontAtlas::~FontAtlas() = default;

bool FontAtlas::Build(const uint8* ttf_data, size_t ttf_size, const Array<float32>& pixel_sizes, uint32 atlas_size) {
    sizes_.clear();
    pixels_.clear();
    width_ = height_ = 0;

    if (!ttf_data || ttf_size == 0 || pixel_sizes.empty() || atlas_size < 64) {
        DRX_LOG_ERROR("FontAtlas::Build called with invalid arguments");
        return false;
    }

    stbtt_fontinfo font_info;
    const int font_offset = stbtt_GetFontOffsetForIndex(ttf_data, 0);
    if (font_offset < 0 || !stbtt_InitFont(&font_info, ttf_data, font_offset)) {
        DRX_LOG_ERROR("Invalid TrueType font data");
        return false;
    }

    const uint32 pack_height = atlas_size;
    Array<uint8> packed(static_cast<size_t>(atlas_size) * pack_height, 0);

    stbtt_pack_context pack;
    if (!stbtt_PackBegin(&pack, packed.data(), static_cast<int>(atlas_size), static_cast<int>(pack_height),
                         0, 1, nullptr)) {
        DRX_LOG_ERROR("Failed to start font packing");
        return false;
    }

    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);

    struct PendingSize {
        float32 pixel_size;
        Array<Array<stbtt_packedchar>> chars;
    };
    Array<PendingSize> pending;

    bool ok = true;
    for (float32 pixel_size : pixel_sizes) {
        if (pixel_size <= 0.0f) continue;

        // Oversampling keeps small sizes crisp under subpixel positioning
        const unsigned int oversample = pixel_size < 30.0f ? 2u : 1u;
        stbtt_PackSetOversampling(&pack, oversample, oversample);

        PendingSize entry;
        entry.pixel_size = pixel_size;
        Array<stbtt_pack_range> ranges;
        entry.chars.resize(std::size(kRanges));
        for (size_t r = 0; r < std::size(kRanges); ++r) {
            entry.chars[r].resize(kRanges[r].count);
            stbtt_pack_range range{};
            range.font_size = pixel_size;
            range.first_unicode_codepoint_in_range = static_cast<int>(kRanges[r].first);
            range.num_chars = static_cast<int>(kRanges[r].count);
            range.chardata_for_range = entry.chars[r].data();
            ranges.push_back(range);
        }

        if (!stbtt_PackFontRanges(&pack, ttf_data, font_offset, ranges.data(), static_cast<int>(ranges.size()))) {
            DRX_LOG_ERROR("Font atlas {}x{} too small for size {}", atlas_size, pack_height, pixel_size);
            ok = false;
            break;
        }
        pending.push_back(std::move(entry));
    }
    stbtt_PackEnd(&pack);

    if (!ok || pending.empty()) {
        return false;
    }

    // Final atlas = packed glyphs + a solid white strip at the bottom for untextured fills
    width_ = atlas_size;
    height_ = pack_height + kWhiteBlockRows;
    pixels_.assign(static_cast<size_t>(width_) * height_, 0);
    std::copy(packed.begin(), packed.end(), pixels_.begin());
    std::fill(pixels_.begin() + static_cast<std::ptrdiff_t>(packed.size()), pixels_.end(), uint8(255));
    white_uv_ = Math::Vector2(0.5f / static_cast<float32>(width_),
                              (static_cast<float32>(pack_height) + kWhiteBlockRows * 0.5f) / static_cast<float32>(height_));

    // Packed char coordinates were computed for the pack height; rescale v to the final height
    const float32 v_scale = static_cast<float32>(pack_height) / static_cast<float32>(height_);

    for (auto& entry : pending) {
        BakedSize baked;
        baked.pixel_size = entry.pixel_size;
        const float32 scale = stbtt_ScaleForPixelHeight(&font_info, entry.pixel_size);
        baked.ascent = static_cast<float32>(ascent) * scale;
        baked.line_height = static_cast<float32>(ascent - descent + line_gap) * scale;

        for (size_t r = 0; r < entry.chars.size(); ++r) {
            for (uint32 i = 0; i < kRanges[r].count; ++i) {
                const uint32 codepoint = kRanges[r].first + i;
                float32 pen_x = 0.0f;
                float32 pen_y = 0.0f;
                stbtt_aligned_quad q;
                stbtt_GetPackedQuad(entry.chars[r].data(), static_cast<int>(width_), static_cast<int>(pack_height),
                                    static_cast<int>(i), &pen_x, &pen_y, &q, 0);

                Glyph glyph;
                glyph.x0 = q.x0; glyph.y0 = q.y0; glyph.x1 = q.x1; glyph.y1 = q.y1;
                glyph.u0 = q.s0; glyph.u1 = q.s1;
                glyph.v0 = q.t0 * v_scale; glyph.v1 = q.t1 * v_scale;
                glyph.advance = entry.chars[r][i].xadvance;
                const int glyph_index = stbtt_FindGlyphIndex(&font_info, static_cast<int>(codepoint));
                if (glyph_index == 0 || stbtt_IsGlyphEmpty(&font_info, glyph_index)) {
                    glyph.x1 = glyph.x0;   // whitespace: advance only, no quad
                    glyph.y1 = glyph.y0;
                }
                baked.glyphs.emplace(codepoint, glyph);
            }
        }
        sizes_.push_back(std::move(baked));
    }

    std::sort(sizes_.begin(), sizes_.end(),
              [](const BakedSize& a, const BakedSize& b) { return a.pixel_size < b.pixel_size; });
    return true;
}

const FontAtlas::BakedSize* FontAtlas::SelectSize(float32 pixel_size) const {
    if (sizes_.empty()) return nullptr;

    // Prefer the smallest baked size that is not much smaller than requested (downscaling looks better)
    for (const auto& size : sizes_) {
        if (size.pixel_size >= pixel_size * 0.9f) {
            return &size;
        }
    }
    return &sizes_.back();
}

const FontAtlas::Glyph* FontAtlas::FindGlyph(const BakedSize& baked, uint32 codepoint) const {
    auto it = baked.glyphs.find(codepoint);
    if (it != baked.glyphs.end()) return &it->second;
    it = baked.glyphs.find('?');
    return it != baked.glyphs.end() ? &it->second : nullptr;
}

float32 FontAtlas::GetLineHeight(float32 pixel_size) const {
    const BakedSize* baked = SelectSize(pixel_size);
    return baked ? baked->line_height * (pixel_size / baked->pixel_size) : pixel_size;
}

float32 FontAtlas::MeasureText(const String& utf8_text, float32 pixel_size) const {
    const BakedSize* baked = SelectSize(pixel_size);
    if (!baked) return 0.0f;

    const float32 scale = pixel_size / baked->pixel_size;
    float32 width = 0.0f;
    for (size_t i = 0; i < utf8_text.size();) {
        const uint32 codepoint = DecodeUtf8(utf8_text, i);
        if (const Glyph* glyph = FindGlyph(*baked, codepoint)) {
            width += glyph->advance * scale;
        }
    }
    return width;
}

void FontAtlas::LayoutText(const String& utf8_text, float32 x, float32 y, float32 pixel_size,
                           Array<GlyphQuad>& out_quads) const {
    const BakedSize* baked = SelectSize(pixel_size);
    if (!baked) return;

    const float32 scale = pixel_size / baked->pixel_size;
    const float32 baseline = y + baked->ascent * scale;
    float32 pen = x;

    for (size_t i = 0; i < utf8_text.size();) {
        const uint32 codepoint = DecodeUtf8(utf8_text, i);
        const Glyph* glyph = FindGlyph(*baked, codepoint);
        if (!glyph) continue;

        if (glyph->x1 > glyph->x0 && glyph->y1 > glyph->y0) {
            GlyphQuad quad;
            quad.x0 = pen + glyph->x0 * scale;
            quad.x1 = pen + glyph->x1 * scale;
            quad.y0 = baseline + glyph->y0 * scale;
            quad.y1 = baseline + glyph->y1 * scale;
            quad.u0 = glyph->u0; quad.v0 = glyph->v0;
            quad.u1 = glyph->u1; quad.v1 = glyph->v1;
            out_quads.push_back(quad);
        }
        pen += glyph->advance * scale;
    }
}

uint32 FontAtlas::DecodeUtf8(const String& text, size_t& index) {
    const auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(index);

    uint32 codepoint = 0;
    size_t length = 0;
    if (lead < 0x80) { codepoint = lead; length = 1; }
    else if ((lead & 0xE0) == 0xC0) { codepoint = lead & 0x1F; length = 2; }
    else if ((lead & 0xF0) == 0xE0) { codepoint = lead & 0x0F; length = 3; }
    else if ((lead & 0xF8) == 0xF0) { codepoint = lead & 0x07; length = 4; }
    else { ++index; return kReplacementChar; }

    if (index + length > text.size()) {
        index = text.size();
        return kReplacementChar;
    }
    for (size_t k = 1; k < length; ++k) {
        const unsigned char next = byte(index + k);
        if ((next & 0xC0) != 0x80) {
            index += k;
            return kReplacementChar;
        }
        codepoint = (codepoint << 6) | (next & 0x3F);
    }
    index += length;
    return codepoint;
}

} // namespace DRX::Renderer
