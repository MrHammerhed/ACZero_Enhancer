#pragma once
// ============================================================================
// ACZero Enhancer — RmlUi 6.1 HTML/CSS Backend (ui/acz_rmlui_backend.h)
// Renders standard .rml (HTML) + .rcss (CSS) UI files from mods\enhancer\ui\
// directly onto acz.exe's D3D12 backbuffer via UiRenderer, using stb_truetype
// for 100% Windows & Linux / Steam Deck (Proton) compatibility.
// Includes automatic live Hot-Reload (file mtime watch + F5 key).
// ============================================================================

#ifndef RMLUI_STATIC_LIB
#define RMLUI_STATIC_LIB
#endif

#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/CallbackTexture.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <algorithm>

#include "acz_ui_renderer.h"
#include "acz_ui_font.h"
#include "acz_ui_widgets.h"
#include "screen_options.h"

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#endif
#include "stb_image.h"

namespace acz_ui {

// ============================================================================
// 1. SystemInterface (monotonic clock + enhancer.log bridge)
// ============================================================================
class AczRmlSystemInterface final : public Rml::SystemInterface {
public:
    using FnLog = void(*)(const char*, ...);
    explicit AczRmlSystemInterface(FnLog logFn) : m_LogFn(logFn) {
        QueryPerformanceFrequency(&m_Freq);
        QueryPerformanceCounter(&m_Start);
    }

    double GetElapsedTime() override {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return (m_Freq.QuadPart > 0)
            ? static_cast<double>(now.QuadPart - m_Start.QuadPart) / static_cast<double>(m_Freq.QuadPart)
            : 0.0;
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (m_LogFn && (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_WARNING || type == Rml::Log::LT_ASSERT)) {
            m_LogFn("[RmlUi] %s", message.c_str());
        }
        return true;
    }

private:
    FnLog         m_LogFn = nullptr;
    LARGE_INTEGER m_Freq{};
    LARGE_INTEGER m_Start{};
};

// ============================================================================
// 2. FileInterface (loads .rml / .rcss / .ttf relative to mods\enhancer\ui\)
// ============================================================================
class AczRmlFileInterface final : public Rml::FileInterface {
public:
    explicit AczRmlFileInterface(const std::filesystem::path& uiRoot) : m_UiRoot(uiRoot) {}

    Rml::FileHandle Open(const Rml::String& path) override {
        std::filesystem::path p = ResolvePath(path);
        FILE* f = nullptr;
        if (_wfopen_s(&f, p.c_str(), L"rb") != 0 || !f) return 0;
        return reinterpret_cast<Rml::FileHandle>(f);
    }

    void Close(Rml::FileHandle file) override {
        if (file) std::fclose(reinterpret_cast<FILE*>(file));
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        if (!file) return 0;
        return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        if (!file) return false;
        return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
    }

    size_t Tell(Rml::FileHandle file) override {
        if (!file) return 0;
        return static_cast<size_t>(std::ftell(reinterpret_cast<FILE*>(file)));
    }

    std::filesystem::path ResolvePath(const std::string& rel) const {
        std::filesystem::path p(rel);
        if (p.is_absolute()) return p;
        return m_UiRoot / p;
    }

private:
    std::filesystem::path m_UiRoot;
};

// ============================================================================
// 3. RenderInterface (bridges RmlUi 6.1 directly into UiRenderer D3D12)
// ============================================================================
class AczRmlRenderInterface final : public Rml::RenderInterface {
public:
    AczRmlRenderInterface(UiRenderer* renderer, const std::filesystem::path& uiRoot)
        : m_Renderer(renderer), m_UiRoot(uiRoot) {}

    struct CompiledGeom {
        std::vector<Rml::Vertex> vertices;
        std::vector<uint32_t>    indices;
    };

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
        auto* g = new CompiledGeom();
        g->vertices.assign(vertices.begin(), vertices.end());
        g->indices.reserve(indices.size());
        for (int idx : indices) {
            g->indices.push_back(static_cast<uint32_t>(idx));
        }
        return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        if (!m_Renderer || !geometry) return;
        const auto* g = reinterpret_cast<const CompiledGeom*>(geometry);
        if (g->vertices.empty() || g->indices.empty()) return;

        m_TempVerts.resize(g->vertices.size());
        for (size_t i = 0; i < g->vertices.size(); ++i) {
            const Rml::Vertex& rv = g->vertices[i];
            float x = rv.position.x + translation.x;
            float y = rv.position.y + translation.y;
            if (m_HasTransform) {
                Rml::Vector4f p = m_Transform * Rml::Vector4f(x, y, 0.0f, 1.0f);
                x = p.x;
                y = p.y;
            }
            UiVertex& uv = m_TempVerts[i];
            uv.x    = x * m_UiScale;
            uv.y    = y * m_UiScale;
            uv.u    = rv.tex_coord.x;
            uv.v    = rv.tex_coord.y;
            uv.r    = rv.colour.red   / 255.0f;
            uv.g    = rv.colour.green / 255.0f;
            uv.b    = rv.colour.blue  / 255.0f;
            uv.a    = rv.colour.alpha / 255.0f;
            uv.mode = 0u; // premultiplied RGBA8 texture (white 1x1 when untextured)
        }

        const uint32_t slot = (texture != 0) ? static_cast<uint32_t>(texture) : UiRenderer::kTexSlotWhite;
        m_Renderer->DrawRawIndexedMeshPx(
            m_TempVerts.data(), static_cast<uint32_t>(m_TempVerts.size()),
            g->indices.data(),  static_cast<uint32_t>(g->indices.size()),
            slot
        );
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override {
        delete reinterpret_cast<CompiledGeom*>(geometry);
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override {
        texture_dimensions = Rml::Vector2i(0, 0);
        if (!m_Renderer || source.empty()) return 0;

        std::filesystem::path p(source);
        if (!p.is_absolute()) p = m_UiRoot / p;

        FILE* f = nullptr;
        if (_wfopen_s(&f, p.c_str(), L"rb") != 0 || !f) return 0;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 64 * 1024 * 1024) { std::fclose(f); return 0; }

        std::vector<uint8_t> fileBytes(static_cast<size_t>(sz));
        size_t rd = std::fread(fileBytes.data(), 1, static_cast<size_t>(sz), f);
        std::fclose(f);
        if (rd != static_cast<size_t>(sz)) return 0;

        int w = 0, h = 0, comp = 0;
        uint8_t* rgba = stbi_load_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()), &w, &h, &comp, 4);
        if (!rgba || w <= 0 || h <= 0) {
            if (rgba) stbi_image_free(rgba);
            return 0;
        }

        // Convert to premultiplied RGBA8 for RmlUi blending
        const size_t pxCount = static_cast<size_t>(w) * static_cast<size_t>(h);
        for (size_t i = 0; i < pxCount; ++i) {
            uint8_t* px = rgba + i * 4u;
            const uint32_t a = px[3];
            px[0] = static_cast<uint8_t>((static_cast<uint32_t>(px[0]) * a + 127u) / 255u);
            px[1] = static_cast<uint8_t>((static_cast<uint32_t>(px[1]) * a + 127u) / 255u);
            px[2] = static_cast<uint8_t>((static_cast<uint32_t>(px[2]) * a + 127u) / 255u);
        }

        uint32_t slot = m_Renderer->CreateDynamicTexture(static_cast<uint32_t>(w), static_cast<uint32_t>(h), DXGI_FORMAT_R8G8B8A8_UNORM, 4);
        if (slot != 0) {
            m_Renderer->UpdateTexturePixels(slot, rgba, static_cast<uint32_t>(w) * 4u);
            texture_dimensions = Rml::Vector2i(w, h);
        }
        stbi_image_free(rgba);
        return static_cast<Rml::TextureHandle>(slot);
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override {
        if (!m_Renderer || source_dimensions.x <= 0 || source_dimensions.y <= 0) return 0;
        const uint32_t w = static_cast<uint32_t>(source_dimensions.x);
        const uint32_t h = static_cast<uint32_t>(source_dimensions.y);
        uint32_t slot = m_Renderer->CreateDynamicTexture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
        if (slot == 0) return 0;
        m_Renderer->UpdateTexturePixels(slot, source.data(), w * 4u);
        return static_cast<Rml::TextureHandle>(slot);
    }

    void ReleaseTexture(Rml::TextureHandle texture) override {
        if (!m_Renderer || texture == 0) return;
        m_Renderer->ReleaseDynamicTexture(static_cast<uint32_t>(texture));
    }

    void SetUiScale(float scale) {
        m_UiScale = (scale > 0.05f) ? scale : 1.0f;
    }

    void EnableScissorRegion(bool enable) override {
        m_ScissorEnabled = enable;
        ApplyEffectiveScissor();
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
        m_ScissorRect = region;
        ApplyEffectiveScissor();
    }

    void EnableClipMask(bool enable) override {
        m_ClipMaskEnabled = enable;
        if (!enable) {
            m_ClipRectValid = false;
        }
        ApplyEffectiveScissor();
    }

    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override {
        if (!geometry || operation == Rml::ClipMaskOperation::SetInverse) {
            m_ClipRectValid = false;
            ApplyEffectiveScissor();
            return;
        }
        const auto* g = reinterpret_cast<const CompiledGeom*>(geometry);
        if (g->vertices.empty()) return;

        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (const Rml::Vertex& rv : g->vertices) {
            float x = rv.position.x + translation.x;
            float y = rv.position.y + translation.y;
            if (m_HasTransform) {
                Rml::Vector4f p = m_Transform * Rml::Vector4f(x, y, 0.0f, 1.0f);
                x = p.x;
                y = p.y;
            }
            const float px = x * m_UiScale;
            const float py = y * m_UiScale;
            if (px < minX) minX = px;
            if (py < minY) minY = py;
            if (px > maxX) maxX = px;
            if (py > maxY) maxY = py;
        }

        const int l = static_cast<int>(std::floor(minX));
        const int t = static_cast<int>(std::floor(minY));
        const int r = static_cast<int>(std::ceil(maxX));
        const int b = static_cast<int>(std::ceil(maxY));

        if (operation == Rml::ClipMaskOperation::Set || !m_ClipRectValid) {
            m_ClipL = l; m_ClipT = t; m_ClipR = r; m_ClipB = b;
            m_ClipRectValid = true;
        } else if (operation == Rml::ClipMaskOperation::Intersect) {
            m_ClipL = (std::max)(m_ClipL, l);
            m_ClipT = (std::max)(m_ClipT, t);
            m_ClipR = (std::min)(m_ClipR, r);
            m_ClipB = (std::min)(m_ClipB, b);
            if (m_ClipR < m_ClipL) m_ClipR = m_ClipL;
            if (m_ClipB < m_ClipT) m_ClipB = m_ClipT;
        }
        ApplyEffectiveScissor();
    }

    void SetTransform(const Rml::Matrix4f* transform) override {
        if (transform) {
            m_HasTransform = true;
            m_Transform    = *transform;
        } else {
            m_HasTransform = false;
        }
    }

private:
    void ApplyEffectiveScissor() {
        if (!m_Renderer) return;
        const bool useScissor = m_ScissorEnabled && m_ScissorRect.Valid();
        const bool useClip    = m_ClipMaskEnabled && m_ClipRectValid;
        if (!useScissor && !useClip) {
            m_Renderer->SetScissorPx(false, 0, 0, 0, 0);
            return;
        }
        int l = 0, t = 0, r = 0, b = 0;
        if (useScissor) {
            l = static_cast<int>(std::floor(m_ScissorRect.Left()   * m_UiScale));
            t = static_cast<int>(std::floor(m_ScissorRect.Top()    * m_UiScale));
            r = static_cast<int>(std::ceil(m_ScissorRect.Right()   * m_UiScale));
            b = static_cast<int>(std::ceil(m_ScissorRect.Bottom()  * m_UiScale));
        }
        if (useClip) {
            if (useScissor) {
                l = (std::max)(l, m_ClipL);
                t = (std::max)(t, m_ClipT);
                r = (std::min)(r, m_ClipR);
                b = (std::min)(b, m_ClipB);
                if (r < l) r = l;
                if (b < t) b = t;
            } else {
                l = m_ClipL; t = m_ClipT; r = m_ClipR; b = m_ClipB;
            }
        }
        m_Renderer->SetScissorPx(true, l, t, r, b);
    }

    UiRenderer*           m_Renderer = nullptr;
    std::filesystem::path m_UiRoot;
    std::vector<UiVertex> m_TempVerts;
    float                 m_UiScale = 1.0f;
    bool                  m_ScissorEnabled = false;
    Rml::Rectanglei       m_ScissorRect = Rml::Rectanglei::MakeInvalid();
    bool                  m_ClipMaskEnabled = false;
    bool                  m_ClipRectValid   = false;
    int                   m_ClipL = 0, m_ClipT = 0, m_ClipR = 0, m_ClipB = 0;
    bool                  m_HasTransform = false;
    Rml::Matrix4f         m_Transform = Rml::Matrix4f::Identity();
};

// ============================================================================
// 4. FontEngineInterface (pure stb_truetype.h, zero FreeType, Steam Deck safe)
// ============================================================================
class AczRmlFontEngine final : public Rml::FontEngineInterface {
public:
    static constexpr uint32_t kAtlasSize = 1024;

    AczRmlFontEngine(UiRenderer* renderer, const std::filesystem::path& uiRoot)
        : m_Renderer(renderer), m_UiRoot(uiRoot) {}

    void Initialize() override {
        m_AtlasRgba.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4u, 0u);
        m_Glyphs.clear();
        m_PackX = 2;
        m_PackY = 2;
        m_PackRowH = 0;
        m_AtlasDirty = false;
        m_AtlasVersion = 1;

        // Preload fallback system font if available (for extended Unicode/Cyrillic)
        std::filesystem::path fb = m_UiRoot / ".." / "fonts" / "fallback.ttf";
        if (!LoadFaceFromPath(fb, "__fallback__", true)) {
            if (!LoadFaceFromPath("C:\\Windows\\Fonts\\ebrima.ttf", "__fallback__", true)) {
                LoadFaceFromPath("C:\\Windows\\Fonts\\arial.ttf", "__fallback__", true);
            }
        }
    }

    void Shutdown() override {
        m_Faces.clear();
        m_Handles.clear();
        m_Glyphs.clear();
        m_AtlasRgba.clear();
        m_AtlasCallbackTex = Rml::CallbackTexture();
        m_AtlasSlot = 0;
    }

    bool LoadFontFace(const Rml::String& file_name, int face_index, bool fallback_face, Rml::Style::FontWeight weight) override {
        (void)face_index;
        (void)weight;
        std::filesystem::path p(file_name);
        if (!p.is_absolute()) p = m_UiRoot / p;
        std::string family = GuessFamilyFromPath(file_name);
        return LoadFaceFromPath(p, family, fallback_face);
    }

    bool LoadFontFace(Rml::Span<const Rml::byte> data, int face_index, const Rml::String& family,
                      Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback_face) override {
        (void)style;
        (void)weight;
        if (data.empty()) return false;
        FaceEntry e;
        e.family = LowerStr(family);
        e.isFallback = fallback_face;
        e.bytes.assign(data.begin(), data.end());
        int off = stbtt_GetFontOffsetForIndex(e.bytes.data(), face_index);
        if (off < 0) off = 0;
        if (!stbtt_InitFont(&e.info, e.bytes.data(), off)) return false;
        stbtt_GetFontVMetrics(&e.info, &e.ascent, &e.descent, &e.lineGap);
        m_Faces.push_back(std::move(e));
        return true;
    }

    bool LoadFaceWithFamily(const std::filesystem::path& path, const std::string& family, bool fallback_face) {
        return LoadFaceFromPath(path, family, fallback_face);
    }

    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight, int size) override {
        (void)style;
        if (m_Faces.empty()) return 0;
        if (size < 8) size = 8;
        if (size > 96) size = 96;
        const bool isBold = (static_cast<int>(weight) >= static_cast<int>(Rml::Style::FontWeight::Bold));

        const std::string famLow = LowerStr(family);
        int faceIdx = -1;
        for (int i = 0; i < static_cast<int>(m_Faces.size()); ++i) {
            if (!m_Faces[i].isFallback && m_Faces[i].family == famLow) {
                faceIdx = i;
                break;
            }
        }
        if (faceIdx < 0) {
            for (int i = 0; i < static_cast<int>(m_Faces.size()); ++i) {
                if (!m_Faces[i].isFallback) { faceIdx = i; break; }
            }
        }
        if (faceIdx < 0) faceIdx = 0;

        const uint64_t key = (static_cast<uint64_t>(faceIdx) << 33)
                           | (static_cast<uint64_t>(isBold ? 1u : 0u) << 32)
                           | static_cast<uint64_t>(size);
        auto it = m_HandleMap.find(key);
        if (it != m_HandleMap.end()) return it->second;

        HandleEntry he{};
        he.faceIdx = faceIdx;
        he.size    = size;
        he.bold    = isBold;
        const FaceEntry& fe = m_Faces[faceIdx];
        const float scale = stbtt_ScaleForPixelHeight(&fe.info, static_cast<float>(size));
        he.metrics.size                = size;
        he.metrics.ascent              = static_cast<float>(fe.ascent) * scale;
        he.metrics.descent             = static_cast<float>(-fe.descent) * scale;
        he.metrics.line_spacing        = static_cast<float>(fe.ascent - fe.descent + fe.lineGap) * scale;
        he.metrics.x_height            = he.metrics.ascent * 0.65f;
        he.metrics.underline_position  = he.metrics.descent * 0.4f;
        he.metrics.underline_thickness = (std::max)(1.0f, static_cast<float>(size) * 0.06f);

        m_Handles.push_back(he);
        Rml::FontFaceHandle handle = static_cast<Rml::FontFaceHandle>(m_Handles.size()); // 1-based
        m_HandleMap[key] = handle;
        return handle;
    }

    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle, const Rml::FontEffectList& font_effects) override {
        (void)handle;
        if (font_effects.empty()) return 0;
        // Take the first active font-effect (glow / outline / shadow)
        for (const auto& fe : font_effects) {
            if (!fe) continue;
            EffectEntry ee{};
            ee.color = fe->GetColour();
            Rml::FontGlyph dummy{};
            dummy.bitmap_dimensions = Rml::Vector2i(16, 16);
            Rml::Vector2i orig(0, 0);
            Rml::Vector2i dims = dummy.bitmap_dimensions;
            if (fe->GetGlyphMetrics(orig, dims, dummy)) {
                const int padX = (dims.x - dummy.bitmap_dimensions.x) / 2;
                const int padY = (dims.y - dummy.bitmap_dimensions.y) / 2;
                if (padX > 0 || padY > 0) {
                    ee.isGlow = true;
                    ee.radius = static_cast<float>((std::max)(padX, padY));
                } else {
                    ee.isGlow = false;
                    ee.offX   = static_cast<float>(orig.x);
                    ee.offY   = static_cast<float>(orig.y);
                }
            } else {
                ee.isGlow = true;
                ee.radius = 2.0f;
            }
            m_Effects.push_back(ee);
            return static_cast<Rml::FontEffectsHandle>(m_Effects.size()); // 1-based
        }
        return 0;
    }

    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override {
        if (handle == 0 || handle > m_Handles.size()) return m_EmptyMetrics;
        return m_Handles[handle - 1].metrics;
    }

    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
                       const Rml::TextShapingContext& ctx, Rml::Character prior_character) override {
        (void)prior_character;
        if (handle == 0 || handle > m_Handles.size() || string.empty()) return 0;
        const HandleEntry& he = m_Handles[handle - 1];
        float w = 0.0f;
        const char* p = string.begin();
        const char* end = string.end();
        while (p < end) {
            uint32_t cp = DecodeUtf8(p, end);
            const GlyphInfo* g = GetOrRasterize(he.faceIdx, he.size, cp);
            if (g) w += g->advance + ctx.letter_spacing;
        }
        return static_cast<int>(std::ceil(w));
    }

    int GenerateString(Rml::RenderManager& render_manager, Rml::FontFaceHandle face_handle,
                       Rml::FontEffectsHandle font_effects_handle, Rml::StringView string,
                       Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity,
                       const Rml::TextShapingContext& ctx, Rml::TexturedMeshList& mesh_list) override {
        (void)font_effects_handle;
        (void)opacity;
        if (face_handle == 0 || face_handle > m_Handles.size() || string.empty()) return 0;

        const HandleEntry& he = m_Handles[face_handle - 1];
        EnsureAtlasCallbackTexture(render_manager);

        Rml::TexturedMesh tm;
        tm.texture = static_cast<Rml::Texture>(m_AtlasCallbackTex);

        float penX = std::floor(position.x);
        const float baseY = std::floor(position.y);
        const float startX = penX;

        const char* p = string.begin();
        const char* end = string.end();
        while (p < end) {
            uint32_t cp = DecodeUtf8(p, end);
            const GlyphInfo* g = GetOrRasterize(he.faceIdx, he.size, cp);
            if (!g) continue;
            if (g->hasBitmap) {
                const float x0 = std::floor(penX + g->xOff);
                const float y0 = std::floor(baseY + g->yOff);
                const float x1 = x0 + g->w;
                const float y1 = y0 + g->h;

                auto PushQuad = [&](float qx0, float qy0, float qx1, float qy1, Rml::ColourbPremultiplied qcol) {
                    const int qb = static_cast<int>(tm.mesh.vertices.size());
                    tm.mesh.vertices.push_back(Rml::Vertex{ {qx0, qy0}, qcol, {g->u0, g->v0} });
                    tm.mesh.vertices.push_back(Rml::Vertex{ {qx1, qy0}, qcol, {g->u1, g->v0} });
                    tm.mesh.vertices.push_back(Rml::Vertex{ {qx1, qy1}, qcol, {g->u1, g->v1} });
                    tm.mesh.vertices.push_back(Rml::Vertex{ {qx0, qy1}, qcol, {g->u0, g->v1} });
                    tm.mesh.indices.push_back(qb + 0); tm.mesh.indices.push_back(qb + 1); tm.mesh.indices.push_back(qb + 2);
                    tm.mesh.indices.push_back(qb + 0); tm.mesh.indices.push_back(qb + 2); tm.mesh.indices.push_back(qb + 3);
                };

                if (font_effects_handle > 0 && font_effects_handle <= m_Effects.size()) {
                    const EffectEntry& ee = m_Effects[font_effects_handle - 1];
                    const float effAlpha = (static_cast<float>(ee.color.alpha) / 255.0f) * opacity;
                    Rml::ColourbPremultiplied effCol(
                        static_cast<Rml::byte>(ee.color.red   * effAlpha),
                        static_cast<Rml::byte>(ee.color.green * effAlpha),
                        static_cast<Rml::byte>(ee.color.blue  * effAlpha),
                        static_cast<Rml::byte>(255.0f         * effAlpha)
                    );
                    if (ee.isGlow) {
                        const int blurR = (std::max)(1, (std::min)(8, static_cast<int>(std::round(ee.radius))));
                        const GlyphInfo* bg = GetOrRasterize(he.faceIdx, he.size, cp, blurR);
                        if (bg && bg->hasBitmap) {
                            const float bx0 = std::floor(penX + bg->xOff);
                            const float by0 = std::floor(baseY + bg->yOff);
                            const float bx1 = bx0 + bg->w;
                            const float by1 = by0 + bg->h;
                            const int qb = static_cast<int>(tm.mesh.vertices.size());
                            tm.mesh.vertices.push_back(Rml::Vertex{ {bx0, by0}, effCol, {bg->u0, bg->v0} });
                            tm.mesh.vertices.push_back(Rml::Vertex{ {bx1, by0}, effCol, {bg->u1, bg->v0} });
                            tm.mesh.vertices.push_back(Rml::Vertex{ {bx1, by1}, effCol, {bg->u1, bg->v1} });
                            tm.mesh.vertices.push_back(Rml::Vertex{ {bx0, by1}, effCol, {bg->u0, bg->v1} });
                            tm.mesh.indices.push_back(qb + 0); tm.mesh.indices.push_back(qb + 1); tm.mesh.indices.push_back(qb + 2);
                            tm.mesh.indices.push_back(qb + 0); tm.mesh.indices.push_back(qb + 2); tm.mesh.indices.push_back(qb + 3);
                        }
                    } else {
                        PushQuad(x0 + ee.offX, y0 + ee.offY, x1 + ee.offX, y1 + ee.offY, effCol);
                    }
                } else if (colour.red > 180 && colour.green > 120 && colour.alpha > 160) {
                    // Subtle authentic ACZ warm golden phosphor glow behind bright yellow UI text
                    Rml::ColourbPremultiplied glowCol(
                        static_cast<Rml::byte>(colour.red   / 4),
                        static_cast<Rml::byte>(colour.green / 5),
                        static_cast<Rml::byte>(colour.blue  / 6),
                        static_cast<Rml::byte>(colour.alpha / 5)
                    );
                    static constexpr float kOff[4][2] = { {-1.5f, 0.0f}, {1.5f, 0.0f}, {0.0f, -1.5f}, {0.0f, 1.5f} };
                    for (int k = 0; k < 4; ++k) {
                        PushQuad(x0 + kOff[k][0], y0 + kOff[k][1], x1 + kOff[k][0], y1 + kOff[k][1], glowCol);
                    }
                }

                // Crisp synthetic bold pass when CSS specifies font-weight: bold
                if (he.bold) {
                    PushQuad(x0 + 1.0f, y0, x1 + 1.0f, y1, colour);
                    PushQuad(x0 + 0.5f, y0 + 0.5f, x1 + 0.5f, y1 + 0.5f, colour);
                }

                PushQuad(x0, y0, x1, y1, colour);
            }
            penX += g->advance + ctx.letter_spacing;
        }

        SyncAtlasToGpu();

        if (!tm.mesh.indices.empty()) {
            mesh_list.push_back(std::move(tm));
        }
        return static_cast<int>(std::ceil(penX - startX));
    }

    int GetVersion(Rml::FontFaceHandle handle) override {
        (void)handle;
        return m_AtlasVersion;
    }

    void ReleaseFontResources() override {
        m_AtlasCallbackTex = Rml::CallbackTexture();
        m_AtlasSlot = 0;
    }

    // Must be called after Rml::ReleaseTextures() so the GPU font atlas slot is recreated cleanly.
    void OnTexturesReleased() {
        m_AtlasSlot  = 0;
        m_AtlasDirty = true;
    }

    void SyncAtlasToGpu() {
        if (m_Renderer && m_AtlasSlot != 0 && m_AtlasDirty) {
            m_Renderer->UpdateTexturePixels(m_AtlasSlot, m_AtlasRgba.data(), kAtlasSize * 4u);
            m_AtlasDirty = false;
        }
    }

