#pragma once
// ============================================================================
// ACZero Enhancer — TrueType Font Engine (ui/acz_ui_font.h)
// Pure C++ stb_truetype.h dynamic UTF-8 glyph atlas renderer.
// 100% Windows & Linux / Steam Deck (Proton) compatible (zero DirectWrite/GDI).
// Loads TTF fonts from mods\enhancer\fonts\ with automatic fallback chain.
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "acz_ui_renderer.h"

namespace acz_ui {

enum class FontRole : uint8_t {
    Title = 0, // EuroStyle Normal.ttf (headers, tabs, banners)
    Body  = 1, // ACZ_HUD_Font.ttf (menu items, values, descriptions, HUD)
    Serif = 2, // Classic serif face (Georgia / Times) for Title Screen credits
};

enum class TextAlign : uint8_t {
    Left = 0,
    Center = 1,
    Right = 2,
};

struct LoadedFontFace {
    std::vector<uint8_t> ttfBytes;
    stbtt_fontinfo       info{};
    int                  ascent  = 0;
    int                  descent = 0;
    int                  lineGap = 0;
    bool                 valid   = false;
};

struct CachedGlyph {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    float xOff = 0.0f, yOff = 0.0f;
    float w = 0.0f, h = 0.0f;
    float advance = 0.0f;
    bool  hasBitmap = false;
};

class UiFontManager {
public:
    static constexpr uint32_t kAtlasSize = 1024;

    bool Initialize(const std::wstring& exeDir) {
        if (m_Initialized) return true;
        m_AtlasPixels.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0u);

        const std::wstring fontDir = exeDir + L"\\mods\\enhancer\\fonts\\";

        // Load Title font (EuroStyle Normal.ttf)
        if (!LoadFace(m_TitleFace, fontDir + L"EuroStyle Normal.ttf")) {
            if (!LoadFace(m_TitleFace, fontDir + L"ACZ_HUD_Font.ttf")) {
                LoadFace(m_TitleFace, L"C:\\Windows\\Fonts\\segoeuib.ttf");
            }
        }

        // Load Body / HUD font (ACZ_HUD_Font.ttf)
        if (!LoadFace(m_BodyFace, fontDir + L"ACZ_HUD_Font.ttf")) {
            if (!LoadFace(m_BodyFace, fontDir + L"EuroStyle Normal.ttf")) {
                LoadFace(m_BodyFace, L"C:\\Windows\\Fonts\\ebrima.ttf");
            }
        }

        // Load Serif font (for Title Screen classic typography)
        if (!LoadFace(m_SerifFace, fontDir + L"serif.ttf")) {
            if (!LoadFace(m_SerifFace, L"C:\\Windows\\Fonts\\georgia.ttf")) {
                if (!LoadFace(m_SerifFace, L"C:\\Windows\\Fonts\\times.ttf")) {
                    LoadFace(m_SerifFace, fontDir + L"EuroStyle Normal.ttf");
                }
            }
        }

        // Load Fallback font (for symbols / Cyrillic / extended Unicode)
        if (!LoadFace(m_FallbackFace, fontDir + L"fallback.ttf")) {
            if (!LoadFace(m_FallbackFace, L"C:\\Windows\\Fonts\\ebrima.ttf")) {
                if (!LoadFace(m_FallbackFace, L"C:\\Windows\\Fonts\\arial.ttf")) {
                    LoadFace(m_FallbackFace, L"C:\\Windows\\Fonts\\segoeui.ttf");
                }
            }
        }

        m_Initialized = (m_TitleFace.valid || m_BodyFace.valid || m_FallbackFace.valid);
        return m_Initialized;
    }

    bool IsReady() const { return m_Initialized; }

