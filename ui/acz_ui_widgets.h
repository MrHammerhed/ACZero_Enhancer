#pragma once
// ============================================================================
// ACZero Enhancer — Declarative UI Theme, Input & Widgets (ui/acz_ui_widgets.h)
// Authentic Ace Combat Zero visual language rendered in native resolution via
// UiRenderer + UiFontManager. Supports Gamepad + Keyboard (no mouse cursor),
// automatic layout across 4:3 / 16:9 / 21:9 / 32:9, and reusable widgets.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

#include "acz_ui_renderer.h"
#include "acz_ui_font.h"

namespace acz_ui {

// ============================================================================
// ACZ Visual Theme Palette (Belkan War Tactical Blue / Steel / Cyan / Amber)
// ============================================================================
namespace Theme {
    constexpr Color kBackdropDim       = Color(  4,   9,  18, 178);
    constexpr Color kPanelBgTop        = Color(  8,  18,  34, 232);
    constexpr Color kPanelBgBot        = Color(  5,  11,  22, 238);
    constexpr Color kHeaderBgTop       = Color( 12,  28,  52, 240);
    constexpr Color kHeaderBgBot       = Color(  7,  16,  32, 240);
    constexpr Color kBorderDim         = Color( 78, 130, 175, 115);
    constexpr Color kBorderBright      = Color(120, 210, 255, 220);
    constexpr Color kAccentCyan        = Color( 80, 210, 255, 255);
    constexpr Color kAccentAmber       = Color(255, 196,  64, 255);
    constexpr Color kAccentGreen       = Color( 75, 230, 145, 255);
    constexpr Color kAccentRed         = Color(255,  96,  86, 255);
    constexpr Color kRowHoverLeft      = Color( 42, 135, 205, 118);
    constexpr Color kRowHoverRight     = Color( 20,  70, 115,  24);
    constexpr Color kRowStripe         = Color( 18,  34,  58,  52);
    constexpr Color kTextPrimary       = Color(238, 246, 255, 255);
    constexpr Color kTextSelected      = Color(255, 255, 255, 255);
    constexpr Color kTextSecondary     = Color(165, 195, 222, 235);
    constexpr Color kTextMuted         = Color(110, 136, 162, 200);
    constexpr Color kTabActiveBg       = Color( 36, 115, 180, 185);
    constexpr Color kTabInactiveBg     = Color( 14,  28,  48, 140);
    constexpr Color kSliderTrackBg     = Color( 22,  42,  68, 210);
    constexpr Color kSliderFillLeft    = Color( 52, 165, 230, 245);
    constexpr Color kSliderFillRight   = Color(110, 225, 255, 255);
    constexpr Color kFooterBg          = Color(  5,  12,  24, 236);
} // namespace Theme

// ============================================================================
// Unified Gamepad + Keyboard UI Input Controller (with hold auto-repeat)
// ============================================================================
enum class InputDeviceKind : uint8_t {
    PlayStation = 0,
    Xbox        = 1,
    Keyboard    = 2,
};

struct UiInputSnapshot {
    bool up       = false;
    bool down     = false;
    bool left     = false;
    bool right    = false;
    bool confirm  = false;
    bool cancel   = false;
    bool prevTab  = false;
    bool nextTab  = false;
    bool resetDef = false; // Triangle / Y / R
    bool toggleUi = false; // F1 hotkey
};

class UiInputController {
public:
    static constexpr uint16_t kBtnUp       = 0x0010u;
    static constexpr uint16_t kBtnRight    = 0x0020u;
    static constexpr uint16_t kBtnDown     = 0x0040u;
    static constexpr uint16_t kBtnLeft     = 0x0080u;
    static constexpr uint16_t kBtnL1       = 0x0400u;
    static constexpr uint16_t kBtnR1       = 0x0800u;
    static constexpr uint16_t kBtnTriangle = 0x1000u;
    static constexpr uint16_t kBtnCircle   = 0x2000u;
    static constexpr uint16_t kBtnCross    = 0x4000u;
    static constexpr uint16_t kBtnSquare   = 0x8000u;

    void SetActiveDevice(InputDeviceKind kind) { m_Device = kind; }
    InputDeviceKind ActiveDevice() const { return m_Device; }