private:
    struct FaceEntry {
        std::string          family;
        bool                 isFallback = false;
        std::vector<uint8_t> bytes;
        stbtt_fontinfo       info{};
        int                  ascent = 0, descent = 0, lineGap = 0;
    };

    struct HandleEntry {
        int              faceIdx = 0;
        int              size    = 16;
        bool             bold    = false;
        Rml::FontMetrics metrics{};
    };

    struct EffectEntry {
        bool         isGlow = true;
        float        radius = 2.0f;
        float        offX   = 0.0f;
        float        offY   = 0.0f;
        Rml::Colourb color  = Rml::Colourb(255, 200, 40, 255);
    };

    struct GlyphInfo {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float xOff = 0, yOff = 0, w = 0, h = 0;
        float advance = 0;
        bool  hasBitmap = false;
    };

    static std::string LowerStr(std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    }

    static std::string GuessFamilyFromPath(const std::string& p) {
        std::string low = LowerStr(p);
        if (low.find("eurostyle") != std::string::npos) return "acztitle";
        if (low.find("acz_hud") != std::string::npos)   return "aczbody";
        return std::filesystem::path(p).stem().string();
    }

    bool LoadFaceFromPath(const std::filesystem::path& path, const std::string& family, bool fallback_face) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 32 * 1024 * 1024) { std::fclose(f); return false; }

        FaceEntry e;
        e.family     = LowerStr(family);
        e.isFallback = fallback_face;
        e.bytes.resize(static_cast<size_t>(sz));
        size_t rd = std::fread(e.bytes.data(), 1, static_cast<size_t>(sz), f);
        std::fclose(f);
        if (rd != static_cast<size_t>(sz)) return false;

        int off = stbtt_GetFontOffsetForIndex(e.bytes.data(), 0);
        if (off < 0) off = 0;
        if (!stbtt_InitFont(&e.info, e.bytes.data(), off)) return false;
        stbtt_GetFontVMetrics(&e.info, &e.ascent, &e.descent, &e.lineGap);
        m_Faces.push_back(std::move(e));
        return true;
    }

    void EnsureAtlasCallbackTexture(Rml::RenderManager& rm) {
        if (static_cast<bool>(m_AtlasCallbackTex)) return;
        m_AtlasCallbackTex = rm.MakeCallbackTexture([this](const Rml::CallbackTextureInterface& cti) -> bool {
            if (!m_Renderer || !m_Renderer->IsReady()) return false;
            // RmlUi invokes this callback whenever the texture handle needs to be (re)created.
            if (!cti.GenerateTexture(
                    Rml::Span<const Rml::byte>(m_AtlasRgba.data(), m_AtlasRgba.size()),
                    Rml::Vector2i(static_cast<int>(kAtlasSize), static_cast<int>(kAtlasSize)))) {
                return false;
            }
            m_AtlasSlot  = m_Renderer->LastCreatedTextureSlot();
            m_AtlasDirty = false;
            return true;
        });
    }

    static uint32_t DecodeUtf8(const char*& p, const char* end) {
        unsigned char c = static_cast<unsigned char>(*p++);
        if (c < 0x80) return c;
        if ((c & 0xE0) == 0xC0 && p < end) {
            return ((c & 0x1F) << 6) | (static_cast<unsigned char>(*p++) & 0x3F);
        }
        if ((c & 0xF0) == 0xE0 && p + 1 < end) {
            uint32_t r = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(p[0]) & 0x3F) << 6) | (static_cast<unsigned char>(p[1]) & 0x3F);
            p += 2;
            return r;
        }
        if ((c & 0xF8) == 0xF0 && p + 2 < end) {
            uint32_t r = ((c & 0x07) << 18) | ((static_cast<unsigned char>(p[0]) & 0x3F) << 12) |
                         ((static_cast<unsigned char>(p[1]) & 0x3F) << 6) | (static_cast<unsigned char>(p[2]) & 0x3F);
            p += 3;
            return r;
        }
        return '?';
    }

    const GlyphInfo* GetOrRasterize(int faceIdx, int pxHeight, uint32_t cp, int blurRadius = 0) {
        if (blurRadius < 0) blurRadius = 0;
        if (blurRadius > 8) blurRadius = 8;
        const uint64_t key = (static_cast<uint64_t>(faceIdx) << 48) |
                             (static_cast<uint64_t>(blurRadius) << 40) |
                             (static_cast<uint64_t>(pxHeight) << 32) |
                             static_cast<uint64_t>(cp);
        auto it = m_Glyphs.find(key);
        if (it != m_Glyphs.end()) return &it->second;

        const FaceEntry* chosen = &m_Faces[faceIdx];
        int glyphIdx = stbtt_FindGlyphIndex(&chosen->info, static_cast<int>(cp));
        if (glyphIdx == 0) {
            for (const FaceEntry& fb : m_Faces) {
                if (fb.isFallback) {
                    int fi = stbtt_FindGlyphIndex(&fb.info, static_cast<int>(cp));
                    if (fi != 0) { chosen = &fb; glyphIdx = fi; break; }
                }
            }
        }

        const float scale = stbtt_ScaleForPixelHeight(&chosen->info, static_cast<float>(pxHeight));
        int advW = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&chosen->info, glyphIdx, &advW, &lsb);

        GlyphInfo gi{};
        gi.advance = static_cast<float>(advW) * scale;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&chosen->info, glyphIdx, scale, scale, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0;
        const int gh = y1 - y0;
        const int pad = blurRadius;
        const int bw = gw + pad * 2;
        const int bh = gh + pad * 2;

        if (gw > 0 && gh > 0 && bw < static_cast<int>(kAtlasSize) - 4 && bh < static_cast<int>(kAtlasSize) - 4) {
            constexpr uint32_t kPad = 2;
            if (m_PackX + static_cast<uint32_t>(bw) + kPad >= kAtlasSize) {
                m_PackX = kPad;
                m_PackY += m_PackRowH + kPad;
                m_PackRowH = 0;
            }
            if (m_PackY + static_cast<uint32_t>(bh) + kPad >= kAtlasSize) {
                m_Glyphs.clear();
                std::memset(m_AtlasRgba.data(), 0, m_AtlasRgba.size());
                m_PackX = kPad;
                m_PackY = kPad;
                m_PackRowH = 0;
                ++m_AtlasVersion;
            }

            std::vector<uint8_t> tmp(static_cast<size_t>(bw) * bh, 0u);
            stbtt_MakeGlyphBitmap(&chosen->info, tmp.data() + static_cast<size_t>(pad) * bw + pad, gw, gh, bw, scale, scale, glyphIdx);

            if (blurRadius > 0) {
                // True separable 2-pass Gaussian blur for silky-smooth CRT phosphor glow
                const float sigma = (std::max)(0.8f, static_cast<float>(blurRadius) * 0.52f);
                const float inv2s2 = 1.0f / (2.0f * sigma * sigma);
                float kernel[17]{};
                float ksum = 0.0f;
                for (int k = -blurRadius; k <= blurRadius; ++k) {
                    const float w = std::exp(-static_cast<float>(k * k) * inv2s2);
                    kernel[k + blurRadius] = w;
                    ksum += w;
                }
                for (int k = 0; k <= blurRadius * 2; ++k) kernel[k] /= ksum;

                std::vector<float> horiz(static_cast<size_t>(bw) * bh, 0.0f);
                for (int y = 0; y < bh; ++y) {
                    const uint8_t* rowIn = tmp.data() + static_cast<size_t>(y) * bw;
                    float* rowOut = horiz.data() + static_cast<size_t>(y) * bw;
                    for (int x = 0; x < bw; ++x) {
                        float acc = 0.0f;
                        for (int k = -blurRadius; k <= blurRadius; ++k) {
                            const int sx = x + k;
                            if (sx >= 0 && sx < bw) acc += static_cast<float>(rowIn[sx]) * kernel[k + blurRadius];
                        }
                        rowOut[x] = acc;
                    }
                }
                for (int y = 0; y < bh; ++y) {
                    uint8_t* rowOut = tmp.data() + static_cast<size_t>(y) * bw;
                    for (int x = 0; x < bw; ++x) {
                        float acc = 0.0f;
                        for (int k = -blurRadius; k <= blurRadius; ++k) {
                            const int sy = y + k;
                            if (sy >= 0 && sy < bh) acc += horiz[static_cast<size_t>(sy) * bw + x] * kernel[k + blurRadius];
                        }
                        const int v = static_cast<int>(acc * 1.45f + 0.5f);
                        rowOut[x] = static_cast<uint8_t>((std::min)(255, (std::max)(0, v)));
                    }
                }
            }

            // Pack into premultiplied RGBA8 (r=g=b=a=alpha) for RmlUi
            for (int row = 0; row < bh; ++row) {
                uint8_t* dst = m_AtlasRgba.data() + (static_cast<size_t>(m_PackY + row) * kAtlasSize + m_PackX) * 4u;
                const uint8_t* src = tmp.data() + static_cast<size_t>(row) * bw;
                for (int col = 0; col < bw; ++col) {
                    uint8_t a = src[col];
                    dst[col * 4 + 0] = a;
                    dst[col * 4 + 1] = a;
                    dst[col * 4 + 2] = a;
                    dst[col * 4 + 3] = a;
                }
            }

            const float inv = 1.0f / static_cast<float>(kAtlasSize);
            gi.u0 = static_cast<float>(m_PackX) * inv;
            gi.v0 = static_cast<float>(m_PackY) * inv;
            gi.u1 = static_cast<float>(m_PackX + bw) * inv;
            gi.v1 = static_cast<float>(m_PackY + bh) * inv;
            gi.xOff = static_cast<float>(x0 - pad);
            gi.yOff = static_cast<float>(y0 - pad);
            gi.w    = static_cast<float>(bw);
            gi.h    = static_cast<float>(bh);
            gi.hasBitmap = true;

            m_PackX += static_cast<uint32_t>(bw) + kPad;
            if (static_cast<uint32_t>(bh) > m_PackRowH) m_PackRowH = static_cast<uint32_t>(bh);
            m_AtlasDirty = true;
        }

        auto res = m_Glyphs.emplace(key, gi);
        return &res.first->second;
    }

    UiRenderer*                              m_Renderer = nullptr;
    std::filesystem::path                    m_UiRoot;
    std::vector<FaceEntry>                   m_Faces;
    std::vector<HandleEntry>                 m_Handles;
    std::vector<EffectEntry>                 m_Effects;
    std::unordered_map<uint64_t, Rml::FontFaceHandle> m_HandleMap;
    Rml::FontMetrics                         m_EmptyMetrics{};

    std::vector<uint8_t>                     m_AtlasRgba;
    std::unordered_map<uint64_t, GlyphInfo>  m_Glyphs;
    uint32_t                                 m_PackX = 2;
    uint32_t                                 m_PackY = 2;
    uint32_t                                 m_PackRowH = 0;
    bool                                     m_AtlasDirty = false;
    int                                      m_AtlasVersion = 1;
    uint32_t                                 m_AtlasSlot = 0;
    Rml::CallbackTexture                     m_AtlasCallbackTex;
};

// ============================================================================
// Procedural Floating Embers Simulation (GPU Pixel-Shader Mode 2)
//   - 3 Depth-of-Field Layers: Far (small/slow), Mid (crisp), Near (large bokeh)
//   - Global Wind Gusts every 3..5s: all embers surge & flare together in oxygen
//   - Unique organic ember silhouette + glowing internal veins per particle
// ============================================================================
class UiSparkSystem {
public:
    struct Spark {
        float x, y;          // normalized [0..1]
        float vx, vy;        // normalized velocity / sec
        float liftBase;      // individual buoyancy
        float windFactor;    // individual wind susceptibility
        float drag;          // air resistance
        float initialSize;   // starting size in virtual 1080p px
        float aspect;        // flake elongation
        float rotAngle;      // tumbling angle (rad)
        float rotSpeed;      // tumbling angular velocity (rad/sec)
        float fuel;          // 1.0 (fresh) -> 0.0 (burned out)
        float burnRate;      // fuel consumed per second
        float age;           // seconds alive
        float phase;         // oscillation phase
        float freq1, freq2;  // flicker frequencies
        float heat;          // combustion temperature [0.15..1.0]
        float heatTarget;    // stochastic target temperature
        float flareTimer;    // countdown to next random brightness shift
        float seed01;        // unique shader shape seed [0..1]
        float bokehBlur;     // 0.0 = crisp mid-plane, 0.85 = soft near-plane bokeh
        int   layer;         // 0 = far, 1 = mid, 2 = near bokeh
    };

    void Configure(int count, float speedScale, Color baseColor) {
        m_TargetCount = (std::max)(0, (std::min)(count, 96));
        m_SpeedScale  = (std::max)(0.1f, (std::min)(speedScale, 5.0f));
        m_BaseColor   = baseColor;
        if (static_cast<int>(m_Sparks.size()) != m_TargetCount) {
            m_Sparks.resize(static_cast<size_t>(m_TargetCount));
            for (int i = 0; i < m_TargetCount; ++i) {
                Respawn(m_Sparks[i], true);
            }
        }
    }

    void Update(float dtSec) {
        if (m_TargetCount <= 0) return;
        m_Time += dtSec;

        // Global wind gust simulation: every 2.8..5.2s a warm gust sweeps across the scene,
        // accelerating embers along the arc and making them flare brightly from oxygen!
        m_GustTimer -= dtSec;
        if (m_GustTimer <= 0.0f) {
            if (m_GustTarget < 0.2f) {
                m_GustTarget = 0.65f + NextRand() * 0.55f;
                m_GustTimer  = 0.9f + NextRand() * 1.1f;
            } else {
                m_GustTarget = 0.0f;
                m_GustTimer  = 2.2f + NextRand() * 2.8f;
            }
        }
        m_Gust += (m_GustTarget - m_Gust) * (std::min)(1.0f, dtSec * 2.4f);

        for (Spark& s : m_Sparks) {
            StepSpark(s, dtSec);
            if (s.fuel <= 0.0f || s.x < -0.10f || s.x > 1.12f || s.y < -0.10f || s.y > 1.14f) {
                Respawn(s, false);
            }
        }
    }

