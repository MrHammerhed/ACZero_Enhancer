#pragma once
// ============================================================================
// screen_memcheck.h - Custom D3D12 Memory Card / System Data Check Screen
// Replaces legacy MemCheck.swa (CModeWakeMemorycard) with crisp TTF rendering,
// authentic ACZ minimal black-and-gold frame, and 4:3 / 16:9 / 21:9 support.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "acz_ui_renderer.h"
#include "acz_ui_font.h"

namespace acz_ui {

class MemCheckScreen {
public:
    void Begin() {
        if (!m_Active) {
            m_Active     = true;
            m_ElapsedSec = 0.0f;
            m_State      = 0u;
        }
    }

    void Reset() {
        m_Active     = false;
        m_ElapsedSec = 0.0f;
        m_State      = 0u;
    }

    bool IsActive() const { return m_Active; }

    void SetMemoryCardState(uint32_t state) {
        m_State = state;
    }

    void Update(float dtSec) {
        if (!m_Active) return;
        m_ElapsedSec += (std::max)(0.0f, (std::min)(dtSec, 0.1f));
    }

    void Render(UiRenderer& r, UiFontManager& fonts) {
        if (!m_Active) return;

        const float vw = r.VirtualWidth();
        const float vh = r.VirtualHeight(); // 1080.0f

        const float fadeIn = Clamp01(m_ElapsedSec / 0.14f);

        // 1. Pure black backdrop (matches original MemCheck.swa)
        r.DrawRectFilled(0.0f, 0.0f, vw, vh, Color(0, 0, 0, 255));

        // 2. Authentic ACZ MemCheck box (classic 3:1 proportions, olive-gold frame on pure black)
        const float panelW = 900.0f;
        const float panelH = 270.0f;
        const float px     = (vw - panelW) * 0.5f;
        const float py     = (vh - panelH) * 0.5f;

        // Pure black interior
        r.DrawRectFilled(px, py, panelW, panelH, Color(0, 0, 0, 255));

        // Authentic ACZ olive-gold outer frame + subtle inner hairline
        r.DrawRectOutline(px, py, panelW, panelH, 3.0f,
                          Color(156, 138, 36, ToByte(fadeIn * 0.92f)));
        r.DrawRectOutline(px + 6.0f, py + 6.0f, panelW - 12.0f, panelH - 12.0f, 1.0f,
                          Color(112, 98, 26, ToByte(fadeIn * 0.55f)));

        // 3. Determine status copy from CSelectMemorycard state (0x30F2C4)
        const bool isSuccess = (m_State == 2u || m_State == 6u);
        const bool isSaving  = (m_State == 3u || m_State == 4u);

        const char* line1 = "Checking save data...";
        const char* line2 = "Do not close the game while accessing save data.";
        if (isSuccess) {
            line1 = "Load complete.";
            line2 = "";
        } else if (isSaving) {
            line1 = "Saving data...";
            line2 = "Do not close the game while accessing save data.";
        }

        // 4. Clean centered ACZ typography
        const float centerX = px + panelW * 0.5f;
        const float line1Y  = (*line2 != '\0') ? (py + 92.0f) : (py + 116.0f);

        fonts.DrawText(
            r, FontRole::Body, 30.0f,
            centerX, line1Y,
            line1,
            Color(238, 238, 232, ToByte(fadeIn * 0.98f)),
            TextAlign::Center, true, 0.4f
        );

        if (*line2 != '\0') {
            fonts.DrawText(
                r, FontRole::Body, 22.0f,
                centerX, py + 148.0f,
                line2,
                Color(186, 184, 168, ToByte(fadeIn * 0.90f)),
                TextAlign::Center, false, 0.25f
            );
        }

        fonts.SyncGpuAtlas(r);
    }

private:
    static float Clamp01(float v) {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
    static uint8_t ToByte(float v) {
        const float c = Clamp01(v) * 255.0f;
        return static_cast<uint8_t>(c + 0.5f);
    }

    bool     m_Active     = false;
    float    m_ElapsedSec = 0.0f;
    uint32_t m_State      = 0u;
};

} // namespace acz_ui