    // Called once per frame with merged gamepad buttons, left stick axes, and foreground check
    UiInputSnapshot Update(uint16_t padBtns, float stickX, float stickY, bool windowFocused, double nowSec) {
        UiInputSnapshot out{};

        auto keyDown = [windowFocused](int vk) -> bool {
            return windowFocused && ((GetAsyncKeyState(vk) & 0x8000) != 0);
        };

        const bool rawUp      = ((padBtns & kBtnUp) != 0)    || (stickY < -0.55f) || keyDown(VK_UP)    || keyDown('W');
        const bool rawDown    = ((padBtns & kBtnDown) != 0)  || (stickY >  0.55f) || keyDown(VK_DOWN)  || keyDown('S');
        const bool rawLeft    = ((padBtns & kBtnLeft) != 0)  || (stickX < -0.55f) || keyDown(VK_LEFT)  || keyDown('A');
        const bool rawRight   = ((padBtns & kBtnRight) != 0) || (stickX >  0.55f) || keyDown(VK_RIGHT) || keyDown('D');
        const bool rawConfirm = ((padBtns & kBtnCross) != 0) || keyDown(VK_RETURN) || keyDown(VK_SPACE);
        const bool rawCancel  = ((padBtns & kBtnCircle) != 0) || keyDown(VK_ESCAPE) || keyDown(VK_BACK);
        const bool rawPrevTab = ((padBtns & kBtnL1) != 0)    || keyDown('Q') || keyDown(VK_PRIOR);
        const bool rawNextTab = ((padBtns & kBtnR1) != 0)    || keyDown('E') || keyDown(VK_NEXT) || keyDown(VK_TAB);
        const bool rawReset   = ((padBtns & kBtnTriangle) != 0) || keyDown('R');
        const bool rawToggle  = keyDown(VK_F1);

        if (keyDown(VK_UP) || keyDown(VK_DOWN) || keyDown(VK_LEFT) || keyDown(VK_RIGHT) ||
            keyDown(VK_RETURN) || keyDown(VK_ESCAPE) || keyDown('W') || keyDown('S') ||
            keyDown('A') || keyDown('D') || keyDown('Q') || keyDown('E') || keyDown('R')) {
            m_Device = InputDeviceKind::Keyboard;
        }

        out.up       = RepeatChannel(0, rawUp,    nowSec);
        out.down     = RepeatChannel(1, rawDown,  nowSec);
        out.left     = RepeatChannel(2, rawLeft,  nowSec);
        out.right    = RepeatChannel(3, rawRight, nowSec);
        out.confirm  = EdgeChannel(4, rawConfirm);
        out.cancel   = EdgeChannel(5, rawCancel);
        out.prevTab  = EdgeChannel(6, rawPrevTab);
        out.nextTab  = EdgeChannel(7, rawNextTab);
        out.resetDef = EdgeChannel(8, rawReset);
        out.toggleUi = EdgeChannel(9, rawToggle);

        return out;
    }

    void ResetHeldState() {
        for (int i = 0; i < 10; ++i) {
            m_Prev[i] = true; // require release before next edge
            m_NextRepeat[i] = 0.0;
        }
    }

private:
    bool EdgeChannel(int idx, bool now) {
        bool fired = now && !m_Prev[idx];
        m_Prev[idx] = now;
        return fired;
    }

    bool RepeatChannel(int idx, bool now, double nowSec) {
        if (!now) {
            m_Prev[idx] = false;
            m_NextRepeat[idx] = 0.0;
            return false;
        }
        if (!m_Prev[idx]) {
            m_Prev[idx] = true;
            m_NextRepeat[idx] = nowSec + 0.34;
            return true;
        }
        if (nowSec >= m_NextRepeat[idx]) {
            m_NextRepeat[idx] = nowSec + 0.075;
            return true;
        }
        return false;
    }

    InputDeviceKind m_Device = InputDeviceKind::PlayStation;
    bool            m_Prev[10]{};
    double          m_NextRepeat[10]{};
};

// ============================================================================
// Declarative Option Item & Tab Definitions
// ============================================================================
enum class ApplyScope : uint8_t {
    None = 0,
    Instant,
    NextMission,
    RestartRequired,
    GameSave,
};

enum class ItemKind : uint8_t {
    SectionHeader = 0,
    Toggle,
    Choice,
    Slider,
    Action,
    Info,
};

struct OptionItem {
    ItemKind    kind        = ItemKind::Info;
    std::string label;
    std::string description;
    ApplyScope  scope       = ApplyScope::None;
    bool        enabled     = true;

    // For Choice / Toggle / Slider
    std::vector<std::string> choices;
    int         minVal      = 0;
    int         maxVal      = 10;
    int         step        = 1;
    std::string suffix; // e.g. "%" or ""

    std::function<int()>         getter;
    std::function<void(int)>     setter;
    std::function<void()>        onAction;
    std::function<std::string()> infoValue;

    bool IsSelectable() const { return kind != ItemKind::SectionHeader && kind != ItemKind::Info && enabled; }

    static OptionItem MakeHeader(const char* text) {
        OptionItem it;
        it.kind  = ItemKind::SectionHeader;
        it.label = text;
        return it;
    }

    static OptionItem MakeInfo(const char* label, const char* desc, std::function<std::string()> valFn) {
        OptionItem it;
        it.kind        = ItemKind::Info;
        it.label       = label;
        it.description = desc;
        it.infoValue   = std::move(valFn);
        return it;
    }

    static OptionItem MakeToggle(
        const char* label, const char* desc, ApplyScope scope,
        std::function<int()> getFn, std::function<void(int)> setFn
    ) {
        OptionItem it;
        it.kind        = ItemKind::Toggle;
        it.label       = label;
        it.description = desc;
        it.scope       = scope;
        it.choices     = { "OFF", "ON" };
        it.minVal      = 0;
        it.maxVal      = 1;
        it.getter      = std::move(getFn);
        it.setter      = std::move(setFn);
        return it;
    }