    void Render(UiRenderer& r) const {
        if (m_TargetCount <= 0) return;
        const float vw = r.VirtualWidth();
        const float vh = r.VirtualHeight();

        // Dual Premultiplied-Ash + Additive-Fire PSO (ONE + INV_SRC_ALPHA):
        // black coal crust occludes background while fire cracks & bloom add pure light!
        r.SetPremultipliedBlend(true);

        for (const Spark& s : m_Sparks) {
            if (s.fuel <= 0.005f) continue;

            const float spawnFade = (std::min)(1.0f, s.age / 0.50f);
            const float fuelFade  = (s.fuel < 0.28f) ? (s.fuel / 0.28f) : 1.0f;

            // Tumbling face + dual-frequency flicker + synchronous oxygen boost during wind gusts!
            const float tumble = 0.65f + 0.35f * std::fabs(std::cos(s.rotAngle * 0.9f));
            const float osc    = 0.5f + 0.5f * std::sin(m_Time * s.freq1 + s.phase) * std::cos(m_Time * s.freq2 + s.phase * 1.6f);
            const float gustBoost = m_Gust * 0.34f;
            const float heat   = (std::max)(0.14f, (std::min)(1.0f, (s.heat * 0.65f + osc * 0.35f + gustBoost) * tumble * (0.42f + 0.58f * s.fuel)));

            const float layerAlpha = (s.layer == 0) ? 0.52f
                                   : ((s.layer == 2) ? 0.62f
                                   : ((s.layer == 3) ? 0.18f : 1.0f));
            const float alpha = (std::max)(0.0f, (std::min)(1.0f, spawnFade * fuelFade * layerAlpha * (0.45f + 0.55f * heat)));
            if (alpha <= 0.02f) continue;

            // Burn-down size scaling: layers 0..2 shrink as fuel burns; layer 3 stays massive foreground bokeh
            const float burnScale = (s.layer == 3) ? 1.0f : (0.26f + 0.74f * std::sqrt(s.fuel));
            const float curSize = s.initialSize * burnScale * (0.90f + 0.22f * heat);
            const float w = curSize * s.aspect;
            const float h = curSize;

            // Pack integer seed (1..29) + fractional ash crust growth (0.02..0.92 as fuel burns down)
            const float crust = (std::max)(0.02f, (std::min)(0.92f, 1.0f - s.fuel));
            const float seedAndCrust = std::floor(1.0f + s.seed01 * 28.0f) + crust;

            r.DrawEmberQuad(s.x * vw, s.y * vh, w, h, s.rotAngle, heat, s.bokehBlur, seedAndCrust, alpha);
        }

        r.SetPremultipliedBlend(false);
    }

private:
    float NextRand() {
        m_Rng = m_Rng * 1664525u + 1013904223u;
        return static_cast<float>((m_Rng >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }

    void StepSpark(Spark& s, float dtSec) {
        s.age += dtSec;
        s.rotAngle += s.rotSpeed * (1.0f + m_Gust * 0.7f) * dtSec;

        s.flareTimer -= dtSec;
        if (s.flareTimer <= 0.0f) {
            if (NextRand() < 0.30f) {
                s.heatTarget = 0.85f + NextRand() * 0.15f;
                s.flareTimer = 0.16f + NextRand() * 0.32f;
            } else {
                s.heatTarget = 0.18f + NextRand() * 0.45f;
                s.flareTimer = 0.24f + NextRand() * 0.60f;
            }
        }
        s.heat += (s.heatTarget - s.heat) * (std::min)(1.0f, dtSec * 5.2f);
        s.fuel -= s.burnRate * (0.72f + 0.55f * s.heat + 0.25f * m_Gust) * dtSec;

        const float depthSpeed = (s.layer == 0) ? 0.32f
                               : ((s.layer == 2) ? 2.00f
                               : ((s.layer == 3) ? 3.10f : 1.0f));
        const float altitude = (std::max)(0.0f, (std::min)(1.0f, 1.0f - s.y));
        const float swirlX = std::sin(m_Time * 1.15f + s.phase + s.y * 4.5f) * 0.022f;
        const float swirlY = std::cos(m_Time * 0.95f + s.phase * 1.3f + s.x * 3.8f) * 0.014f;

        const float gustPushX = -m_Gust * 0.055f * s.windFactor;
        const float gustPushY = -m_Gust * 0.018f;

        const float targetVx = (-(0.018f + altitude * 0.068f) * s.windFactor + gustPushX + swirlX) * depthSpeed;
        const float targetVy = (-(s.liftBase * (0.65f + 0.35f * s.fuel)) + gustPushY + swirlY) * depthSpeed;

        const float damp = (std::min)(1.0f, s.drag * dtSec);
        s.vx += (targetVx - s.vx) * damp;
        s.vy += (targetVy - s.vy) * damp;

        s.x += s.vx * m_SpeedScale * dtSec;
        s.y += s.vy * m_SpeedScale * dtSec;
    }

    void Respawn(Spark& s, bool prewarm) {
        // 4-plane depth contrast:
        //   38% far (tiny/slow), 38% mid (crisp), 16% near (large bokeh), 8% foreground (giant pure soft bokeh spots)
        const float rl = NextRand();
        if (rl < 0.38f)      { s.layer = 0; s.bokehBlur = 0.22f; }
        else if (rl < 0.76f) { s.layer = 1; s.bokehBlur = 0.00f; }
        else if (rl < 0.92f) { s.layer = 2; s.bokehBlur = 0.48f + NextRand() * 0.16f; }
        else                 { s.layer = 3; s.bokehBlur = 0.95f; }

        // Clearly biased toward lower-right corner, naturally dispersed
        if (NextRand() < 0.66f) {
            s.x = 0.54f + NextRand() * 0.50f;
            s.y = 0.94f + NextRand() * 0.14f;
        } else {
            s.x = 0.94f + NextRand() * 0.10f;
            s.y = 0.56f + NextRand() * 0.48f;
        }

        s.liftBase    = 0.028f + NextRand() * 0.036f;
        s.windFactor  = 0.58f  + NextRand() * 1.05f;
        s.drag        = 1.15f  + NextRand() * 1.25f;
        s.vx          = -(0.015f + NextRand() * 0.028f);
        s.vy          = -s.liftBase;

        // 4-plane size separation: far (8..15), mid (26..48), near (55..85), foreground massive bokeh (fixed 1050px)
        const float rSz = NextRand();
        if (s.layer == 0)      s.initialSize = 8.0f   + rSz * 7.0f;
        else if (s.layer == 1) s.initialSize = 26.0f  + (rSz * rSz) * 24.0f;
        else if (s.layer == 2) s.initialSize = 55.0f  + rSz * 30.0f;
        else                   s.initialSize = 1050.0f;

        s.aspect      = 0.90f + NextRand() * 0.45f;
        s.rotAngle    = NextRand() * 6.28318f;
        s.rotSpeed    = (NextRand() - 0.5f) * 2.4f;
        s.seed01      = NextRand();

        // Long graceful smoldering lifetime (12s..22s)
        s.fuel        = 1.0f;
        s.burnRate    = 0.042f + NextRand() * 0.045f;
        s.age         = 0.0f;
        s.phase       = NextRand() * 6.28318f;
        s.freq1       = 2.6f + NextRand() * 5.2f;
        s.freq2       = 1.3f + NextRand() * 3.4f;
        s.heat        = 0.55f + NextRand() * 0.45f;
        s.heatTarget  = s.heat;
        s.flareTimer  = 0.15f + NextRand() * 0.45f;

        if (prewarm) {
            const float simTime = NextRand() * (0.85f / s.burnRate);
            const int steps = 36;
            const float stepDt = simTime / static_cast<float>(steps);
            for (int i = 0; i < steps; ++i) {
                StepSpark(s, stepDt);
                if (s.fuel <= 0.05f) break;
            }
        }
    }

    std::vector<Spark> m_Sparks;
    int      m_TargetCount = 0;
    float    m_SpeedScale  = 1.0f;
    Color    m_BaseColor   = Color(255, 72, 24, 255);
    float    m_Time        = 0.0f;
    float    m_Gust        = 0.0f;
    float    m_GustTarget  = 0.0f;
    float    m_GustTimer   = 2.0f;
    uint32_t m_Rng         = 0xAC02026u;
};

// Native acz.exe MainSelect SWG callback RVAs (v1.0.2.1 base)
namespace NativeMainRva {
    constexpr uintptr_t kStartNewGame     = 0x001EFB80;
    constexpr uintptr_t kStartContinue    = 0x001EFC30;
    constexpr uintptr_t kStartFreeMission = 0x001EFC80;
    constexpr uintptr_t kStartVS          = 0x001EFCE0;
    constexpr uintptr_t kStartAceStyle    = 0x001EFD30;
    constexpr uintptr_t kStartSave        = 0x001EFD80;
    constexpr uintptr_t kStartLoad        = 0x001EFDD0;
    constexpr uintptr_t kStartTitle       = 0x001EFE20;
    constexpr uintptr_t kCallControl      = 0x001EFE70;
    constexpr uintptr_t kCallLanguage     = 0x001EFEC0;
    constexpr uintptr_t kCallSound        = 0x001EFF10;
    constexpr uintptr_t kCallScrPosition  = 0x001EFF60;
    constexpr uintptr_t kCallBrightness   = 0x001EFFB0;
    constexpr uintptr_t kIsContinue       = 0x001F0070;
    constexpr uintptr_t kIsFreeMission    = 0x001F00E0;
    constexpr uintptr_t kIsSP             = 0x001F0150;
    constexpr uintptr_t kIsLevel          = 0x001F01C0;
} // namespace NativeMainRva

inline void SafeSwgStartNewGameSEH(uintptr_t fnAddr, int difficulty, int controlType, int voiceLang, int subLang, int isSp) {
    if (!fnAddr) return;
    __try {
        // Verified from CModeMainSelect::StartNewGame (RVA 0x171360 in v1.0.2.1):
        // args[0]=difficulty(0..5), args[1]=controlType(0=Normal,1=Novice),
        // args[2]=voiceLang(0=JP,1=EN), args[3]=subLang(0..7), args[4]=isSp(0=New,1=SP)
        int64_t args[5] = { difficulty, controlType, voiceLang, subLang, isSp };
        using FnNewGame = void (__fastcall*)(void*, void*, void*, int64_t*);
        reinterpret_cast<FnNewGame>(fnAddr)(nullptr, nullptr, nullptr, args);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// External UTF-8 localization table loaded from mods\enhancer\ui\strings_en.ini (live Hot-Reloadable!)
class UiStringTable {
public:
    bool Load(const std::filesystem::path& path) {
        m_Map.clear();
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        std::string line;
        bool firstLine = true;
        while (std::getline(f, line)) {
            if (firstLine && line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
            firstLine = false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#' || trimmed[0] == '[') continue;
            const size_t eq = trimmed.find('=');
            if (eq == std::string::npos) continue;
            std::string key = Trim(trimmed.substr(0, eq));
            std::string val = Trim(trimmed.substr(eq + 1));
            if (key.empty()) continue;
            // Unescape literal \n -> newline
            std::string unesc;
            unesc.reserve(val.size());
            for (size_t i = 0; i < val.size(); ++i) {
                if (val[i] == '\\' && i + 1 < val.size() && val[i + 1] == 'n') {
                    unesc.push_back('\n');
                    ++i;
                } else {
                    unesc.push_back(val[i]);
                }
            }
            m_Map[key] = std::move(unesc);
        }
        return true;
    }

    std::string Get(const char* key, const char* fallback) const {
        if (!key) return fallback ? std::string(fallback) : std::string();
        auto it = m_Map.find(key);
        if (it != m_Map.end()) return it->second;
        return fallback ? std::string(fallback) : std::string();
    }

private:
    static std::string Trim(const std::string& s) {
        size_t a = 0;
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
        size_t b = s.size();
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
        return s.substr(a, b - a);
    }

    std::unordered_map<std::string, std::string> m_Map;
};

// Invokes the native engine SWG sound handler vtable[7] (OnVoice2D @ 0x1ED6C0 / +0x680 in v2.0.0.1).
// Uses the game's own NUD sound mixer (respects SE volume and Stereo/Mono/Pro Logic II Surround).
inline void SafeSwgOnVoice2DSEH(void* soundObj, const char* nudName) {
    if (!soundObj || !nudName || !nudName[0]) return;
    __try {
        void** vtbl = *reinterpret_cast<void***>(soundObj);
        if (!vtbl || !vtbl[7]) return;
        using FnOnVoice2D = void (__fastcall*)(void* this_ptr, const char* name, int a3, int a4);
        reinterpret_cast<FnOnVoice2D>(vtbl[7])(soundObj, nudName, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ============================================================================
// 5. RmlOptionsController — Unified HTML/CSS Controller for Main Menu & Options
//    - In MainSelect: renders mods\enhancer\ui\main_menu.rml (A/B toggle via F1)
//    - Elsewhere:     renders mods\enhancer\ui\options.rml   (overlay via F1)
//    - Automatic live Hot-Reload (file mtime watch every 0.5s + F5 key)
// ============================================================================
class RmlOptionsController {
public:
    using FnLog = void(*)(const char*, ...);
    using FnRva = std::function<uintptr_t(uintptr_t)>;

    void SetSwgSoundObject(void* soundObj) {
        m_SwgSoundObj = soundObj;
    }

    void PlayMenuSE(const char* nudName) const {
        if (m_SwgSoundObj && nudName && nudName[0]) {
            SafeSwgOnVoice2DSEH(m_SwgSoundObj, nudName);
        }
    }

    bool Initialize(UiRenderer* renderer, const std::filesystem::path& gameDir, uintptr_t exeBase, FnRva rvaFn, FnLog logFn) {
        if (m_Initialized) return true;
        m_Renderer = renderer;
        m_ExeBase  = exeBase;
        m_RvaFn    = std::move(rvaFn);
        m_LogFn    = logFn;
        m_UiRoot   = gameDir / "mods" / "enhancer" / "ui";

        m_SysIface    = std::make_unique<AczRmlSystemInterface>(logFn);
        m_FileIface   = std::make_unique<AczRmlFileInterface>(m_UiRoot);
        m_RenderIface = std::make_unique<AczRmlRenderInterface>(renderer, m_UiRoot);
        m_FontEngine  = std::make_unique<AczRmlFontEngine>(renderer, m_UiRoot);

        Rml::SetSystemInterface(m_SysIface.get());
        Rml::SetFileInterface(m_FileIface.get());
        Rml::SetRenderInterface(m_RenderIface.get());
        Rml::SetFontEngineInterface(m_FontEngine.get());

        if (!Rml::Initialise()) {
            if (m_LogFn) m_LogFn("[RmlUi] ERROR: Rml::Initialise() failed.");
            return false;
        }

        // Register fonts from mods\enhancer\fonts\ explicitly by family name
        const std::filesystem::path fontsDir = gameDir / "mods" / "enhancer" / "fonts";
        m_FontEngine->LoadFaceWithFamily(fontsDir / "EuroStyle Normal.ttf", "ACZTitle", false);
        m_FontEngine->LoadFaceWithFamily(fontsDir / "ACZ_HUD_Font.ttf",     "ACZBody",  false);

        m_Context = Rml::CreateContext("acz_main", Rml::Vector2i(1920, 1080));
        if (!m_Context) {
            if (m_LogFn) m_LogFn("[RmlUi] ERROR: CreateContext failed.");
            return false;
        }

        m_Initialized = true;
        // Defer loading .rml document until first Render() when D3D12 UiRenderer is ready for GPU textures!
        return true;
    }

    bool IsReady() const { return m_Initialized && m_Context != nullptr; }
    bool IsMainMenuMode() const { return m_MainMenuMode; }
    bool IsLeavingMainMenu() const { return m_LeavingMainMenu; }
    Rml::Context* GetContext() const { return m_Context; }
    AczRmlRenderInterface* GetRenderInterface() const { return m_RenderIface.get(); }
    const UiStringTable& GetStrings() const { return m_Strings; }
    void ReloadStrings() {
        m_Strings.Load(m_UiRoot / "strings_en.ini");
    }
    void SetMainDocVisible(bool vis) {
        if (!m_Doc) return;
        if (vis) {
            if (!m_Doc->IsVisible()) m_Doc->Show();
        } else {
            if (m_Doc->IsVisible()) m_Doc->Hide();
        }
    }
    void UpdateSharedMenuSparks(float dtSec) {
        m_Sparks.Update(dtSec);
    }
    void RenderSharedMenuBackdropBase() {
        if (!m_Renderer) return;
        const float vw = m_Renderer->VirtualWidth();
        const float vh = m_Renderer->VirtualHeight();
        if (m_BgCrimsonSlot != 0) {
            const float t = static_cast<float>(m_SysIface ? m_SysIface->GetElapsedTime() : 0.0);
            const float osc = 0.94f + 0.055f * std::sin(t * 1.15f) + 0.030f * std::sin(t * 2.70f + 1.1f);
            const uint8_t cr = static_cast<uint8_t>((std::min)(255.0f, 255.0f * osc));
            m_Renderer->SetPremultipliedBlend(false);
            m_Renderer->DrawImage(0.0f, 0.0f, vw, vh, m_BgCrimsonSlot, Color(cr, cr, cr, 255));
        }
        if (m_BgCommandSlot != 0) {
            float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
            const float scrAspect = (vh > 0.0f) ? (vw / vh) : (16.0f / 9.0f);
            const float texAspect = (m_BgCommandAspect > 0.1f) ? m_BgCommandAspect : (16.0f / 9.0f);
            if (scrAspect > texAspect * 1.005f) {
                const float cropV = 1.0f - (texAspect / scrAspect);
                v0 = cropV * 0.5f;
                v1 = 1.0f - v0;
            } else if (scrAspect < texAspect * 0.995f) {
                const float cropU = 1.0f - (scrAspect / texAspect);
                u0 = cropU * 0.5f;
                u1 = 1.0f - u0;
            }
            m_Renderer->SetAdditiveBlend(true);
            m_Renderer->DrawImage(0.0f, 0.0f, vw, vh, m_BgCommandSlot, Color(16, 21, 22, 255), u0, v0, u1, v1);
            m_Renderer->SetAdditiveBlend(false);
        }
        m_Renderer->SetPremultipliedBlend(false);
        m_Renderer->DrawTacticalGrid(0.10f, 0.09f, 0.20f, 1.0f);
    }
    void RenderSharedMenuBackdropOverlay() {
        if (!m_Renderer) return;
        if (m_BgScanlineSlot != 0) {
            const float vw = m_Renderer->VirtualWidth();
            const float vh = m_Renderer->VirtualHeight();
            const double t = m_SysIface ? m_SysIface->GetElapsedTime() : 0.0;
            const float cycle = static_cast<float>(std::fmod(t, 3.0));
            if (cycle < 2.0f) {
                const float u = cycle / 2.0f;
                const float yPs2 = 448.0f + (-180.0f - 448.0f) * u;
                const float yVirt = (yPs2 / 448.0f) * vh;
                const float hVirt = (128.0f / 448.0f) * vh;
                m_Renderer->SetAdditiveBlend(true);
                m_Renderer->DrawImage(0.0f, yVirt, vw, hVirt, m_BgScanlineSlot, Color(38, 37, 19, 255));
                m_Renderer->SetAdditiveBlend(false);
            }
        }
        m_Sparks.Render(*m_Renderer);
    }

    void ApplyAspectLayoutToDoc(Rml::ElementDocument* doc, int w, int h, float dpRatio = 1.0f) const {
        if (!doc || w <= 0 || h <= 0 || dpRatio <= 0.01f) return;
        const float virtW  = static_cast<float>(w) / dpRatio;
        const float virtH  = static_cast<float>(h) / dpRatio;
        const float aspect = virtW / virtH;
        auto* topEl    = doc->GetElementById("top-prompts");
        auto* frameEl  = doc->GetElementById("menu-frame");
        auto* footerEl = doc->GetElementById("footer-desc");

        bool applyCustomSide = false;
        float sideDp = virtW * 0.048f;

        if (m_UiWidescreenOverride == 1 && aspect > 1.36f) {
            const float targetOuterW = virtH * (4.0f / 3.0f);
            sideDp = (std::max)(virtW * 0.048f, (virtW - targetOuterW) * 0.5f + targetOuterW * 0.048f);
            applyCustomSide = true;
        } else if (m_UiWidescreenOverride == 2 && aspect > 1.80f) {
            const float targetOuterW = virtH * (16.0f / 9.0f);
            sideDp = (std::max)(virtW * 0.048f, (virtW - targetOuterW) * 0.5f + targetOuterW * 0.048f);
            applyCustomSide = true;
        } else if (m_UiWidescreenOverride == 0 && aspect > 1.85f) {
            constexpr float kMaxFrameW = 2160.0f;
            sideDp = (std::max)(virtW * 0.048f, (virtW - kMaxFrameW) * 0.5f);
            applyCustomSide = true;
        }

        if (applyCustomSide) {
            char pxBuf[32];
            std::snprintf(pxBuf, sizeof(pxBuf), "%.1fpx", sideDp);
            if (topEl) {
                topEl->SetProperty("left", pxBuf);
                topEl->SetProperty("right", pxBuf);
            }
            if (frameEl) {
                frameEl->SetProperty("left", pxBuf);
                frameEl->SetProperty("right", pxBuf);
            }
            if (footerEl) {
                char footBuf[32];
                std::snprintf(footBuf, sizeof(footBuf), "%.1fpx", sideDp + 8.0f);
                footerEl->SetProperty("padding-left", footBuf);
                footerEl->SetProperty("padding-right", footBuf);
            }
        } else {
            if (topEl)    { topEl->RemoveProperty("left"); topEl->RemoveProperty("right"); }
            if (frameEl)  { frameEl->RemoveProperty("left"); frameEl->RemoveProperty("right"); }
            if (footerEl) { footerEl->RemoveProperty("padding-left"); footerEl->RemoveProperty("padding-right"); }
        }
    }

    void SetUiWidescreenOverride(int mode) {
        mode = (mode < 0) ? 0 : ((mode > 2) ? 2 : mode);
        if (m_UiWidescreenOverride == mode) return;
        m_UiWidescreenOverride = mode;
        if (m_Doc && m_LastW > 0 && m_LastH > 0) {
            const float uiScale = (std::min)(static_cast<float>(m_LastW) / 1920.0f, static_cast<float>(m_LastH) / 1080.0f);
            const int virtW = (std::max)(640, static_cast<int>(std::lround(static_cast<float>(m_LastW) / uiScale)));
            const int virtH = (std::max)(480, static_cast<int>(std::lround(static_cast<float>(m_LastH) / uiScale)));
            ApplyAspectLayout(virtW, virtH, 1.0f);
        }
    }

    void ExitMainMenuMode() {
        m_MainMenuMode        = false;
        m_LeavingMainMenu     = false;
        m_DispDialogMode      = DispDialogMode::None;
        m_DispConfirmDomDirty = true;
        m_StagedDispSynced    = false;
        m_LeafScrollTop       = 0;
    }

    void EnterMainMenuMode(OptionsScreen& screen, bool playOpenSound = true, bool resetTree = true) {
        m_MainMenuMode        = true;
        m_LeavingMainMenu     = false;
        m_DispDialogMode      = DispDialogMode::None;
        m_DispConfirmDomDirty = true;
        m_StagedDispSynced    = false;
        m_LeafScrollTop       = 0;
        m_IdleTimerSec        = 0.0f;
        if (resetTree) {
            m_MainDepth    = 0;
            m_MainRootIdx  = 0;
            m_CampIdx      = 0;
            m_DiffIdx      = 2; // NORMAL
            m_ExitConfirmIdx = 0; // NO
            screen.SetActiveTab(0);
        }
        if (playOpenSound) {
            PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
        }
        if (m_ActiveDocName != "main_menu.rml") {
            m_ActiveDocName = "main_menu.rml";
            m_NeedDocReload = true;
        }
        m_BuiltViewKey = -1;
        m_TransPhase = 0;
        m_TransTimer = 0.0f;
        m_FooterPending = false;
        m_DomDirty = true;
    }

    void OnOpened(OptionsScreen& screen, int initialTab, bool fromSubmenu) {
        m_MainMenuMode = false;
        m_LeavingMainMenu = false;
        m_Depth = fromSubmenu ? 1 : 0; // Jump straight into category if opened via ControlSet/SoundSet/Brightness
        if (m_ActiveDocName != "options.rml") {
            m_ActiveDocName = "options.rml";
            m_NeedDocReload = true;
        }
        screen.SetActiveTab(initialTab);
        auto& tabs = screen.GetTabs();
        if (initialTab >= 0 && initialTab < static_cast<int>(tabs.size())) {
            tabs[initialTab].EnsureValidSelection();
        }
        m_BuiltViewKey = -1;
        m_TransPhase = 0;
        m_TransTimer = 0.0f;
        m_FooterPending = false;
        m_DomDirty = true;
    }

    // Update Main Menu 3-column HTML/CSS screen (when in MainSelect with Custom UI active)
    void UpdateMainMenu(OptionsScreen& screen, const UiInputSnapshot& in, float dtSec, bool f5Pressed, bool unlock2P) {
        m_Unlock2P = unlock2P;
        screen.TickToast(dtSec);
        m_Sparks.Update(dtSec);

        // Authentic ACZ single 10-second YES/NO display confirmation dialog (pauses underlying menu & attract timer)
        if (m_DispDialogMode != DispDialogMode::None) {
            m_IdleTimerSec = 0.0f;
            const auto& br = screen.GetBridge();
            if (in.up || in.down) {
                m_DispConfirmIdx = 1 - (m_DispConfirmIdx & 1);
                PlayMenuSE("NUD_COMMON_SYS_CURSOR");
                m_DispConfirmDomDirty = true;
            }
            m_DispConfirmTimer -= dtSec;
            if (in.confirm && m_DispConfirmIdx == 0) {
                // YES -> confirm, save settings/setting.ini, and exit GRAPHICS back to Depth 2
                if (br.confirmDisplayChanges) br.confirmDisplayChanges();
                m_DispDialogMode      = DispDialogMode::None;
                m_DispConfirmDomDirty = true;
                PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                screen.TriggerToast("DISPLAY SETTINGS SAVED");
                SyncStagedDisplayFromHost(br, true);
                m_MainDepth    = 2;
                m_BuiltViewKey = -1;
                m_DomDirty     = true;
            } else if (in.cancel || (in.confirm && m_DispConfirmIdx == 1) || m_DispConfirmTimer <= 0.0f) {
                // NO / CANCEL / TIMEOUT -> revert live and stay in GRAPHICS so player can pick another resolution
                if (br.revertDisplayChanges) br.revertDisplayChanges();
                m_DispDialogMode      = DispDialogMode::None;
                m_DispConfirmDomDirty = true;
                m_StagedDispSynced    = false; // Re-sync cleanly once OnAfterPresent finishes reverting
                PlayMenuSE("NUD_COMMON_SYS_CANCEL");
                screen.TriggerToast("DISPLAY SETTINGS REVERTED");
                m_BuiltViewKey = -1;
                m_DomDirty     = true;
            }
            return;
        }

        // Keep staged display UI synced with external Alt+Enter / revert changes when inside Enhancer Settings
        if (m_MainDepth >= 2 && m_MainRootIdx == 2 && m_OptCatIdx == 5) {
            SyncStagedDisplayFromHost(screen.GetBridge(), false);
        }

        // Authentic ACZ idle attract timer (60 seconds of real inactivity -> return to Title/Logos via kStartTitle).
        // Any interaction with our custom menu resets the timer cleanly!
        const bool anyInput = in.up || in.down || in.left || in.right || in.confirm || in.cancel || in.prevTab || in.nextTab || f5Pressed;
        if (anyInput) {
            m_IdleTimerSec = 0.0f;
        } else if (!m_LeavingMainMenu) {
            m_IdleTimerSec += dtSec;
            if (m_IdleTimerSec >= 60.0f) {
                m_LeavingMainMenu = true;
                SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartTitle));
                return;
            }
        }

        // Authentic ACZ directional submenu transitions (video-verified in animation_analysis.md):
        //   Downward (going deeper):
        //     Phase 1 (0.09s): Blinking yellow squares appear at right edge of Col 1 & Col 2 (Col 3 hidden, like 00:04)
        //     Phase 2 (0.16s): Squares glide right -> left (ease-out) to left edge of Col 1 & Col 2
        //     Phase 3: Turn into teletype text (Col 1: [C][O][N], Col 2 & Col 3: text + 3 empty blocks [][][]);
        //              Col 2 sq-icons fade in after Col 2 finishes typing; cursor bar & footer reveal near end of Col 1
        //   Upward (going back):
        //     Phase 4 (0.05s): Clean blank frame (no fly squares, folder visible, prompts briefly hidden, like 00:02)
        //     Phase 3: Same teletype reveal across all 3 columns (like 00:03)
        if (m_Doc && m_TransPhase == 1) {
            m_TransTimer -= dtSec;
            if (m_TransTimer <= 0.0f) {
                m_TransPhase = 2;
                m_FlyElapsed = 0.0f;
            }
        } else if (m_Doc && m_TransPhase == 2) {
            constexpr float kFlyDuration = 0.20f;
            m_FlyElapsed += dtSec;
            const float u = (std::min)(1.0f, m_FlyElapsed / kFlyDuration);
            const float ease = u * u * (3.0f - 2.0f * u); // Smoothstep: clearly visible glide across full column width
            if (!m_UseTwoCol) {
                if (auto* el = m_Doc->GetElementById("col-left-items")) {
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "left: %.2f%%;", 84.0f * (1.0f - ease));
                    el->SetAttribute("style", buf);
                }
            }
            if (auto* el = m_Doc->GetElementById("col-mid-items")) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "left: %.2f%%;", 82.0f * (1.0f - ease));
                el->SetAttribute("style", buf);
            }
            if (u >= 1.0f) {
                StartTypingPhase();
            }
        } else if (m_Doc && m_TransPhase == 4) {
            m_TransTimer -= dtSec;
            if (m_TransTimer <= 0.0f) {
                StartTypingPhase();
            }
        } else if (m_Doc && m_TransPhase == 3) {
            bool anyRunning = false;
            if (!m_Col1Texts.empty() && m_Col1Reveal < m_Col1Max + 3.0f) {
                m_Col1Reveal = (std::min)(m_Col1Max + 3.0f, m_Col1Reveal + dtSec * 58.0f);
                const size_t n = static_cast<size_t>((std::max)(0.0f, m_Col1Reveal));
                if (n != m_Col1Shown) {
                    m_Col1Shown = n;
                    for (size_t i = 0; i < m_Col1Texts.size(); ++i) {
                        if (auto* el = m_Doc->GetElementById("t-row-" + std::to_string(i))) {
                            el->SetInnerRML(FormatTeletypeBlocks(m_Col1Texts[i], n));
                        }
                    }
                }
                if (m_Col1Reveal < m_Col1Max + 2.5f) anyRunning = true;
            }
            if (!m_Col2Texts.empty() && m_Col2Reveal < m_Col2Max + m_Col2MaxLag + 3.5f) {
                m_Col2Reveal = (std::min)(m_Col2Max + m_Col2MaxLag + 3.5f, m_Col2Reveal + dtSec * 56.0f);
                const int nInt = static_cast<int>(m_Col2Reveal);
                if (nInt != static_cast<int>(m_Col2Shown)) {
                    m_Col2Shown = static_cast<size_t>((std::max)(0, nInt));
                    for (size_t i = 0; i < m_Col2Texts.size(); ++i) {
                        const int lag = (i < m_Col2Lags.size()) ? m_Col2Lags[i] : 0;
                        const size_t rowStep = static_cast<size_t>((std::max)(0, nInt - lag));
                        if (auto* el = m_Doc->GetElementById("tm-row-" + std::to_string(i))) {
                            el->SetInnerRML(FormatTeletypePreview(m_Col2Texts[i], rowStep));
                        }
                    }
                }
                if (m_Col2Reveal < m_Col2Max + m_Col2MaxLag + 3.0f) {
                    anyRunning = true;
                } else if (!m_Col2Done) {
                    m_Col2Done = true;
                    UpdateColumnsClass(); // Smoothly fade in blinking sq-icons once Col 2 finishes typing!
                }
            } else if (!m_Col2Done) {
                m_Col2Done = true;
                UpdateColumnsClass();
            }
            if (!m_Col3Texts.empty() && m_Col3Reveal < m_Col3Max + m_Col3MaxLag + 3.5f) {
                m_Col3Reveal = (std::min)(m_Col3Max + m_Col3MaxLag + 3.5f, m_Col3Reveal + dtSec * 56.0f);
                const int nInt = static_cast<int>(m_Col3Reveal);
                if (nInt != static_cast<int>(m_Col3Shown)) {
                    m_Col3Shown = static_cast<size_t>((std::max)(0, nInt));
                    for (size_t i = 0; i < m_Col3Texts.size(); ++i) {
                        const int lag = (i < m_Col3Lags.size()) ? m_Col3Lags[i] : 0;
                        const size_t rowStep = static_cast<size_t>((std::max)(0, nInt - lag));
                        if (auto* el = m_Doc->GetElementById("tr-row-" + std::to_string(i))) {
                            el->SetInnerRML(FormatTeletypePreview(m_Col3Texts[i], rowStep));
                        }
                    }
                }
                if (m_Col3Reveal < m_Col3Max + m_Col3MaxLag + 3.0f) anyRunning = true;
            }

            // Reveal yellow highlight cursor bar + start footer teletype near end of Col 1 wave
            float activeRowLen = m_Col1Max;
            if (m_ActiveRowIdx >= 0 && m_ActiveRowIdx < static_cast<int>(m_Col1Texts.size())) {
                activeRowLen = static_cast<float>(m_Col1Texts[m_ActiveRowIdx].size());
            }
            const float revealThreshold = (std::max)(activeRowLen * 0.85f, m_Col1Max * 0.78f);
            if (m_CursorHiddenForTrans && (m_Col1Texts.empty() || m_Col1Reveal >= revealThreshold)) {
                RevealCursorAfterTransition();
            }
            if (!anyRunning) {
                if (m_CursorHiddenForTrans) RevealCursorAfterTransition();
                m_TransPhase = 0;
                m_Col2Done   = true;
                UpdateColumnsClass();
            }
        }

        // Authentic ACZ terminal block-cursor teletype reveal for #footer-desc (60 chars/sec = 1 char/frame @ 60fps)
        if (m_Doc && !m_FooterPending && m_FooterRevealChars < static_cast<float>(m_FooterFullText.size()) + 3.0f) {
            m_FooterRevealChars = (std::min)(static_cast<float>(m_FooterFullText.size()) + 3.0f,
                                             m_FooterRevealChars + dtSec * 60.0f);
            const size_t n = static_cast<size_t>(m_FooterRevealChars);
            if (n != m_FooterShownCount) {
                m_FooterShownCount = n;
                if (auto* el = m_Doc->GetElementById("footer-desc")) {
                    el->SetInnerRML(FormatTeletypeBlocks(m_FooterFullText, n));
                }
            }
        }
        // Same terminal block-cursor reveal for #breadcrumb-text on submenu change (60 chars/sec)
        if (m_Doc && !m_BcPending && m_BcRevealChars < static_cast<float>(m_BcFullText.size()) + 3.0f) {
            m_BcRevealChars = (std::min)(static_cast<float>(m_BcFullText.size()) + 3.0f,
                                         m_BcRevealChars + dtSec * 60.0f);
            const size_t n = static_cast<size_t>(m_BcRevealChars);
            if (n != m_BcShownCount) {
                m_BcShownCount = n;
                if (auto* el = m_Doc->GetElementById("breadcrumb-text")) {
                    el->SetInnerRML(FormatTeletypeBlocks(m_BcFullText, n));
                }
            }
        }

        // Smooth per-frame C++ interpolation for scrollbar thumb & Col 3 vertical scroll (cursor bar snaps instantly!)
        if (m_Doc) {
            const float scrAlpha = (std::min)(1.0f, dtSec * 12.0f);
            const bool scrollVisible = m_ScrollHasRange && (m_TransPhase == 0 || m_TransPhase == 3);
            if (std::fabs(m_ScrollPct - m_ScrollTargetPct) > 0.05f) {
                m_ScrollPct += (m_ScrollTargetPct - m_ScrollPct) * scrAlpha;
                if (std::fabs(m_ScrollPct - m_ScrollTargetPct) <= 0.05f) m_ScrollPct = m_ScrollTargetPct;
                if (auto* th = m_Doc->GetElementById("scroll-thumb")) {
                    char stBuf[96];
                    std::snprintf(stBuf, sizeof(stBuf), "top: %.2f%%; opacity: %.1f;",
                                  m_ScrollPct, scrollVisible ? 1.0f : 0.0f);
                    th->SetAttribute("style", stBuf);
                }
            }
            if (std::fabs(m_Col2Y - m_Col2TargetY) > 0.5f) {
                m_Col2Y += (m_Col2TargetY - m_Col2Y) * scrAlpha;
                if (std::fabs(m_Col2Y - m_Col2TargetY) <= 0.5f) m_Col2Y = m_Col2TargetY;
                if (auto* c2 = m_Doc->GetElementById("col-mid-scroll")) {
                    char c2Buf[64];
                    std::snprintf(c2Buf, sizeof(c2Buf), "position: relative; top: %.1fpx;", m_Col2Y);
                    c2->SetAttribute("style", c2Buf);
                }
            }
            if (std::fabs(m_Col3Y - m_Col3TargetY) > 0.5f) {
                m_Col3Y += (m_Col3TargetY - m_Col3Y) * scrAlpha;
                if (std::fabs(m_Col3Y - m_Col3TargetY) <= 0.5f) m_Col3Y = m_Col3TargetY;
                if (auto* c3 = m_Doc->GetElementById("col-right-scroll")) {
                    char c3Buf[64];
                    std::snprintf(c3Buf, sizeof(c3Buf), "position: relative; top: %.1fpx;", m_Col3Y);
                    c3->SetAttribute("style", c3Buf);
                }
            }
        }

        // Hot-Reload check (F5 or automatic file modification check every 0.5s)
        // Always defer actual ReloadDocument() to Render() so D3D12 texture/DOM updates run on the Present thread.
        m_HotReloadTimer += dtSec;
        if (f5Pressed || m_HotReloadTimer >= 0.5f) {
            m_HotReloadTimer = 0.0f;
            if (f5Pressed || CheckFilesModified()) {
                m_ActiveDocName = "main_menu.rml";
                m_NeedDocReload = true;
                screen.TriggerToast("UI RELOADED");
                m_DomDirty = true;
            }
        }

        auto& tabs = screen.GetTabs();
        const int tabCount = static_cast<int>(tabs.size());

        if (m_MainDepth == 0) {
            // Level 0 (strict 1:1 vanilla): CAMPAIGN | VS MODE | OPTIONS
            constexpr int kRootCount = 3;
            if (m_MainRootIdx >= kRootCount) m_MainRootIdx = 0;
            if (in.cancel) {
                PlayMenuSE("NUD_COMMON_SYS_CANCEL");
                m_LeavingMainMenu = true;
                SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartTitle));
                return;
            }
            if (in.up)   { m_MainRootIdx = (m_MainRootIdx - 1 + kRootCount) % kRootCount; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
            if (in.down) { m_MainRootIdx = (m_MainRootIdx + 1) % kRootCount; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
            if (in.confirm) {
                if (m_MainRootIdx == 1) {
                    // VS MODE
                    if (m_Unlock2P) {
                        PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                        m_LeavingMainMenu = true;
                        SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartVS));
                    } else {
                        PlayMenuSE("NUD_COMMON_SYS_ERROR");
                        screen.TriggerToast("ENABLE 2P MODE IN OPTIONS > ENHANCER");
                        m_DomDirty = true;
                    }
                } else {
                    PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                    PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                    m_MainDepth = 1;
                    if (m_MainRootIdx == 0) {
                        m_HasContinue    = (SafeSwgGetIntSEH(ResolveMainRva(NativeMainRva::kIsContinue), 0) != 0);
                        m_HasSP          = (SafeSwgGetIntSEH(ResolveMainRva(NativeMainRva::kIsSP), 1) != 0);
                        m_HasFreeMission = (SafeSwgGetIntSEH(ResolveMainRva(NativeMainRva::kIsFreeMission), 1) != 0);
                        m_CampIdx = 0;
                    }
                    if (m_MainRootIdx == 3) m_ExitConfirmIdx = 0;
                    m_DomDirty = true;
                }
            }
            return;
        }

        auto CampActionAt = [this](int idx) -> int {
            // 0=CONTINUE, 1=NEW GAME, 2=SP NEW GAME, 3=FREE MISSION, 4=ACE STYLE, 5=GAME DATA
            return m_HasContinue ? idx : (idx + 1);
        };
        const int campCount = m_HasContinue ? 6 : 5;

        if (m_MainDepth == 1) {
            if (in.cancel) {
                PlayMenuSE("NUD_COMMON_SYS_CANCEL");
                if (m_MainRootIdx == 2) {
                    // Save any changed settings when leaving OPTIONS
                    screen.Close(false);
                }
                m_MainDepth = 0;
                m_DomDirty = true;
                return;
            }

            if (m_MainRootIdx == 0) {
                // CAMPAIGN submenu (strict 1:1 vanilla): [CONTINUE] | NEW GAME | SP NEW GAME | FREE MISSION | ACE STYLE | GAME DATA
                if (m_CampIdx >= campCount) m_CampIdx = 0;
                if (in.up)   { m_CampIdx = (m_CampIdx - 1 + campCount) % campCount; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.down) { m_CampIdx = (m_CampIdx + 1) % campCount; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.confirm) {
                    const int act = CampActionAt(m_CampIdx);
                    switch (act) {
                        case 0: // CONTINUE
                            PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                            m_LeavingMainMenu = true;
                            SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartContinue));
                            break;
                        case 1: // NEW GAME -> difficulty selection (VERY EASY..HARD)
                            PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                            PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                            m_DiffIdx = 2; // NORMAL default
                            m_MainDepth = 2;
                            m_DomDirty = true;
                            break;
                        case 2: // SP NEW GAME -> difficulty selection (VERY EASY..ACE)
                            if (m_HasSP) {
                                PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                                PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                                m_DiffIdx = 2; // NORMAL default
                                m_MainDepth = 2;
                                m_DomDirty = true;
                            } else {
                                PlayMenuSE("NUD_COMMON_SYS_ERROR");
                            }
                            break;
                        case 3: // FREE MISSION -> difficulty + FREE FLIGHT selection
                            if (m_HasFreeMission) {
                                PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                                PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                                m_DiffIdx = 2; // NORMAL default
                                m_MainDepth = 2;
                                m_DomDirty = true;
                            } else {
                                PlayMenuSE("NUD_COMMON_SYS_ERROR");
                                screen.TriggerToast("CLEAR MISSION 1 TO UNLOCK");
                                m_DomDirty = true;
                            }
                            break;
                        case 4: // ACE STYLE
                            PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                            m_LeavingMainMenu = true;
                            SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartAceStyle));
                            break;
                        case 5: // GAME DATA -> SAVE / LOAD
                            PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                            PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                            m_DataIdx = 0;
                            m_MainDepth = 2;
                            m_DomDirty = true;
                            break;
                    }
                }
            } else if (m_MainRootIdx == 2) {
                // OPTIONS categories (6 items):
                // 0: CONTROLLER, 1: LANGUAGE, 2: DISPLAY, 3: SOUND, 4: SCREEN, 5: ENHANCER
                constexpr int kOptCount = 6;
                if (m_OptCatIdx >= kOptCount) m_OptCatIdx = 0;
                if (in.up)   { m_OptCatIdx = (m_OptCatIdx - 1 + kOptCount) % kOptCount; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.down) { m_OptCatIdx = (m_OptCatIdx + 1) % kOptCount; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.confirm) {
                    if (m_OptCatIdx == 0) {
                        PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                        SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kCallControl));
                    } else if (m_OptCatIdx == 1) {
                        PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                        SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kCallLanguage));
                    } else if (m_OptCatIdx == 3) {
                        PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                        SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kCallSound));
                    } else {
                        // DISPLAY SETTINGS (2), SCREEN SETTINGS (4), or ENHANCER SETTINGS (5)
                        PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                        PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                        m_OptSubIdx = 0;
                        m_MainDepth = 2;
                        m_DomDirty  = true;
                    }
                }
            }
            return;
        }

        if (m_MainDepth == 2) {
            if (in.cancel) {
                PlayMenuSE("NUD_COMMON_SYS_CANCEL");
                m_MainDepth = 1;
                m_DomDirty = true;
                return;
            }
            if (m_MainRootIdx == 0) {
                const int act = CampActionAt(m_CampIdx);
                if (act == 1 || act == 2 || act == 3) {
                    // NEW GAME: 4 (VERY EASY..HARD), SP NEW GAME: 6 (..ACE), FREE MISSION: 7 (..ACE + FREE FLIGHT)
                    const int diffCnt = (act == 1) ? 4 : ((act == 2) ? 6 : 7);
                    if (m_DiffIdx >= diffCnt) m_DiffIdx = 0;
                    if (in.up)   { m_DiffIdx = (m_DiffIdx - 1 + diffCnt) % diffCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                    if (in.down) { m_DiffIdx = (m_DiffIdx + 1) % diffCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                    if (in.confirm) {
                        if (act == 3) {
                            // FREE MISSION launches immediately with chosen difficulty / FREE FLIGHT (6)
                            PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                            m_LeavingMainMenu = true;
                            SafeSwgSetIntSEH(ResolveMainRva(NativeMainRva::kStartFreeMission), m_DiffIdx);
                        } else {
                            // NEW GAME / SP NEW GAME -> Depth 3 (Control Scheme: NORMAL / NOVICE)
                            PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                            PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                            m_CtrlIdx = 0;
                            m_MainDepth = 3;
                            m_DomDirty = true;
                        }
                    }
                } else if (act == 5) {
                    // GAME DATA: SAVE (0) / LOAD (1)
                    constexpr int kDataCnt = 2;
                    if (in.up)   { m_DataIdx = (m_DataIdx - 1 + kDataCnt) % kDataCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                    if (in.down) { m_DataIdx = (m_DataIdx + 1) % kDataCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                    if (in.confirm) {
                        PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                        m_LeavingMainMenu = true;
                        SafeSwgCallVoidSEH(ResolveMainRva(m_DataIdx == 0 ? NativeMainRva::kStartSave : NativeMainRva::kStartLoad));
                    }
                }
            } else if (m_MainRootIdx == 2) {
                int enhCatCnt = 0;
                GetEnhancerCats(enhCatCnt);
                const int subCnt = (m_OptCatIdx == 2) ? 4 : ((m_OptCatIdx == 5) ? enhCatCnt : 2);
                if (m_OptSubIdx >= subCnt) m_OptSubIdx = 0;
                if (in.up)   { m_OptSubIdx = (m_OptSubIdx - 1 + subCnt) % subCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.down) { m_OptSubIdx = (m_OptSubIdx + 1) % subCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.confirm) {
                    if (m_OptCatIdx == 2) {
                        // Enter DISPLAY SETTINGS leaf 2-column selector (ON/OFF or KNOTS/KPH)
                        PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                        PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                        m_DispLeafIdx = GetDisplaySettingRow(m_OptSubIdx);
                        m_MainDepth   = 3;
                        m_DomDirty    = true;
                    } else if (m_OptCatIdx == 4) {
                        // SCREEN SETTINGS: 0 = SCREEN POSITION, 1 = BRIGHTNESS (native sub-screens)
                        PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                        SafeSwgCallVoidSEH(ResolveMainRva(m_OptSubIdx == 0 ? NativeMainRva::kCallScrPosition : NativeMainRva::kCallBrightness));
                    } else if (m_OptCatIdx == 5) {
                        // Enter ENHANCER SETTINGS category item list (Depth 3)
                        PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                        PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                        if (m_OptSubIdx == 0) {
                            SyncStagedDisplayFromHost(screen.GetBridge(), true);
                        }
                        m_EnhItemIdx = 0;
                        m_MainDepth  = 3;
                        m_DomDirty   = true;
                    }
                }
            }
            return;
        }

        if (m_MainDepth == 3) {
            if (in.cancel) {
                const auto& br = screen.GetBridge();
                if (m_MainRootIdx == 2 && m_OptCatIdx == 5 && m_OptSubIdx == 0 && HasPendingDisplayChanges(br)) {
                    // Immediately apply pending display changes and show single 10-second Keep/Revert dialog!
                    if (br.applyDisplayChanges) br.applyDisplayChanges(m_StagedDispMode, m_StagedResIdx);
                    PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                    PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                    m_DispDialogMode      = DispDialogMode::KeepTimer;
                    m_DispConfirmTimer    = 10.0f;
                    m_DispConfirmIdx      = 1; // Safe default: NO (revert)
                    m_DispConfirmLastSec  = -1;
                    m_DispConfirmDomDirty = true;
                    return;
                }
                PlayMenuSE("NUD_COMMON_SYS_CANCEL");
                m_MainDepth = 2;
                m_DomDirty  = true;
                return;
            }
            if (m_MainRootIdx == 0) {
                // Depth 3 under NEW GAME / SP NEW GAME: Control Scheme (NORMAL / NOVICE)
                constexpr int kCtrlCnt = 2;
                if (in.up)   { m_CtrlIdx = (m_CtrlIdx - 1 + kCtrlCnt) % kCtrlCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.down) { m_CtrlIdx = (m_CtrlIdx + 1) % kCtrlCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.confirm) {
                    PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                    PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                    m_VoiceIdx  = 0;
                    m_MainDepth = 4;
                    m_DomDirty  = true;
                }
                return;
            }
            if (m_MainRootIdx == 2 && m_OptCatIdx == 2) {
                // Depth 3 under OPTIONS > DISPLAY SETTINGS: 2-option leaf selector in Col 2
                if (in.up || in.down) {
                    m_DispLeafIdx = 1 - (m_DispLeafIdx & 1);
                    PlayMenuSE("NUD_COMMON_SYS_CURSOR");
                    m_DomDirty = true;
                }
                if (in.confirm) {
                    SetDisplaySettingRow(m_OptSubIdx, m_DispLeafIdx);
                    PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                    m_MainDepth = 2;
                    m_DomDirty  = true;
                }
                return;
            }
            if (m_MainRootIdx == 2 && m_OptCatIdx == 5) {
                // Depth 3 under OPTIONS > ENHANCER SETTINGS > <CATEGORY>: item list
                int catCnt = 0;
                const EnhCatSpec* cats = GetEnhancerCats(catCnt);
                const int cIdx = (m_OptSubIdx >= 0 && m_OptSubIdx < catCnt) ? m_OptSubIdx : 0;
                const int itemCnt = cats[cIdx].itemCount;
                if (itemCnt <= 0) return;
                if (m_EnhItemIdx >= itemCnt) m_EnhItemIdx = 0;
                if (in.up)   { m_EnhItemIdx = (m_EnhItemIdx - 1 + itemCnt) % itemCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.down) { m_EnhItemIdx = (m_EnhItemIdx + 1) % itemCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
                if (in.confirm) {
                    const char* optKey = cats[cIdx].items[m_EnhItemIdx].optionLabel;
                    if (std::strcmp(optKey, "__disp_res__") == 0 && m_StagedDispMode == 1) {
                        PlayMenuSE("NUD_COMMON_SYS_ERROR");
                        screen.TriggerToast("BORDERLESS USES NATIVE MONITOR RESOLUTION");
                        m_DomDirty = true;
                    } else {
                        std::vector<std::string> choices;
                        int cur = 0;
                        bool isAct = false, locked = false;
                        if (GetEnhItemChoices(screen, optKey, choices, cur, isAct, locked) && !choices.empty() && !locked) {
                            const int chCnt = static_cast<int>(choices.size());
                            if (cur < 0 || cur >= chCnt) cur = 0;
                            m_EnhChoiceIdx = cur;
                            if (chCnt > kLeafMaxVisibleRows) {
                                m_LeafScrollTop = (std::max)(0, (std::min)(chCnt - kLeafMaxVisibleRows, cur - 3));
                            } else {
                                m_LeafScrollTop = 0;
                            }
                            PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                            PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                            m_MainDepth = 4;
                            m_DomDirty  = true;
                        } else {
                            PlayMenuSE("NUD_COMMON_SYS_ERROR");
                        }
                    }
                }
            }
            return;
        }

        if (m_MainDepth == 4 && m_MainRootIdx == 0) {
            // Depth 4 under NEW GAME / SP NEW GAME: Voice Language (JAPANESE / ENGLISH)
            if (in.cancel) {
                PlayMenuSE("NUD_COMMON_SYS_CANCEL");
                m_MainDepth = 3;
                m_DomDirty  = true;
                return;
            }
            constexpr int kVoiceCnt = 2;
            if (in.up)   { m_VoiceIdx = (m_VoiceIdx - 1 + kVoiceCnt) % kVoiceCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
            if (in.down) { m_VoiceIdx = (m_VoiceIdx + 1) % kVoiceCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
            if (in.confirm) {
                PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                PlayMenuSE("NUD_COMMON_SYS_WINDOW_OPEN");
                m_SubIdx    = 0;
                m_MainDepth = 5;
                m_DomDirty  = true;
            }
            return;
        }

        if (m_MainDepth == 4 && m_MainRootIdx == 2 && m_OptCatIdx == 5) {
            // Depth 4 under OPTIONS > ENHANCER SETTINGS > <CATEGORY> > <ITEM>: scrollable 2-column leaf selector in Col 2
            if (in.cancel) {
                PlayMenuSE("NUD_COMMON_SYS_CANCEL");
                m_LeafScrollTop = 0;
                m_MainDepth = 3;
                m_DomDirty  = true;
                return;
            }
            int catCnt = 0;
            const EnhCatSpec* cats = GetEnhancerCats(catCnt);
            const int cIdx = (m_OptSubIdx >= 0 && m_OptSubIdx < catCnt) ? m_OptSubIdx : 0;
            const int iIdx = (m_EnhItemIdx >= 0 && m_EnhItemIdx < cats[cIdx].itemCount) ? m_EnhItemIdx : 0;
            const char* optKey = cats[cIdx].items[iIdx].optionLabel;

            std::vector<std::string> choices;
            int curVal = 0;
            bool isAct = false, locked = false;
            if (!GetEnhItemChoices(screen, optKey, choices, curVal, isAct, locked) || choices.empty()) return;
            const int chCnt = static_cast<int>(choices.size());
            if (m_EnhChoiceIdx >= chCnt) m_EnhChoiceIdx = 0;

            if (in.up)   { m_EnhChoiceIdx = (m_EnhChoiceIdx - 1 + chCnt) % chCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
            if (in.down) { m_EnhChoiceIdx = (m_EnhChoiceIdx + 1) % chCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
            if (chCnt > kLeafMaxVisibleRows) {
                if (m_EnhChoiceIdx < m_LeafScrollTop) m_LeafScrollTop = m_EnhChoiceIdx;
                else if (m_EnhChoiceIdx >= m_LeafScrollTop + kLeafMaxVisibleRows) m_LeafScrollTop = m_EnhChoiceIdx - kLeafMaxVisibleRows + 1;
                m_LeafScrollTop = (std::max)(0, (std::min)(m_LeafScrollTop, chCnt - kLeafMaxVisibleRows));
            } else {
                m_LeafScrollTop = 0;
            }

            if (in.confirm) {
                const auto& br = screen.GetBridge();
                if (std::strcmp(optKey, "__disp_mode__") == 0) {
                    m_StagedDispMode = (m_EnhChoiceIdx != 0) ? 1 : 0;
                    PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                    if (HasPendingDisplayChanges(br)) {
                        screen.TriggerToast("PRESS BACK TO APPLY");
                    }
                    m_BuiltViewKey = -1;
                } else if (std::strcmp(optKey, "__disp_res__") == 0) {
                    m_StagedResIdx = m_EnhChoiceIdx;
                    PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                    if (HasPendingDisplayChanges(br)) {
                        screen.TriggerToast("PRESS BACK TO APPLY");
                    }
                    m_BuiltViewKey = -1;
                } else {
                    OptionItem* opt = FindOptionByLabel(screen, optKey);
                    if (opt && opt->setter) opt->setter(m_EnhChoiceIdx);
                    PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                    if (opt && opt->scope == ApplyScope::RestartRequired)      screen.TriggerToast("SAVED (RESTART REQUIRED)");
                    else if (opt && opt->scope == ApplyScope::NextMission)     screen.TriggerToast("SAVED (NEXT MISSION)");
                    else                                                       screen.TriggerToast("SAVED");
                }
                m_LeafScrollTop = 0;
                m_MainDepth = 3;
                m_DomDirty  = true;
            }
            return;
        }

        if (m_MainDepth == 5 && m_MainRootIdx == 0) {
            // Depth 5 under NEW GAME / SP NEW GAME: Subtitle Language (8 options) -> launch campaign!
            if (in.cancel) {
                PlayMenuSE("NUD_COMMON_SYS_CANCEL");
                m_MainDepth = 4;
                m_DomDirty  = true;
                return;
            }
            constexpr int kSubCnt = 8;
            if (in.up)   { m_SubIdx = (m_SubIdx - 1 + kSubCnt) % kSubCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
            if (in.down) { m_SubIdx = (m_SubIdx + 1) % kSubCnt; PlayMenuSE("NUD_COMMON_SYS_CURSOR"); m_DomDirty = true; }
            if (in.confirm) {
                const int act = CampActionAt(m_CampIdx);
                const int isSp = (act == 2) ? 1 : 0;
                PlayMenuSE("NUD_COMMON_SYS_DECIDE");
                m_LeavingMainMenu = true;
                SafeSwgStartNewGameSEH(ResolveMainRva(NativeMainRva::kStartNewGame),
                                       m_DiffIdx, m_CtrlIdx, m_VoiceIdx, m_SubIdx, isSp);
            }
            return;
        }
    }

    // Handles 3-column ACZ navigation (depth 0 = category list, depth 1 = option list)
    bool Update(OptionsScreen& screen, const UiInputSnapshot& in, float dtSec, bool f5Pressed) {
        if (!screen.IsOpen()) return false;

        screen.TickToast(dtSec);

        // Hot-Reload check (F5 or automatic file modification check every 0.5s)
        // Always defer actual ReloadDocument() to Render() so D3D12 texture/DOM updates run on the Present thread.
        m_HotReloadTimer += dtSec;
        if (f5Pressed || m_HotReloadTimer >= 0.5f) {
            m_HotReloadTimer = 0.0f;
            if (f5Pressed || CheckFilesModified()) {
                m_ActiveDocName = "options.rml";
                m_NeedDocReload = true;
                screen.TriggerToast("UI RELOADED");
                m_DomDirty = true;
            }
        }

        if (in.toggleUi) {
            screen.Close(true);
            return true;
        }

        auto& tabs = screen.GetTabs();
        if (tabs.empty()) return true;
        const int tabCount = static_cast<int>(tabs.size());
        int curTab = screen.GetActiveTab();

        if (m_Depth == 0) {
            // Level 0: selecting a category in the left column
            if (in.cancel) {
                screen.Close(true);
                return true;
            }
            if (in.up) {
                curTab = (curTab - 1 + tabCount) % tabCount;
                screen.SetActiveTab(curTab);
                m_DomDirty = true;
            } else if (in.down) {
                curTab = (curTab + 1) % tabCount;
                screen.SetActiveTab(curTab);
                m_DomDirty = true;
            } else if (in.confirm) {
                m_Depth = 1;
                tabs[curTab].EnsureValidSelection();
                m_DomDirty = true;
            }
        } else {
            // Level 1: editing options inside the active category
            if (in.cancel) {
                if (screen.OpenedFromSwgSubmenu()) {
                    screen.Close(true);
                } else {
                    m_Depth = 0;
                    m_DomDirty = true;
                }
                return true;
            }
            if (in.prevTab) {
                curTab = (curTab - 1 + tabCount) % tabCount;
                screen.SetActiveTab(curTab);
                tabs[curTab].EnsureValidSelection();
                m_DomDirty = true;
            } else if (in.nextTab) {
                curTab = (curTab + 1) % tabCount;
                screen.SetActiveTab(curTab);
                tabs[curTab].EnsureValidSelection();
                m_DomDirty = true;
            }

            OptionTab& tab = tabs[screen.GetActiveTab()];
            tab.EnsureValidSelection();

            if (in.up)   { tab.MoveSelection(-1); m_DomDirty = true; }
            if (in.down) { tab.MoveSelection(+1); m_DomDirty = true; }

            if (tab.selectedIdx >= 0 && tab.selectedIdx < static_cast<int>(tab.items.size())) {
                OptionItem& it = tab.items[tab.selectedIdx];
                if (it.IsSelectable()) {
                    if (it.kind == ItemKind::Toggle || it.kind == ItemKind::Choice) {
                        int delta = in.left ? -1 : ((in.right || in.confirm) ? +1 : 0);
                        if (delta != 0 && it.getter && it.setter) {
                            int cnt = (std::max)(1, static_cast<int>(it.choices.size()));
                            int cur = it.getter();
                            if (cur < 0 || cur >= cnt) cur = 0;
                            it.setter((cur + delta + cnt) % cnt);
                            screen.TriggerToast("SAVED");
                            m_DomDirty = true;
                        }
                    } else if (it.kind == ItemKind::Slider) {
                        int delta = in.left ? -it.step : (in.right ? +it.step : 0);
                        if (delta != 0 && it.getter && it.setter) {
                            int cur = it.getter();
                            int nxt = (std::max)(it.minVal, (std::min)(it.maxVal, cur + delta));
                            if (nxt != cur) {
                                it.setter(nxt);
                                screen.TriggerToast("SAVED");
                                m_DomDirty = true;
                            }
                        }
                    } else if (it.kind == ItemKind::Action && in.confirm && it.onAction) {
                        it.onAction();
                        m_DomDirty = true;
                    }
                }
            }
        }

        return true;
    }

    void Render(OptionsScreen& screen, InputDeviceKind activeDev, bool activeOverride = false) {
        if (!IsReady() || (!activeOverride && !screen.IsOpen()) || !m_Renderer || !m_Renderer->IsReady()) return;

        if (!m_Doc || m_NeedDocReload) {
            m_NeedDocReload = false;
            ReloadDocument(m_ActiveDocName);
            if (!m_Doc) return;
        }

        const int w = static_cast<int>(m_Renderer->ScreenWidth());
        const int h = static_cast<int>(m_Renderer->ScreenHeight());
        if (w <= 0 || h <= 0) return;

        const float uiScale = (std::min)(static_cast<float>(w) / 1920.0f, static_cast<float>(h) / 1080.0f);
        const int virtW = (std::max)(640, static_cast<int>(std::lround(static_cast<float>(w) / uiScale)));
        const int virtH = (std::max)(480, static_cast<int>(std::lround(static_cast<float>(h) / uiScale)));
        if (m_RenderIface) m_RenderIface->SetUiScale(uiScale);
        if (w != m_LastW || h != m_LastH) {
            m_LastW = w;
            m_LastH = h;
            m_Context->SetDimensions(Rml::Vector2i(virtW, virtH));
            m_Context->SetDensityIndependentPixelRatio(1.0f);
            ApplyAspectLayout(virtW, virtH, 1.0f);
            m_DomDirty = true;
        }

        if (m_DomDirty || screen.GetStatusToastTimer() > 0.0f || activeDev != m_LastDev) {
            m_LastDev = activeDev;
            if (m_MainMenuMode) {
                RebuildMainMenuDom(screen, activeDev);
            } else {
                RebuildDom(screen, activeDev);
            }
            m_DomDirty = false;
        }
        if (m_MainMenuMode) {
            UpdateDisplayConfirmDom(screen);
        }

        // Authentic PS2 GS background compositing for Main Menu:
        //   1. Exact ACZ crimson base with subtle living firelight oscillation
        //   2. Blue Command Center overlaid ADDITIVELY (aspect-preserving cover fill for any aspect ratio)
        if (m_MainMenuMode) {
            const float vw = m_Renderer->VirtualWidth();
            const float vh = m_Renderer->VirtualHeight();
            if (m_BgCrimsonSlot != 0) {
                const float t = static_cast<float>(m_SysIface ? m_SysIface->GetElapsedTime() : 0.0);
                const float osc = 0.94f + 0.055f * std::sin(t * 1.15f) + 0.030f * std::sin(t * 2.70f + 1.1f);
                const uint8_t cr = static_cast<uint8_t>((std::min)(255.0f, 255.0f * osc));
                m_Renderer->SetPremultipliedBlend(false);
                m_Renderer->DrawImage(0.0f, 0.0f, vw, vh, m_BgCrimsonSlot, Color(cr, cr, cr, 255));
            }
            if (m_BgCommandSlot != 0) {
                float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
                const float scrAspect = (vh > 0.0f) ? (vw / vh) : (16.0f / 9.0f);
                const float texAspect = (m_BgCommandAspect > 0.1f) ? m_BgCommandAspect : (16.0f / 9.0f);
                if (scrAspect > texAspect * 1.005f) {
                    const float cropV = 1.0f - (texAspect / scrAspect);
                    v0 = cropV * 0.5f;
                    v1 = 1.0f - v0;
                } else if (scrAspect < texAspect * 0.995f) {
                    const float cropU = 1.0f - (scrAspect / texAspect);
                    u0 = cropU * 0.5f;
                    u1 = 1.0f - u0;
                }
                m_Renderer->SetAdditiveBlend(true);
                // Exact measured MainSelect.swa additive blend factor (~0.083 = 21/255 on untouched entry_001)
                m_Renderer->DrawImage(0.0f, 0.0f, vw, vh, m_BgCommandSlot, Color(16, 21, 22, 255), u0, v0, u1, v1);
                m_Renderer->SetAdditiveBlend(false);
            }
            // 3. Resolution-independent 3-layer soft-blurred procedural ACZ tactical grid (subtle major grid matching vanilla)
            m_Renderer->SetPremultipliedBlend(false);
            m_Renderer->DrawTacticalGrid(0.10f, 0.09f, 0.20f, 1.0f);
        }

        m_Renderer->SetPremultipliedBlend(true);
        m_Context->Update();
        m_Context->Render();
        m_Renderer->SetPremultipliedBlend(false);
        if (m_MainMenuMode) {
            // Exact MainSelect.swa MC[265] scanline (180 frames @ 60fps = 3.0s cycle):
            //   F0..F119 (0.0s..2.0s): linear upward sweep ty = +448 -> -180 (on 448px canvas)
            //   F120..F179 (2.0s..3.0s): 1.0s pause offscreen
            //   Additive blend (T2[3] blend=1) + exact T1[106] ColorTransform (R=1.0, G=0.98, B=0.50, A=0.148)
            if (m_BgScanlineSlot != 0) {
                const float vw = m_Renderer->VirtualWidth();
                const float vh = m_Renderer->VirtualHeight();
                const double t = m_SysIface ? m_SysIface->GetElapsedTime() : 0.0;
                const float cycle = static_cast<float>(std::fmod(t, 3.0));
                if (cycle < 2.0f) {
                    const float u = cycle / 2.0f;
                    const float yPs2 = 448.0f + (-180.0f - 448.0f) * u;
                    const float yVirt = (yPs2 / 448.0f) * vh;
                    const float hVirt = (128.0f / 448.0f) * vh;
                    m_Renderer->SetAdditiveBlend(true);
                    m_Renderer->DrawImage(0.0f, yVirt, vw, hVirt, m_BgScanlineSlot, Color(38, 37, 19, 255));
                    m_Renderer->SetAdditiveBlend(false);
                }
            }
            m_Sparks.Render(*m_Renderer);
        }
    }

private:
    struct EnhItemSpec {
        const char* optionLabel;
        const char* labelKey;
        const char* labelFb;
        const char* descKey;
        const char* descFb;
    };

    struct EnhCatSpec {
        const char*        labelKey;
        const char*        labelFb;
        const char*        descKey;
        const char*        descFb;
        const EnhItemSpec* items;
        int                itemCount;
    };

    static const EnhCatSpec* GetEnhancerCats(int& outCount) {
        static const EnhItemSpec kGraphics[] = {
            { "__disp_mode__",               "enh.disp_mode.label",  "DISPLAY MODE",           "enh.disp_mode.desc",  "Switches between Windowed and native-resolution Borderless Fullscreen.\n[Applied on exit]" },
            { "__disp_res__",                "enh.disp_res.label",   "RESOLUTION",             "enh.disp_res.desc",   "Selects output resolution for Windowed mode (Borderless uses native monitor resolution).\n[Applied on exit]" },
            { "Widescreen Override",         "enh.wide_ovr.label",   "WIDESCREEN OVERRIDE",    "enh.wide_ovr.desc",   "Limits menu width on ultrawide displays (AUTO, FORCE 4:3, or FORCE 16:9).\n[Instant apply]" },
            { "Extended Draw Distance (2x)", "enh.draw_dist.label",  "EXTENDED DRAW DISTANCE", "enh.draw_dist.desc",  "Expands coarse terrain grid to 19x19 with procedural OOB terrain.\n[Applies on next mission]" },
            { "Spherical Radial Fog",        "enh.radial_fog.label", "SPHERICAL RADIAL FOG",   "enh.radial_fog.desc", "Replaces planar Z-depth fog with 3D spherical distance fog.\n[Restart required]" }
        };
        static const EnhItemSpec kGameplay[] = {
            { "Fast Startup",                "enh.fast_start.label", "FAST STARTUP",           "enh.fast_start.desc", "Controls skipping of launcher, logos, and title screen.\n[Restart required]" },
            { "2-Player Co-Op / VS Mode",    "enh.unlock_2p.label",  "2P CO-OP / VS MODE",     "enh.unlock_2p.desc",  "Unlocks 2-Player modes (P1 Gamepad + P2 Keyboard).\n[Instant apply]" },
            { "Custom D3D12 UI Menus",       "enh.custom_ui.label",  "CUSTOM D3D12 UI",        "enh.custom_ui.desc",  "Enables the native-resolution D3D12 HTML/CSS UI engine.\n[Instant apply]" }
        };
        static const EnhItemSpec kTextures[] = {
            { "Gamepad Button Glyphs",       "enh.hd_glyphs.label",  "GAMEPAD GLYPHS",         "enh.hd_glyphs.desc",  "Controller prompt icon pack (Auto detects controller live).\n[Instant apply]" },
            { "BC7 / BGRA8 DDS Cache",       "enh.hd_cache.label",   "DDS CACHE",              "enh.hd_cache.desc",   "Compiles PNG/JPG textures into .dds_cache for fast loading.\n[Restart required]" },
            { "Auto-Install Texture Packs",  "enh.hd_packs.label",   "AUTO-INSTALL PACKS",     "enh.hd_packs.desc",   "Automatically unpacks texture packs from mods\\ on startup.\n[Restart required]" },
            { "Auto-Share Master Font Glyphs","enh.hd_fonts.label",  "SHARE FONT GLYPHS",      "enh.hd_fonts.desc",   "Shares HD font glyphs across matching dynamic font atlases.\n[Restart required]" },
            { "Hot Reload Textures",         "enh.hd_hot.label",     "HOT-RELOAD TEXTURES",    "enh.hd_hot.desc",     "Re-reads PNG/JPG files from disk when textures are recreated.\n[Instant apply]" },
            { "Dump All Game Textures",      "enh.hd_dump.label",    "DUMP ALL TEXTURES",      "enh.hd_dump.desc",    "Exports loaded textures and fonts to mods\\textures_dump\\.\n[Instant apply]" }
        };
        static const EnhItemSpec kDiagnostics[] = {
            { "Enhancer Logging",              "enh.log.label",       "ENHANCER LOGGING",      "enh.log.desc",        "Writes runtime diagnostics to mods\\enhancer\\enhancer.log.\n[Instant apply]" },
            { "Enable Memory Dump Engine",     "enh.md_enable.label", "MEMORY DUMP ENGINE",    "enh.md_enable.desc",  "Master switch for diagnostic dumps in mods\\memory_dumps\\.\n[Restart required]" },
            { "F2 Quick Diagnostic Dump",      "enh.md_f2.label",     "F2 QUICK DUMP",         "enh.md_f2.desc",      "Press F2 in-game to capture state summary and screenshot.\n[Restart required]" },
            { "Save D3D12 Screenshot on Dump", "enh.md_shot.label",   "SAVE SCREENSHOT",       "enh.md_shot.desc",    "Captures the D3D12 backbuffer screenshot on each dump.\n[Restart required]" },
            { "Dump Memory on Game Exit",      "enh.md_exit.label",   "DUMP ON EXIT",          "enh.md_exit.desc",    "Writes a diagnostic dump folder when acz.exe exits.\n[Restart required]" },
            { "Write Windows Minidump (.dmp)", "enh.md_dmp.label",    "MINIDUMP (.DMP)",       "enh.md_dmp.desc",     "Generates a Windows acz_process.dmp minidump inside dumps.\n[Restart required]" },
            { "Auto-Clean Old Dumps (24h)",    "enh.md_clean.label",  "AUTO-CLEAN (24H)",      "enh.md_clean.desc",   "Automatically deletes dump folders older than 24 hours.\n[Restart required]" }
        };
        static const EnhCatSpec kCats[] = {
            { "enh.cat.0.label", "GRAPHICS",    "enh.cat.0.desc", "Configure display mode, resolution, draw distance, and fog.", kGraphics,    static_cast<int>(sizeof(kGraphics) / sizeof(kGraphics[0])) },
            { "enh.cat.1.label", "GAMEPLAY",    "enh.cat.1.desc", "Configure fast startup, 2-Player mode, and custom UI.",                     kGameplay,    static_cast<int>(sizeof(kGameplay) / sizeof(kGameplay[0])) },
            { "enh.cat.2.label", "HD TEXTURES", "enh.cat.2.desc", "Configure acz_hdtex.asi texture engine and button glyphs.",                 kTextures,    static_cast<int>(sizeof(kTextures) / sizeof(kTextures[0])) },
            { "enh.cat.3.label", "DIAGNOSTICS", "enh.cat.3.desc", "Configure logging and crash memory dump tools.",                            kDiagnostics, static_cast<int>(sizeof(kDiagnostics) / sizeof(kDiagnostics[0])) }
        };
        outCount = static_cast<int>(sizeof(kCats) / sizeof(kCats[0]));
        return kCats;
    }

    void SyncStagedDisplayFromHost(const OptionsHostBridge& br, bool force) {
        const int activeMode = br.getActiveDisplayMode ? br.getActiveDisplayMode() : 0;
        std::vector<std::string> resList = br.getResolutionChoices ? br.getResolutionChoices() : std::vector<std::string>{"1920x1080"};
        if (resList.empty()) resList.push_back("1920x1080");
        int activeRes = br.getWindowedResIndex ? br.getWindowedResIndex() : 0;
        if (activeRes < 0 || activeRes >= static_cast<int>(resList.size())) activeRes = 0;

        const bool hadPending = m_StagedDispSynced &&
            ((m_StagedDispMode != m_LastSeenActiveMode) || (m_StagedDispMode == 0 && m_StagedResIdx != m_LastSeenActiveRes));
        const bool extChanged = (activeMode != m_LastSeenActiveMode || activeRes != m_LastSeenActiveRes);

        m_CachedResChoices   = std::move(resList);
        m_LastSeenActiveMode = activeMode;
        m_LastSeenActiveRes  = activeRes;

        if (force || !m_StagedDispSynced || (extChanged && !hadPending)) {
            if (m_StagedDispMode != activeMode || m_StagedResIdx != activeRes || !m_StagedDispSynced) {
                m_BuiltViewKey = -1;
                m_DomDirty     = true;
            }
            m_StagedDispMode   = activeMode;
            m_StagedResIdx     = activeRes;
            m_StagedDispSynced = true;
        }
        if (m_StagedResIdx < 0 || m_StagedResIdx >= static_cast<int>(m_CachedResChoices.size())) {
            m_StagedResIdx = activeRes;
        }
    }

    bool HasPendingDisplayChanges(const OptionsHostBridge& br) const {
        if (!m_StagedDispSynced) return false;
        const int activeMode = br.getActiveDisplayMode ? br.getActiveDisplayMode() : 0;
        const int activeRes  = br.getWindowedResIndex  ? br.getWindowedResIndex()  : 0;
        if (m_StagedDispMode != activeMode) return true;
        if (m_StagedDispMode == 0 && m_StagedResIdx != activeRes) return true;
        return false;
    }

    bool GetEnhItemChoices(OptionsScreen& screen, const char* optKey, std::vector<std::string>& outChoices, int& outCur, bool& outIsAction, bool& outLocked) {
        outChoices.clear();
        outCur      = 0;
        outIsAction = false;
        outLocked   = false;
        const auto& br = screen.GetBridge();
        if (!m_StagedDispSynced) SyncStagedDisplayFromHost(br, true);

        if (std::strcmp(optKey, "__disp_mode__") == 0) {
            outChoices = { "WINDOWED", "BORDERLESS" };
            outCur     = (m_StagedDispMode != 0) ? 1 : 0;
            return true;
        }
        if (std::strcmp(optKey, "__disp_res__") == 0) {
            outChoices = m_CachedResChoices;
            outCur     = (m_StagedResIdx >= 0 && m_StagedResIdx < static_cast<int>(outChoices.size())) ? m_StagedResIdx : 0;
            outLocked  = (m_StagedDispMode == 1);
            return true;
        }
        if (std::strcmp(optKey, "__disp_apply__") == 0) {
            outIsAction = true;
            outLocked   = !HasPendingDisplayChanges(br);
            return true;
        }
        OptionItem* opt = FindOptionByLabel(screen, optKey);
        if (!opt || opt->choices.empty()) return false;
        outChoices = opt->choices;
        outCur = opt->getter ? opt->getter() : 0;
        if (outCur < 0 || outCur >= static_cast<int>(outChoices.size())) outCur = 0;
        return true;
    }

    std::string FormatEnhItemSummary(OptionsScreen& screen, const char* optKey, bool useActive) {
        const auto& br = screen.GetBridge();
        if (!m_StagedDispSynced) SyncStagedDisplayFromHost(br, true);

        const int activeMode = br.getActiveDisplayMode ? br.getActiveDisplayMode() : 0;
        const int activeRes  = br.getWindowedResIndex  ? br.getWindowedResIndex()  : 0;
        const int modeVal    = useActive ? activeMode : m_StagedDispMode;
        const int resIdx     = useActive ? activeRes  : m_StagedResIdx;

        if (std::strcmp(optKey, "__disp_mode__") == 0) {
            std::string s = (modeVal != 0) ? "BORDERLESS" : "WINDOWED";
            if (!useActive && m_StagedDispMode != activeMode) s += " *";
            return ">" + s;
        }
        if (std::strcmp(optKey, "__disp_res__") == 0) {
            if (modeVal != 0) {
                std::string nat = br.getNativeMonitorResLabel ? br.getNativeMonitorResLabel() : "NATIVE";
                return ">" + nat;
            }
            std::string resStr = (resIdx >= 0 && resIdx < static_cast<int>(m_CachedResChoices.size()))
                                 ? m_CachedResChoices[resIdx] : "1920x1080";
            if (!useActive && (m_StagedDispMode != activeMode || m_StagedResIdx != activeRes)) resStr += " *";
            return ">" + resStr;
        }
        OptionItem* opt = FindOptionByLabel(screen, optKey);
        if (opt && opt->getter && !opt->choices.empty()) {
            int v = opt->getter();
            if (v < 0 || v >= static_cast<int>(opt->choices.size())) v = 0;
            return ">" + opt->choices[v];
        }
        return "-";
    }

    void UpdateDisplayConfirmDom(OptionsScreen& screen) {
        if (!m_Doc) return;
        auto* ov = m_Doc->GetElementById("display-confirm-overlay");
        if (!ov) return;

        if (m_DispDialogMode == DispDialogMode::None) {
            if (m_DispConfirmDomDirty) {
                ov->SetAttribute("style", "display: none;");
                ov->SetInnerRML("");
                m_DispConfirmDomDirty = false;
                m_DispConfirmLastSec  = -1;
            }
            return;
        }

        const int secLeft = (std::max)(1, static_cast<int>(std::ceil(m_DispConfirmTimer)));
        if (!m_DispConfirmDomDirty && secLeft == m_DispConfirmLastSec) return;
        m_DispConfirmLastSec  = secLeft;
        m_DispConfirmDomDirty = false;

        auto S = [this](const char* key, const char* fb) -> std::string {
            return m_Strings.Get(key, fb);
        };
        std::string questionTxt = S("enh.disp_keep.title", "Keep display settings? (%d)");
        size_t pos = questionTxt.find("%d");
        if (pos != std::string::npos) {
            questionTxt.replace(pos, 2, std::to_string(secLeft));
        }

        const std::string yesTxt = S("enh.disp_dialog.yes", "YES");
        const std::string noTxt  = S("enh.disp_dialog.no",  "NO");

        // Authentic ACZero dialog styling (1:1 to vanilla "Cancel load?" / YES / NO popup):
        //   - Translucent dark crimson box with thin golden border (no filled header bar)
        //   - Question left-aligned at top-left
        //   - Vertical centered YES / NO buttons (selected = solid gold bar with dark text)
        const char* yesStyle = (m_DispConfirmIdx == 0)
            ? "width: 228px; height: 54px; line-height: 50px; margin: 7px 0px; text-align: center; background-color: #E8BC18; color: #1C0502; font-size: 30px; font-weight: bold; letter-spacing: 6px; border: 2px #FAD63C;"
            : "width: 228px; height: 54px; line-height: 50px; margin: 7px 0px; text-align: center; background-color: rgba(24, 5, 2, 130); color: rgba(224, 178, 42, 185); font-size: 30px; letter-spacing: 6px; border: 2px rgba(224, 178, 42, 155);";
        const char* noStyle = (m_DispConfirmIdx == 1)
            ? "width: 228px; height: 54px; line-height: 50px; margin: 7px 0px; text-align: center; background-color: #E8BC18; color: #1C0502; font-size: 30px; font-weight: bold; letter-spacing: 6px; border: 2px #FAD63C;"
            : "width: 228px; height: 54px; line-height: 50px; margin: 7px 0px; text-align: center; background-color: rgba(24, 5, 2, 130); color: rgba(224, 178, 42, 185); font-size: 30px; letter-spacing: 6px; border: 2px rgba(224, 178, 42, 155);";

        std::string html;
        html += "<div style=\"width: 1326px; height: 350px; border: 3px rgba(240, 192, 42, 175); background-color: rgba(28, 5, 2, 195); display: flex; flex-direction: column;\">";
        html += "<div style=\"padding: 18px 26px 0px 26px; text-align: left; font-size: 34px; letter-spacing: 3px; color: #F7D442;\">" + EscapeHtml(questionTxt) + "</div>";
        html += "<div style=\"margin-top: auto; margin-bottom: 24px; display: flex; flex-direction: column; align-items: center;\">";
        html += "<div style=\"" + std::string(yesStyle) + "\">" + EscapeHtml(yesTxt) + "</div>";
        html += "<div style=\"" + std::string(noStyle)  + "\">" + EscapeHtml(noTxt)  + "</div>";
        html += "</div></div>";

        ov->SetAttribute("style", "position: absolute; top: 0px; left: 0px; width: 100%; height: 100%; z-index: 200; background-color: rgba(14, 3, 1, 135); display: flex; align-items: center; justify-content: center;");
        ov->SetInnerRML(html);
    }

    uintptr_t ResolveMainRva(uintptr_t rva1021) const {
        if (!m_ExeBase || !m_RvaFn) return 0;
        return m_ExeBase + m_RvaFn(rva1021);
    }

    // Read/write native ACZ DISPLAY SETTINGS mapped to MainSelect UI row index (0 or 1)
    int GetDisplaySettingRow(int subIdx) const {
        switch (subIdx) {
            case 0: // QUICK SELECT: native 1=ON (row 0), 0=OFF (row 1)
                return (SafeSwgGetIntSEH(ResolveMainRva(NativeOptionRva::kGetQuickSelect), 0) != 0) ? 0 : 1;
            case 1: // WINGMAN COMMAND: native 0=ON (row 0), 1=OFF (row 1)
                return (SafeSwgGetIntSEH(ResolveMainRva(NativeOptionRva::kGetCommand), 1) == 0) ? 0 : 1;
            case 2: // HUD: native 0=ON (row 0), non-zero=OFF (row 1)
                return (SafeSwgGetIntSEH(ResolveMainRva(NativeOptionRva::kGetHUD), 0) == 0) ? 0 : 1;
            case 3: // HUD SCALE: native 0=KNOTS/FEET (row 0), 1=KPH/METERS (row 1)
                return (SafeSwgGetIntSEH(ResolveMainRva(NativeOptionRva::kGetUnit), 0) == 0) ? 0 : 1;
            default:
                return 0;
        }
    }

    void SetDisplaySettingRow(int subIdx, int row) const {
        const int r = (row & 1);
        switch (subIdx) {
            case 0: SafeSwgSetIntSEH(ResolveMainRva(NativeOptionRva::kSetQuickSelect), r == 0 ? 1 : 0); break;
            case 1: SafeSwgSetIntSEH(ResolveMainRva(NativeOptionRva::kSetCommand),     r == 0 ? 0 : 1); break;
            case 2: SafeSwgSetIntSEH(ResolveMainRva(NativeOptionRva::kSetHUD),         r == 0 ? 0 : 1); break;
            case 3: SafeSwgSetIntSEH(ResolveMainRva(NativeOptionRva::kSetUnit),        r == 0 ? 0 : 1); break;
            default: break;
        }
    }

    // Auto-adapt MainSelect frame horizontal safe-area to any aspect ratio, respecting Widescreen Override:
    //   - AUTO (0): <= 16:9 uses default 4.8% CSS margins; > 16:9 expands smoothly up to 2160px max frame width
    //   - FORCE 4:3 (1): clamps menu frame & prompts to centered 4:3 width (while crimson background stays full-screen)
    //   - FORCE 16:9 (2): clamps menu frame & prompts to centered 16:9 width on ultrawide displays
    void ApplyAspectLayout(int w, int h, float dpRatio) {
        ApplyAspectLayoutToDoc(m_Doc, w, h, dpRatio);
    }

    void RebuildMainMenuDom(OptionsScreen& screen, InputDeviceKind dev) {
        if (!m_Doc) return;

        if (auto* el = m_Doc->GetElementById("ico-ok")) {
            el->SetAttribute("src", dev == InputDeviceKind::Keyboard ? "assets/icons/kb/Enter_Alt_Key_Dark.png"
                                  : (dev == InputDeviceKind::Xbox    ? "assets/icons/xbox/XboxSeriesX_A.png"
                                                                     : "assets/icons/ps/PS5_Cross.png"));
        }
        if (auto* el = m_Doc->GetElementById("ico-cancel")) {
            el->SetAttribute("src", dev == InputDeviceKind::Keyboard ? "assets/icons/kb/Esc_Key_Dark.png"
                                  : (dev == InputDeviceKind::Xbox    ? "assets/icons/xbox/XboxSeriesX_B.png"
                                                                     : "assets/icons/ps/PS5_Circle.png"));
        }
        if (auto* el = m_Doc->GetElementById("ico-change")) {
            el->SetAttribute("src", dev == InputDeviceKind::Keyboard ? "assets/icons/kb/Arrow_Up_Key_Dark.png"
                                  : (dev == InputDeviceKind::Xbox    ? "assets/icons/xbox/XboxSeriesX_Dpad.png"
                                                                     : "assets/icons/ps/PS5_Dpad.png"));
        }

        auto S = [this](const char* key, const char* fb) -> std::string {
            return m_Strings.Get(key, fb);
        };

        const std::string kRootNames[3] = {
            S("root.0.label", "CAMPAIGN"),
            S("root.1.label", "VS MODE"),
            S("root.2.label", "OPTIONS")
        };
        const std::string kRootDescs[3] = {
            S("root.0.desc", "Play through the game following a storyline."),
            S("root.1.desc", "Play against other players in this mode."),
            S("root.2.desc", "Adjust various settings.")
        };
        // Full 6-action Campaign table indexed by CampAction: 0=CONTINUE, 1=NEW GAME, 2=SP NEW GAME, 3=FREE MISSION, 4=ACE STYLE, 5=GAME DATA
        const std::string kCampActNames[6] = {
            S("camp.continue.label", "CONTINUE"),
            S("camp.new.label",      "NEW GAME"),
            S("camp.sp.label",       "SP NEW GAME"),
            S("camp.free.label",     "FREE MISSION"),
            S("camp.ace.label",      "ACE STYLE"),
            S("camp.data.label",     "GAME DATA")
        };
        const std::string kCampActDescs[6] = {
            S("camp.continue.desc", "Continue with the mission."),
            S("camp.new.desc",      "Start a new campaign."),
            S("camp.sp.desc",       "Start a new campaign.\nAll aircraft, money, and unlocked modes\nobtained previously are carried over."),
            S("camp.free.desc",     "Play any mission that has been cleared in Campaign Mode.\nChanges in the Ace Style Gauge are not reflected."),
            S("camp.ace.desc",      "View flight, Ace Style Gauge, Medal, and\nrival pilot data."),
            S("camp.data.desc",     "Save and/or load data.")
        };
        const std::string kCampNames[5] = {
            kCampActNames[1], kCampActNames[2], kCampActNames[3], kCampActNames[4], kCampActNames[5]
        };
        const std::string kOptRootNames[6] = {
            S("opt.0.label", "CONTROLLER SETTINGS"),
            S("opt.1.label", "LANGUAGE SETTINGS"),
            S("opt.2.label", "DISPLAY SETTINGS"),
            S("opt.3.label", "SOUND SETTINGS"),
            S("opt.4.label", "SCREEN SETTINGS"),
            S("opt.5.label", "ENHANCER SETTINGS")
        };
        const std::string kOptCatDescs[6] = {
            S("opt.0.desc", "Configure controls."),
            S("opt.1.desc", "Adjust language settings."),
            S("opt.2.desc", "Adjust display settings."),
            S("opt.3.desc", "Adjust sound settings."),
            S("opt.4.desc", "Adjust screen settings."),
            S("opt.5.desc", "Configure ACZero Enhancer, HD Textures, and Diagnostics.")
        };
        const std::string kDiffNames[7] = {
            S("diff.0.label", "VERY EASY"),
            S("diff.1.label", "EASY"),
            S("diff.2.label", "NORMAL"),
            S("diff.3.label", "HARD"),
            S("diff.4.label", "EXPERT"),
            S("diff.5.label", "ACE"),
            S("diff.6.label", "FREE FLIGHT")
        };
        const std::string kDiffDescs[7] = {
            S("diff.0.desc", "Level of difficulty suitable for\nfirst-time players."),
            S("diff.1.desc", "Level of difficulty suitable for\nnovice players."),
            S("diff.2.desc", "Level of difficulty suitable for\naverage players."),
            S("diff.3.desc", "Level of difficulty suitable for\nadvanced players."),
            S("diff.4.desc", "Level of difficulty suitable for\nexpert players."),
            S("diff.5.desc", "Level of difficulty suitable for\nonly the most advanced players."),
            S("diff.6.desc", "Fly any stage without enemies.")
        };
        const std::string kCtrlNames[2] = {
            S("ctrl.0.label", "NORMAL"),
            S("ctrl.1.label", "NOVICE")
        };
        const std::string kCtrlDescs[2] = {
            S("ctrl.0.desc", "The standard control scheme. Default settings use the left stick\nfor pitch and roll, and LB and RB for yaw. Allows for realistic,\nprecise aircraft control. Beginners should try this first."),
            S("ctrl.1.desc", "A simplified control scheme. Simply move the left stick left\nor right to turn. Recommended for those who find Normal\ncontrols too difficult.")
        };
        const std::string kVoiceNames[2] = {
            S("voice.0.label", "JAPANESE"),
            S("voice.1.label", "ENGLISH")
        };
        const std::string kVoiceDescs[2] = {
            S("voice.0.desc", "Use Japanese voiceover audio."),
            S("voice.1.desc", "Use English voiceover audio.")
        };
        const std::string kSubNames[8] = {
            S("sub.0.label", "JAPANESE"),
            S("sub.1.label", "ENGLISH(US)"),
            S("sub.2.label", "ENGLISH(UK)"),
            S("sub.3.label", "GERMAN"),
            S("sub.4.label", "FRENCH"),
            S("sub.5.label", "ITALIAN"),
            S("sub.6.label", "SPANISH"),
            S("sub.7.label", "NO SUB")
        };
        const std::string kSubDescs[8] = {
            S("sub.0.desc", "Display Japanese subtitles."),
            S("sub.1.desc", "Display English (US) subtitles."),
            S("sub.2.desc", "Display English (UK) subtitles."),
            S("sub.3.desc", "Display German subtitles."),
            S("sub.4.desc", "Display French subtitles."),
            S("sub.5.desc", "Display Italian subtitles."),
            S("sub.6.desc", "Display Spanish subtitles."),
            S("sub.7.desc", "No in-game subtitles.")
        };
        const std::string kDataNames[2] = {
            S("data.save.label", "SAVE"),
            S("data.load.label", "LOAD")
        };
        const std::string kDataDescs[2] = {
            S("data.save.desc", "Save a game file."),
            S("data.load.desc", "Load a game file.")
        };
        const std::string kDispNames[4] = {
            S("disp.0.label", "QUICK SELECT"),
            S("disp.1.label", "WINGMAN COMMAND"),
            S("disp.2.label", "HUD"),
            S("disp.3.label", "HUD SCALE")
        };
        const std::string kDispDescs[4] = {
            S("disp.0.desc", "Change plane select settings."),
            S("disp.1.desc", "Adjust Wingman Command display."),
            S("disp.2.desc", "Change HUD display settings."),
            S("disp.3.desc", "Switch HUD airspeed and altitude display.")
        };
        const std::string kDispOptNames[4][2] = {
            { S("disp.0.opt0.label", "ON"),           S("disp.0.opt1.label", "OFF") },
            { S("disp.1.opt0.label", "ON"),           S("disp.1.opt1.label", "OFF") },
            { S("disp.2.opt0.label", "ON"),           S("disp.2.opt1.label", "OFF") },
            { S("disp.3.opt0.label", "KNOTS / FEET"), S("disp.3.opt1.label", "KPH / METERS") }
        };
        const std::string kDispOptDescs[4][2] = {
            { S("disp.0.opt0.desc", "Switch to simplified plane select display."),
              S("disp.0.opt1.desc", "Normal setting. View planes during plane select.") },
            { S("disp.1.opt0.desc", "Tap the D-pad to display commands.\nTap once more to select a command."),
              S("disp.1.opt1.desc", "Tap the D-pad to immediately enter a command without first\ndisplaying commands.") },
            { S("disp.2.opt0.desc", "Display HUD."),
              S("disp.2.opt1.desc", "Hide HUD.") },
            { S("disp.3.opt0.desc", "Display airspeed in nautical miles\nper hour and altitude in feet."),
              S("disp.3.opt1.desc", "Display airspeed in kilometers per hour\nand altitude in meters.") }
        };
        const std::string kScrNames[2] = {
            S("scr.0.label", "SCREEN POSITION"),
            S("scr.1.label", "BRIGHTNESS")
        };
        const std::string kScrDescs[2] = {
            S("scr.0.desc", "Adjust screen position."),
            S("scr.1.desc", "Adjust screen brightness.")
        };
        auto CampActionAt = [this](int idx) -> int {
            return m_HasContinue ? idx : (idx + 1);
        };
        const int campCount = m_HasContinue ? 6 : 5;

        auto SqSlot = [](bool active, const std::string& imgId = "") -> std::string {
            std::string idAttr = imgId.empty() ? "" : (" id=\"" + imgId + "\"");
            return "<span class=\"sq-slot\"><img class=\"sq-icon" + std::string(active ? "" : " off")
                 + "\"" + idAttr + " src=\"assets/icons/ui/sq_filled.png\" /></span>";
        };
        auto BadgeRow = [](bool isCol3, const char* extraCls = "", const std::string& prefixHtml = "") -> std::string {
            if (isCol3) {
                return "<div class=\"opt-row preview badge-row-col3" + std::string(extraCls)
                     + "\"><span class=\"badge-wipe\"></span><img class=\"submenu-badge-col3\" src=\"assets/icons/ui/submenu.png\" /></div>";
            }
            return "<div class=\"opt-row preview badge-row" + std::string(extraCls)
                 + "\">" + prefixHtml + "<span class=\"badge-wipe\"></span><img class=\"submenu-badge-icon\" src=\"assets/icons/ui/submenu.png\" /></div>";
        };

        const int64_t viewKey = (static_cast<int64_t>(m_MainDepth == 4 ? (m_LeafScrollTop & 0x3F) : 0) << 32)
                              | (static_cast<int64_t>(m_MainDepth) << 28)
                              | (static_cast<int64_t>(m_MainDepth > 0 ? m_MainRootIdx : 0) << 24)
                              | (static_cast<int64_t>(m_MainDepth >= 2 && m_MainRootIdx == 2 ? m_OptCatIdx : 0) << 20)
                              | (static_cast<int64_t>(m_MainDepth >= 3 && m_MainRootIdx == 2 ? m_OptSubIdx : 0) << 16)
                              | (static_cast<int64_t>(m_MainDepth >= 4 && m_MainRootIdx == 2 ? m_EnhItemIdx : 0) << 12)
                              | (static_cast<int64_t>(m_MainDepth >= 2 && m_MainRootIdx == 0 ? m_CampIdx : 0) << 8)
                              | (static_cast<int64_t>(m_MainDepth >= 3 && m_MainRootIdx == 0 ? m_DiffIdx : 0) << 4)
                              | (static_cast<int64_t>(m_MainDepth >= 4 && m_MainRootIdx == 0 ? m_CtrlIdx : 0) << 2)
                              | (static_cast<int64_t>(m_MainDepth >= 5 && m_MainRootIdx == 0 ? m_VoiceIdx : 0));
        const bool depthChanged = (m_MainDepth != m_LastRenderedDepth);
        const bool inPlaceOnly  = (m_BuiltViewKey == viewKey && !depthChanged);

        std::string bc = S("breadcrumb.root", "MAIN MENU");
        std::string leftHtml, midHtml, rightHtml, footerDesc;
        std::vector<std::string> col1RowTexts, col2RowTexts, col3RowTexts;
        std::vector<int> col2RowLags, col3RowLags;
        auto AddMidText = [&](const std::string& txt, const std::string& prefixHtml = "", const char* extraCls = "", int groupLag = 0) -> std::string {
            const size_t idx = col2RowTexts.size();
            col2RowTexts.push_back(txt);
            col2RowLags.push_back(groupLag);
            return "<div class=\"opt-row preview" + std::string(extraCls) + "\">" + prefixHtml
                 + "<span id=\"tm-row-" + std::to_string(idx) + "\">" + EscapeHtml(txt) + "</span></div>";
        };
        auto AddRightText = [&](const std::string& txt, const char* extraCls = "", int groupLag = 0) -> std::string {
            const size_t idx = col3RowTexts.size();
            col3RowTexts.push_back(txt);
            col3RowLags.push_back(groupLag);
            return "<div class=\"opt-row preview" + std::string(extraCls) + "\"><span id=\"tr-row-"
                 + std::to_string(idx) + "\">" + EscapeHtml(txt) + "</span></div>";
        };

        int cursorRowIdx = 0;
        int cursorRowH   = 69;
        int scrollCurIdx = 0;
        int scrollTotal  = 3;
        bool useTwoCol   = false;
        std::string layoutClass;
        const bool allowSelectedClass = (!depthChanged && !m_CursorHiddenForTrans);

        if (m_MainDepth == 0) {
            if (m_MainRootIdx >= 3) m_MainRootIdx = 0;
            cursorRowIdx = m_MainRootIdx;
            scrollCurIdx = m_MainRootIdx;
            scrollTotal  = 3;
            footerDesc   = kRootDescs[m_MainRootIdx];
            // Vanilla ACZ keeps Col 3 scroll position unchanged when hovering VS MODE (no Col 3 items)!
            if (m_MainRootIdx == 0)      m_Col3TargetY = 0.0f;
            else if (m_MainRootIdx == 2) m_Col3TargetY = -436.0f;

            if (inPlaceOnly) {
                for (int i = 0; i < 3; ++i) {
                    if (auto* el = m_Doc->GetElementById("row-" + std::to_string(i))) {
                        std::string cls = "cat-row";
                        if (i == m_MainRootIdx && allowSelectedClass) cls += " selected";
                        if (i == 1 && !m_Unlock2P) cls += " dimmed";
                        el->SetAttribute("class", cls);
                    }
                    if (auto* grp = m_Doc->GetElementById("mid-grp-" + std::to_string(i))) {
                        grp->SetAttribute("class", (i == m_MainRootIdx) ? "preview-group" : "preview-group dim");
                    }
                }
                for (int i = 0; i < 5; ++i) {
                    if (auto* sq = m_Doc->GetElementById("mid-sq-0-" + std::to_string(i))) {
                        sq->SetAttribute("class", (m_MainRootIdx == 0) ? "sq-icon" : "sq-icon off");
                    }
                }
                for (int i = 0; i < 6; ++i) {
                    if (auto* sq = m_Doc->GetElementById("mid-sq-2-" + std::to_string(i))) {
                        sq->SetAttribute("class", (m_MainRootIdx == 2) ? "sq-icon" : "sq-icon off");
                    }
                }
                if (auto* sq = m_Doc->GetElementById("mid-sq-1-0")) {
                    sq->SetAttribute("class", (m_MainRootIdx == 1) ? "sq-icon" : "sq-icon off");
                }
                if (auto* rg0 = m_Doc->GetElementById("rgt-grp-0")) {
                    rg0->SetAttribute("class", (m_MainRootIdx == 0) ? "preview-group" : "preview-group dim");
                }
                if (auto* rg2 = m_Doc->GetElementById("rgt-grp-2")) {
                    rg2->SetAttribute("class", (m_MainRootIdx == 2) ? "preview-group" : "preview-group dim");
                }
            } else {
                for (int i = 0; i < 3; ++i) {
                    col1RowTexts.push_back(kRootNames[i]);
                    leftHtml += "<div id=\"row-" + std::to_string(i) + "\" class=\"cat-row";
                    if (i == m_MainRootIdx && allowSelectedClass) leftHtml += " selected";
                    if (i == 1 && !m_Unlock2P) leftHtml += " dimmed";
                    leftHtml += "\"><span id=\"t-row-" + std::to_string(i) + "\">" + std::string(kRootNames[i]) + "</span></div>";
                }

                // Exact 1:1 vanilla Col 2 preview tree (tight within groups, large gap between groups)
                midHtml += "<div id=\"mid-grp-0\" class=\"preview-group" + std::string(m_MainRootIdx == 0 ? "" : " dim") + "\">";
                for (int i = 0; i < 5; ++i) {
                    midHtml += AddMidText(kCampNames[i], SqSlot(m_MainRootIdx == 0, "mid-sq-0-" + std::to_string(i)), "", 0);
                }
                midHtml += "</div>";

                midHtml += "<div id=\"mid-grp-1\" class=\"preview-group" + std::string(m_MainRootIdx == 1 ? "" : " dim") + "\">";
                midHtml += BadgeRow(false, "", SqSlot(m_MainRootIdx == 1, "mid-sq-1-0"));
                midHtml += "</div>";

                midHtml += "<div id=\"mid-grp-2\" class=\"preview-group" + std::string(m_MainRootIdx == 2 ? "" : " dim") + "\">";
                for (int i = 0; i < 6; ++i) {
                    midHtml += AddMidText(kOptRootNames[i], SqSlot(m_MainRootIdx == 2, "mid-sq-2-" + std::to_string(i)), "", 1);
                }
                midHtml += "</div>";

                // Exact 1:1 vanilla Col 3 preview tree with distinct subgroup gaps & clean scroll offset (-384px)
                rightHtml += "<div id=\"col-right-scroll\">";
                rightHtml += "<div id=\"rgt-grp-0\" class=\"preview-group" + std::string(m_MainRootIdx == 0 ? "" : " dim") + "\">";
                for (int d = 0; d < 6; ++d) {
                    rightHtml += AddRightText(kDiffNames[d], "", 0);
                }
                rightHtml += AddRightText("FREE FLIGHT", " micro-gap", 1);
                rightHtml += BadgeRow(true, " sub-gap");
                rightHtml += AddRightText("SAVE", " sub-gap", 1);
                rightHtml += AddRightText("LOAD", "", 1);
                rightHtml += "</div>";

                rightHtml += "<div id=\"rgt-grp-2\" class=\"preview-group" + std::string(m_MainRootIdx == 2 ? "" : " dim") + "\">";
                rightHtml += BadgeRow(true);
                rightHtml += BadgeRow(true, " badge-gap");
                rightHtml += AddRightText(kDispNames[0], " sub-gap", 1);
                rightHtml += AddRightText(kDispNames[1], "", 1);
                rightHtml += AddRightText(kDispNames[2], "", 1);
                rightHtml += AddRightText(kDispNames[3], "", 1);
                rightHtml += BadgeRow(true, " sub-gap");
                rightHtml += AddRightText(kScrNames[0], " sub-gap", 2);
                rightHtml += AddRightText(kScrNames[1], "", 2);
                {
                    int enhCatCnt = 0;
                    const EnhCatSpec* enhCats = GetEnhancerCats(enhCatCnt);
                    for (int ec = 0; ec < enhCatCnt; ++ec) {
                        rightHtml += AddRightText(S(enhCats[ec].labelKey, enhCats[ec].labelFb), (ec == 0) ? " sub-gap" : "", 2);
                    }
                }
                rightHtml += "</div>";
                rightHtml += "</div>";
            }
        } else {
            m_Col2TargetY = 0.0f;
            m_Col2Y       = 0.0f;
            if (!(m_MainDepth == 1 && m_MainRootIdx == 2) && !(m_MainDepth == 2 && m_MainRootIdx == 2 && m_OptCatIdx == 5)) {
                m_Col3TargetY = 0.0f;
                m_Col3Y       = 0.0f;
            }
        }

        if (m_MainDepth == 1) {
            bc += " > " + std::string(kRootNames[m_MainRootIdx]);
            if (m_MainRootIdx == 0) {
                layoutClass = "lay-campaign";
                if (m_CampIdx >= campCount) m_CampIdx = 0;
                const int curAct = CampActionAt(m_CampIdx);
                cursorRowIdx = m_CampIdx;
                scrollCurIdx = m_CampIdx;
                scrollTotal  = campCount;
                footerDesc   = kCampActDescs[curAct];

                // Exact 1:1 vanilla Col 2 & Col 3 highlighting per Campaign item (matching 00:02..00:06):
                //   NEW GAME (1):     4 diffs (0..3) lit, 4..6 dim | Col 3: NORMAL/NOVICE lit
                //   SP NEW GAME (2):  6 diffs (0..5) lit, 6 dim    | Col 3: NORMAL/NOVICE lit
                //   FREE MISSION (3): all 7 (0..6) lit             | Col 3: badge 1 lit
                //   ACE STYLE (4):    badge lit                    | Col 3: badge 2 lit
                //   GAME DATA (5):    SAVE/LOAD lit                | Col 3: badge 3 lit
                auto DiffLit = [curAct](int d) -> bool {
                    if (curAct == 1) return d < 4;
                    if (curAct == 2) return d < 6;
                    if (curAct == 3) return d < 7;
                    return false;
                };
                const bool col3CtrlLit = (curAct == 1 || curAct == 2);

                if (inPlaceOnly) {
                    for (int i = 0; i < campCount; ++i) {
                        const int act = CampActionAt(i);
                        const bool locked = (act == 2 && !m_HasSP) || (act == 3 && !m_HasFreeMission);
                        if (auto* el = m_Doc->GetElementById("row-" + std::to_string(i))) {
                            std::string cls = "cat-row";
                            if (i == m_CampIdx && allowSelectedClass) cls += " selected";
                            if (locked) cls += " dimmed";
                            el->SetAttribute("class", cls);
                        }
                    }
                    for (int d = 0; d < 7; ++d) {
                        if (auto* g = m_Doc->GetElementById("mid-d-" + std::to_string(d))) {
                            g->SetAttribute("class", DiffLit(d) ? "preview-group" : "preview-group dim");
                        }
                        if (auto* sq = m_Doc->GetElementById("mid-sq-d-" + std::to_string(d))) {
                            sq->SetAttribute("class", DiffLit(d) ? "sq-icon" : "sq-icon off");
                        }
                    }
                    if (auto* g = m_Doc->GetElementById("mid-grp-3")) g->SetAttribute("class", curAct == 4 ? "preview-group" : "preview-group dim");
                    if (auto* sq = m_Doc->GetElementById("mid-sq-3-0")) sq->SetAttribute("class", curAct == 4 ? "sq-icon" : "sq-icon off");
                    if (auto* g = m_Doc->GetElementById("mid-grp-4")) g->SetAttribute("class", curAct == 5 ? "preview-group" : "preview-group dim");
                    for (int k = 0; k < 2; ++k) {
                        if (auto* sq = m_Doc->GetElementById("mid-sq-4-" + std::to_string(k))) {
                            sq->SetAttribute("class", curAct == 5 ? "sq-icon" : "sq-icon off");
                        }
                    }
                    if (auto* rg0 = m_Doc->GetElementById("rgt-grp-0")) rg0->SetAttribute("class", col3CtrlLit ? "preview-group" : "preview-group dim");
                    if (auto* rg2 = m_Doc->GetElementById("rgt-grp-2")) rg2->SetAttribute("class", curAct == 3 ? "preview-group" : "preview-group dim");
                    if (auto* rg3 = m_Doc->GetElementById("rgt-grp-3")) rg3->SetAttribute("class", curAct == 4 ? "preview-group" : "preview-group dim");
                    if (auto* rg4 = m_Doc->GetElementById("rgt-grp-4")) rg4->SetAttribute("class", curAct == 5 ? "preview-group" : "preview-group dim");
                } else {
                    for (int i = 0; i < campCount; ++i) {
                        const int act = CampActionAt(i);
                        const bool locked = (act == 2 && !m_HasSP) || (act == 3 && !m_HasFreeMission);
                        col1RowTexts.push_back(kCampActNames[act]);
                        std::string cls = "cat-row";
                        if (i == m_CampIdx && allowSelectedClass) cls += " selected";
                        if (locked) cls += " dimmed";
                        leftHtml += "<div id=\"row-" + std::to_string(i) + "\" class=\"" + cls
                                 + "\"><span id=\"t-row-" + std::to_string(i) + "\">" + EscapeHtml(kCampActNames[act]) + "</span></div>";
                    }
                    // Col 2: each difficulty row wrapped with individual dim state so NEW GAME / SP NEW GAME / FREE MISSION match 00:02..00:04!
                    for (int d = 0; d < 7; ++d) {
                        const bool lit = DiffLit(d);
                        const char* extraCls = (d == 6) ? " micro-gap" : "";
                        midHtml += "<div id=\"mid-d-" + std::to_string(d) + "\" class=\"preview-group" + std::string(lit ? "" : " dim")
                                 + "\" style=\"margin-bottom: 0px;\">"
                                 + AddMidText(kDiffNames[d], SqSlot(lit, "mid-sq-d-" + std::to_string(d)), extraCls, 0)
                                 + "</div>";
                    }
                    midHtml += "<div id=\"mid-grp-3\" class=\"preview-group" + std::string(curAct == 4 ? "" : " dim") + "\" style=\"margin-top: 28px;\">";
                    midHtml += BadgeRow(false, "", SqSlot(curAct == 4, "mid-sq-3-0")) + "</div>";
                    midHtml += "<div id=\"mid-grp-4\" class=\"preview-group" + std::string(curAct == 5 ? "" : " dim") + "\">";
                    midHtml += AddMidText(kDataNames[0], SqSlot(curAct == 5, "mid-sq-4-0"), "", 2);
                    midHtml += AddMidText(kDataNames[1], SqSlot(curAct == 5, "mid-sq-4-1"), "", 2) + "</div>";

                    // Col 3: NORMAL / NOVICE + submenu badges matching 00:02..00:06
                    rightHtml += "<div id=\"rgt-grp-0\" class=\"preview-group" + std::string(col3CtrlLit ? "" : " dim") + "\">";
                    rightHtml += AddRightText(kCtrlNames[0], "", 0);
                    rightHtml += AddRightText(kCtrlNames[1], "", 0) + "</div>";
                    rightHtml += "<div id=\"rgt-grp-2\" class=\"preview-group" + std::string(curAct == 3 ? "" : " dim") + "\">" + BadgeRow(true) + "</div>";
                    rightHtml += "<div id=\"rgt-grp-3\" class=\"preview-group" + std::string(curAct == 4 ? "" : " dim") + "\">" + BadgeRow(true) + "</div>";
                    rightHtml += "<div id=\"rgt-grp-4\" class=\"preview-group" + std::string(curAct == 5 ? "" : " dim") + "\">" + BadgeRow(true) + "</div>";
                }
            } else if (m_MainRootIdx == 2) {
                // OPTIONS (6 categories: 5 vanilla + ENHANCER SETTINGS, Col 1 widened to 52%)
                int enhCatCnt = 0;
                const EnhCatSpec* enhCats = GetEnhancerCats(enhCatCnt);
                if (m_OptCatIdx >= 6) m_OptCatIdx = 0;
                layoutClass  = "lay-options";
                cursorRowIdx = m_OptCatIdx;
                scrollCurIdx = m_OptCatIdx;
                scrollTotal  = 6;
                footerDesc   = kOptCatDescs[m_OptCatIdx];
                m_Col3TargetY = (m_OptCatIdx == 5) ? -160.0f : 0.0f;

                if (inPlaceOnly) {
                    for (int i = 0; i < 6; ++i) {
                        if (auto* el = m_Doc->GetElementById("row-" + std::to_string(i))) {
                            el->SetAttribute("class", (i == m_OptCatIdx && allowSelectedClass) ? "cat-row selected" : "cat-row");
                        }
                        if (auto* grp = m_Doc->GetElementById("mid-grp-" + std::to_string(i))) {
                            grp->SetAttribute("class", i == m_OptCatIdx ? "preview-group" : "preview-group dim");
                        }
                    }
                    if (auto* sq = m_Doc->GetElementById("mid-sq-0-0")) sq->SetAttribute("class", m_OptCatIdx == 0 ? "sq-icon" : "sq-icon off");
                    if (auto* sq = m_Doc->GetElementById("mid-sq-1-0")) sq->SetAttribute("class", m_OptCatIdx == 1 ? "sq-icon" : "sq-icon off");
                    for (int i = 0; i < 4; ++i) {
                        if (auto* sq = m_Doc->GetElementById("mid-sq-2-" + std::to_string(i))) {
                            sq->SetAttribute("class", m_OptCatIdx == 2 ? "sq-icon" : "sq-icon off");
                        }
                    }
                    if (auto* sq = m_Doc->GetElementById("mid-sq-3-0")) sq->SetAttribute("class", m_OptCatIdx == 3 ? "sq-icon" : "sq-icon off");
                    for (int i = 0; i < 2; ++i) {
                        if (auto* sq = m_Doc->GetElementById("mid-sq-4-" + std::to_string(i))) {
                            sq->SetAttribute("class", m_OptCatIdx == 4 ? "sq-icon" : "sq-icon off");
                        }
                    }
                    for (int i = 0; i < enhCatCnt; ++i) {
                        if (auto* sq = m_Doc->GetElementById("mid-sq-5-" + std::to_string(i))) {
                            sq->SetAttribute("class", m_OptCatIdx == 5 ? "sq-icon" : "sq-icon off");
                        }
                    }
                    if (auto* rg2 = m_Doc->GetElementById("rgt-grp-2")) {
                        rg2->SetAttribute("class", m_OptCatIdx == 2 ? "preview-group" : "preview-group dim");
                    }
                    if (auto* rg4 = m_Doc->GetElementById("rgt-grp-4")) {
                        rg4->SetAttribute("class", m_OptCatIdx == 4 ? "preview-group" : "preview-group dim");
                    }
                    if (auto* rg5 = m_Doc->GetElementById("rgt-grp-5")) {
                        rg5->SetAttribute("class", m_OptCatIdx == 5 ? "preview-group" : "preview-group dim");
                    }
                } else {
                    for (int i = 0; i < 6; ++i) {
                        col1RowTexts.push_back(kOptRootNames[i]);
                        leftHtml += "<div id=\"row-" + std::to_string(i) + "\" class=\"cat-row" + std::string((i == m_OptCatIdx && allowSelectedClass) ? " selected" : "")
                                 + "\"><span id=\"t-row-" + std::to_string(i) + "\">" + EscapeHtml(kOptRootNames[i]) + "</span></div>";
                    }

                    // Col 2 preview for OPTIONS
                    midHtml += "<div id=\"mid-grp-0\" class=\"preview-group" + std::string(m_OptCatIdx == 0 ? "" : " dim") + "\">"
                             + BadgeRow(false, "", SqSlot(m_OptCatIdx == 0, "mid-sq-0-0")) + "</div>";
                    midHtml += "<div id=\"mid-grp-1\" class=\"preview-group" + std::string(m_OptCatIdx == 1 ? "" : " dim") + "\">"
                             + BadgeRow(false, "", SqSlot(m_OptCatIdx == 1, "mid-sq-1-0")) + "</div>";
                    midHtml += "<div id=\"mid-grp-2\" class=\"preview-group" + std::string(m_OptCatIdx == 2 ? "" : " dim") + "\">";
                    for (int i = 0; i < 4; ++i) {
                        midHtml += AddMidText(kDispNames[i], SqSlot(m_OptCatIdx == 2, "mid-sq-2-" + std::to_string(i)), "", 0);
                    }
                    midHtml += "</div>";
                    midHtml += "<div id=\"mid-grp-3\" class=\"preview-group" + std::string(m_OptCatIdx == 3 ? "" : " dim") + "\">"
                             + BadgeRow(false, "", SqSlot(m_OptCatIdx == 3, "mid-sq-3-0")) + "</div>";
                    midHtml += "<div id=\"mid-grp-4\" class=\"preview-group" + std::string(m_OptCatIdx == 4 ? "" : " dim") + "\">";
                    for (int i = 0; i < 2; ++i) {
                        midHtml += AddMidText(kScrNames[i], SqSlot(m_OptCatIdx == 4, "mid-sq-4-" + std::to_string(i)), "", 1);
                    }
                    midHtml += "</div>";
                    midHtml += "<div id=\"mid-grp-5\" class=\"preview-group" + std::string(m_OptCatIdx == 5 ? "" : " dim") + "\">";
                    for (int i = 0; i < enhCatCnt; ++i) {
                        midHtml += AddMidText(S(enhCats[i].labelKey, enhCats[i].labelFb), SqSlot(m_OptCatIdx == 5, "mid-sq-5-" + std::to_string(i)), "", 2);
                    }
                    midHtml += "</div>";

                    // Live Col 3 values preview for DISPLAY SETTINGS, SCREEN SETTINGS & ENHANCER SETTINGS
                    rightHtml += "<div id=\"col-right-scroll\" style=\"position: relative; top: 0px;\">";
                    rightHtml += "<div id=\"rgt-grp-2\" class=\"preview-group" + std::string(m_OptCatIdx == 2 ? "" : " dim") + "\">";
                    for (int i = 0; i < 4; ++i) {
                        const int curRow = GetDisplaySettingRow(i);
                        const int lag = (i == 0) ? 0 : ((i == 3) ? 2 : 1);
                        rightHtml += AddRightText(std::string(curRow == 0 ? ">" : " ") + kDispOptNames[i][0], (i > 0) ? " sub-gap" : "", lag);
                        rightHtml += AddRightText(std::string(curRow == 1 ? ">" : " ") + kDispOptNames[i][1], "", lag);
                    }
                    rightHtml += "</div>";
                    rightHtml += "<div id=\"rgt-grp-4\" class=\"preview-group" + std::string(m_OptCatIdx == 4 ? "" : " dim") + "\">";
                    rightHtml += BadgeRow(true);
                    rightHtml += BadgeRow(true, " badge-gap");
                    rightHtml += "</div>";
                    rightHtml += "<div id=\"rgt-grp-5\" class=\"preview-group" + std::string(m_OptCatIdx == 5 ? "" : " dim") + "\">";
                    for (int c = 0; c < enhCatCnt; ++c) {
                        for (int k = 0; k < enhCats[c].itemCount && k < 2; ++k) {
                            rightHtml += AddRightText(S(enhCats[c].items[k].labelKey, enhCats[c].items[k].labelFb), (c > 0 && k == 0) ? " sub-gap" : "", 2);
                        }
                    }
                    rightHtml += "</div></div>";
                }
            }
        } else if (m_MainDepth == 2) {
            if (m_MainRootIdx == 0) {
                const int act = CampActionAt(m_CampIdx);
                bc = S("breadcrumb.root", "MAIN MENU") + " > " + kRootNames[0] + " > " + kCampActNames[act];
                if (act == 1 || act == 2 || act == 3) {
                    const int diffCnt = (act == 1) ? 4 : ((act == 2) ? 6 : 7);
                    if (m_DiffIdx >= diffCnt) m_DiffIdx = 0;
                    layoutClass  = "lay-campaign";
                    cursorRowIdx = m_DiffIdx;
                    scrollCurIdx = m_DiffIdx;
                    scrollTotal  = diffCnt;
                    footerDesc   = kDiffDescs[m_DiffIdx];
                    if (inPlaceOnly) {
                        for (int d = 0; d < diffCnt; ++d) {
                            if (auto* el = m_Doc->GetElementById("row-" + std::to_string(d))) {
                                el->SetAttribute("class", (d == m_DiffIdx && allowSelectedClass) ? "cat-row selected" : "cat-row");
                            }
                        }
                    } else {
                        for (int d = 0; d < diffCnt; ++d) {
                            col1RowTexts.push_back(kDiffNames[d]);
                            leftHtml += "<div id=\"row-" + std::to_string(d) + "\" class=\"cat-row" + std::string((d == m_DiffIdx && allowSelectedClass) ? " selected" : "")
                                     + "\"><span id=\"t-row-" + std::to_string(d) + "\">" + EscapeHtml(kDiffNames[d]) + "</span></div>";
                        }
                        if (act == 3) {
                            midHtml += "<div class=\"preview-group\">" + BadgeRow(false, "", SqSlot(true)) + "</div>";
                        } else {
                            // Exact 00:10..00:11 preview: Col 2 = NORMAL/NOVICE, Col 3 = JAPANESE/ENGLISH
                            midHtml += "<div class=\"preview-group\">";
                            midHtml += AddMidText(kCtrlNames[0], SqSlot(true), "", 0);
                            midHtml += AddMidText(kCtrlNames[1], SqSlot(true), "", 0) + "</div>";
                            rightHtml += "<div class=\"preview-group\">";
                            rightHtml += AddRightText(kVoiceNames[0], "", 0);
                            rightHtml += AddRightText(kVoiceNames[1], "", 0) + "</div>";
                        }
                    }
                } else if (act == 5) {
                    // GAME DATA: SAVE / LOAD
                    layoutClass  = "lay-campaign";
                    cursorRowIdx = m_DataIdx;
                    scrollCurIdx = m_DataIdx;
                    scrollTotal  = 2;
                    footerDesc   = kDataDescs[m_DataIdx];
                    if (inPlaceOnly) {
                        for (int k = 0; k < 2; ++k) {
                            if (auto* el = m_Doc->GetElementById("row-" + std::to_string(k))) {
                                el->SetAttribute("class", (k == m_DataIdx && allowSelectedClass) ? "cat-row selected" : "cat-row");
                            }
                        }
                    } else {
                        for (int k = 0; k < 2; ++k) {
                            col1RowTexts.push_back(kDataNames[k]);
                            leftHtml += "<div id=\"row-" + std::to_string(k) + "\" class=\"cat-row" + std::string((k == m_DataIdx && allowSelectedClass) ? " selected" : "")
                                     + "\"><span id=\"t-row-" + std::to_string(k) + "\">" + EscapeHtml(kDataNames[k]) + "</span></div>";
                        }
                        midHtml += "<div class=\"preview-group\">" + BadgeRow(false, "", SqSlot(true)) + "</div>";
                    }
                }
            } else if (m_MainRootIdx == 2) {
                // Authentic 2-column layout for DISPLAY SETTINGS (4 items) & SCREEN SETTINGS (2 items) — no Col 3!
                layoutClass = "two-col";
                bc += " > " + kRootNames[2] + " > " + kOptRootNames[m_OptCatIdx];
                cursorRowIdx = m_OptSubIdx;
                scrollCurIdx = m_OptSubIdx;
                scrollTotal  = (m_OptCatIdx == 2) ? 4 : 2;
                if (m_OptCatIdx == 2) {
                    if (m_OptSubIdx >= 4) m_OptSubIdx = 0;
                    cursorRowIdx = m_OptSubIdx;
                    scrollCurIdx = m_OptSubIdx;
                    footerDesc = kDispDescs[m_OptSubIdx];
                    if (inPlaceOnly) {
                        for (int i = 0; i < 4; ++i) {
                            if (auto* el = m_Doc->GetElementById("row-" + std::to_string(i))) {
                                el->SetAttribute("class", (i == m_OptSubIdx && allowSelectedClass) ? "cat-row selected" : "cat-row");
                            }
                            if (auto* grp = m_Doc->GetElementById("mid-grp-" + std::to_string(i))) {
                                grp->SetAttribute("class", i == m_OptSubIdx ? "preview-group" : "preview-group dim");
                            }
                            for (int k = 0; k < 2; ++k) {
                                if (auto* sq = m_Doc->GetElementById("mid-sq-" + std::to_string(i) + "-" + std::to_string(k))) {
                                    sq->SetAttribute("class", i == m_OptSubIdx ? "sq-icon" : "sq-icon off");
                                }
                            }
                        }
                    } else {
                        for (int i = 0; i < 4; ++i) {
                            col1RowTexts.push_back(kDispNames[i]);
                            leftHtml += "<div id=\"row-" + std::to_string(i) + "\" class=\"cat-row" + std::string((i == m_OptSubIdx && allowSelectedClass) ? " selected" : "")
                                     + "\"><span id=\"t-row-" + std::to_string(i) + "\">" + EscapeHtml(kDispNames[i]) + "</span></div>";
                        }
                        for (int i = 0; i < 4; ++i) {
                            const int curRow = GetDisplaySettingRow(i);
                            const int lag = (i == 0) ? 0 : ((i == 3) ? 2 : 1);
                            midHtml += "<div id=\"mid-grp-" + std::to_string(i) + "\" class=\"preview-group" + std::string(m_OptSubIdx == i ? "" : " dim") + "\">"
                                    + AddMidText(std::string(curRow == 0 ? ">" : " ") + kDispOptNames[i][0], SqSlot(m_OptSubIdx == i, "mid-sq-" + std::to_string(i) + "-0"), "", lag)
                                    + AddMidText(std::string(curRow == 1 ? ">" : " ") + kDispOptNames[i][1], SqSlot(m_OptSubIdx == i, "mid-sq-" + std::to_string(i) + "-1"), "", lag)
                                    + "</div>";
                        }
                    }
                } else if (m_OptCatIdx == 4) {
                    if (m_OptSubIdx >= 2) m_OptSubIdx = 0;
                    cursorRowIdx = m_OptSubIdx;
                    scrollCurIdx = m_OptSubIdx;
                    footerDesc = kScrDescs[m_OptSubIdx];
                    if (inPlaceOnly) {
                        for (int i = 0; i < 2; ++i) {
                            if (auto* el = m_Doc->GetElementById("row-" + std::to_string(i))) {
                                el->SetAttribute("class", (i == m_OptSubIdx && allowSelectedClass) ? "cat-row selected" : "cat-row");
                            }
                            if (auto* grp = m_Doc->GetElementById("mid-grp-" + std::to_string(i))) {
                                grp->SetAttribute("class", i == m_OptSubIdx ? "preview-group" : "preview-group dim");
                            }
                        }
                        if (auto* sq = m_Doc->GetElementById("mid-sq-0-0")) sq->SetAttribute("class", m_OptSubIdx == 0 ? "sq-icon" : "sq-icon off");
                        if (auto* sq = m_Doc->GetElementById("mid-sq-1-0")) sq->SetAttribute("class", m_OptSubIdx == 1 ? "sq-icon" : "sq-icon off");
                    } else {
                        for (int i = 0; i < 2; ++i) {
                            col1RowTexts.push_back(kScrNames[i]);
                            leftHtml += "<div id=\"row-" + std::to_string(i) + "\" class=\"cat-row" + std::string((i == m_OptSubIdx && allowSelectedClass) ? " selected" : "")
                                     + "\"><span id=\"t-row-" + std::to_string(i) + "\">" + EscapeHtml(kScrNames[i]) + "</span></div>";
                        }
                        midHtml += "<div id=\"mid-grp-0\" class=\"preview-group" + std::string(m_OptSubIdx == 0 ? "" : " dim") + "\">"
                                + BadgeRow(false, "", SqSlot(m_OptSubIdx == 0, "mid-sq-0-0")) + "</div>";
                        midHtml += "<div id=\"mid-grp-1\" class=\"preview-group" + std::string(m_OptSubIdx == 1 ? "" : " dim") + "\">"
                                + BadgeRow(false, "", SqSlot(m_OptSubIdx == 1, "mid-sq-1-0")) + "</div>";
                    }
                } else if (m_OptCatIdx == 5) {
                    // ENHANCER SETTINGS (Depth 2): styled identically to Main Menu (41.5% / 29% / 29.5% + scrollbar + smooth scroll!)
                    int enhCatCnt = 0;
                    const EnhCatSpec* enhCats = GetEnhancerCats(enhCatCnt);
                    if (m_OptSubIdx >= enhCatCnt) m_OptSubIdx = 0;
                    layoutClass   = "";
                    cursorRowIdx  = m_OptSubIdx;
                    scrollCurIdx  = m_OptSubIdx;
                    scrollTotal   = enhCatCnt;
                    footerDesc    = S(enhCats[m_OptSubIdx].descKey, enhCats[m_OptSubIdx].descFb);
                    const float enhScrollY = (m_OptSubIdx <= 1) ? 0.0f : ((m_OptSubIdx == 2) ? -130.0f : -295.0f);
                    m_Col2TargetY = enhScrollY;
                    m_Col3TargetY = enhScrollY;

                    if (inPlaceOnly) {
                        for (int c = 0; c < enhCatCnt; ++c) {
                            if (auto* el = m_Doc->GetElementById("row-" + std::to_string(c))) {
                                el->SetAttribute("class", (c == m_OptSubIdx && allowSelectedClass) ? "cat-row selected" : "cat-row");
                            }
                            if (auto* grp = m_Doc->GetElementById("mid-grp-" + std::to_string(c))) {
                                grp->SetAttribute("class", c == m_OptSubIdx ? "preview-group" : "preview-group dim");
                            }
                            if (auto* rgrp = m_Doc->GetElementById("rgt-grp-" + std::to_string(c))) {
                                rgrp->SetAttribute("class", c == m_OptSubIdx ? "preview-group" : "preview-group dim");
                            }
                            for (int k = 0; k < enhCats[c].itemCount; ++k) {
                                if (auto* sq = m_Doc->GetElementById("mid-sq-" + std::to_string(c) + "-" + std::to_string(k))) {
                                    sq->SetAttribute("class", c == m_OptSubIdx ? "sq-icon" : "sq-icon off");
                                }
                            }
                        }
                    } else {
                        midHtml   += "<div id=\"col-mid-scroll\" style=\"position: relative; top: 0px;\">";
                        rightHtml += "<div id=\"col-right-scroll\" style=\"position: relative; top: 0px;\">";
                        for (int c = 0; c < enhCatCnt; ++c) {
                            const std::string cName = S(enhCats[c].labelKey, enhCats[c].labelFb);
                            col1RowTexts.push_back(cName);
                            leftHtml += "<div id=\"row-" + std::to_string(c) + "\" class=\"cat-row" + std::string((c == m_OptSubIdx && allowSelectedClass) ? " selected" : "")
                                     + "\"><span id=\"t-row-" + std::to_string(c) + "\">" + EscapeHtml(cName) + "</span></div>";

                            midHtml += "<div id=\"mid-grp-" + std::to_string(c) + "\" class=\"preview-group" + std::string(c == m_OptSubIdx ? "" : " dim") + "\">";
                            rightHtml += "<div id=\"rgt-grp-" + std::to_string(c) + "\" class=\"preview-group" + std::string(c == m_OptSubIdx ? "" : " dim") + "\">";
                            for (int k = 0; k < enhCats[c].itemCount; ++k) {
                                const std::string iName = S(enhCats[c].items[k].labelKey, enhCats[c].items[k].labelFb);
                                midHtml += AddMidText(iName, SqSlot(c == m_OptSubIdx, "mid-sq-" + std::to_string(c) + "-" + std::to_string(k)), "", c > 1 ? 1 : 0);
                                std::string valStr = FormatEnhItemSummary(screen, enhCats[c].items[k].optionLabel, true);
                                rightHtml += AddRightText(valStr, "", c > 1 ? 1 : 0);
                            }
                            midHtml += "</div>";
                            rightHtml += "</div>";
                        }
                        midHtml   += "</div>";
                        rightHtml += "</div>";
                    }
                }
            }
        } else if (m_MainDepth == 3 && m_MainRootIdx == 0) {
            // Exact 00:13..00:14: CAMPAIGN > NEW GAME > NORMAL (Control Scheme: NORMAL / NOVICE)
            const int act = CampActionAt(m_CampIdx);
            layoutClass  = "lay-campaign";
            bc           = kRootNames[0] + " > " + kCampActNames[act] + " > " + kDiffNames[m_DiffIdx];
            cursorRowIdx = m_CtrlIdx;
            scrollCurIdx = m_CtrlIdx;
            scrollTotal  = 2;
            footerDesc   = kCtrlDescs[m_CtrlIdx];
            if (inPlaceOnly) {
                for (int c = 0; c < 2; ++c) {
                    if (auto* el = m_Doc->GetElementById("row-" + std::to_string(c))) {
                        el->SetAttribute("class", (c == m_CtrlIdx && allowSelectedClass) ? "cat-row selected" : "cat-row");
                    }
                }
            } else {
                for (int c = 0; c < 2; ++c) {
                    col1RowTexts.push_back(kCtrlNames[c]);
                    leftHtml += "<div id=\"row-" + std::to_string(c) + "\" class=\"cat-row" + std::string((c == m_CtrlIdx && allowSelectedClass) ? " selected" : "")
                             + "\"><span id=\"t-row-" + std::to_string(c) + "\">" + EscapeHtml(kCtrlNames[c]) + "</span></div>";
                }
                midHtml += "<div class=\"preview-group\">";
                midHtml += AddMidText(kVoiceNames[0], SqSlot(true), "", 0);
                midHtml += AddMidText(kVoiceNames[1], SqSlot(true), "", 0) + "</div>";
                rightHtml += "<div class=\"preview-group\">";
                for (int s = 0; s < 8; ++s) {
                    rightHtml += AddRightText(kSubNames[s], "", 0);
                }
                rightHtml += "</div>";
            }
        } else if (m_MainDepth == 4 && m_MainRootIdx == 0) {
            // Exact 00:15: NEW GAME > NORMAL > NORMAL (Voiceover Language: JAPANESE / ENGLISH)
            const int act = CampActionAt(m_CampIdx);
            layoutClass  = "lay-campaign";
            bc           = kCampActNames[act] + " > " + kDiffNames[m_DiffIdx] + " > " + kCtrlNames[m_CtrlIdx];
            cursorRowIdx = m_VoiceIdx;
            scrollCurIdx = m_VoiceIdx;
            scrollTotal  = 2;
            footerDesc   = kVoiceDescs[m_VoiceIdx];
            if (inPlaceOnly) {
                for (int v = 0; v < 2; ++v) {
                    if (auto* el = m_Doc->GetElementById("row-" + std::to_string(v))) {
                        el->SetAttribute("class", (v == m_VoiceIdx && allowSelectedClass) ? "cat-row selected" : "cat-row");
                    }
                }
            } else {
                for (int v = 0; v < 2; ++v) {
                    col1RowTexts.push_back(kVoiceNames[v]);
                    leftHtml += "<div id=\"row-" + std::to_string(v) + "\" class=\"cat-row" + std::string((v == m_VoiceIdx && allowSelectedClass) ? " selected" : "")
                             + "\"><span id=\"t-row-" + std::to_string(v) + "\">" + EscapeHtml(kVoiceNames[v]) + "</span></div>";
                }
                midHtml += "<div class=\"preview-group\">";
                for (int s = 0; s < 8; ++s) {
                    midHtml += AddMidText(kSubNames[s], SqSlot(true), "", 0);
                }
                midHtml += "</div>";
                rightHtml += "<div class=\"preview-group\">" + BadgeRow(true) + "</div>";
            }
        } else if (m_MainDepth == 5 && m_MainRootIdx == 0) {
            // Exact 00:17..00:18: NORMAL > NORMAL > JAPANESE (Subtitle Language: 8 options)
            layoutClass  = "lay-campaign";
            bc           = kDiffNames[m_DiffIdx] + " > " + kCtrlNames[m_CtrlIdx] + " > " + kVoiceNames[m_VoiceIdx];
            cursorRowIdx = m_SubIdx;
            scrollCurIdx = m_SubIdx;
            scrollTotal  = 8;
            footerDesc   = kSubDescs[m_SubIdx];
            if (inPlaceOnly) {
                for (int s = 0; s < 8; ++s) {
                    if (auto* el = m_Doc->GetElementById("row-" + std::to_string(s))) {
                        el->SetAttribute("class", (s == m_SubIdx && allowSelectedClass) ? "cat-row selected" : "cat-row");
                    }
                }
            } else {
                for (int s = 0; s < 8; ++s) {
                    col1RowTexts.push_back(kSubNames[s]);
                    leftHtml += "<div id=\"row-" + std::to_string(s) + "\" class=\"cat-row" + std::string((s == m_SubIdx && allowSelectedClass) ? " selected" : "")
                             + "\"><span id=\"t-row-" + std::to_string(s) + "\">" + EscapeHtml(kSubNames[s]) + "</span></div>";
                }
                midHtml += "<div class=\"preview-group\">" + BadgeRow(false, "", SqSlot(true)) + "</div>";
            }
        } else if (m_MainDepth == 3 && m_MainRootIdx == 2 && m_OptCatIdx == 2) {
            // Authentic 2-column leaf selection under OPTIONS > DISPLAY SETTINGS — cursor bar moves to Col 2!
            useTwoCol   = true;
            layoutClass = "two-col two-col-leaf";
            const int sub = (m_OptSubIdx >= 0 && m_OptSubIdx < 4) ? m_OptSubIdx : 0;
            bc           = kRootNames[2] + " > " + kOptRootNames[2] + " > " + kDispNames[sub];
            const int sel = (m_DispLeafIdx & 1);
            cursorRowIdx = sel;
            scrollCurIdx = sel;
            scrollTotal  = 2;
            footerDesc   = kDispOptDescs[sub][sel];

            if (inPlaceOnly) {
                for (int r = 0; r < 2; ++r) {
                    if (auto* el = m_Doc->GetElementById("row-" + std::to_string(r))) {
                        el->SetAttribute("class", (r == sel && allowSelectedClass) ? "cat-row selected" : "cat-row");
                    }
                }
            } else {
                for (int i = 0; i < 4; ++i) {
                    leftHtml += "<div class=\"cat-row" + std::string(i == sub ? " active-parent" : " dimmed")
                             + "\"><span>" + EscapeHtml(kDispNames[i]) + "</span></div>";
                }
                for (int r = 0; r < 2; ++r) {
                    col1RowTexts.push_back(kDispOptNames[sub][r]);
                    midHtml += "<div id=\"row-" + std::to_string(r) + "\" class=\"cat-row" + std::string((r == sel && allowSelectedClass) ? " selected" : "")
                            + "\"><span id=\"t-row-" + std::to_string(r) + "\">" + EscapeHtml(kDispOptNames[sub][r]) + "</span></div>";
                }
            }
        } else if (m_MainDepth == 3 && m_MainRootIdx == 2 && m_OptCatIdx == 5) {
            // Depth 3 under OPTIONS > ENHANCER SETTINGS > <CATEGORY>: 2-column list of settings + live choice preview
            int enhCatCnt = 0;
            const EnhCatSpec* enhCats = GetEnhancerCats(enhCatCnt);
            const int cIdx = (m_OptSubIdx >= 0 && m_OptSubIdx < enhCatCnt) ? m_OptSubIdx : 0;
            const EnhCatSpec& cat = enhCats[cIdx];
            if (m_EnhItemIdx >= cat.itemCount) m_EnhItemIdx = 0;
            const auto& br = screen.GetBridge();
            const bool hasPendingDisp = HasPendingDisplayChanges(br);

            layoutClass  = "two-col";
            bc           = kRootNames[2] + " > " + kOptRootNames[5] + " > " + S(cat.labelKey, cat.labelFb);
            cursorRowIdx = m_EnhItemIdx;
            scrollCurIdx = m_EnhItemIdx;
            scrollTotal  = cat.itemCount;
            footerDesc   = S(cat.items[m_EnhItemIdx].descKey, cat.items[m_EnhItemIdx].descFb);

            auto IsItemDimmed = [&](const char* optKey) -> bool {
                if (std::strcmp(optKey, "__disp_res__") == 0 && m_StagedDispMode == 1) return true;
                if (std::strcmp(optKey, "__disp_apply__") == 0 && !hasPendingDisp) return true;
                return false;
            };
            auto UseCompactSummary = [&](const char* optKey, size_t choiceCount) -> bool {
                if (std::strcmp(optKey, "__disp_res__") == 0 || std::strcmp(optKey, "__disp_apply__") == 0) return true;
                return choiceCount > 3;
            };

            if (inPlaceOnly) {
                for (int i = 0; i < cat.itemCount; ++i) {
                    const char* optKey = cat.items[i].optionLabel;
                    if (auto* el = m_Doc->GetElementById("row-" + std::to_string(i))) {
                        std::string cls = "cat-row";
                        if (i == m_EnhItemIdx && allowSelectedClass) cls += " selected";
                        if (IsItemDimmed(optKey)) cls += " dimmed";
                        el->SetAttribute("class", cls);
                    }
                    if (auto* grp = m_Doc->GetElementById("mid-grp-" + std::to_string(i))) {
                        grp->SetAttribute("class", i == m_EnhItemIdx ? "preview-group" : "preview-group dim");
                    }
                    std::vector<std::string> choices;
                    int curVal = 0;
                    bool isAct = false, locked = false;
                    GetEnhItemChoices(screen, optKey, choices, curVal, isAct, locked);
                    const int sqCnt = UseCompactSummary(optKey, choices.size()) ? 1 : (std::max)(1, static_cast<int>(choices.size()));
                    for (int k = 0; k < sqCnt; ++k) {
                        if (auto* sq = m_Doc->GetElementById("mid-sq-" + std::to_string(i) + "-" + std::to_string(k))) {
                            sq->SetAttribute("class", i == m_EnhItemIdx ? "sq-icon" : "sq-icon off");
                        }
                    }
                }
            } else {
                for (int i = 0; i < cat.itemCount; ++i) {
                    const char* optKey = cat.items[i].optionLabel;
                    const std::string iName = S(cat.items[i].labelKey, cat.items[i].labelFb);
                    col1RowTexts.push_back(iName);
                    std::string cls = "cat-row";
                    if (i == m_EnhItemIdx && allowSelectedClass) cls += " selected";
                    if (IsItemDimmed(optKey)) cls += " dimmed";
                    leftHtml += "<div id=\"row-" + std::to_string(i) + "\" class=\"" + cls
                             + "\"><span id=\"t-row-" + std::to_string(i) + "\">" + EscapeHtml(iName) + "</span></div>";

                    std::vector<std::string> choices;
                    int cur = 0;
                    bool isAct = false, locked = false;
                    GetEnhItemChoices(screen, optKey, choices, cur, isAct, locked);

                    midHtml += "<div id=\"mid-grp-" + std::to_string(i) + "\" class=\"preview-group" + std::string(i == m_EnhItemIdx ? "" : " dim") + "\">";
                    if (UseCompactSummary(optKey, choices.size()) || isAct || choices.empty()) {
                        std::string summary = FormatEnhItemSummary(screen, optKey, false);
                        midHtml += AddMidText(summary, SqSlot(i == m_EnhItemIdx, "mid-sq-" + std::to_string(i) + "-0"), "", i > 2 ? 1 : 0);
                    } else {
                        const int chCnt = static_cast<int>(choices.size());
                        if (cur < 0 || cur >= chCnt) cur = 0;
                        const bool pendingMark = (std::strcmp(optKey, "__disp_mode__") == 0) &&
                            (m_StagedDispMode != (br.getActiveDisplayMode ? br.getActiveDisplayMode() : 0));
                        for (int k = 0; k < chCnt; ++k) {
                            std::string rowTxt = std::string(k == cur ? ">" : " ") + choices[k];
                            if (pendingMark && k == cur) rowTxt += " *";
                            midHtml += AddMidText(rowTxt, SqSlot(i == m_EnhItemIdx, "mid-sq-" + std::to_string(i) + "-" + std::to_string(k)), "", i > 2 ? 1 : 0);
                        }
                    }
                    midHtml += "</div>";
                }
            }
        } else if (m_MainDepth == 4 && m_MainRootIdx == 2 && m_OptCatIdx == 5) {
            // Depth 4 under OPTIONS > ENHANCER SETTINGS > <CATEGORY> > <ITEM>: scrollable 2-column leaf selector in Col 2!
            int enhCatCnt = 0;
            const EnhCatSpec* enhCats = GetEnhancerCats(enhCatCnt);
            const int cIdx = (m_OptSubIdx >= 0 && m_OptSubIdx < enhCatCnt) ? m_OptSubIdx : 0;
            const EnhCatSpec& cat = enhCats[cIdx];
            const int iIdx = (m_EnhItemIdx >= 0 && m_EnhItemIdx < cat.itemCount) ? m_EnhItemIdx : 0;
            const char* optKey = cat.items[iIdx].optionLabel;

            std::vector<std::string> choices;
            int curVal = 0;
            bool isAct = false, locked = false;
            GetEnhItemChoices(screen, optKey, choices, curVal, isAct, locked);
            const int chCnt = !choices.empty() ? static_cast<int>(choices.size()) : 1;
            const int sel = (m_EnhChoiceIdx >= 0 && m_EnhChoiceIdx < chCnt) ? m_EnhChoiceIdx : 0;
            if (chCnt > kLeafMaxVisibleRows) {
                if (sel < m_LeafScrollTop) m_LeafScrollTop = sel;
                else if (sel >= m_LeafScrollTop + kLeafMaxVisibleRows) m_LeafScrollTop = sel - kLeafMaxVisibleRows + 1;
                m_LeafScrollTop = (std::max)(0, (std::min)(m_LeafScrollTop, chCnt - kLeafMaxVisibleRows));
            } else {
                m_LeafScrollTop = 0;
            }
            const int visCnt = (std::min)(chCnt - m_LeafScrollTop, kLeafMaxVisibleRows);
            const int visSel = sel - m_LeafScrollTop;

            useTwoCol    = true;
            layoutClass  = "two-col two-col-leaf";
            bc           = kOptRootNames[5] + " > " + S(cat.labelKey, cat.labelFb) + " > " + S(cat.items[iIdx].labelKey, cat.items[iIdx].labelFb);
            cursorRowIdx = visSel;
            scrollCurIdx = sel;
            scrollTotal  = chCnt;
            footerDesc   = S(cat.items[iIdx].descKey, cat.items[iIdx].descFb);

            if (inPlaceOnly) {
                for (int r = 0; r < visCnt; ++r) {
                    if (auto* el = m_Doc->GetElementById("row-" + std::to_string(r))) {
                        el->SetAttribute("class", (r == visSel && allowSelectedClass) ? "cat-row selected" : "cat-row");
                    }
                }
            } else {
                for (int i = 0; i < cat.itemCount; ++i) {
                    leftHtml += "<div class=\"cat-row" + std::string(i == iIdx ? " active-parent" : " dimmed")
                             + "\"><span>" + EscapeHtml(S(cat.items[i].labelKey, cat.items[i].labelFb)) + "</span></div>";
                }
                if (!choices.empty()) {
                    for (int r = 0; r < visCnt; ++r) {
                        const int realIdx = m_LeafScrollTop + r;
                        col1RowTexts.push_back(choices[realIdx]);
                        midHtml += "<div id=\"row-" + std::to_string(r) + "\" class=\"cat-row" + std::string((r == visSel && allowSelectedClass) ? " selected" : "")
                                + "\"><span id=\"t-row-" + std::to_string(r) + "\">" + EscapeHtml(choices[realIdx]) + "</span></div>";
                    }
                }
            }
        }

        m_ActiveRowIdx   = cursorRowIdx;
        m_UseTwoCol      = useTwoCol;
        m_LayoutClass    = layoutClass;
        m_ScrollHasRange = (m_MainDepth == 0)
                        || (m_MainDepth == 2 && m_MainRootIdx == 2 && m_OptCatIdx == 5)
                        || (m_MainDepth == 4 && scrollTotal > kLeafMaxVisibleRows);

        if (auto* el = m_Doc->GetElementById("toast-msg")) el->SetInnerRML(screen.GetStatusToastTimer() > 0.0f ? EscapeHtml(screen.GetStatusToast()) : "");
        if (!inPlaceOnly) {
            if (auto* el = m_Doc->GetElementById("col-left-items")) el->SetInnerRML(leftHtml);
            if (auto* el = m_Doc->GetElementById("col-mid-items"))  el->SetInnerRML(midHtml);
            if (auto* el = m_Doc->GetElementById("col-right"))      el->SetInnerRML(rightHtml);
            m_BuiltViewKey = viewKey;
            // Immediately apply Col 2 & Col 3 vertical scroll positions so newly built screens never jump from 0px!
            m_Col2Y = m_Col2TargetY;
            m_Col3Y = m_Col3TargetY;
            if (auto* c2 = m_Doc->GetElementById("col-mid-scroll")) {
                char c2Buf[64];
                std::snprintf(c2Buf, sizeof(c2Buf), "position: relative; top: %.1fpx;", m_Col2Y);
                c2->SetAttribute("style", c2Buf);
            }
            if (auto* c3 = m_Doc->GetElementById("col-right-scroll")) {
                char c3Buf[64];
                std::snprintf(c3Buf, sizeof(c3Buf), "position: relative; top: %.1fpx;", m_Col3Y);
                c3->SetAttribute("style", c3Buf);
            }
        }

        // Authentic ACZ directional transition trigger:
        //   Downward (depth increases): Blinking squares at right edge of Col 1 & Col 2 -> glide left -> type (00:04 -> 00:02)
        //   Upward (depth decreases): Clean blank frame (00:02, no fly squares) -> simultaneous teletype (00:03)
        if (depthChanged) {
            const bool goingDeeper = (m_MainDepth > m_LastRenderedDepth);
            m_LastRenderedDepth = m_MainDepth;
            m_Col1Texts = col1RowTexts;
            m_Col2Texts = col2RowTexts;
            m_Col3Texts = col3RowTexts;
            m_Col2Lags  = col2RowLags;
            m_Col3Lags  = col3RowLags;
            auto MaxLen = [](const std::vector<std::string>& v) -> float {
                size_t m = 0;
                for (const auto& s : v) if (s.size() > m) m = s.size();
                return static_cast<float>(m);
            };
            auto MaxLag = [](const std::vector<int>& v) -> float {
                int m = 0;
                for (int x : v) if (x > m) m = x;
                return static_cast<float>(m);
            };
            m_Col1Max    = MaxLen(m_Col1Texts);
            m_Col2Max    = MaxLen(m_Col2Texts);
            m_Col3Max    = MaxLen(m_Col3Texts);
            m_Col2MaxLag = MaxLag(m_Col2Lags);
            m_Col3MaxLag = MaxLag(m_Col3Lags);
            m_Col2Done   = false;
            m_CursorHiddenForTrans = true;
            m_FooterPending = true;
            m_BcPending     = true;

            if (auto* el = m_Doc->GetElementById("breadcrumb-text")) el->SetInnerRML("");
            if (auto* el = m_Doc->GetElementById("footer-desc"))     el->SetInnerRML("");

            if (goingDeeper) {
                // Phase 1: Blinking squares at right edge of Col 1 & Col 2 (Col 3 hidden, folder hidden)
                m_TransPhase = 1;
                m_TransTimer = 0.12f;
                m_FlyElapsed = 0.0f;
                if (auto* fi = m_Doc->GetElementById("folder-icon")) fi->SetAttribute("class", "folder-icon hidden");
                if (auto* tp = m_Doc->GetElementById("top-prompts")) tp->SetAttribute("style", "opacity: 1.0;");
                if (auto* el = m_Doc->GetElementById("col-left-items")) {
                    el->SetAttribute("style", useTwoCol ? "left: 0px;" : "left: 84.00%;");
                }
                if (auto* el = m_Doc->GetElementById("col-mid-items")) {
                    el->SetAttribute("style", "left: 82.00%;");
                }
                for (size_t i = 0; i < m_Col1Texts.size(); ++i) {
                    if (auto* el = m_Doc->GetElementById("t-row-" + std::to_string(i))) {
                        el->SetInnerRML("<span class=\"fly-sq-lg\"></span>");
                    }
                }
                for (size_t i = 0; i < m_Col2Texts.size(); ++i) {
                    if (auto* el = m_Doc->GetElementById("tm-row-" + std::to_string(i))) {
                        el->SetInnerRML("<span class=\"fly-sq-sm\"></span>");
                    }
                }
                for (size_t i = 0; i < m_Col3Texts.size(); ++i) {
                    if (auto* el = m_Doc->GetElementById("tr-row-" + std::to_string(i))) {
                        el->SetInnerRML("");
                    }
                }
            } else {
                // Phase 4 (upward): Clean blank frame (00:02) — no fly squares, folder visible, prompts briefly hidden
                m_TransPhase = 4;
                m_TransTimer = 0.06f;
                if (auto* fi = m_Doc->GetElementById("folder-icon")) fi->SetAttribute("class", "folder-icon");
                if (auto* tp = m_Doc->GetElementById("top-prompts")) tp->SetAttribute("style", "opacity: 0.0;");
                if (auto* el = m_Doc->GetElementById("col-left-items")) el->SetAttribute("style", "left: 0px;");
                if (auto* el = m_Doc->GetElementById("col-mid-items"))  el->SetAttribute("style", "left: 0px;");
            }
        } else if (!inPlaceOnly) {
            m_Col1Texts.clear();
            m_Col2Texts.clear();
            m_Col3Texts.clear();
            m_Col2Lags.clear();
            m_Col3Lags.clear();
            if (auto* el = m_Doc->GetElementById("col-left-items")) el->SetAttribute("style", "left: 0px;");
            if (auto* el = m_Doc->GetElementById("col-mid-items"))  el->SetAttribute("style", "left: 0px;");
        }
        UpdateColumnsClass();

        // Instant snap for persistent yellow highlight bar (#cursor-bar or #cursor-bar-mid), 1:1 to vanilla ACZ!
        m_CursorInMid   = useTwoCol;
        m_CursorRowH    = cursorRowH;
        m_CursorTargetY = static_cast<float>(cursorRowIdx * cursorRowH);
        m_CursorY       = m_CursorTargetY;
        auto* barLeft = m_Doc->GetElementById("cursor-bar");
        auto* barMid  = m_Doc->GetElementById("cursor-bar-mid");
        auto* actBar  = useTwoCol ? barMid : barLeft;
        auto* othBar  = useTwoCol ? barLeft : barMid;
        if (othBar) othBar->SetAttribute("style", "opacity: 0.0;");
        if (actBar) {
            m_CursorInitialized = true;
            if (depthChanged) {
                char curBuf[128];
                std::snprintf(curBuf, sizeof(curBuf), "top: %.1fpx; height: %dpx; opacity: 0.0;", m_CursorY, m_CursorRowH);
                actBar->SetAttribute("style", curBuf);
            } else {
                char curBuf[128];
                std::snprintf(curBuf, sizeof(curBuf), "top: %.1fpx; height: %dpx; opacity: %.1f;",
                              m_CursorY, m_CursorRowH, m_CursorHiddenForTrans ? 0.0f : 1.0f);
                actBar->SetAttribute("style", curBuf);
            }
        }
        if (auto* arr = m_Doc->GetElementById("cursor-arrow")) {
            arr->SetAttribute("style", "opacity: 0.0;"); // Vanilla has no arrow inside highlight bar
        }

        // Smooth per-frame target for right-edge scrollbar thumb (hidden during transition phases 1/2/4, visible on Depth 0)
        {
            const float frac = (scrollTotal > 1) ? (static_cast<float>(scrollCurIdx) / static_cast<float>(scrollTotal - 1)) : 0.0f;
            m_ScrollTargetPct = frac * 58.0f;
            const bool scrollVisible = m_ScrollHasRange && (m_TransPhase == 0 || m_TransPhase == 3);
            if (depthChanged || !m_ScrollHasRange) {
                m_ScrollPct = m_ScrollTargetPct;
                if (auto* th = m_Doc->GetElementById("scroll-thumb")) {
                    char stBuf[96];
                    std::snprintf(stBuf, sizeof(stBuf), "top: %.2f%%; opacity: %.1f;",
                                  m_ScrollPct, scrollVisible ? 1.0f : 0.0f);
                    th->SetAttribute("style", stBuf);
                }
            }
        }

        // Start ACZ terminal block-cursor reveal whenever breadcrumb or footer text changes
        if (bc != m_BcFullText || depthChanged) {
            m_BcFullText = bc;
            if (!m_BcPending) {
                m_BcRevealChars = 3.0f;
                m_BcShownCount  = 0;
                if (auto* el = m_Doc->GetElementById("breadcrumb-text")) {
                    el->SetInnerRML(FormatTeletypeBlocks(m_BcFullText, 3));
                }
            }
        }
        if (footerDesc != m_FooterFullText || depthChanged) {
            m_FooterFullText = footerDesc;
            if (!m_FooterPending) {
                m_FooterRevealChars = 1.0f;
                m_FooterShownCount  = 0;
                if (auto* el = m_Doc->GetElementById("footer-desc")) {
                    el->SetInnerRML(FormatTeletypeBlocks(m_FooterFullText, 1));
                }
            }
        }
    }

    void StartTypingPhase() {
        if (!m_Doc) return;
        m_TransPhase = 3;
        m_Col2Done   = m_Col2Texts.empty();
        if (auto* el = m_Doc->GetElementById("col-left-items")) el->SetAttribute("style", "left: 0px;");
        if (auto* el = m_Doc->GetElementById("col-mid-items"))  el->SetAttribute("style", "left: 0px;");
        if (auto* fi = m_Doc->GetElementById("folder-icon"))    fi->SetAttribute("class", "folder-icon");
        if (auto* tp = m_Doc->GetElementById("top-prompts"))    tp->SetAttribute("style", "opacity: 1.0;");
        UpdateColumnsClass();

        if (auto* th = m_Doc->GetElementById("scroll-thumb")) {
            char stBuf[96];
            std::snprintf(stBuf, sizeof(stBuf), "top: %.2f%%; opacity: %.1f;",
                          m_ScrollPct, m_ScrollHasRange ? 1.0f : 0.0f);
            th->SetAttribute("style", stBuf);
        }

        if (m_BcPending) {
            m_BcPending     = false;
            m_BcRevealChars = 1.0f;
            m_BcShownCount  = 0;
            if (auto* el = m_Doc->GetElementById("breadcrumb-text")) {
                el->SetInnerRML(FormatTeletypeBlocks(m_BcFullText, 1));
            }
        }

        m_Col1Reveal = 1.0f; m_Col1Shown = 1;
        m_Col2Reveal = 0.0f; m_Col2Shown = static_cast<size_t>(-1);
        m_Col3Reveal = 0.0f; m_Col3Shown = static_cast<size_t>(-1);
        for (size_t i = 0; i < m_Col1Texts.size(); ++i) {
            if (auto* el = m_Doc->GetElementById("t-row-" + std::to_string(i))) {
                el->SetInnerRML(FormatTeletypeBlocks(m_Col1Texts[i], 1));
            }
        }
        for (size_t i = 0; i < m_Col2Texts.size(); ++i) {
            if (auto* el = m_Doc->GetElementById("tm-row-" + std::to_string(i))) {
                el->SetInnerRML(FormatTeletypePreview(m_Col2Texts[i], 0));
            }
        }
        for (size_t i = 0; i < m_Col3Texts.size(); ++i) {
            if (auto* el = m_Doc->GetElementById("tr-row-" + std::to_string(i))) {
                el->SetInnerRML(FormatTeletypePreview(m_Col3Texts[i], 0));
            }
        }
    }

    void UpdateColumnsClass() {
        if (!m_Doc) return;
        if (auto* cols = m_Doc->GetElementById("columns")) {
            std::string cls = m_LayoutClass;
            auto Append = [&](const char* tok) {
                if (!cls.empty()) cls += " ";
                cls += tok;
            };
            if (!m_ScrollHasRange) {
                Append("no-scroll");
            }
            if (m_TransPhase == 1 || m_TransPhase == 2) {
                Append("trans-fly");
            } else if (m_TransPhase == 4) {
                Append("trans-clear");
            } else if (m_TransPhase == 3 && !m_Col2Done) {
                Append("trans-type-mid");
            }
            cols->SetAttribute("class", cls);
        }
    }

    void RevealCursorAfterTransition() {
        m_CursorHiddenForTrans = false;
        if (!m_Doc) return;
        const char* activeBarId = m_CursorInMid ? "cursor-bar-mid" : "cursor-bar";
        if (auto* bar = m_Doc->GetElementById(activeBarId)) {
            char curBuf[128];
            std::snprintf(curBuf, sizeof(curBuf), "top: %.1fpx; height: %dpx; opacity: 1.0;", m_CursorY, m_CursorRowH);
            bar->SetAttribute("style", curBuf);
        }
        if (auto* row = m_Doc->GetElementById("row-" + std::to_string(m_ActiveRowIdx))) {
            row->SetAttribute("class", "cat-row selected");
        }
        if (m_FooterPending) {
            m_FooterPending     = false;
            m_FooterRevealChars = 1.0f;
            m_FooterShownCount  = 0;
            if (auto* el = m_Doc->GetElementById("footer-desc")) {
                el->SetInnerRML(FormatTeletypeBlocks(m_FooterFullText, 1));
            }
        }
    }

    // Authentic ACZ Col 2 & Col 3 preview teletype (00:03: NEW [][][], CON[][][], QUI[][][], HUD[][][]):
    // Revealed plain text followed by 3 fixed-width empty yellow blocks with rising opacity that burn down at end.
    static std::string FormatTeletypePreview(const std::string& full, size_t step) {
        const size_t len = full.size();
        if (len == 0) return "";
        if (step >= len + 3) return EscapeHtml(full);
        const size_t visLen    = (std::min)(len, step);
        const size_t leadCount = (step <= len) ? size_t(3) : (len + 3 - step);
        std::string out = EscapeHtml(full.substr(0, visLen));
        if (leadCount >= 3) out += "<span class=\"tt-lead-1\"></span><span class=\"tt-lead-2\"></span><span class=\"tt-lead-3\"></span>";
        else if (leadCount == 2) out += "<span class=\"tt-lead-2\"></span><span class=\"tt-lead-3\"></span>";
        else if (leadCount == 1) out += "<span class=\"tt-lead-3\"></span>";
        return out;
    }

    // Authentic ACZ Col 1 / Breadcrumb / Footer teletype (00:02 [C][O][N], 00:03 CAMPA[I][G][N], 00:08 Ad[j][u][s]):
    // Each trailing revealed character (up to 3) renders inside its own uniform-width yellow inverted cell!
    static std::string FormatTeletypeBlocks(const std::string& full, size_t step) {
        const size_t len = full.size();
        if (len == 0) return "";
        if (step >= len + 2) return EscapeHtml(full);
        const size_t visLen   = (std::min)(len, step);
        const size_t tailCnt  = (step <= len) ? (std::min)(size_t(3), visLen)
                                              : (len + 2 - step);
        const size_t plainEnd = (visLen > tailCnt) ? (visLen - tailCnt) : 0;
        std::string out = EscapeHtml(full.substr(0, plainEnd));
        for (size_t i = plainEnd; i < visLen; ++i) {
            const char c = full[i];
            if (c == '\n') {
                out += "<br/>";
            } else if (c == ' ') {
                out.push_back(' ');
            } else {
                const bool isOldestOfThree = (tailCnt == 3 && i == plainEnd);
                out += isOldestOfThree ? "<span class=\"tt-ch-dim\">" : "<span class=\"tt-ch\">";
                out += EscapeHtml(std::string(1, c));
                out += "</span>";
            }
        }
        return out;
    }
    static OptionItem* FindOptionByLabel(OptionsScreen& screen, const char* label) {
        for (auto& t : screen.GetTabs()) {
            for (auto& it : t.items) {
                if (it.label == label) return &it;
            }
        }
        return nullptr;
    }

    static std::string ToUpperAscii(std::string s) {
        for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        return s;
    }

    static std::string EscapeHtml(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else if (c == '\n') out += "<br/>";
            else out.push_back(c);
        }
        return out;
    }

    static std::string CategoryDisplayName(const std::string& title) {
        if (title == "ENHANCER")    return "ENHANCER SETTINGS";
        if (title == "DISPLAY")     return "DISPLAY SETTINGS";
        if (title == "CONTROLS")    return "CONTROLLER SETTINGS";
        if (title == "AUDIO")       return "SOUND SETTINGS";
        if (title == "TEXTURES")    return "TEXTURE ENGINE";
        if (title == "DIAGNOSTICS") return "DIAGNOSTICS";
        return title;
    }

    static std::string CategoryDescription(const std::string& title) {
        if (title == "ENHANCER")    return "Configure DrawDistance, RadialFog, FastStartup, and Enhancer features.";
        if (title == "DISPLAY")     return "Adjust screen aspect ratio (4:3 / 16:9 / 21:9), brightness, and HUD display.";
        if (title == "CONTROLS")    return "Adjust flight control scheme, button layout, stick axes, and vibration.";
        if (title == "AUDIO")       return "Adjust sound output mode and BGM, Voice, SFX, and System SE volume.";
        if (title == "TEXTURES")    return "Configure acz_hdtex.asi glyphs, DDS cache, and texture modding tools.";
        if (title == "DIAGNOSTICS") return "Configure memdump.asi F2 diagnostic dumps, screenshots, and minidumps.";
        return "";
    }

    static std::string FormatOptionValueShort(const OptionItem& it) {
        if (it.kind == ItemKind::Toggle || it.kind == ItemKind::Choice) {
            int v = it.getter ? it.getter() : 0;
            int cnt = static_cast<int>(it.choices.size());
            if (cnt > 0) {
                if (v < 0 || v >= cnt) v = 0;
                return it.choices[v];
            }
        } else if (it.kind == ItemKind::Slider) {
            int v = it.getter ? it.getter() : it.minVal;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d%s", v, it.suffix.c_str());
            return std::string(buf);
        } else if (it.kind == ItemKind::Info && it.infoValue) {
            return it.infoValue();
        }
        return "";
    }

    void RebuildDom(OptionsScreen& screen, InputDeviceKind dev) {
        if (!m_Doc) return;

        // Top prompts
        if (auto* el = m_Doc->GetElementById("ico-ok")) {
            el->SetAttribute("src", dev == InputDeviceKind::Keyboard ? "assets/icons/kb/Enter_Alt_Key_Dark.png"
                                  : (dev == InputDeviceKind::Xbox    ? "assets/icons/xbox/XboxSeriesX_A.png"
                                                                     : "assets/icons/ps/PS5_Cross.png"));
        }
        if (auto* el = m_Doc->GetElementById("ico-cancel")) {
            el->SetAttribute("src", dev == InputDeviceKind::Keyboard ? "assets/icons/kb/Esc_Key_Dark.png"
                                  : (dev == InputDeviceKind::Xbox    ? "assets/icons/xbox/XboxSeriesX_B.png"
                                                                     : "assets/icons/ps/PS5_Circle.png"));
        }
        if (auto* el = m_Doc->GetElementById("ico-change")) {
            el->SetAttribute("src", dev == InputDeviceKind::Keyboard ? "assets/icons/kb/Arrow_Up_Key_Dark.png"
                                  : (dev == InputDeviceKind::Xbox    ? "assets/icons/xbox/XboxSeriesX_Dpad.png"
                                                                     : "assets/icons/ps/PS5_Dpad.png"));
        }

        auto& tabs = screen.GetTabs();
        if (tabs.empty()) return;
        const int curTab = screen.GetActiveTab();
        OptionTab& activeTab = tabs[curTab];

        // Breadcrumb bar
        if (auto* el = m_Doc->GetElementById("breadcrumb-text")) {
            std::string bc = "MAIN MENU &gt; OPTIONS";
            if (m_Depth == 1) {
                bc += " &gt; " + EscapeHtml(CategoryDisplayName(activeTab.title));
            }
            el->SetInnerRML(bc);
        }
        if (auto* el = m_Doc->GetElementById("toast-msg")) {
            el->SetInnerRML(screen.GetStatusToastTimer() > 0.0f ? EscapeHtml(screen.GetStatusToast()) : "");
        }

        std::string leftHtml, midHtml, rightHtml, footerDesc;

        if (m_Depth == 0) {
            // 3-column ACZ overview: Col1 = Categories, Col2 = Option previews, Col3 = Current values
            for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
                const bool sel = (i == curTab);
                leftHtml += "<div class=\"cat-row";
                if (sel) leftHtml += " selected";
                leftHtml += "\">" + EscapeHtml(CategoryDisplayName(tabs[i].title)) + "</div>";
            }
            for (const OptionItem& it : activeTab.items) {
                if (it.kind == ItemKind::SectionHeader) continue;
                midHtml   += "<div class=\"opt-row preview\">" + EscapeHtml(it.label) + "</div>";
                rightHtml += "<div class=\"opt-row preview\">&gt; " + EscapeHtml(FormatOptionValueShort(it)) + "</div>";
            }
            footerDesc = CategoryDescription(activeTab.title);
        } else {
            // Inside a category: Col1 = Options list, Col2 = Value choices / slider, Col3 = Scope badge
            for (int i = 0; i < static_cast<int>(activeTab.items.size()); ++i) {
                const OptionItem& it = activeTab.items[i];
                if (it.kind == ItemKind::SectionHeader) continue;
                const bool sel = (i == activeTab.selectedIdx);
                leftHtml += "<div class=\"opt-row";
                if (sel) leftHtml += " selected";
                leftHtml += "\"><span>" + EscapeHtml(it.label) + "</span>"
                         + "<span class=\"opt-val\">" + EscapeHtml(FormatOptionValueShort(it)) + "</span></div>";
            }

            if (activeTab.selectedIdx >= 0 && activeTab.selectedIdx < static_cast<int>(activeTab.items.size())) {
                const OptionItem& selIt = activeTab.items[activeTab.selectedIdx];
                footerDesc = selIt.description;

                if (selIt.kind == ItemKind::Toggle || selIt.kind == ItemKind::Choice) {
                    int val = selIt.getter ? selIt.getter() : 0;
                    for (int c = 0; c < static_cast<int>(selIt.choices.size()); ++c) {
                        const bool isAct = (c == val);
                        midHtml += "<div class=\"val-choice";
                        if (isAct) midHtml += " active";
                        midHtml += "\"><span>" + std::string(isAct ? "&gt; " : "   ") + EscapeHtml(selIt.choices[c]) + "</span></div>";
                    }
                } else if (selIt.kind == ItemKind::Slider) {
                    int val = selIt.getter ? selIt.getter() : selIt.minVal;
                    int pct = (selIt.maxVal > selIt.minVal)
                        ? ((val - selIt.minVal) * 100 / (selIt.maxVal - selIt.minVal))
                        : 0;
                    pct = (std::max)(0, (std::min)(100, pct));
                    char sbuf[256];
                    std::snprintf(sbuf, sizeof(sbuf),
                        "<div class=\"val-choice active\"><span>&lt; %d%s &gt;</span></div>"
                        "<div class=\"slider-wrap\"><div class=\"slider-track\">"
                        "<div class=\"slider-fill\" style=\"width: %d%%;\"></div></div></div>",
                        val, selIt.suffix.c_str(), pct);
                    midHtml += sbuf;
                } else if (selIt.kind == ItemKind::Info) {
                    midHtml += "<div class=\"val-choice active\"><span>" + EscapeHtml(FormatOptionValueShort(selIt)) + "</span></div>";
                }

                // Right column: apply scope badge
                switch (selIt.scope) {
                    case ApplyScope::Instant:
                        rightHtml += "<div class=\"badge instant\">INSTANT APPLY</div>";
                        break;
                    case ApplyScope::NextMission:
                        rightHtml += "<div class=\"badge mission\">NEXT MISSION</div>";
                        break;
                    case ApplyScope::RestartRequired:
                        rightHtml += "<div class=\"badge restart\">RESTART REQUIRED</div>";
                        break;
                    case ApplyScope::GameSave:
                        rightHtml += "<div class=\"badge\">GAME SAVE</div>";
                        break;
                    default: break;
                }
            }
        }

        if (auto* el = m_Doc->GetElementById("col-left"))    el->SetInnerRML(leftHtml);
        if (auto* el = m_Doc->GetElementById("col-mid"))     el->SetInnerRML(midHtml);
        if (auto* el = m_Doc->GetElementById("col-right"))   el->SetInnerRML(rightHtml);
        if (auto* el = m_Doc->GetElementById("footer-desc")) el->SetInnerRML(EscapeHtml(footerDesc));
    }

    bool CheckFilesModified() {
        std::error_code ec;
        auto t1 = std::filesystem::last_write_time(m_UiRoot / m_ActiveDocName, ec);
        auto t2 = std::filesystem::last_write_time(m_UiRoot / "acz_theme.rcss", ec);
        auto t3 = std::filesystem::last_write_time(m_UiRoot / "strings_en.ini", ec);
        if (t1 != m_RmlMtime || t2 != m_RcssMtime || t3 != m_StringsMtime) {
            m_RmlMtime     = t1;
            m_RcssMtime    = t2;
            m_StringsMtime = t3;
            return true;
        }
        return false;
    }

    void ReloadDocument(const std::string& docName = "") {
        if (!m_Context) return;
        if (!docName.empty()) m_ActiveDocName = docName;
        if (m_ActiveDocName.empty()) m_ActiveDocName = "main_menu.rml";
        if (m_Doc) {
            m_Doc->Close();
            m_Doc = nullptr;
        }
        Rml::ReleaseTextures();
        if (m_FontEngine) m_FontEngine->OnTexturesReleased();
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();

        std::error_code ec;
        m_RmlMtime     = std::filesystem::last_write_time(m_UiRoot / m_ActiveDocName, ec);
        m_RcssMtime    = std::filesystem::last_write_time(m_UiRoot / "acz_theme.rcss", ec);
        m_StringsMtime = std::filesystem::last_write_time(m_UiRoot / "strings_en.ini", ec);
        m_Strings.Load(m_UiRoot / "strings_en.ini");

        m_Doc = m_Context->LoadDocument(m_ActiveDocName);
        if (m_Doc) {
            m_Doc->Show();
            SyncSparksFromHtml();
            if (m_LastW > 0 && m_LastH > 0) {
                const float uiScale = (std::min)(static_cast<float>(m_LastW) / 1920.0f, static_cast<float>(m_LastH) / 1080.0f);
                const int virtW = (std::max)(640, static_cast<int>(std::lround(static_cast<float>(m_LastW) / uiScale)));
                const int virtH = (std::max)(480, static_cast<int>(std::lround(static_cast<float>(m_LastH) / uiScale)));
                if (m_RenderIface) m_RenderIface->SetUiScale(uiScale);
                ApplyAspectLayout(virtW, virtH, 1.0f);
            }
            if (m_LogFn) m_LogFn("[RmlUi] Loaded mods\\enhancer\\ui\\%s + acz_theme.rcss", m_ActiveDocName.c_str());
        } else if (m_LogFn) {
            m_LogFn("[RmlUi] WARNING: Failed to load mods\\enhancer\\ui\\%s.", m_ActiveDocName.c_str());
        }
        m_BuiltViewKey = -1;
        m_CursorInitialized = false;
        m_CursorHiddenForTrans = false;
        m_TransPhase = 0;
        m_TransTimer = 0.0f;
        m_FooterPending = false;
        m_DispConfirmDomDirty = true;
        m_DomDirty = true;
    }

    void SyncSparksFromHtml() {
        if (!m_Doc) { m_Sparks.Configure(0, 1.0f, Color(255, 72, 24, 255)); return; }
        auto* el = m_Doc->GetElementById("bg-sparks");
        if (!el) { m_Sparks.Configure(0, 1.0f, Color(255, 72, 24, 255)); return; }

        int count = el->GetAttribute<int>("data-count", 28);
        float speed = el->GetAttribute<float>("data-speed", 1.0f);
        std::string hex = el->GetAttribute<Rml::String>("data-color", "#ff4818");
        Color col(255, 72, 24, 255);
        if (hex.size() == 7 && hex[0] == '#') {
            unsigned int rgb = 0;
            if (std::sscanf(hex.c_str() + 1, "%x", &rgb) == 1) {
                col = Color(
                    static_cast<uint8_t>((rgb >> 16) & 0xFF),
                    static_cast<uint8_t>((rgb >>  8) & 0xFF),
                    static_cast<uint8_t>((rgb >>  0) & 0xFF),
                    255
                );
            }
        }
        m_Sparks.Configure(count, speed, col);
        if (m_RenderIface) {
            if (m_BgCrimsonSlot  != 0) { m_RenderIface->ReleaseTexture(static_cast<Rml::TextureHandle>(m_BgCrimsonSlot));  m_BgCrimsonSlot  = 0; }
            if (m_BgCommandSlot  != 0) { m_RenderIface->ReleaseTexture(static_cast<Rml::TextureHandle>(m_BgCommandSlot));  m_BgCommandSlot  = 0; }
            if (m_BgScanlineSlot != 0) { m_RenderIface->ReleaseTexture(static_cast<Rml::TextureHandle>(m_BgScanlineSlot)); m_BgScanlineSlot = 0; }
            Rml::Vector2i dims(0, 0);
            m_BgCrimsonSlot  = static_cast<uint32_t>(m_RenderIface->LoadTexture(dims, "assets/bg_crimson_grad.png"));
            dims = Rml::Vector2i(0, 0);
            m_BgCommandSlot  = static_cast<uint32_t>(m_RenderIface->LoadTexture(dims, "assets/bg_command_center.png"));
            m_BgCommandAspect = (dims.x > 0 && dims.y > 0) ? (static_cast<float>(dims.x) / static_cast<float>(dims.y)) : (16.0f / 9.0f);
            dims = Rml::Vector2i(0, 0);
            m_BgScanlineSlot = static_cast<uint32_t>(m_RenderIface->LoadTexture(dims, "assets/bg_scanline.png"));
        }
    }

    bool                                    m_Initialized = false;
    UiRenderer*                             m_Renderer    = nullptr;
    uintptr_t                               m_ExeBase     = 0;
    FnRva                                   m_RvaFn;
    FnLog                                   m_LogFn       = nullptr;
    std::filesystem::path                   m_UiRoot;
    std::string                             m_ActiveDocName = "main_menu.rml";
    std::unique_ptr<AczRmlSystemInterface>  m_SysIface;
    std::unique_ptr<AczRmlFileInterface>    m_FileIface;
    std::unique_ptr<AczRmlRenderInterface>  m_RenderIface;
    std::unique_ptr<AczRmlFontEngine>       m_FontEngine;
    Rml::Context*                           m_Context     = nullptr;
    Rml::ElementDocument*                   m_Doc         = nullptr;
    void*                                   m_SwgSoundObj = nullptr;

    bool                                    m_MainMenuMode      = false;
    bool                                    m_LeavingMainMenu   = false;
    float                                   m_IdleTimerSec      = 0.0f;
    int                                     m_MainDepth         = 0;
    int                                     m_LastRenderedDepth = 0;
    std::string                             m_FooterFullText;
    float                                   m_FooterRevealChars = 0.0f;
    size_t                                  m_FooterShownCount  = 0;
    bool                                    m_FooterPending     = false;
    std::string                             m_BcFullText;
    float                                   m_BcRevealChars     = 0.0f;
    size_t                                  m_BcShownCount      = 0;
    bool                                    m_BcPending         = false;
    bool                                    m_UseTwoCol         = false;
    std::string                             m_LayoutClass;
    bool                                    m_ScrollHasRange    = true;
    float                                   m_CursorY           = 0.0f;
    float                                   m_CursorTargetY     = 0.0f;
    int                                     m_CursorRowH        = 69;
    int                                     m_ActiveRowIdx      = 0;
    bool                                    m_CursorInitialized = false;
    bool                                    m_CursorInMid       = false;
    bool                                    m_CursorHiddenForTrans = false;
    float                                   m_ScrollPct         = 0.0f;
    float                                   m_ScrollTargetPct   = 0.0f;
    float                                   m_Col2Y             = 0.0f;
    float                                   m_Col2TargetY       = 0.0f;
    float                                   m_Col3Y             = 0.0f;
    float                                   m_Col3TargetY       = 0.0f;
    int                                     m_UiWidescreenOverride = 0;
    int64_t                                 m_BuiltViewKey      = -1;
    int                                     m_OptCatIdx         = 0;
    int                                     m_OptSubIdx         = 0;
    int                                     m_DispLeafIdx       = 0;
    int                                     m_EnhItemIdx        = 0;
    int                                     m_EnhChoiceIdx      = 0;
    int                                     m_StagedDispMode    = 0;
    int                                     m_StagedResIdx      = 0;
    bool                                    m_StagedDispSynced  = false;
    int                                     m_LastSeenActiveMode = -1;
    int                                     m_LastSeenActiveRes  = -1;
    std::vector<std::string>                m_CachedResChoices;
    int                                     m_LeafScrollTop     = 0;
    static constexpr int                    kLeafMaxVisibleRows = 8;
    enum class DispDialogMode : int {
        None      = 0,
        AskApply  = 1,
        KeepTimer = 2
    };
    DispDialogMode                          m_DispDialogMode      = DispDialogMode::None;
    float                                   m_DispConfirmTimer    = 0.0f;
    int                                     m_DispConfirmIdx      = 1;
    int                                     m_DispConfirmLastSec  = -1;
    bool                                    m_DispConfirmDomDirty = false;
    int                                     m_TransPhase        = 0;
    float                                   m_TransTimer        = 0.0f;
    float                                   m_FlyElapsed        = 0.0f;
    bool                                    m_Col2Done          = true;
    std::vector<std::string>                m_Col1Texts;
    std::vector<std::string>                m_Col2Texts;
    std::vector<std::string>                m_Col3Texts;
    std::vector<int>                        m_Col2Lags;
    std::vector<int>                        m_Col3Lags;
    float                                   m_Col1Reveal        = 0.0f;
    float                                   m_Col1Max           = 0.0f;
    size_t                                  m_Col1Shown         = 0;
    float                                   m_Col2Reveal        = 0.0f;
    float                                   m_Col2Max           = 0.0f;
    float                                   m_Col2MaxLag        = 0.0f;
    size_t                                  m_Col2Shown         = 0;
    float                                   m_Col3Reveal        = 0.0f;
    float                                   m_Col3Max           = 0.0f;
    float                                   m_Col3MaxLag        = 0.0f;
    size_t                                  m_Col3Shown         = 0;
    int                                     m_MainRootIdx    = 0;
    int                                     m_CampIdx        = 0;
    int                                     m_DiffIdx        = 2;
    int                                     m_CtrlIdx        = 0;
    int                                     m_VoiceIdx       = 0;
    int                                     m_SubIdx         = 1;
    int                                     m_DataIdx        = 0;
    bool                                    m_HasContinue    = false;
    bool                                    m_HasSP          = true;
    bool                                    m_HasFreeMission = true;
    int                                     m_ExitConfirmIdx = 0;
    bool                                    m_Unlock2P       = false;

    int                                     m_Depth       = 0;
    int                                     m_LastW       = 0;
    int                                     m_LastH       = 0;
    InputDeviceKind                         m_LastDev     = InputDeviceKind::PlayStation;
    bool                                    m_DomDirty    = true;
    bool                                    m_NeedDocReload = true;
    float                                   m_HotReloadTimer = 0.0f;
    std::filesystem::file_time_type         m_RmlMtime{};
    std::filesystem::file_time_type         m_RcssMtime{};
    std::filesystem::file_time_type         m_StringsMtime{};
    UiStringTable                           m_Strings;
    UiSparkSystem                           m_Sparks;
    uint32_t                                m_BgCrimsonSlot   = 0;
    uint32_t                                m_BgCommandSlot   = 0;
    float                                   m_BgCommandAspect = 16.0f / 9.0f;
    uint32_t                                m_BgScanlineSlot  = 0;
};

} // namespace acz_ui