    // Ensure GPU atlas texture exists and upload if new glyphs were rasterized
    void SyncGpuAtlas(UiRenderer& renderer) {
        if (!m_Initialized || !renderer.IsReady()) return;
        if (m_AtlasTexSlot == 0) {
            m_AtlasTexSlot = renderer.CreateDynamicTexture(kAtlasSize, kAtlasSize, DXGI_FORMAT_R8_UNORM, 1);
            if (m_AtlasTexSlot != 0) {
                m_AtlasDirty = true;
            }
        }
        if (m_AtlasTexSlot != 0 && m_AtlasDirty) {
            renderer.UpdateTexturePixels(m_AtlasTexSlot, m_AtlasPixels.data(), kAtlasSize);
            m_AtlasDirty = false;
        }
    }

    // Measure text width in Virtual 1080p coordinates
    float MeasureTextWidth(const UiRenderer& renderer, FontRole role, float virtualSize, const char* utf8Text, float letterSpacing = 0.0f) {
        if (!m_Initialized || !utf8Text || !*utf8Text) return 0.0f;
        const float s = renderer.VirtualScale();
        const int pxHeight = ClampPxHeight(static_cast<int>(std::round(virtualSize * s)));
        const float trackPx = letterSpacing * s;

        float widthPx = 0.0f;
        int glyphCount = 0;
        const char* p = utf8Text;
        while (*p) {
            uint32_t cp = DecodeUtf8(p);
            const CachedGlyph* g = GetOrRasterizeGlyph(role, pxHeight, cp);
            if (g) {
                widthPx += g->advance;
                ++glyphCount;
            }
        }
        if (glyphCount > 1 && trackPx != 0.0f) {
            widthPx += trackPx * static_cast<float>(glyphCount - 1);
        }
        return (s > 0.001f) ? (widthPx / s) : widthPx;
    }

    // Draw text in Virtual 1080p coordinates (vy = top of text line)
    void DrawText(
        UiRenderer& renderer,
        FontRole role,
        float virtualSize,
        float vx, float vy,
        const char* utf8Text,
        Color color,
        TextAlign align = TextAlign::Left,
        bool dropShadow = true,
        float letterSpacing = 0.0f
    ) {
        if (!m_Initialized || !utf8Text || !*utf8Text) return;
        if (m_AtlasTexSlot == 0) {
            SyncGpuAtlas(renderer);
            if (m_AtlasTexSlot == 0) return;
        }

        const float s = renderer.VirtualScale();
        const int pxHeight = ClampPxHeight(static_cast<int>(std::round(virtualSize * s)));
        const float trackPx = letterSpacing * s;

        float startX = vx * s;
        if (align != TextAlign::Left) {
            float wVirtual = MeasureTextWidth(renderer, role, virtualSize, utf8Text, letterSpacing);
            if (align == TextAlign::Center) startX -= (wVirtual * s) * 0.5f;
            else if (align == TextAlign::Right) startX -= (wVirtual * s);
        }

        const LoadedFontFace* primary = PickPrimaryFace(role);
        float scale = primary ? stbtt_ScaleForPixelHeight(&primary->info, static_cast<float>(pxHeight)) : 1.0f;
        float baselineY = std::floor(vy * s + (primary ? (static_cast<float>(primary->ascent) * scale) : (static_cast<float>(pxHeight) * 0.8f)));

        if (dropShadow) {
            const float shOff = (std::max)(1.0f, std::round(1.5f * s));
            Color shCol(0, 0, 0, static_cast<uint8_t>((static_cast<int>(color.a) * 190) / 255));
            DrawGlyphRun(renderer, role, pxHeight, std::floor(startX) + shOff, baselineY + shOff, utf8Text, shCol, trackPx);
        }
        DrawGlyphRun(renderer, role, pxHeight, std::floor(startX), baselineY, utf8Text, color, trackPx);
    }

private:
    static int ClampPxHeight(int h) {
        if (h < 10) return 10;
        if (h > 96) return 96;
        return h;
    }

