#pragma once
// ============================================================================
// ACZero Enhancer — D3D12 2D UI Renderer (ui/acz_ui_renderer.h)
// Self-contained, zero-link-dependency D3D12 2D batched renderer injected
// directly into acz.exe's active frame command list before EndFrameAndPresent.
// Designed for all 4 UI stages (Options/Enhancer, MainSelect/Pause, All Menus,
// and Gameplay HUD).
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

#include "acz_ui_shaders.h"

namespace acz_ui {

// Inline COM IIDs to avoid dxguid.lib / d3d12.lib link dependencies
static const GUID kIID_ID3D12Resource       = { 0x696442BE, 0xA72E, 0x4059, { 0xBC, 0x79, 0x5B, 0x5C, 0x98, 0x04, 0x0F, 0xAD } };
static const GUID kIID_ID3D12RootSignature  = { 0xC54A6B66, 0x72DF, 0x4EE8, { 0x8B, 0xE5, 0xA9, 0x46, 0xA1, 0x42, 0x92, 0x14 } };
static const GUID kIID_ID3D12PipelineState  = { 0x765A30F3, 0xF624, 0x4C6F, { 0xA8, 0x28, 0xAC, 0xE9, 0x48, 0x62, 0x24, 0x45 } };
static const GUID kIID_ID3D12DescriptorHeap = { 0x8EFB471D, 0x616C, 0x4F49, { 0x90, 0xF7, 0x12, 0x7B, 0xB7, 0x63, 0xFA, 0x51 } };

struct Color {
    uint8_t r, g, b, a;
    constexpr Color() : r(255), g(255), b(255), a(255) {}
    constexpr Color(uint8_t rr, uint8_t gg, uint8_t bb, uint8_t aa = 255) : r(rr), g(gg), b(bb), a(aa) {}
    static constexpr Color FromRGBA(uint32_t rgba) {
        return Color(
            static_cast<uint8_t>((rgba >> 24) & 0xFF),
            static_cast<uint8_t>((rgba >> 16) & 0xFF),
            static_cast<uint8_t>((rgba >>  8) & 0xFF),
            static_cast<uint8_t>((rgba >>  0) & 0xFF)
        );
    }
    constexpr Color WithAlpha(uint8_t aa) const { return Color(r, g, b, aa); }
    constexpr Color ScaleAlpha(float s) const {
        int v = static_cast<int>(static_cast<float>(a) * s);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return Color(r, g, b, static_cast<uint8_t>(v));
    }
};

struct UiVertex {
    float    x, y;
    float    u, v;
    float    r, g, b, a;
    uint32_t mode; // 0 = RGBA multiply, 1 = R8 glyph alpha
};

struct UiBatch {
    uint32_t   indexOffset;
    uint32_t   indexCount;
    uint32_t   texSlot;
    D3D12_RECT scissor;
    uint8_t    blendMode; // 0 = alpha, 1 = premultiplied, 2 = additive bloom
};

struct UiTextureSlot {
    ID3D12Resource* gpuTex         = nullptr;
    ID3D12Resource* uploadBuf      = nullptr;
    uint8_t*        uploadPtr      = nullptr;
    uint32_t        width          = 0;
    uint32_t        height         = 0;
    uint32_t        rowPitch       = 0;
    uint32_t        bpp            = 4;
    DXGI_FORMAT     format         = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool            initialized    = false;
    bool            dirty          = false;
    bool            inUse          = false;
    uint64_t        freeAfterFrame = 0;
};

struct UiDeferredRelease {
    ID3D12Resource* resource    = nullptr;
    uint64_t        retireFrame = 0;
};

class UiRenderer {
public:
    static constexpr uint32_t kMaxTextures   = 256;
    static constexpr uint32_t kMaxVertices   = 65536;
    static constexpr uint32_t kMaxIndices    = 131072;
    static constexpr uint32_t kTexSlotWhite  = 0;

    bool IsReady() const { return m_Initialized; }
    void InvalidateBackbuffers() {
        m_CachedBackbuffers[0] = nullptr;
        m_CachedBackbuffers[1] = nullptr;
    }
    uint32_t ScreenWidth()  const { return m_Width; }
    uint32_t ScreenHeight() const { return m_Height; }
    float VirtualScale() const { return (m_Height > 0) ? (static_cast<float>(m_Height) / 1080.0f) : 1.0f; }
    float VirtualWidth() const { return (m_Height > 0) ? (1080.0f * static_cast<float>(m_Width) / static_cast<float>(m_Height)) : 1920.0f; }
    float VirtualHeight() const { return 1080.0f; }

    // Call once per frame from Hook_EndFrameAndPresent before recording commands.
    // Returns false if D3D12 device/resources are not ready.
    bool BeginFrame(ID3D12Device* device, ID3D12Resource* backbuffer, uint32_t backbufIdx) {
        if (!device || !backbuffer) return false;
        backbufIdx &= 1u;

        D3D12_RESOURCE_DESC desc = backbuffer->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.Width == 0 || desc.Height == 0) {
            return false;
        }

        const uint32_t w = static_cast<uint32_t>(desc.Width);
        const uint32_t h = desc.Height;

        if (!m_Initialized || m_Device != device || m_RtvFormat != desc.Format) {
            if (!InitializeDeviceResources(device, desc.Format)) {
                return false;
            }
        }

        // Update cached RTV descriptor if backbuffer pointer or dimensions changed (SwapChain resize)
        if (m_CachedBackbuffers[backbufIdx] != backbuffer || m_Width != w || m_Height != h) {
            m_CachedBackbuffers[backbufIdx] = backbuffer;
            m_Width  = w;
            m_Height = h;
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtvHandle.ptr += static_cast<SIZE_T>(backbufIdx) * m_RtvDescriptorSize;
            device->CreateRenderTargetView(backbuffer, nullptr, rtvHandle);
        }

