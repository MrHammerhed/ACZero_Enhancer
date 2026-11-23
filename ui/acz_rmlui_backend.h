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
            uv.x    = x;
            uv.y    = y;
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

    void EnableScissorRegion(bool enable) override {
        m_ScissorEnabled = enable;
        if (m_Renderer) {
            if (!enable) m_Renderer->SetScissorPx(false, 0, 0, 0, 0);
            else m_Renderer->SetScissorPx(true, m_ScissorRect.Left(), m_ScissorRect.Top(), m_ScissorRect.Right(), m_ScissorRect.Bottom());
        }
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
        m_ScissorRect = region;
        if (m_Renderer && m_ScissorEnabled) {
            m_Renderer->SetScissorPx(true, region.Left(), region.Top(), region.Right(), region.Bottom());
        }
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
    UiRenderer*           m_Renderer = nullptr;
    std::filesystem::path m_UiRoot;
    std::vector<UiVertex> m_TempVerts;
    bool                 m_ScissorEnabled = false;
    Rml::Rectanglei      m_ScissorRect = Rml::Rectanglei::MakeInvalid();
    bool                 m_HasTransform = false;
    Rml::Matrix4f        m_Transform = Rml::Matrix4f::Identity();
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
        (void)weight;
        if (m_Faces.empty()) return 0;
        if (size < 8) size = 8;
        if (size > 96) size = 96;

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

        const uint64_t key = (static_cast<uint64_t>(faceIdx) << 32) | static_cast<uint64_t>(size);
        auto it = m_HandleMap.find(key);
        if (it != m_HandleMap.end()) return it->second;

        HandleEntry he{};
        he.faceIdx = faceIdx;
        he.size    = size;
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
        (void)font_effects;
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
                const int base = static_cast<int>(tm.mesh.vertices.size());

                tm.mesh.vertices.push_back(Rml::Vertex{ {x0, y0}, colour, {g->u0, g->v0} });
                tm.mesh.vertices.push_back(Rml::Vertex{ {x1, y0}, colour, {g->u1, g->v0} });
                tm.mesh.vertices.push_back(Rml::Vertex{ {x1, y1}, colour, {g->u1, g->v1} });
                tm.mesh.vertices.push_back(Rml::Vertex{ {x0, y1}, colour, {g->u0, g->v1} });

                tm.mesh.indices.push_back(base + 0);
                tm.mesh.indices.push_back(base + 1);
                tm.mesh.indices.push_back(base + 2);
                tm.mesh.indices.push_back(base + 0);
                tm.mesh.indices.push_back(base + 2);
                tm.mesh.indices.push_back(base + 3);
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
        Rml::FontMetrics metrics{};
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
            if (m_AtlasSlot == 0) {
                // GenerateTexture calls AczRmlRenderInterface::GenerateTexture which creates a D3D12 slot
                if (!cti.GenerateTexture(
                        Rml::Span<const Rml::byte>(m_AtlasRgba.data(), m_AtlasRgba.size()),
                        Rml::Vector2i(static_cast<int>(kAtlasSize), static_cast<int>(kAtlasSize)))) {
                    return false;
                }
                // Note: slot index is tracked via m_LastCreatedSlot; instead, let's keep a direct mirror
            }
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

    const GlyphInfo* GetOrRasterize(int faceIdx, int pxHeight, uint32_t cp) {
        const uint64_t key = (static_cast<uint64_t>(faceIdx) << 44) |
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

        if (gw > 0 && gh > 0 && gw < static_cast<int>(kAtlasSize) - 4 && gh < static_cast<int>(kAtlasSize) - 4) {
            constexpr uint32_t kPad = 2;
            if (m_PackX + static_cast<uint32_t>(gw) + kPad >= kAtlasSize) {
                m_PackX = kPad;
                m_PackY += m_PackRowH + kPad;
                m_PackRowH = 0;
            }
            if (m_PackY + static_cast<uint32_t>(gh) + kPad >= kAtlasSize) {
                m_Glyphs.clear();
                std::memset(m_AtlasRgba.data(), 0, m_AtlasRgba.size());
                m_PackX = kPad;
                m_PackY = kPad;
                m_PackRowH = 0;
                ++m_AtlasVersion;
            }

            std::vector<uint8_t> tmp(static_cast<size_t>(gw) * gh, 0u);
            stbtt_MakeGlyphBitmap(&chosen->info, tmp.data(), gw, gh, gw, scale, scale, glyphIdx);

            // Pack into premultiplied RGBA8 (r=g=b=a=alpha) for RmlUi
            for (int row = 0; row < gh; ++row) {
                uint8_t* dst = m_AtlasRgba.data() + (static_cast<size_t>(m_PackY + row) * kAtlasSize + m_PackX) * 4u;
                const uint8_t* src = tmp.data() + static_cast<size_t>(row) * gw;
                for (int col = 0; col < gw; ++col) {
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
            gi.u1 = static_cast<float>(m_PackX + gw) * inv;
            gi.v1 = static_cast<float>(m_PackY + gh) * inv;
            gi.xOff = static_cast<float>(x0);
            gi.yOff = static_cast<float>(y0);
            gi.w    = static_cast<float>(gw);
            gi.h    = static_cast<float>(gh);
            gi.hasBitmap = true;

            m_PackX += static_cast<uint32_t>(gw) + kPad;
            if (static_cast<uint32_t>(gh) > m_PackRowH) m_PackRowH = static_cast<uint32_t>(gh);
            m_AtlasDirty = true;
            // Invalidate callback texture so RmlUi re-uploads the updated atlas cleanly
            if (static_cast<bool>(m_AtlasCallbackTex)) {
                m_AtlasCallbackTex.Release();
            }
        }

        auto res = m_Glyphs.emplace(key, gi);
        return &res.first->second;
    }

    UiRenderer*                              m_Renderer = nullptr;
    std::filesystem::path                    m_UiRoot;
    std::vector<FaceEntry>                   m_Faces;
    std::vector<HandleEntry>                 m_Handles;
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
    constexpr uintptr_t kIsContinue       = 0x001F0070;
    constexpr uintptr_t kIsFreeMission    = 0x001F00E0;
    constexpr uintptr_t kIsSP             = 0x001F0150;
    constexpr uintptr_t kIsLevel          = 0x001F01C0;
} // namespace NativeMainRva

inline void SafeSwgStartNewGameSEH(uintptr_t fnAddr, int difficulty, int isSp) {
    if (!fnAddr) return;
    __try {
        int64_t args[5] = { difficulty, isSp, 0, 0, 0 };
        using FnNewGame = void (__fastcall*)(void*, void*, void*, int64_t*);
        reinterpret_cast<FnNewGame>(fnAddr)(nullptr, nullptr, nullptr, args);
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

    void EnterMainMenuMode(OptionsScreen& screen) {
        m_MainMenuMode = true;
        m_MainDepth    = 0;
        m_MainRootIdx  = 0;
        m_CampIdx      = 0;
        m_DiffIdx      = 2; // NORMAL
        m_ExitConfirmIdx = 0; // NO
        if (m_ActiveDocName != "main_menu.rml") {
            m_ActiveDocName = "main_menu.rml";
            m_NeedDocReload = true;
        }
        screen.SetActiveTab(0);
        m_DomDirty = true;
    }

    void OnOpened(OptionsScreen& screen, int initialTab, bool fromSubmenu) {
        m_MainMenuMode = false;
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
        m_DomDirty = true;
    }

    // Update Main Menu 3-column HTML/CSS screen (when in MainSelect with Custom UI active)
    void UpdateMainMenu(OptionsScreen& screen, const UiInputSnapshot& in, float dtSec, bool f5Pressed, bool unlock2P) {
        m_Unlock2P = unlock2P;
        screen.TickToast(dtSec);
        m_Sparks.Update(dtSec);

        // Hot-Reload check (F5 or automatic file modification check every 0.5s)
        m_HotReloadTimer += dtSec;
        if (f5Pressed || m_HotReloadTimer >= 0.5f) {
            m_HotReloadTimer = 0.0f;
            if (f5Pressed || CheckFilesModified()) {
                ReloadDocument("main_menu.rml");
                screen.TriggerToast("UI RELOADED");
                m_DomDirty = true;
            }
        }

        auto& tabs = screen.GetTabs();
        const int tabCount = static_cast<int>(tabs.size());

        if (m_MainDepth == 0) {
            // Level 0: CAMPAIGN | VS MODE | OPTIONS | EXIT GAME
            constexpr int kRootCount = 4;
            if (in.up)   { m_MainRootIdx = (m_MainRootIdx - 1 + kRootCount) % kRootCount; m_DomDirty = true; }
            if (in.down) { m_MainRootIdx = (m_MainRootIdx + 1) % kRootCount; m_DomDirty = true; }
            if (in.confirm || in.right) {
                if (m_MainRootIdx == 1) {
                    // VS MODE
                    if (m_Unlock2P) {
                        SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartVS));
                    } else {
                        screen.TriggerToast("ENABLE 2P MODE IN OPTIONS > ENHANCER");
                        m_DomDirty = true;
                    }
                } else {
                    m_MainDepth = 1;
                    if (m_MainRootIdx == 0) m_CampIdx = 0;
                    if (m_MainRootIdx == 3) m_ExitConfirmIdx = 0;
                    m_DomDirty = true;
                }
            }
            return;
        }

        if (m_MainDepth == 1) {
            if (in.cancel || in.left) {
                if (m_MainRootIdx == 2) {
                    // Save any changed settings when leaving OPTIONS
                    screen.Close(false);
                }
                m_MainDepth = 0;
                m_DomDirty = true;
                return;
            }

            if (m_MainRootIdx == 0) {
                // CAMPAIGN submenu: CONTINUE | NEW GAME | FREE MISSION | FREE FLIGHT | CAMPAIGN DATA | SAVE | LOAD
                constexpr int kCampCount = 7;
                if (in.up)   { m_CampIdx = (m_CampIdx - 1 + kCampCount) % kCampCount; m_DomDirty = true; }
                if (in.down) { m_CampIdx = (m_CampIdx + 1) % kCampCount; m_DomDirty = true; }
                if (in.confirm || in.right) {
                    switch (m_CampIdx) {
                        case 0: // CONTINUE
                            if (SafeSwgGetIntSEH(ResolveMainRva(NativeMainRva::kIsContinue), 1) != 0) {
                                SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartContinue));
                            } else {
                                screen.TriggerToast("NO SAVE DATA");
                                m_DomDirty = true;
                            }
                            break;
                        case 1: // NEW GAME -> difficulty selection
                            m_MainDepth = 2;
                            m_DomDirty = true;
                            break;
                        case 2: // FREE MISSION
                            if (SafeSwgGetIntSEH(ResolveMainRva(NativeMainRva::kIsFreeMission), 1) != 0) {
                                SafeSwgSetIntSEH(ResolveMainRva(NativeMainRva::kStartFreeMission), 0);
                            } else {
                                screen.TriggerToast("CLEAR MISSION 1 TO UNLOCK");
                                m_DomDirty = true;
                            }
                            break;
                        case 3: // FREE FLIGHT
                            if (SafeSwgGetIntSEH(ResolveMainRva(NativeMainRva::kIsFreeMission), 1) != 0) {
                                SafeSwgSetIntSEH(ResolveMainRva(NativeMainRva::kStartFreeMission), 1);
                            } else {
                                screen.TriggerToast("CLEAR MISSION 1 TO UNLOCK");
                                m_DomDirty = true;
                            }
                            break;
                        case 4: // CAMPAIGN DATA / ASSAULT RECORDS
                            SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartAceStyle));
                            break;
                        case 5: // SAVE
                            SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartSave));
                            break;
                        case 6: // LOAD
                            SafeSwgCallVoidSEH(ResolveMainRva(NativeMainRva::kStartLoad));
                            break;
                    }
                }
            } else if (m_MainRootIdx == 2 && tabCount > 0) {
                // OPTIONS categories
                int curTab = screen.GetActiveTab();
                if (in.up)   { screen.SetActiveTab((curTab - 1 + tabCount) % tabCount); m_DomDirty = true; }
                if (in.down) { screen.SetActiveTab((curTab + 1) % tabCount); m_DomDirty = true; }
                if (in.confirm || in.right) {
                    m_MainDepth = 2;
                    tabs[screen.GetActiveTab()].EnsureValidSelection();
                    m_DomDirty = true;
                }
            } else if (m_MainRootIdx == 3) {
                // EXIT GAME confirmation (NO / YES)
                if (in.up || in.down) { m_ExitConfirmIdx = 1 - m_ExitConfirmIdx; m_DomDirty = true; }
                if (in.confirm) {
                    if (m_ExitConfirmIdx == 1) {
                        HWND fg = GetForegroundWindow();
                        if (fg) PostMessageW(fg, WM_CLOSE, 0, 0);
                        else ExitProcess(0);
                    } else {
                        m_MainDepth = 0;
                        m_DomDirty = true;
                    }
                }
            }
            return;
        }

        if (m_MainDepth == 2) {
            if (in.cancel) {
                m_MainDepth = 1;
                m_DomDirty = true;
                return;
            }
            if (m_MainRootIdx == 0 && m_CampIdx == 1) {
                // NEW GAME difficulty selection: VERY EASY..ACE
                constexpr int kDiffCount = 6;
                if (in.up)   { m_DiffIdx = (m_DiffIdx - 1 + kDiffCount) % kDiffCount; m_DomDirty = true; }
                if (in.down) { m_DiffIdx = (m_DiffIdx + 1) % kDiffCount; m_DomDirty = true; }
                if (in.confirm) {
                    SafeSwgStartNewGameSEH(ResolveMainRva(NativeMainRva::kStartNewGame), m_DiffIdx, 0);
                }
            } else if (m_MainRootIdx == 2 && tabCount > 0) {
                // Editing options inside active category
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
                        }
                    }
                }
            }
        }
    }

    // Handles 3-column ACZ navigation (depth 0 = category list, depth 1 = option list)
    bool Update(OptionsScreen& screen, const UiInputSnapshot& in, float dtSec, bool f5Pressed) {
        if (!screen.IsOpen()) return false;

        screen.TickToast(dtSec);

        // Hot-Reload check (F5 or automatic file modification check every 0.5s)
        m_HotReloadTimer += dtSec;
        if (f5Pressed || m_HotReloadTimer >= 0.5f) {
            m_HotReloadTimer = 0.0f;
            if (f5Pressed || CheckFilesModified()) {
                ReloadDocument("options.rml");
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
            } else if (in.confirm || in.right) {
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

        if (w != m_LastW || h != m_LastH) {
            m_LastW = w;
            m_LastH = h;
            m_Context->SetDimensions(Rml::Vector2i(w, h));
            m_Context->SetDensityIndependentPixelRatio(static_cast<float>(h) / 1080.0f);
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

        // Authentic PS2 GS background compositing for Main Menu:
        //   1. Exact ACZ crimson base with subtle living firelight oscillation
        //   2. Blue Command Center overlaid ADDITIVELY (0.083 = 21/255 on untouched entry_001)
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
                m_Renderer->SetAdditiveBlend(true);
                // Exact measured MainSelect.swa additive blend factor (~0.083 = 21/255 on untouched entry_001)
                m_Renderer->DrawImage(0.0f, 0.0f, vw, vh, m_BgCommandSlot, Color(16, 21, 22, 255));
                m_Renderer->SetAdditiveBlend(false);
            }
        }

        m_Renderer->SetPremultipliedBlend(true);
        m_Context->Update();
        m_Context->Render();
        m_Renderer->SetPremultipliedBlend(false);
        if (m_MainMenuMode) {
            m_Sparks.Render(*m_Renderer);
        }
    }

private:
    uintptr_t ResolveMainRva(uintptr_t rva1021) const {
        if (!m_ExeBase || !m_RvaFn) return 0;
        return m_ExeBase + m_RvaFn(rva1021);
    }

    void RebuildMainMenuDom(OptionsScreen& screen, InputDeviceKind dev) {
        if (!m_Doc) return;

        if (auto* el = m_Doc->GetElementById("cap-ok")) {
            el->SetInnerRML(dev == InputDeviceKind::Keyboard ? "ENTER" : (dev == InputDeviceKind::Xbox ? "A" : "X"));
        }
        if (auto* el = m_Doc->GetElementById("cap-cancel")) {
            el->SetInnerRML(dev == InputDeviceKind::Keyboard ? "ESC" : (dev == InputDeviceKind::Xbox ? "B" : "O"));
        }
        if (auto* el = m_Doc->GetElementById("cap-change")) {
            el->SetInnerRML(dev == InputDeviceKind::Keyboard ? "LT/RT" : "D-PAD");
        }

        static const char* kRootNames[4] = { "CAMPAIGN", "VS MODE", "OPTIONS", "EXIT GAME" };
        static const char* kRootDescs[4] = {
            "Play through the game following a storyline.",
            "Compete head-to-head against a second player in 2P VS Mode.",
            "Configure game controls, audio, display, and ACZero Enhancer settings.",
            "Exit Ace Combat Zero: The Belkan War to Windows."
        };
        static const char* kCampNames[7] = {
            "CONTINUE", "NEW GAME", "FREE MISSION", "FREE FLIGHT", "CAMPAIGN DATA", "SAVE", "LOAD"
        };
        static const char* kCampDescs[7] = {
            "Resume your current campaign from the last saved mission.",
            "Start a brand new campaign and choose your difficulty level.",
            "Replay any unlocked campaign mission with any owned aircraft.",
            "Fly freely over unlocked mission maps without enemy threats.",
            "View Assault Records, Ace Style, Medals, and Scene Viewer.",
            "Save your current progress to the save file.",
            "Load previously saved campaign progress."
        };
        static const char* kDiffNames[6] = { "VERY EASY", "EASY", "NORMAL", "HARD", "EXPERT", "ACE" };

        auto& tabs = screen.GetTabs();
        std::string bc = "MAIN MENU";
        std::string leftHtml, midHtml, rightHtml, footerDesc;

        if (m_MainDepth == 0) {
            for (int i = 0; i < 4; ++i) {
                leftHtml += "<div class=\"cat-row";
                if (i == m_MainRootIdx) leftHtml += " selected";
                leftHtml += "\"><span>" + std::string(kRootNames[i]) + "</span></div>";
            }
            footerDesc = kRootDescs[m_MainRootIdx];

            if (m_MainRootIdx == 0) {
                for (int i = 0; i < 7; ++i) {
                    midHtml += "<div class=\"opt-row preview\"><span>" + std::string(kCampNames[i]) + "</span></div>";
                }
                const bool hasSave = SafeSwgGetIntSEH(ResolveMainRva(NativeMainRva::kIsContinue), 1) != 0;
                rightHtml += "<div class=\"opt-row preview\"><span>" + std::string(hasSave ? "SAVE READY" : "NO SAVE") + "</span></div>";
            } else if (m_MainRootIdx == 1) {
                midHtml += "<div class=\"opt-row preview\"><span>2-PLAYER SPLIT MODE</span></div>";
                rightHtml += "<div class=\"badge " + std::string(m_Unlock2P ? "instant" : "mission") + "\"><span>"
                          + std::string(m_Unlock2P ? "UNLOCKED" : "LOCKED (1P)") + "</span></div>";
            } else if (m_MainRootIdx == 2) {
                for (const auto& t : tabs) {
                    midHtml += "<div class=\"opt-row preview\"><span>" + EscapeHtml(CategoryDisplayName(t.title)) + "</span></div>";
                }
            } else if (m_MainRootIdx == 3) {
                midHtml += "<div class=\"opt-row preview\"><span>QUIT TO DESKTOP</span></div>";
            }
        } else if (m_MainDepth == 1) {
            bc += " &gt; " + std::string(kRootNames[m_MainRootIdx]);
            if (m_MainRootIdx == 0) {
                for (int i = 0; i < 7; ++i) {
                    leftHtml += "<div class=\"cat-row";
                    if (i == m_CampIdx) leftHtml += " selected";
                    leftHtml += "\"><span>" + std::string(kCampNames[i]) + "</span></div>";
                }
                footerDesc = kCampDescs[m_CampIdx];
                if (m_CampIdx == 1) {
                    for (int d = 0; d < 6; ++d) {
                        midHtml += "<div class=\"val-choice" + std::string(d == m_DiffIdx ? " active" : "") + "\"><span>"
                                + std::string(d == m_DiffIdx ? "&gt; " : "   ") + kDiffNames[d] + "</span></div>";
                    }
                }
            } else if (m_MainRootIdx == 2 && !tabs.empty()) {
                const int curTab = screen.GetActiveTab();
                for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
                    leftHtml += "<div class=\"cat-row";
                    if (i == curTab) leftHtml += " selected";
                    leftHtml += "\"><span>" + EscapeHtml(CategoryDisplayName(tabs[i].title)) + "</span></div>";
                }
                for (const OptionItem& it : tabs[curTab].items) {
                    if (it.kind == ItemKind::SectionHeader) continue;
                    midHtml   += "<div class=\"opt-row preview\"><span>" + EscapeHtml(it.label) + "</span></div>";
                    rightHtml += "<div class=\"opt-row preview\"><span>&gt; " + EscapeHtml(FormatOptionValueShort(it)) + "</span></div>";
                }
                footerDesc = CategoryDescription(tabs[curTab].title);
            } else if (m_MainRootIdx == 3) {
                leftHtml += "<div class=\"cat-row" + std::string(m_ExitConfirmIdx == 0 ? " selected" : "") + "\"><span>NO - RETURN TO MENU</span></div>";
                leftHtml += "<div class=\"cat-row" + std::string(m_ExitConfirmIdx == 1 ? " selected" : "") + "\"><span>YES - EXIT GAME</span></div>";
                footerDesc = "Are you sure you want to exit Ace Combat Zero?";
            }
        } else if (m_MainDepth == 2) {
            if (m_MainRootIdx == 0 && m_CampIdx == 1) {
                bc += " &gt; CAMPAIGN &gt; NEW GAME";
                for (int d = 0; d < 6; ++d) {
                    leftHtml += "<div class=\"cat-row" + std::string(d == m_DiffIdx ? " selected" : "") + "\"><span>" + kDiffNames[d] + "</span></div>";
                }
                footerDesc = "Select campaign difficulty level and press OK to begin.";
            } else if (m_MainRootIdx == 2 && !tabs.empty()) {
                OptionTab& activeTab = tabs[screen.GetActiveTab()];
                bc += " &gt; OPTIONS &gt; " + EscapeHtml(CategoryDisplayName(activeTab.title));
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
                            midHtml += "<div class=\"val-choice" + std::string(isAct ? " active" : "") + "\"><span>"
                                    + std::string(isAct ? "&gt; " : "   ") + EscapeHtml(selIt.choices[c]) + "</span></div>";
                        }
                    } else if (selIt.kind == ItemKind::Slider) {
                        int val = selIt.getter ? selIt.getter() : selIt.minVal;
                        int pct = (selIt.maxVal > selIt.minVal) ? ((val - selIt.minVal) * 100 / (selIt.maxVal - selIt.minVal)) : 0;
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
                    switch (selIt.scope) {
                        case ApplyScope::Instant:         rightHtml += "<div class=\"badge instant\"><span>INSTANT APPLY</span></div>"; break;
                        case ApplyScope::NextMission:     rightHtml += "<div class=\"badge mission\"><span>NEXT MISSION</span></div>"; break;
                        case ApplyScope::RestartRequired: rightHtml += "<div class=\"badge restart\"><span>RESTART REQUIRED</span></div>"; break;
                        case ApplyScope::GameSave:        rightHtml += "<div class=\"badge\"><span>GAME SAVE</span></div>"; break;
                        default: break;
                    }
                }
            }
        }

        if (auto* el = m_Doc->GetElementById("breadcrumb-text")) el->SetInnerRML(bc);
        if (auto* el = m_Doc->GetElementById("toast-msg"))       el->SetInnerRML(screen.GetStatusToastTimer() > 0.0f ? EscapeHtml(screen.GetStatusToast()) : "");
        if (auto* el = m_Doc->GetElementById("col-left"))        el->SetInnerRML(leftHtml);
        if (auto* el = m_Doc->GetElementById("col-mid"))         el->SetInnerRML(midHtml);
        if (auto* el = m_Doc->GetElementById("col-right"))       el->SetInnerRML(rightHtml);
        if (auto* el = m_Doc->GetElementById("footer-desc"))     el->SetInnerRML(EscapeHtml(footerDesc));
    }
    static std::string EscapeHtml(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
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
        if (auto* el = m_Doc->GetElementById("cap-ok")) {
            el->SetInnerRML(dev == InputDeviceKind::Keyboard ? "ENTER" : (dev == InputDeviceKind::Xbox ? "A" : "X"));
        }
        if (auto* el = m_Doc->GetElementById("cap-cancel")) {
            el->SetInnerRML(dev == InputDeviceKind::Keyboard ? "ESC" : (dev == InputDeviceKind::Xbox ? "B" : "O"));
        }
        if (auto* el = m_Doc->GetElementById("cap-change")) {
            el->SetInnerRML(dev == InputDeviceKind::Keyboard ? "LT/RT" : "D-PAD");
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
        if (t1 != m_RmlMtime || t2 != m_RcssMtime) {
            m_RmlMtime  = t1;
            m_RcssMtime = t2;
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
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();

        std::error_code ec;
        m_RmlMtime  = std::filesystem::last_write_time(m_UiRoot / m_ActiveDocName, ec);
        m_RcssMtime = std::filesystem::last_write_time(m_UiRoot / "acz_theme.rcss", ec);

        m_Doc = m_Context->LoadDocument(m_ActiveDocName);
        if (m_Doc) {
            m_Doc->Show();
            SyncSparksFromHtml();
            if (m_LogFn) m_LogFn("[RmlUi] Loaded mods\\enhancer\\ui\\%s + acz_theme.rcss", m_ActiveDocName.c_str());
        } else if (m_LogFn) {
            m_LogFn("[RmlUi] WARNING: Failed to load mods\\enhancer\\ui\\%s.", m_ActiveDocName.c_str());
        }
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
            if (m_BgCrimsonSlot != 0) { m_RenderIface->ReleaseTexture(static_cast<Rml::TextureHandle>(m_BgCrimsonSlot)); m_BgCrimsonSlot = 0; }
            if (m_BgCommandSlot != 0) { m_RenderIface->ReleaseTexture(static_cast<Rml::TextureHandle>(m_BgCommandSlot)); m_BgCommandSlot = 0; }
            Rml::Vector2i dims(0, 0);
            m_BgCrimsonSlot = static_cast<uint32_t>(m_RenderIface->LoadTexture(dims, "assets/bg_crimson_grad.png"));
            m_BgCommandSlot = static_cast<uint32_t>(m_RenderIface->LoadTexture(dims, "assets/bg_command_center.png"));
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

    bool                                    m_MainMenuMode   = false;
    int                                     m_MainDepth      = 0;
    int                                     m_MainRootIdx    = 0;
    int                                     m_CampIdx        = 0;
    int                                     m_DiffIdx        = 2;
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
    UiSparkSystem                           m_Sparks;
    uint32_t                                m_BgCrimsonSlot = 0;
    uint32_t                                m_BgCommandSlot = 0;
};

} // namespace acz_ui