    static OptionItem MakeChoice(
        const char* label, const char* desc, ApplyScope scope,
        std::vector<std::string> opts,
        std::function<int()> getFn, std::function<void(int)> setFn
    ) {
        OptionItem it;
        it.kind        = ItemKind::Choice;
        it.label       = label;
        it.description = desc;
        it.scope       = scope;
        it.choices     = std::move(opts);
        it.minVal      = 0;
        it.maxVal      = static_cast<int>(it.choices.size()) - 1;
        it.getter      = std::move(getFn);
        it.setter      = std::move(setFn);
        return it;
    }

    static OptionItem MakeSlider(
        const char* label, const char* desc, ApplyScope scope,
        int minV, int maxV, int stepV, const char* suf,
        std::function<int()> getFn, std::function<void(int)> setFn
    ) {
        OptionItem it;
        it.kind        = ItemKind::Slider;
        it.label       = label;
        it.description = desc;
        it.scope       = scope;
        it.minVal      = minV;
        it.maxVal      = maxV;
        it.step        = stepV;
        it.suffix      = suf ? suf : "";
        it.getter      = std::move(getFn);
        it.setter      = std::move(setFn);
        return it;
    }

    static OptionItem MakeAction(
        const char* label, const char* desc, ApplyScope scope,
        std::function<void()> actionFn
    ) {
        OptionItem it;
        it.kind        = ItemKind::Action;
        it.label       = label;
        it.description = desc;
        it.scope       = scope;
        it.onAction    = std::move(actionFn);
        return it;
    }
};

struct OptionTab {
    std::string             title;
    std::string             subtitle;
    std::vector<OptionItem> items;
    int                     selectedIdx = 0;
    float                   scrollOffset = 0.0f;

    void EnsureValidSelection() {
        if (items.empty()) { selectedIdx = 0; return; }
        if (selectedIdx >= 0 && selectedIdx < static_cast<int>(items.size()) && items[selectedIdx].IsSelectable()) {
            return;
        }
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i].IsSelectable()) { selectedIdx = i; return; }
        }
        selectedIdx = 0;
    }

    bool MoveSelection(int delta) {
        const int n = static_cast<int>(items.size());
        if (n <= 0) return false;
        int cur = selectedIdx;
        for (int step = 0; step < n; ++step) {
            cur = (cur + delta + n) % n;
            if (items[cur].IsSelectable()) {
                bool changed = (cur != selectedIdx);
                selectedIdx = cur;
                return changed;
            }
        }
        return false;
    }
};

// ============================================================================
// Reusable ACZ Widget Drawing Helpers
// ============================================================================
inline void DrawScopeBadge(
    UiRenderer& r, UiFontManager& f,
    float vx, float vy, ApplyScope scope
) {
    const char* text = nullptr;
    Color col = Theme::kAccentCyan;
    switch (scope) {
        case ApplyScope::Instant:         text = "INSTANT";      col = Theme::kAccentGreen; break;
        case ApplyScope::NextMission:     text = "NEXT MISSION"; col = Theme::kAccentAmber; break;
        case ApplyScope::RestartRequired: text = "RESTART";      col = Theme::kAccentRed;   break;
        case ApplyScope::GameSave:        text = "GAME SAVE";    col = Theme::kAccentCyan;  break;
        default: return;
    }
    const float fontSize = 15.0f;
    const float tw = f.MeasureTextWidth(r, FontRole::Body, fontSize, text);
    const float bw = tw + 16.0f;
    const float bh = 22.0f;
    r.DrawRectFilled(vx, vy, bw, bh, Color(col.r, col.g, col.b, 38));
    r.DrawRectOutline(vx, vy, bw, bh, 1.0f, Color(col.r, col.g, col.b, 185));
    f.DrawText(r, FontRole::Body, fontSize, vx + bw * 0.5f, vy + 3.0f, text, col, TextAlign::Center, false);
}

inline void DrawButtonPrompt(
    UiRenderer& r, UiFontManager& f,
    float& curX, float vy, const char* keyCap, const char* actionLabel
) {
    const float capSize = 18.0f;
    const float lblSize = 19.0f;
    const float capW = (std::max)(34.0f, f.MeasureTextWidth(r, FontRole::Body, capSize, keyCap) + 18.0f);
    const float capH = 28.0f;

    r.DrawRectFilled(curX, vy, capW, capH, Color(24, 58, 96, 220));
    r.DrawRectOutline(curX, vy, capW, capH, 1.5f, Theme::kAccentCyan.ScaleAlpha(0.85f));
    f.DrawText(r, FontRole::Body, capSize, curX + capW * 0.5f, vy + 5.0f, keyCap, Theme::kTextSelected, TextAlign::Center, false);
    curX += capW + 10.0f;

    f.DrawText(r, FontRole::Body, lblSize, curX, vy + 4.0f, actionLabel, Theme::kTextSecondary, TextAlign::Left, true);
    curX += f.MeasureTextWidth(r, FontRole::Body, lblSize, actionLabel) + 32.0f;
}

} // namespace acz_ui