        ++m_FrameNumber;
        if (!m_DeferredReleases.empty()) {
            size_t writeIdx = 0;
            for (size_t i = 0; i < m_DeferredReleases.size(); ++i) {
                if (m_FrameNumber >= m_DeferredReleases[i].retireFrame) {
                    if (m_DeferredReleases[i].resource) {
                        m_DeferredReleases[i].resource->Release();
                    }
                } else {
                    if (writeIdx != i) m_DeferredReleases[writeIdx] = m_DeferredReleases[i];
                    ++writeIdx;
                }
            }
            m_DeferredReleases.resize(writeIdx);
        }

        m_CurrentFrameSlot = backbufIdx;
        m_Vertices.clear();
        m_Indices.clear();
        m_Batches.clear();
        m_ClipStack.clear();

        D3D12_RECT fullRect{ 0, 0, static_cast<LONG>(m_Width), static_cast<LONG>(m_Height) };
        m_ClipStack.push_back(fullRect);
        return true;
    }

    // Create or resize a dynamic GPU texture slot (e.g. R8_UNORM font atlas or RGBA8 icon).
    // Returns slot index (>= 1), or 0 on failure.
    uint32_t CreateDynamicTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t bpp) {
        if (!m_Device || width == 0 || height == 0) return 0;
        for (uint32_t i = 1; i < kMaxTextures; ++i) {
            if (!m_Textures[i].inUse && m_FrameNumber >= m_Textures[i].freeAfterFrame) {
                if (AllocateTextureSlot(i, width, height, format, bpp)) {
                    m_LastCreatedSlot = i;
                    return i;
                }
                return 0;
            }
        }
        return 0;
    }

    uint32_t LastCreatedTextureSlot() const { return m_LastCreatedSlot; }

    // Upload raw pixels into a dynamic texture slot (queued for GPU copy at EndFrame).
    bool UpdateTexturePixels(uint32_t slot, const uint8_t* pixels, uint32_t srcRowPitch) {
        if (slot >= kMaxTextures || !m_Textures[slot].inUse || !m_Textures[slot].uploadPtr || !pixels) {
            return false;
        }
        UiTextureSlot& tex = m_Textures[slot];
        const uint32_t copyBytes = tex.width * tex.bpp;
        for (uint32_t y = 0; y < tex.height; ++y) {
            std::memcpy(
                tex.uploadPtr + static_cast<size_t>(y) * tex.rowPitch,
                pixels + static_cast<size_t>(y) * srcRowPitch,
                copyBytes
            );
        }
        tex.dirty = true;
        return true;
    }

    void ReleaseDynamicTexture(uint32_t slot) {
        if (slot == 0 || slot >= kMaxTextures) return;
        UiTextureSlot& tex = m_Textures[slot];
        if (!tex.inUse) return;
        const uint64_t retireFrame = m_FrameNumber + 4u;
        if (tex.uploadBuf) {
            tex.uploadBuf->Unmap(0, nullptr);
            m_DeferredReleases.push_back(UiDeferredRelease{ tex.uploadBuf, retireFrame });
            tex.uploadBuf = nullptr;
            tex.uploadPtr = nullptr;
        }
        if (tex.gpuTex) {
            m_DeferredReleases.push_back(UiDeferredRelease{ tex.gpuTex, retireFrame });
            tex.gpuTex = nullptr;
        }
        tex.inUse          = false;
        tex.dirty          = false;
        tex.initialized    = false;
        tex.freeAfterFrame = retireFrame;
    }

    void SetPremultipliedBlend(bool enable) { m_ActiveBlendMode = enable ? 1u : 0u; }
    void SetAdditiveBlend(bool enable)      { m_ActiveBlendMode = enable ? 2u : 0u; }
    void SetAdditiveAlphaBlend(bool enable) { m_ActiveBlendMode = enable ? 3u : 0u; }

    void SetScissorPx(bool enable, int left, int top, int right, int bottom) {
        D3D12_RECT r;
        if (!enable) {
            r = D3D12_RECT{ 0, 0, static_cast<LONG>(m_Width), static_cast<LONG>(m_Height) };
        } else {
            r.left   = (std::max)(0L, static_cast<LONG>(left));
            r.top    = (std::max)(0L, static_cast<LONG>(top));
            r.right  = (std::min)(static_cast<LONG>(m_Width),  static_cast<LONG>(right));
            r.bottom = (std::min)(static_cast<LONG>(m_Height), static_cast<LONG>(bottom));
            if (r.right < r.left) r.right = r.left;
            if (r.bottom < r.top) r.bottom = r.top;
        }
        if (m_ClipStack.empty()) m_ClipStack.push_back(r);
        else m_ClipStack.back() = r;
    }

    // Raw pixel-space indexed triangle mesh submission (used by RmlUi RenderInterface)
    void DrawRawIndexedMeshPx(
        const UiVertex* verts, uint32_t vCount,
        const uint32_t* indices, uint32_t iCount,
        uint32_t texSlot
    ) {
        if (!verts || !indices || vCount == 0 || iCount == 0) return;
        if (m_Vertices.size() + vCount > kMaxVertices || m_Indices.size() + iCount > kMaxIndices) return;
        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
        m_Vertices.insert(m_Vertices.end(), verts, verts + vCount);

        const D3D12_RECT& clip = m_ClipStack.back();
        if (m_Batches.empty() || m_Batches.back().texSlot != texSlot ||
            m_Batches.back().blendMode != m_ActiveBlendMode || !RectEqual(m_Batches.back().scissor, clip)) {
            UiBatch b{};
            b.indexOffset = static_cast<uint32_t>(m_Indices.size());
            b.indexCount  = 0;
            b.texSlot     = texSlot;
            b.scissor     = clip;
            b.blendMode   = m_ActiveBlendMode;
            m_Batches.push_back(b);
        }
        for (uint32_t i = 0; i < iCount; ++i) {
            m_Indices.push_back(base + indices[i]);
        }
        m_Batches.back().indexCount += iCount;
    }

    // Clipping stack (in virtual 1080p-based coordinates)
    void PushClipRect(float vx, float vy, float vw, float vh) {
        const float s = VirtualScale();
        D3D12_RECT r;
        r.left   = static_cast<LONG>(std::floor(vx * s));
        r.top    = static_cast<LONG>(std::floor(vy * s));
        r.right  = static_cast<LONG>(std::ceil((vx + vw) * s));
        r.bottom = static_cast<LONG>(std::ceil((vy + vh) * s));
        const D3D12_RECT& cur = m_ClipStack.back();
        r.left   = (std::max)(r.left,   cur.left);
        r.top    = (std::max)(r.top,    cur.top);
        r.right  = (std::min)(r.right,  cur.right);
        r.bottom = (std::min)(r.bottom, cur.bottom);
        if (r.right < r.left) r.right = r.left;
        if (r.bottom < r.top) r.bottom = r.top;
        m_ClipStack.push_back(r);
    }

    void PopClipRect() {
        if (m_ClipStack.size() > 1) {
            m_ClipStack.pop_back();
        }
    }

    // ========================================================================
    // 2D Drawing Primitives (all coordinates in Virtual 1080p space)
    // ========================================================================
    void DrawRectFilled(float vx, float vy, float vw, float vh, Color c) {
        const float s = VirtualScale();
        AddQuadPx(vx * s, vy * s, vw * s, vh * s, 0.0f, 0.0f, 1.0f, 1.0f, c, c, c, c, kTexSlotWhite, 0u);
    }

    void DrawRectGradientV(float vx, float vy, float vw, float vh, Color topCol, Color botCol) {
        const float s = VirtualScale();
        AddQuadPx(vx * s, vy * s, vw * s, vh * s, 0.0f, 0.0f, 1.0f, 1.0f, topCol, topCol, botCol, botCol, kTexSlotWhite, 0u);
    }

    void DrawRectGradientH(float vx, float vy, float vw, float vh, Color leftCol, Color rightCol) {
        const float s = VirtualScale();
        AddQuadPx(vx * s, vy * s, vw * s, vh * s, 0.0f, 0.0f, 1.0f, 1.0f, leftCol, rightCol, rightCol, leftCol, kTexSlotWhite, 0u);
    }

    void DrawRectOutline(float vx, float vy, float vw, float vh, float thickness, Color c) {
        DrawRectFilled(vx, vy, vw, thickness, c);
        DrawRectFilled(vx, vy + vh - thickness, vw, thickness, c);
        DrawRectFilled(vx, vy + thickness, thickness, vh - 2.0f * thickness, c);
        DrawRectFilled(vx + vw - thickness, vy + thickness, thickness, vh - 2.0f * thickness, c);
    }

    void DrawCornerBrackets(float vx, float vy, float vw, float vh, float armLen, float thickness, Color c) {
        // Top-left
        DrawRectFilled(vx, vy, armLen, thickness, c);
        DrawRectFilled(vx, vy, thickness, armLen, c);
        // Top-right
        DrawRectFilled(vx + vw - armLen, vy, armLen, thickness, c);
        DrawRectFilled(vx + vw - thickness, vy, thickness, armLen, c);
        // Bottom-left
        DrawRectFilled(vx, vy + vh - thickness, armLen, thickness, c);
        DrawRectFilled(vx, vy + vh - armLen, thickness, armLen, c);
        // Bottom-right
        DrawRectFilled(vx + vw - armLen, vy + vh - thickness, armLen, thickness, c);
        DrawRectFilled(vx + vw - thickness, vy + vh - armLen, thickness, armLen, c);
    }

    void DrawLine(float vx0, float vy0, float vx1, float vy1, float thickness, Color c) {
        const float s = VirtualScale();
        const float x0 = vx0 * s, y0 = vy0 * s;
        const float x1 = vx1 * s, y1 = vy1 * s;
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) return;
        const float halfT = (thickness * s) * 0.5f;
        const float nx = (-dy / len) * halfT;
        const float ny = ( dx / len) * halfT;
        AddArbitraryQuadPx(
            x0 + nx, y0 + ny,
            x1 + nx, y1 + ny,
            x1 - nx, y1 - ny,
            x0 - nx, y0 - ny,
            c, kTexSlotWhite, 0u
        );
    }

    void DrawGlyphPx(
        float px, float py, float pw, float ph,
        float u0, float v0, float u1, float v1,
        Color c, uint32_t texSlot
    ) {
        AddQuadPx(px, py, pw, ph, u0, v0, u1, v1, c, c, c, c, texSlot, 1u);
    }

    void DrawImage(
        float vx, float vy, float vw, float vh,
        uint32_t texSlot, Color tint = Color(255, 255, 255, 255),
        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f
    ) {
        const float s = VirtualScale();
        AddQuadPx(vx * s, vy * s, vw * s, vh * s, u0, v0, u1, v1, tint, tint, tint, tint, texSlot, 0u);
    }

    // Rotated textured quad centered at (vcx, vcy) in virtual 1080p space (for sparks, HUD reticles, needles)
    void DrawRotatedImage(
        float vcx, float vcy, float vw, float vh, float angleRad,
        uint32_t texSlot, Color tint = Color(255, 255, 255, 255),
        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f
    ) {
        if (m_Vertices.size() + 4 > kMaxVertices || m_Indices.size() + 6 > kMaxIndices) return;
        const float s = VirtualScale();
        const float cx = vcx * s, cy = vcy * s;
        const float hw = (vw * s) * 0.5f;
        const float hh = (vh * s) * 0.5f;
        const float ca = std::cos(angleRad);
        const float sa = std::sin(angleRad);

        const float rx = ca * hw, ry = sa * hw;
        const float ux = -sa * hh, uy = ca * hh;

        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
        const float r = tint.r / 255.0f, g = tint.g / 255.0f, b = tint.b / 255.0f, a = tint.a / 255.0f;
        m_Vertices.push_back(UiVertex{ cx - rx - ux, cy - ry - uy, u0, v0, r, g, b, a, 0u });
        m_Vertices.push_back(UiVertex{ cx + rx - ux, cy + ry - uy, u1, v0, r, g, b, a, 0u });
        m_Vertices.push_back(UiVertex{ cx + rx + ux, cy + ry + uy, u1, v1, r, g, b, a, 0u });
        m_Vertices.push_back(UiVertex{ cx - rx + ux, cy - ry + uy, u0, v1, r, g, b, a, 0u });
        PushQuadIndices(base, texSlot);
    }

    // 3D Y-axis perspective-projected image strip (28 vertical slices for artifact-free perspective UV!)
    // Positive yawRad: right edge (u=1) is CLOSER to camera (larger); negative yawRad: left edge (u=0) is closer.
    void DrawPerspectiveImageY(
        float vcx, float vcy, float vw, float vh,
        float yawRad, float focalLen,
        uint32_t texSlot, Color tint = Color(255, 255, 255, 255),
        int slices = 28
    ) {
        if (slices < 2) slices = 2;
        if (slices > 48) slices = 48;
        const uint32_t vNeed = static_cast<uint32_t>((slices + 1) * 2);
        const uint32_t iNeed = static_cast<uint32_t>(slices * 6);
        if (m_Vertices.size() + vNeed > kMaxVertices || m_Indices.size() + iNeed > kMaxIndices) return;

        const float s    = VirtualScale();
        const float cxPx = vcx * s;
        const float cyPx = vcy * s;
        const float halfW = vw * 0.5f;
        const float halfH = vh * 0.5f;
        const float ca   = std::cos(yawRad);
        const float sa   = std::sin(yawRad);
        const float fLen = (focalLen > 200.0f) ? focalLen : 1100.0f;

        const float r = tint.r / 255.0f, g = tint.g / 255.0f, b = tint.b / 255.0f, a = tint.a / 255.0f;
        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());

        for (int i = 0; i <= slices; ++i) {
            const float u      = static_cast<float>(i) / static_cast<float>(slices);
            const float localX = (u - 0.5f) * vw;
            const float rotX   = localX * ca;
            // Positive yawRad -> localX > 0 (right edge) moves closer (smaller Z -> larger projection)
            float z = fLen - localX * sa;
            if (z < 120.0f) z = 120.0f;
            const float persp = fLen / z;
            const float xPx   = cxPx + rotX * persp * s;
            const float hhPx  = halfH * persp * s;

            m_Vertices.push_back(UiVertex{ xPx, cyPx - hhPx, u, 0.0f, r, g, b, a, 0u });
            m_Vertices.push_back(UiVertex{ xPx, cyPx + hhPx, u, 1.0f, r, g, b, a, 0u });
        }

        const D3D12_RECT& clip = m_ClipStack.back();
        if (m_Batches.empty() || m_Batches.back().texSlot != texSlot ||
            m_Batches.back().blendMode != m_ActiveBlendMode || !RectEqual(m_Batches.back().scissor, clip)) {
            UiBatch batch{};
            batch.indexOffset = static_cast<uint32_t>(m_Indices.size());
            batch.indexCount  = 0;
            batch.texSlot     = texSlot;
            batch.scissor     = clip;
            batch.blendMode   = m_ActiveBlendMode;
            m_Batches.push_back(batch);
        }

        for (int i = 0; i < slices; ++i) {
            const uint32_t c0 = base + static_cast<uint32_t>(i * 2);
            const uint32_t c1 = c0 + 2u;
            m_Indices.push_back(c0);
            m_Indices.push_back(c1);
            m_Indices.push_back(c1 + 1u);
            m_Indices.push_back(c0);
            m_Indices.push_back(c1 + 1u);
            m_Indices.push_back(c0 + 1u);
        }
        m_Batches.back().indexCount += iNeed;
    }

    // Procedural real-time burning ember quad (pixel shader mode == 2, color = {heat, blur, seed, alpha})
    void DrawEmberQuad(
        float vcx, float vcy, float vw, float vh, float angleRad,
        float heat, float blur, float seed01, float alpha
    ) {
        if (m_Vertices.size() + 4 > kMaxVertices || m_Indices.size() + 6 > kMaxIndices) return;
        const float s = VirtualScale();
        const float cx = vcx * s, cy = vcy * s;
        const float hw = (vw * s) * 0.5f;
        const float hh = (vh * s) * 0.5f;
        const float ca = std::cos(angleRad);
        const float sa = std::sin(angleRad);

        const float rx = ca * hw, ry = sa * hw;
        const float ux = -sa * hh, uy = ca * hh;

        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
        m_Vertices.push_back(UiVertex{ cx - rx - ux, cy - ry - uy, 0.0f, 0.0f, heat, blur, seed01, alpha, 2u });
        m_Vertices.push_back(UiVertex{ cx + rx - ux, cy + ry - uy, 1.0f, 0.0f, heat, blur, seed01, alpha, 2u });
        m_Vertices.push_back(UiVertex{ cx + rx + ux, cy + ry + uy, 1.0f, 1.0f, heat, blur, seed01, alpha, 2u });
        m_Vertices.push_back(UiVertex{ cx - rx + ux, cy - ry + uy, 0.0f, 1.0f, heat, blur, seed01, alpha, 2u });
        PushQuadIndices(base, kTexSlotWhite);
    }

    // 3-layer soft-blurred procedural ACZ tactical grid (pixel shader mode == 3, resolution-independent)
    void DrawTacticalGrid(float majorAlpha, float minorAlpha, float diagAlpha, float overallAlpha = 1.0f) {
        if (m_Vertices.size() + 4 > kMaxVertices || m_Indices.size() + 6 > kMaxIndices) return;
        const float wPx = static_cast<float>(m_Width);
        const float hPx = static_cast<float>(m_Height);
        const float vw  = VirtualWidth();
        const float vh  = VirtualHeight();
        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
        m_Vertices.push_back(UiVertex{ 0.0f, 0.0f, 0.0f, 0.0f, majorAlpha, minorAlpha, diagAlpha, overallAlpha, 3u });
        m_Vertices.push_back(UiVertex{ wPx,  0.0f, vw,   0.0f, majorAlpha, minorAlpha, diagAlpha, overallAlpha, 3u });
        m_Vertices.push_back(UiVertex{ wPx,  hPx,  vw,   vh,   majorAlpha, minorAlpha, diagAlpha, overallAlpha, 3u });
        m_Vertices.push_back(UiVertex{ 0.0f, hPx,  0.0f, vh,   majorAlpha, minorAlpha, diagAlpha, overallAlpha, 3u });
        PushQuadIndices(base, kTexSlotWhite);
    }

    // Procedural pure optical Gaussian + anamorphic bloom quad (pixel shader mode == 5, use SetAdditiveAlphaBlend)
    void DrawOpticalBloom(float vcx, float vcy, float vw, float vh, Color c) {
        const float s = VirtualScale();
        AddQuadPx((vcx - vw * 0.5f) * s, (vcy - vh * 0.5f) * s, vw * s, vh * s,
                  0.0f, 0.0f, 1.0f, 1.0f, c, c, c, c, kTexSlotWhite, 5u);
    }

    // Procedural Belkan crimson clouds + vanilla forward-zooming mist clouds (pixel shader mode == 4)
    void DrawTitleBackdrop(float timeSec, float cloudIntro, float logoFlash, float masterFade) {
        if (m_Vertices.size() + 4 > kMaxVertices || m_Indices.size() + 6 > kMaxIndices) return;
        const float wPx    = static_cast<float>(m_Width);
        const float hPx    = static_cast<float>(m_Height);
        const float aspect = (hPx > 0.0f) ? (wPx / hPx) : (16.0f / 9.0f);
        const float halfU  = aspect * 0.5f;
        const float halfV  = 0.5f;
        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
        m_Vertices.push_back(UiVertex{ 0.0f, 0.0f, -halfU, -halfV, timeSec, cloudIntro, logoFlash, masterFade, 4u });
        m_Vertices.push_back(UiVertex{ wPx,  0.0f,  halfU, -halfV, timeSec, cloudIntro, logoFlash, masterFade, 4u });
        m_Vertices.push_back(UiVertex{ wPx,  hPx,   halfU,  halfV, timeSec, cloudIntro, logoFlash, masterFade, 4u });
        m_Vertices.push_back(UiVertex{ 0.0f, hPx,  -halfU,  halfV, timeSec, cloudIntro, logoFlash, masterFade, 4u });
        PushQuadIndices(base, kTexSlotWhite);
    }

    // Flush all queued 2D batches into acz.exe's active command list.
    void EndFrame(ID3D12GraphicsCommandList* clist) {
        if (!m_Initialized || !clist || m_Indices.empty() || m_Batches.empty()) {
            return;
        }

        // 1. Upload any dirty textures (white 1x1, font atlases, newly loaded icons)
        for (uint32_t i = 0; i < kMaxTextures; ++i) {
            UiTextureSlot& tex = m_Textures[i];
            if (!tex.inUse || !tex.dirty || !tex.gpuTex || !tex.uploadBuf) continue;

            if (tex.initialized) {
                D3D12_RESOURCE_BARRIER b{};
                b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource   = tex.gpuTex;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
                clist->ResourceBarrier(1, &b);
            }

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource        = tex.gpuTex;
            dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource                          = tex.uploadBuf;
            src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset             = 0;
            src.PlacedFootprint.Footprint.Format   = tex.format;
            src.PlacedFootprint.Footprint.Width    = tex.width;
            src.PlacedFootprint.Footprint.Height   = tex.height;
            src.PlacedFootprint.Footprint.Depth    = 1;
            src.PlacedFootprint.Footprint.RowPitch = tex.rowPitch;

            clist->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            D3D12_RESOURCE_BARRIER bDone{};
            bDone.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bDone.Transition.pResource   = tex.gpuTex;
            bDone.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            bDone.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            bDone.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            clist->ResourceBarrier(1, &bDone);

            tex.initialized = true;
            tex.dirty       = false;
        }

        // 2. Copy vertices & indices into the current frame's upload ring slot
        const uint32_t slot = m_CurrentFrameSlot;
        const uint32_t vCount = (std::min)(static_cast<uint32_t>(m_Vertices.size()), kMaxVertices);
        const uint32_t iCount = (std::min)(static_cast<uint32_t>(m_Indices.size()), kMaxIndices);
        if (vCount == 0 || iCount == 0) return;

        std::memcpy(m_VbPtr[slot], m_Vertices.data(), vCount * sizeof(UiVertex));
        std::memcpy(m_IbPtr[slot], m_Indices.data(), iCount * sizeof(uint32_t));

        // 3. Bind backbuffer RTV + our PSO/RootSig/DescriptorHeap
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += static_cast<SIZE_T>(slot) * m_RtvDescriptorSize;
        clist->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        D3D12_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width    = static_cast<float>(m_Width);
        vp.Height   = static_cast<float>(m_Height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        clist->RSSetViewports(1, &vp);

        clist->SetPipelineState(m_Pso);
        clist->SetGraphicsRootSignature(m_RootSig);

        ID3D12DescriptorHeap* heaps[1] = { m_SrvHeap };
        clist->SetDescriptorHeaps(1, heaps);

        float constants[4] = {
            2.0f / static_cast<float>(m_Width),
            2.0f / static_cast<float>(m_Height),
            0.0f,
            0.0f
        };
        clist->SetGraphicsRoot32BitConstants(0, 4, constants, 0);

        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = m_Vb[slot]->GetGPUVirtualAddress();
        vbv.StrideInBytes  = sizeof(UiVertex);
        vbv.SizeInBytes    = vCount * sizeof(UiVertex);

        D3D12_INDEX_BUFFER_VIEW ibv{};
        ibv.BufferLocation = m_Ib[slot]->GetGPUVirtualAddress();
        ibv.SizeInBytes    = iCount * sizeof(uint32_t);
        ibv.Format         = DXGI_FORMAT_R32_UINT;

        clist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        clist->IASetVertexBuffers(0, 1, &vbv);
        clist->IASetIndexBuffer(&ibv);

        D3D12_GPU_DESCRIPTOR_HANDLE srvBase = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
        uint32_t boundTex = 0xFFFFFFFFu;
        uint8_t  boundBlend = 0u;

        for (const UiBatch& b : m_Batches) {
            if (b.indexCount == 0) continue;
            if (b.scissor.right <= b.scissor.left || b.scissor.bottom <= b.scissor.top) continue;

            if (b.blendMode != boundBlend) {
                ID3D12PipelineState* pso = m_Pso;
                if (b.blendMode == 1u && m_PsoPremul) pso = m_PsoPremul;
                else if (b.blendMode == 2u && m_PsoAdditive) pso = m_PsoAdditive;
                else if (b.blendMode == 3u && m_PsoAdditiveAlpha) pso = m_PsoAdditiveAlpha;
                clist->SetPipelineState(pso);
                boundBlend = b.blendMode;
            }
            if (b.texSlot != boundTex) {
                D3D12_GPU_DESCRIPTOR_HANDLE h = srvBase;
                h.ptr += static_cast<UINT64>(b.texSlot) * m_SrvDescriptorSize;
                clist->SetGraphicsRootDescriptorTable(1, h);
                boundTex = b.texSlot;
            }
            clist->RSSetScissorRects(1, &b.scissor);
            clist->DrawIndexedInstanced(b.indexCount, 1, b.indexOffset, 0, 0);
        }
    }

private:
    static bool RectEqual(const D3D12_RECT& a, const D3D12_RECT& b) {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    }

    void PushQuadIndices(uint32_t baseVertex, uint32_t texSlot) {
        const D3D12_RECT& clip = m_ClipStack.back();
        if (m_Batches.empty() || m_Batches.back().texSlot != texSlot ||
            m_Batches.back().blendMode != m_ActiveBlendMode || !RectEqual(m_Batches.back().scissor, clip)) {
            UiBatch b{};
            b.indexOffset = static_cast<uint32_t>(m_Indices.size());
            b.indexCount  = 0;
            b.texSlot     = texSlot;
            b.scissor     = clip;
            b.blendMode   = m_ActiveBlendMode;
            m_Batches.push_back(b);
        }
        m_Indices.push_back(baseVertex + 0);
        m_Indices.push_back(baseVertex + 1);
        m_Indices.push_back(baseVertex + 2);
        m_Indices.push_back(baseVertex + 0);
        m_Indices.push_back(baseVertex + 2);
        m_Indices.push_back(baseVertex + 3);
        m_Batches.back().indexCount += 6;
    }

    void AddQuadPx(
        float x, float y, float w, float h,
        float u0, float v0, float u1, float v1,
        Color c0, Color c1, Color c2, Color c3,
        uint32_t texSlot, uint32_t mode
    ) {
        if (m_Vertices.size() + 4 > kMaxVertices || m_Indices.size() + 6 > kMaxIndices) return;
        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
        auto mk = [mode](float px, float py, float tu, float tv, Color c) -> UiVertex {
            return UiVertex{ px, py, tu, tv, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f, mode };
        };
        m_Vertices.push_back(mk(x,     y,     u0, v0, c0));
        m_Vertices.push_back(mk(x + w, y,     u1, v0, c1));
        m_Vertices.push_back(mk(x + w, y + h, u1, v1, c2));
        m_Vertices.push_back(mk(x,     y + h, u0, v1, c3));
        PushQuadIndices(base, texSlot);
    }

    void AddArbitraryQuadPx(
        float x0, float y0, float x1, float y1,
        float x2, float y2, float x3, float y3,
        Color c, uint32_t texSlot, uint32_t mode
    ) {
        if (m_Vertices.size() + 4 > kMaxVertices || m_Indices.size() + 6 > kMaxIndices) return;
        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
        const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f, a = c.a / 255.0f;
        m_Vertices.push_back(UiVertex{ x0, y0, 0.0f, 0.0f, r, g, b, a, mode });
        m_Vertices.push_back(UiVertex{ x1, y1, 1.0f, 0.0f, r, g, b, a, mode });
        m_Vertices.push_back(UiVertex{ x2, y2, 1.0f, 1.0f, r, g, b, a, mode });
        m_Vertices.push_back(UiVertex{ x3, y3, 0.0f, 1.0f, r, g, b, a, mode });
        PushQuadIndices(base, texSlot);
    }

    bool AllocateTextureSlot(uint32_t slot, uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t bpp) {
        UiTextureSlot& tex = m_Textures[slot];
        if (tex.uploadBuf) { tex.uploadBuf->Unmap(0, nullptr); tex.uploadBuf->Release(); tex.uploadBuf = nullptr; tex.uploadPtr = nullptr; }
        if (tex.gpuTex)    { tex.gpuTex->Release(); tex.gpuTex = nullptr; }

        const uint32_t rowPitch = (width * bpp + 255u) & ~255u;
        const uint64_t totalBytes = static_cast<uint64_t>(rowPitch) * static_cast<uint64_t>(height);

        D3D12_HEAP_PROPERTIES defHeap{};
        defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width            = width;
        texDesc.Height           = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels        = 1;
        texDesc.Format           = format;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        if (FAILED(m_Device->CreateCommittedResource(
                &defHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                kIID_ID3D12Resource, reinterpret_cast<void**>(&tex.gpuTex)))) {
            return false;
        }

        D3D12_HEAP_PROPERTIES upHeap{};
        upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width            = totalBytes;
        bufDesc.Height           = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels        = 1;
        bufDesc.Format           = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(m_Device->CreateCommittedResource(
                &upHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                kIID_ID3D12Resource, reinterpret_cast<void**>(&tex.uploadBuf)))) {
            tex.gpuTex->Release(); tex.gpuTex = nullptr;
            return false;
        }

        D3D12_RANGE emptyRange{ 0, 0 };
        if (FAILED(tex.uploadBuf->Map(0, &emptyRange, reinterpret_cast<void**>(&tex.uploadPtr)))) {
            tex.uploadBuf->Release(); tex.uploadBuf = nullptr;
            tex.gpuTex->Release();    tex.gpuTex = nullptr;
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                  = format;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
        srvHandle.ptr += static_cast<SIZE_T>(slot) * m_SrvDescriptorSize;
        m_Device->CreateShaderResourceView(tex.gpuTex, &srvDesc, srvHandle);

        tex.width       = width;
        tex.height      = height;
        tex.rowPitch    = rowPitch;
        tex.bpp         = bpp;
        tex.format      = format;
        tex.initialized = false;
        tex.dirty       = false;
        tex.inUse       = true;
        return true;
    }

    bool InitializeDeviceResources(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
        HMODULE hD3D12 = GetModuleHandleW(L"d3d12.dll");
        if (!hD3D12) return false;
        using FnSerializeRootSig = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
        auto pSerializeRootSig = reinterpret_cast<FnSerializeRootSig>(GetProcAddress(hD3D12, "D3D12SerializeRootSignature"));
        if (!pSerializeRootSig) return false;

        m_Device    = device;
        m_RtvFormat = rtvFormat;

        // 1. RTV Heap (2 slots for double-buffered swapchain)
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 2;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, kIID_ID3D12DescriptorHeap, reinterpret_cast<void**>(&m_RtvHeap)))) {
            return false;
        }
        m_RtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // 2. Shader-visible SRV Heap (64 texture slots)
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
        srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.NumDescriptors = kMaxTextures;
        srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&srvHeapDesc, kIID_ID3D12DescriptorHeap, reinterpret_cast<void**>(&m_SrvHeap)))) {
            return false;
        }
        m_SrvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // 3. Root Signature: [0] 4 root constants (b0), [1] SRV table (t0), static linear sampler (s0)
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 1;
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace  = 0;
        params[0].Constants.Num32BitValues = 4;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        sampler.MaxLOD           = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister   = 0;
        sampler.RegisterSpace    = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 2;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &sampler;
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* rsBlob = nullptr;
        ID3DBlob* errBlob = nullptr;
        HRESULT hr = pSerializeRootSig(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &errBlob);
        if (errBlob) errBlob->Release();
        if (FAILED(hr) || !rsBlob) return false;

        hr = device->CreateRootSignature(
            0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
            kIID_ID3D12RootSignature, reinterpret_cast<void**>(&m_RootSig)
        );
        rsBlob->Release();
        if (FAILED(hr)) return false;

        // 4. Graphics PSO (standard alpha blending, no depth, no cull)
        D3D12_INPUT_ELEMENT_DESC inputLayout[4] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32_UINT,           0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature        = m_RootSig;
        psoDesc.VS                    = { kUiQuadVS, sizeof(kUiQuadVS) };
        psoDesc.PS                    = { kUiQuadPS, sizeof(kUiQuadPS) };
        psoDesc.InputLayout           = { inputLayout, 4 };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets      = 1;
        psoDesc.RTVFormats[0]         = rtvFormat;
        psoDesc.SampleDesc.Count      = 1;
        psoDesc.SampleMask            = 0xFFFFFFFFu;

        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = FALSE;

        psoDesc.DepthStencilState.DepthEnable   = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;

        auto& rtBlend = psoDesc.BlendState.RenderTarget[0];
        rtBlend.BlendEnable           = TRUE;
        rtBlend.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
        rtBlend.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
        rtBlend.BlendOp               = D3D12_BLEND_OP_ADD;
        rtBlend.SrcBlendAlpha         = D3D12_BLEND_ONE;
        rtBlend.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
        rtBlend.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, kIID_ID3D12PipelineState, reinterpret_cast<void**>(&m_Pso)))) {
            return false;
        }

        // Premultiplied-alpha PSO for RmlUi HTML/CSS rendering
        rtBlend.SrcBlend       = D3D12_BLEND_ONE;
        rtBlend.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
        rtBlend.SrcBlendAlpha  = D3D12_BLEND_ONE;
        rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        device->CreateGraphicsPipelineState(&psoDesc, kIID_ID3D12PipelineState, reinterpret_cast<void**>(&m_PsoPremul));

        // Additive-glow PSO (ONE + ONE for premultiplied / black-background textures)
        rtBlend.SrcBlend       = D3D12_BLEND_ONE;
        rtBlend.DestBlend      = D3D12_BLEND_ONE;
        rtBlend.SrcBlendAlpha  = D3D12_BLEND_ZERO;
        rtBlend.DestBlendAlpha = D3D12_BLEND_ONE;
        device->CreateGraphicsPipelineState(&psoDesc, kIID_ID3D12PipelineState, reinterpret_cast<void**>(&m_PsoAdditive));

        // Straight-alpha additive PSO (SRC_ALPHA + ONE for PNGs/glyphs with transparent pixels)
        rtBlend.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
        rtBlend.DestBlend      = D3D12_BLEND_ONE;
        rtBlend.SrcBlendAlpha  = D3D12_BLEND_ZERO;
        rtBlend.DestBlendAlpha = D3D12_BLEND_ONE;
        device->CreateGraphicsPipelineState(&psoDesc, kIID_ID3D12PipelineState, reinterpret_cast<void**>(&m_PsoAdditiveAlpha));

        // 5. Double-buffered Upload Vertex & Index Buffers
        D3D12_HEAP_PROPERTIES upHeap{};
        upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RANGE emptyRange{ 0, 0 };

        for (int s = 0; s < 2; ++s) {
            D3D12_RESOURCE_DESC vbDesc{};
            vbDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            vbDesc.Width            = static_cast<UINT64>(kMaxVertices) * sizeof(UiVertex);
            vbDesc.Height           = 1;
            vbDesc.DepthOrArraySize = 1;
            vbDesc.MipLevels        = 1;
            vbDesc.SampleDesc.Count = 1;
            vbDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(
                    &upHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    kIID_ID3D12Resource, reinterpret_cast<void**>(&m_Vb[s])))) {
                return false;
            }
            if (FAILED(m_Vb[s]->Map(0, &emptyRange, reinterpret_cast<void**>(&m_VbPtr[s])))) return false;

            D3D12_RESOURCE_DESC ibDesc = vbDesc;
            ibDesc.Width = static_cast<UINT64>(kMaxIndices) * sizeof(uint32_t);
            if (FAILED(device->CreateCommittedResource(
                    &upHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    kIID_ID3D12Resource, reinterpret_cast<void**>(&m_Ib[s])))) {
                return false;
            }
            if (FAILED(m_Ib[s]->Map(0, &emptyRange, reinterpret_cast<void**>(&m_IbPtr[s])))) return false;
        }

        // 6. Slot 0: 1x1 solid white texture
        if (!AllocateTextureSlot(kTexSlotWhite, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 4)) {
            return false;
        }
        uint32_t whitePixel = 0xFFFFFFFFu;
        UpdateTexturePixels(kTexSlotWhite, reinterpret_cast<const uint8_t*>(&whitePixel), 4);

        m_Vertices.reserve(4096);
        m_Indices.reserve(6144);
        m_Batches.reserve(64);
        m_Initialized = true;
        return true;
    }

    bool                         m_Initialized = false;
    ID3D12Device*                m_Device      = nullptr;
    DXGI_FORMAT                  m_RtvFormat   = DXGI_FORMAT_UNKNOWN;
    ID3D12DescriptorHeap*        m_RtvHeap     = nullptr;
    ID3D12DescriptorHeap*        m_SrvHeap     = nullptr;
    UINT                         m_RtvDescriptorSize = 0;
    UINT                         m_SrvDescriptorSize = 0;
    ID3D12RootSignature*         m_RootSig     = nullptr;
    ID3D12PipelineState*         m_Pso              = nullptr;
    ID3D12PipelineState*         m_PsoPremul        = nullptr;
    ID3D12PipelineState*         m_PsoAdditive      = nullptr;
    ID3D12PipelineState*         m_PsoAdditiveAlpha = nullptr;
    uint8_t                      m_ActiveBlendMode = 0u;
    ID3D12Resource*              m_Vb[2]       = { nullptr, nullptr };
    ID3D12Resource*              m_Ib[2]       = { nullptr, nullptr };
    UiVertex*                    m_VbPtr[2]    = { nullptr, nullptr };
    uint32_t*                    m_IbPtr[2]    = { nullptr, nullptr };
    ID3D12Resource*              m_CachedBackbuffers[2] = { nullptr, nullptr };
    uint32_t                     m_CurrentFrameSlot = 0;
    uint32_t                     m_Width       = 0;
    uint32_t                     m_Height      = 0;
    uint32_t                     m_LastCreatedSlot = 0;
    uint64_t                     m_FrameNumber     = 0;

    UiTextureSlot                m_Textures[kMaxTextures]{};
    std::vector<UiDeferredRelease> m_DeferredReleases;
    std::vector<UiVertex>        m_Vertices;
    std::vector<uint32_t>        m_Indices;
    std::vector<UiBatch>         m_Batches;
    std::vector<D3D12_RECT>      m_ClipStack;
};

} // namespace acz_ui