    static bool LoadFace(LoadedFontFace& out, const std::wstring& path) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 32 * 1024 * 1024) {
            std::fclose(f);
            return false;
        }
        out.ttfBytes.resize(static_cast<size_t>(sz));
        size_t rd = std::fread(out.ttfBytes.data(), 1, static_cast<size_t>(sz), f);
        std::fclose(f);
        if (rd != static_cast<size_t>(sz)) return false;

        int offset = stbtt_GetFontOffsetForIndex(out.ttfBytes.data(), 0);
        if (offset < 0) return false;
        if (!stbtt_InitFont(&out.info, out.ttfBytes.data(), offset)) return false;
        stbtt_GetFontVMetrics(&out.info, &out.ascent, &out.descent, &out.lineGap);
        out.valid = true;
        return true;
    }

    const LoadedFontFace* PickPrimaryFace(FontRole role) const {
        if (role == FontRole::Title && m_TitleFace.valid) return &m_TitleFace;
        if (role == FontRole::Serif && m_SerifFace.valid) return &m_SerifFace;
        if (m_BodyFace.valid) return &m_BodyFace;
        if (m_TitleFace.valid) return &m_TitleFace;
        if (m_FallbackFace.valid) return &m_FallbackFace;
        return nullptr;
    }

    const LoadedFontFace* PickFaceForCodepoint(FontRole role, uint32_t cp, int* outGlyphIdx) const {
        const LoadedFontFace* primary = PickPrimaryFace(role);
        if (primary) {
            int gi = stbtt_FindGlyphIndex(&primary->info, static_cast<int>(cp));
            if (gi != 0) {
                *outGlyphIdx = gi;
                return primary;
            }
        }
        if (m_BodyFace.valid && &m_BodyFace != primary) {
            int gi = stbtt_FindGlyphIndex(&m_BodyFace.info, static_cast<int>(cp));
            if (gi != 0) {
                *outGlyphIdx = gi;
                return &m_BodyFace;
            }
        }
        if (m_FallbackFace.valid) {
            int gi = stbtt_FindGlyphIndex(&m_FallbackFace.info, static_cast<int>(cp));
            if (gi != 0) {
                *outGlyphIdx = gi;
                return &m_FallbackFace;
            }
        }
        *outGlyphIdx = 0;
        return primary;
    }

    static uint32_t DecodeUtf8(const char*& p) {
        unsigned char c = static_cast<unsigned char>(*p++);
        if (c < 0x80) return c;
        if ((c & 0xE0) == 0xC0 && p[0]) {
            uint32_t r = ((c & 0x1F) << 6) | (static_cast<unsigned char>(*p++) & 0x3F);
            return r;
        }
        if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
            uint32_t r = ((c & 0x0F) << 12) |
                         ((static_cast<unsigned char>(p[0]) & 0x3F) << 6) |
                         (static_cast<unsigned char>(p[1]) & 0x3F);
            p += 2;
            return r;
        }
        if ((c & 0xF8) == 0xF0 && p[0] && p[1] && p[2]) {
            uint32_t r = ((c & 0x07) << 18) |
                         ((static_cast<unsigned char>(p[0]) & 0x3F) << 12) |
                         ((static_cast<unsigned char>(p[1]) & 0x3F) << 6) |
                         (static_cast<unsigned char>(p[2]) & 0x3F);
            p += 3;
            return r;
        }
        return '?';
    }

    static uint64_t MakeGlyphKey(FontRole role, int pxHeight, uint32_t cp) {
        return (static_cast<uint64_t>(static_cast<uint8_t>(role)) << 48) |
               (static_cast<uint64_t>(static_cast<uint16_t>(pxHeight)) << 32) |
               static_cast<uint64_t>(cp);
    }

    const CachedGlyph* GetOrRasterizeGlyph(FontRole role, int pxHeight, uint32_t cp) {
        const uint64_t key = MakeGlyphKey(role, pxHeight, cp);
        auto it = m_Glyphs.find(key);
        if (it != m_Glyphs.end()) {
            return &it->second;
        }

        int glyphIdx = 0;
        const LoadedFontFace* face = PickFaceForCodepoint(role, cp, &glyphIdx);
        if (!face) return nullptr;

        const float scale = stbtt_ScaleForPixelHeight(&face->info, static_cast<float>(pxHeight));
        int advW = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&face->info, glyphIdx, &advW, &lsb);

        CachedGlyph cg{};
        cg.advance = static_cast<float>(advW) * scale;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&face->info, glyphIdx, scale, scale, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0;
        const int gh = y1 - y0;

        if (gw > 0 && gh > 0 && gw < static_cast<int>(kAtlasSize) - 4 && gh < static_cast<int>(kAtlasSize) - 4) {
            constexpr uint32_t kPad = 2;
            if (m_PackX + static_cast<uint32_t>(gw) + kPad >= kAtlasSize) {
                m_PackX = kPad;
                m_PackY += m_PackRowH + kPad;
                m_PackRowH = 0;
            }
            if (m_PackY + static_cast<uint32_t>(gh) + kPad >= kAtlasSize) {
                // Atlas full: reset cache cleanly (happens only after resolution changes)
                m_Glyphs.clear();
                std::memset(m_AtlasPixels.data(), 0, m_AtlasPixels.size());
                m_PackX = kPad;
                m_PackY = kPad;
                m_PackRowH = 0;
            }

            stbtt_MakeGlyphBitmap(
                &face->info,
                m_AtlasPixels.data() + static_cast<size_t>(m_PackY) * kAtlasSize + m_PackX,
                gw, gh, static_cast<int>(kAtlasSize),
                scale, scale, glyphIdx
            );

            const float inv = 1.0f / static_cast<float>(kAtlasSize);
            cg.u0 = static_cast<float>(m_PackX) * inv;
            cg.v0 = static_cast<float>(m_PackY) * inv;
            cg.u1 = static_cast<float>(m_PackX + gw) * inv;
            cg.v1 = static_cast<float>(m_PackY + gh) * inv;
            cg.xOff = static_cast<float>(x0);
            cg.yOff = static_cast<float>(y0);
            cg.w    = static_cast<float>(gw);
            cg.h    = static_cast<float>(gh);
            cg.hasBitmap = true;

            m_PackX += static_cast<uint32_t>(gw) + kPad;
            if (static_cast<uint32_t>(gh) > m_PackRowH) m_PackRowH = static_cast<uint32_t>(gh);
            m_AtlasDirty = true;
        }

        auto res = m_Glyphs.emplace(key, cg);
        return &res.first->second;
    }

    void DrawGlyphRun(
        UiRenderer& renderer,
        FontRole role,
        int pxHeight,
        float penX,
        float baselineY,
        const char* utf8Text,
        Color color,
        float trackPx = 0.0f
    ) {
        const char* p = utf8Text;
        while (*p) {
            uint32_t cp = DecodeUtf8(p);
            const CachedGlyph* g = GetOrRasterizeGlyph(role, pxHeight, cp);
            if (!g) continue;
            if (g->hasBitmap) {
                renderer.DrawGlyphPx(
                    std::floor(penX + g->xOff),
                    std::floor(baselineY + g->yOff),
                    g->w, g->h,
                    g->u0, g->v0, g->u1, g->v1,
                    color,
                    m_AtlasTexSlot
                );
            }
            penX += g->advance + trackPx;
        }
    }

    bool             m_Initialized = false;
    LoadedFontFace   m_TitleFace;
    LoadedFontFace   m_BodyFace;
    LoadedFontFace   m_SerifFace;
    LoadedFontFace   m_FallbackFace;

    std::vector<uint8_t> m_AtlasPixels;
    uint32_t         m_AtlasTexSlot = 0;
    uint32_t         m_PackX = 2;
    uint32_t         m_PackY = 2;
    uint32_t         m_PackRowH = 0;
    bool             m_AtlasDirty = false;
    std::unordered_map<uint64_t, CachedGlyph> m_Glyphs;
};

} // namespace acz_ui
