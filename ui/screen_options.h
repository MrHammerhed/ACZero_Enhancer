#pragma once
// ============================================================================
// ACZero Enhancer — Unified OPTIONS & ENHANCER Screen (ui/screen_options.h)
// Stage 1 pilot screen of the ACZ UI Engine:
//   - Completely replaces legacy SWAD/SWG Option submenus (ControlSet,
//     SoundSet, Brightness, ScrPosition, LanguageSet) with a crisp native-res
//     D3D12 UI featuring 6 unified tabs:
//       ENHANCER | DISPLAY | CONTROLS | AUDIO | TEXTURES | DIAGNOSTICS
//   - Also toggleable at any time via F1 hotkey.
//   - Reads/writes native game options directly via acz.exe's SWG callbacks
//     and persists Enhancer / HDTex / MemDump settings to their INI files.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

#include "acz_ui_renderer.h"
#include "acz_ui_font.h"
#include "acz_ui_widgets.h"

namespace acz_ui {

// Host bridge callbacks supplied by enhancer.cpp so ui/ stays decoupled
struct OptionsHostBridge {
    uintptr_t    exeBase = 0;
    std::wstring exeDir;

    // RVA translator (v1.0.2.1 -> active executable)
    std::function<uintptr_t(uintptr_t)> rva;

    // Enhancer runtime settings getters/setters + INI save
    std::function<int()>       getScreenAspect;
    std::function<void(int)>   setScreenAspect;
    std::function<int()>       getDrawDistance;
    std::function<void(int)>   setDrawDistance;
    std::function<int()>       getRadialFog;
    std::function<void(int)>   setRadialFog;
    std::function<int()>       getFastStartup;
    std::function<void(int)>   setFastStartup;
    std::function<int()>       getUnlock2P;
    std::function<void(int)>   setUnlock2P;
    std::function<int()>       getEnableMenuOverrides;
    std::function<void(int)>   setEnableMenuOverrides;
    std::function<int()>       getMenuAspectMode;
    std::function<void(int)>   setMenuAspectMode;
    std::function<int()>       getEnableCustomUI;
    std::function<void(int)>   setEnableCustomUI;
    std::function<int()>       getEnableLogging;
    std::function<void(int)>   setEnableLogging;
    std::function<void()>      saveEnhancerIni;

    // Optional sibling plugin notifications
    std::function<bool()>      isHdTexLoaded;
    std::function<void()>      notifyHdTexReload;
    std::function<bool()>      isMemDumpLoaded;
};

// Native acz.exe SWG callback RVAs (v1.0.2.1 base, mapped via bridge.rva)
namespace NativeOptionRva {
    constexpr uintptr_t kMainEndSubMenu       = 0x001F0010;
    constexpr uintptr_t kGetControl           = 0x001F2970;
    constexpr uintptr_t kGetButton            = 0x001F29E0;
    constexpr uintptr_t kGetLR12Button        = 0x001F2A50;
    constexpr uintptr_t kGetLeftAnalog        = 0x001F2AC0;
    constexpr uintptr_t kGetRightAnalog       = 0x001F2B30;
    constexpr uintptr_t kGetVibration         = 0x001F2BA0;
    constexpr uintptr_t kGetQuickSelect       = 0x001F2C10;
    constexpr uintptr_t kGetCommand           = 0x001F2C80;
    constexpr uintptr_t kGetHUD               = 0x001F2CF0;
    constexpr uintptr_t kGetUnit              = 0x001F2D60;
    constexpr uintptr_t kGetBrightness        = 0x001F2EB0;
    constexpr uintptr_t kGetSound             = 0x001F2F90;
    constexpr uintptr_t kGetBGM               = 0x001F3000;
    constexpr uintptr_t kGetVoice             = 0x001F3070;
    constexpr uintptr_t kGetSE                = 0x001F30E0;
    constexpr uintptr_t kGetSystemSE          = 0x001F3150;

    constexpr uintptr_t kSetControl           = 0x001F32A0;
    constexpr uintptr_t kSetButton            = 0x001F3310;
    constexpr uintptr_t kSetLR12Button        = 0x001F3380;
    constexpr uintptr_t kSetLeftAnalog        = 0x001F33F0;
    constexpr uintptr_t kSetRightAnalog       = 0x001F3460;
    constexpr uintptr_t kSetVibration         = 0x001F34D0;
    constexpr uintptr_t kSetQuickSelect       = 0x001F3540;
    constexpr uintptr_t kSetCommand           = 0x001F35B0;
    constexpr uintptr_t kSetHUD               = 0x001F3620;
    constexpr uintptr_t kSetUnit              = 0x001F3690;
    constexpr uintptr_t kSetBrightness        = 0x001F37E0;
    constexpr uintptr_t kSetSound             = 0x001F38C0;
    constexpr uintptr_t kSetBGM               = 0x001F3900;
    constexpr uintptr_t kSetVoice             = 0x001F3970;
    constexpr uintptr_t kSetSE                = 0x001F39E0;
    constexpr uintptr_t kSetSystemSE          = 0x001F3A50;
} // namespace NativeOptionRva

// SEH-isolated wrappers for calling acz.exe SWG option callbacks safely
using FnSwgGetInt = void (__fastcall*)(void*, int64_t*, void*, void*);
using FnSwgSetInt = void (__fastcall*)(void*, void*, void*, int32_t*);
using FnSwgVoid   = void (__fastcall*)(void*, void*, void*, void*);

inline int SafeSwgGetIntSEH(uintptr_t fnAddr, int fallbackVal) {
    if (!fnAddr) return fallbackVal;
    __try {
        int64_t outVal = fallbackVal;
        auto fn = reinterpret_cast<FnSwgGetInt>(fnAddr);
        fn(nullptr, &outVal, nullptr, nullptr);
        return static_cast<int>(outVal);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallbackVal;
    }
}

inline void SafeSwgSetIntSEH(uintptr_t fnAddr, int val) {
    if (!fnAddr) return;
    __try {
        int32_t v = static_cast<int32_t>(val);
        auto fn = reinterpret_cast<FnSwgSetInt>(fnAddr);
        fn(nullptr, nullptr, nullptr, &v);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

inline void SafeSwgCallVoidSEH(uintptr_t fnAddr) {
    if (!fnAddr) return;
    __try {
        auto fn = reinterpret_cast<FnSwgVoid>(fnAddr);
        fn(nullptr, nullptr, nullptr, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ============================================================================
// OptionsScreen — Full D3D12 Replacement for ACZ Options + Enhancer Settings
// ============================================================================
class OptionsScreen {
public:
    enum TabIndex : int {
        kTabEnhancer    = 0,
        kTabDisplay     = 1,
        kTabControls    = 2,
        kTabAudio       = 3,
        kTabTextures    = 4,
        kTabDiagnostics = 5,
    };

    void Initialize(const OptionsHostBridge& bridge) {
        m_Bridge = bridge;
        m_HdTexIniPath   = m_Bridge.exeDir + L"\\mods\\enhancer\\acz_hdtex.ini";
        m_MemDumpIniPath = m_Bridge.exeDir + L"\\mods\\enhancer\\memdump.ini";
        ReloadExternalInis();
        BuildTabs();
    }

    bool IsOpen() const { return m_Open; }
    bool OpenedFromSwgSubmenu() const { return m_OpenedFromSwgSubmenu; }
    std::vector<OptionTab>& GetTabs() { return m_Tabs; }
    int GetActiveTab() const { return m_ActiveTab; }
    void SetActiveTab(int t) {
        if (m_Tabs.empty()) return;
        m_ActiveTab = (std::max)(0, (std::min)(t, static_cast<int>(m_Tabs.size()) - 1));
    }
    const std::string& GetStatusToast() const { return m_StatusToast; }
    float GetStatusToastTimer() const { return m_StatusToastTimer; }
    void TriggerToast(const char* msg) { ShowToast(msg); }
    void TickToast(float dtSec) {
        if (m_StatusToastTimer > 0.0f) {
            m_StatusToastTimer = (std::max)(0.0f, m_StatusToastTimer - dtSec);
        }
    }

    void Open(int initialTab, bool fromSwgSubmenu) {
        m_Open = true;
        m_OpenedFromSwgSubmenu = fromSwgSubmenu;
        m_ActiveTab = (std::max)(0, (std::min)(initialTab, static_cast<int>(m_Tabs.size()) - 1));
        m_FadeAlpha = 0.0f;
        m_StatusToast.clear();
        m_StatusToastTimer = 0.0f;
        ReloadExternalInis();
        if (!m_Tabs.empty()) {
            m_Tabs[m_ActiveTab].EnsureValidSelection();
        }
    }

    void Close(bool notifyNativeEndSubmenu) {
        if (!m_Open) return;
        const bool wasSubmenu = m_OpenedFromSwgSubmenu;
        m_Open = false;
        m_OpenedFromSwgSubmenu = false;

        if (m_Bridge.saveEnhancerIni) {
            m_Bridge.saveEnhancerIni();
        }
        SaveExternalInisIfDirty();

        if (notifyNativeEndSubmenu && wasSubmenu && m_Bridge.exeBase && m_Bridge.rva) {
            SafeSwgCallVoidSEH(m_Bridge.exeBase + m_Bridge.rva(NativeOptionRva::kMainEndSubMenu));
        }
    }

    // Returns true if the screen consumed input this frame
    bool Update(const UiInputSnapshot& in, float dtSec) {
        if (!m_Open) return false;

        m_TimeSec += dtSec;
        m_FadeAlpha = (std::min)(1.0f, m_FadeAlpha + dtSec * 7.5f);
        if (m_StatusToastTimer > 0.0f) {
            m_StatusToastTimer = (std::max)(0.0f, m_StatusToastTimer - dtSec);
        }

        if (in.cancel || in.toggleUi) {
            Close(true);
            return true;
        }

        if (m_Tabs.empty()) return true;

        if (in.prevTab) {
            m_ActiveTab = (m_ActiveTab - 1 + static_cast<int>(m_Tabs.size())) % static_cast<int>(m_Tabs.size());
            m_Tabs[m_ActiveTab].EnsureValidSelection();
        } else if (in.nextTab) {
            m_ActiveTab = (m_ActiveTab + 1) % static_cast<int>(m_Tabs.size());
            m_Tabs[m_ActiveTab].EnsureValidSelection();
        }

        OptionTab& tab = m_Tabs[m_ActiveTab];
        tab.EnsureValidSelection();

        if (in.up)   tab.MoveSelection(-1);
        if (in.down) tab.MoveSelection(+1);

        if (tab.selectedIdx >= 0 && tab.selectedIdx < static_cast<int>(tab.items.size())) {
            OptionItem& item = tab.items[tab.selectedIdx];
            if (item.IsSelectable()) {
                if (item.kind == ItemKind::Toggle || item.kind == ItemKind::Choice) {
                    int delta = 0;
                    if (in.left) delta = -1;
                    else if (in.right || in.confirm) delta = +1;
                    if (delta != 0 && item.getter && item.setter) {
                        const int count = (std::max)(1, static_cast<int>(item.choices.size()));
                        int cur = item.getter();
                        if (cur < 0 || cur >= count) cur = 0;
                        int next = (cur + delta + count) % count;
                        item.setter(next);
                        ShowToast("SAVED");
                    }
                } else if (item.kind == ItemKind::Slider) {
                    int delta = 0;
                    if (in.left) delta = -item.step;
                    else if (in.right) delta = +item.step;
                    if (delta != 0 && item.getter && item.setter) {
                        int cur = item.getter();
                        int next = (std::max)(item.minVal, (std::min)(item.maxVal, cur + delta));
                        if (next != cur) {
                            item.setter(next);
                            ShowToast("SAVED");
                        }
                    }
                } else if (item.kind == ItemKind::Action) {
                    if (in.confirm && item.onAction) {
                        item.onAction();
                    }
                }
            }
        }

        return true;
    }

    void Render(UiRenderer& r, UiFontManager& f, InputDeviceKind activeDev) {
        if (!m_Open) return;

        const float vw = r.VirtualWidth();
        const float vh = r.VirtualHeight();
        const float a  = m_FadeAlpha;

        // 1. Subtle tactical backdrop dim over live native menu background
        r.DrawRectFilled(0.0f, 0.0f, vw, vh, Theme::kBackdropDim.ScaleAlpha(a));

        // Subtle horizontal scanline accent bands at top and bottom
        r.DrawRectGradientV(0.0f, 0.0f, vw, 118.0f, Theme::kHeaderBgTop.ScaleAlpha(a), Theme::kHeaderBgBot.ScaleAlpha(a));
        r.DrawRectFilled(0.0f, 118.0f, vw, 2.0f, Theme::kAccentCyan.ScaleAlpha(0.75f * a));

        // Centered safe-area layout (adapts cleanly to 4:3, 16:9, 21:9, 32:9)
        const float safeW = (std::min)(vw - 96.0f, 1380.0f);
        const float safeX = (vw - safeW) * 0.5f;

        // 2. Top Header: Title + Version Tag + Toast
        f.DrawText(r, FontRole::Title, 38.0f, safeX, 22.0f, "OPTIONS", Theme::kTextSelected.ScaleAlpha(a), TextAlign::Left, true);
        f.DrawText(r, FontRole::Body,  18.0f, safeX + 205.0f, 36.0f, "ACZERO ENHANCER  v0.25", Theme::kAccentCyan.ScaleAlpha(0.9f * a), TextAlign::Left, true);

        if (m_StatusToastTimer > 0.0f && !m_StatusToast.empty()) {
            float ta = (std::min)(1.0f, m_StatusToastTimer * 2.0f) * a;
            f.DrawText(r, FontRole::Body, 18.0f, safeX + safeW, 36.0f, m_StatusToast.c_str(), Theme::kAccentGreen.ScaleAlpha(ta), TextAlign::Right, true);
        }

        // 3. Tab Bar (L1 / R1)
        const float tabY = 74.0f;
        const float tabH = 36.0f;
        const int tabCount = static_cast<int>(m_Tabs.size());
        const float tabGap = 8.0f;
        const float tabW = (safeW - tabGap * (tabCount - 1)) / static_cast<float>((std::max)(1, tabCount));

        for (int i = 0; i < tabCount; ++i) {
            const float tx = safeX + i * (tabW + tabGap);
            const bool active = (i == m_ActiveTab);
            r.DrawRectFilled(tx, tabY, tabW, tabH, (active ? Theme::kTabActiveBg : Theme::kTabInactiveBg).ScaleAlpha(a));
            r.DrawRectOutline(tx, tabY, tabW, tabH, active ? 2.0f : 1.0f, (active ? Theme::kAccentCyan : Theme::kBorderDim).ScaleAlpha(a));
            if (active) {
                r.DrawRectFilled(tx, tabY + tabH - 3.0f, tabW, 3.0f, Theme::kAccentCyan.ScaleAlpha(a));
            }
            f.DrawText(
                r, FontRole::Title, 19.0f,
                tx + tabW * 0.5f, tabY + 8.0f,
                m_Tabs[i].title.c_str(),
                (active ? Theme::kTextSelected : Theme::kTextSecondary).ScaleAlpha(a),
                TextAlign::Center, true
            );
        }

        // 4. Main Content Panel
        const float panelY = 142.0f;
        const float footerH = 112.0f;
        const float panelH = vh - panelY - footerH - 22.0f;
        r.DrawRectGradientV(safeX, panelY, safeW, panelH, Theme::kPanelBgTop.ScaleAlpha(a), Theme::kPanelBgBot.ScaleAlpha(a));
        r.DrawRectOutline(safeX, panelY, safeW, panelH, 1.5f, Theme::kBorderDim.ScaleAlpha(a));
        r.DrawCornerBrackets(safeX - 2.0f, panelY - 2.0f, safeW + 4.0f, panelH + 4.0f, 18.0f, 2.5f, Theme::kBorderBright.ScaleAlpha(a));

        // 5. Scrollable Option Rows
        OptionTab& tab = m_Tabs[m_ActiveTab];
        const float listPadX = 28.0f;
        const float listPadY = 20.0f;
        const float listX = safeX + listPadX;
        const float listY = panelY + listPadY;
        const float listW = safeW - listPadX * 2.0f - 18.0f; // leave space for scrollbar
        const float listH = panelH - listPadY * 2.0f;
        const float rowH  = 54.0f;
        const float hdrH  = 38.0f;

        // Compute total height and ensure selected row is scrolled into view
        float totalH = 0.0f;
        float selTop = 0.0f, selBot = 0.0f;
        for (int i = 0; i < static_cast<int>(tab.items.size()); ++i) {
            const float h = (tab.items[i].kind == ItemKind::SectionHeader) ? hdrH : rowH;
            if (i == tab.selectedIdx) {
                selTop = totalH;
                selBot = totalH + h;
            }
            totalH += h;
        }
        const float maxScroll = (std::max)(0.0f, totalH - listH);
        if (selTop < tab.scrollOffset) {
            tab.scrollOffset = selTop;
        } else if (selBot > tab.scrollOffset + listH) {
            tab.scrollOffset = selBot - listH;
        }
        tab.scrollOffset = (std::max)(0.0f, (std::min)(maxScroll, tab.scrollOffset));

        r.PushClipRect(listX - 6.0f, listY, listW + 12.0f, listH);

        float curY = listY - tab.scrollOffset;
        int stripeIdx = 0;
        for (int i = 0; i < static_cast<int>(tab.items.size()); ++i) {
            const OptionItem& it = tab.items[i];
            const bool isHdr = (it.kind == ItemKind::SectionHeader);
            const float h = isHdr ? hdrH : rowH;

            if (curY + h >= listY - 8.0f && curY <= listY + listH + 8.0f) {
                if (isHdr) {
                    f.DrawText(r, FontRole::Title, 17.0f, listX + 6.0f, curY + 12.0f, it.label.c_str(), Theme::kAccentCyan.ScaleAlpha(0.9f * a), TextAlign::Left, true);
                    r.DrawRectFilled(listX, curY + h - 4.0f, listW, 1.0f, Theme::kBorderDim.ScaleAlpha(0.75f * a));
                } else {
                    const bool selected = (i == tab.selectedIdx);
                    if (selected) {
                        const float pulse = 0.88f + 0.12f * std::sin(m_TimeSec * 5.0f);
                        r.DrawRectGradientH(listX, curY + 2.0f, listW, h - 4.0f, Theme::kRowHoverLeft.ScaleAlpha(a * pulse), Theme::kRowHoverRight.ScaleAlpha(a));
                        r.DrawRectFilled(listX, curY + 2.0f, 5.0f, h - 4.0f, Theme::kAccentCyan.ScaleAlpha(a));
                        r.DrawRectOutline(listX, curY + 2.0f, listW, h - 4.0f, 1.0f, Theme::kBorderBright.ScaleAlpha(0.65f * a));
                    } else if ((stripeIdx & 1) == 1) {
                        r.DrawRectFilled(listX, curY + 2.0f, listW, h - 4.0f, Theme::kRowStripe.ScaleAlpha(a));
                    }
                    ++stripeIdx;

                    const Color lblCol = !it.enabled ? Theme::kTextMuted
                                       : (selected ? Theme::kTextSelected : Theme::kTextPrimary);
                    f.DrawText(r, FontRole::Body, 23.0f, listX + 20.0f, curY + 14.0f, it.label.c_str(), lblCol.ScaleAlpha(a), TextAlign::Left, true);

                    // Scope badge (right of label, aligned to a clean column)
                    if (it.scope != ApplyScope::None) {
                        DrawScopeBadge(r, f, listX + listW * 0.48f, curY + 16.0f, it.scope);
                    }

                    // Right-side value control
                    const float valRightX = listX + listW - 18.0f;
                    const float valBoxW   = 290.0f;
                    const float valLeftX  = valRightX - valBoxW;

                    if (it.kind == ItemKind::Toggle || it.kind == ItemKind::Choice) {
                        int val = it.getter ? it.getter() : 0;
                        const int cnt = static_cast<int>(it.choices.size());
                        if (val < 0 || val >= cnt) val = 0;
                        const char* valStr = (cnt > 0) ? it.choices[val].c_str() : "-";

                        Color valCol = Theme::kTextSelected;
                        if (it.kind == ItemKind::Toggle) {
                            valCol = (val != 0) ? Theme::kAccentGreen : Theme::kTextSecondary;
                        }
                        if (selected) {
                            f.DrawText(r, FontRole::Body, 22.0f, valLeftX, curY + 14.0f, "<", Theme::kAccentCyan.ScaleAlpha(a), TextAlign::Left, true);
                            f.DrawText(r, FontRole::Body, 22.0f, valRightX, curY + 14.0f, ">", Theme::kAccentCyan.ScaleAlpha(a), TextAlign::Right, true);
                        }
                        f.DrawText(r, FontRole::Body, 22.0f, valLeftX + valBoxW * 0.5f, curY + 14.0f, valStr, valCol.ScaleAlpha(a), TextAlign::Center, true);
                    } else if (it.kind == ItemKind::Slider) {
                        int val = it.getter ? it.getter() : it.minVal;
                        val = (std::max)(it.minVal, (std::min)(it.maxVal, val));
                        const float norm = (it.maxVal > it.minVal)
                            ? (static_cast<float>(val - it.minVal) / static_cast<float>(it.maxVal - it.minVal))
                            : 0.0f;

                        const float barW = valBoxW - 76.0f;
                        const float barH = 12.0f;
                        const float barX = valLeftX;
                        const float barY = curY + (h - barH) * 0.5f;
                        r.DrawRectFilled(barX, barY, barW, barH, Theme::kSliderTrackBg.ScaleAlpha(a));
                        r.DrawRectOutline(barX, barY, barW, barH, 1.0f, Theme::kBorderDim.ScaleAlpha(a));
                        if (norm > 0.001f) {
                            r.DrawRectGradientH(barX + 1.0f, barY + 1.0f, (barW - 2.0f) * norm, barH - 2.0f, Theme::kSliderFillLeft.ScaleAlpha(a), Theme::kSliderFillRight.ScaleAlpha(a));
                        }
                        char numBuf[32];
                        std::snprintf(numBuf, sizeof(numBuf), "%d%s", val, it.suffix.c_str());
                        f.DrawText(r, FontRole::Body, 21.0f, valRightX, curY + 15.0f, numBuf, Theme::kTextSelected.ScaleAlpha(a), TextAlign::Right, true);
                    } else if (it.kind == ItemKind::Action) {
                        f.DrawText(r, FontRole::Body, 20.0f, valRightX, curY + 16.0f, "[ EXECUTE ]", (selected ? Theme::kAccentAmber : Theme::kTextSecondary).ScaleAlpha(a), TextAlign::Right, true);
                    } else if (it.kind == ItemKind::Info) {
                        std::string v = it.infoValue ? it.infoValue() : std::string("-");
                        f.DrawText(r, FontRole::Body, 20.0f, valRightX, curY + 16.0f, v.c_str(), Theme::kAccentCyan.ScaleAlpha(a), TextAlign::Right, true);
                    }
                }
            }
            curY += h;
        }

        r.PopClipRect();

        // 6. Clean Scrollbar (always pixel-accurate — zero SWG bugs!)
        if (maxScroll > 1.0f) {
            const float sbX = safeX + safeW - 18.0f;
            const float sbW = 6.0f;
            r.DrawRectFilled(sbX, listY, sbW, listH, Theme::kSliderTrackBg.ScaleAlpha(a));
            const float thumbH = (std::max)(36.0f, listH * (listH / totalH));
            const float thumbY = listY + (listH - thumbH) * (tab.scrollOffset / maxScroll);
            r.DrawRectFilled(sbX, thumbY, sbW, thumbH, Theme::kAccentCyan.ScaleAlpha(0.85f * a));
        }

        // 7. Footer Bar: Selected Item Description + Dynamic Button Prompts
        const float footY = vh - footerH;
        r.DrawRectFilled(0.0f, footY, vw, footerH, Theme::kFooterBg.ScaleAlpha(a));
        r.DrawRectFilled(0.0f, footY, vw, 2.0f, Theme::kBorderDim.ScaleAlpha(a));

        const char* desc = "";
        if (tab.selectedIdx >= 0 && tab.selectedIdx < static_cast<int>(tab.items.size())) {
            desc = tab.items[tab.selectedIdx].description.c_str();
        }
        f.DrawText(r, FontRole::Body, 21.0f, safeX, footY + 16.0f, desc, Theme::kTextPrimary.ScaleAlpha(a), TextAlign::Left, true);

        float promptX = safeX;
        const float promptY = footY + 62.0f;
        if (activeDev == InputDeviceKind::Keyboard) {
            DrawButtonPrompt(r, f, promptX, promptY, "UP/DN", "NAVIGATE");
            DrawButtonPrompt(r, f, promptX, promptY, "LT/RT", "CHANGE");
            DrawButtonPrompt(r, f, promptX, promptY, "Q / E", "SWITCH TAB");
            DrawButtonPrompt(r, f, promptX, promptY, "ESC",   "SAVE & BACK");
        } else if (activeDev == InputDeviceKind::Xbox) {
            DrawButtonPrompt(r, f, promptX, promptY, "D-PAD", "NAVIGATE");
            DrawButtonPrompt(r, f, promptX, promptY, "A",     "SELECT");
            DrawButtonPrompt(r, f, promptX, promptY, "LB/RB", "SWITCH TAB");
            DrawButtonPrompt(r, f, promptX, promptY, "B",     "SAVE & BACK");
        } else {
            DrawButtonPrompt(r, f, promptX, promptY, "D-PAD", "NAVIGATE");
            DrawButtonPrompt(r, f, promptX, promptY, "X",     "SELECT");
            DrawButtonPrompt(r, f, promptX, promptY, "L1/R1", "SWITCH TAB");
            DrawButtonPrompt(r, f, promptX, promptY, "O",     "SAVE & BACK");
        }
    }

private:
    uintptr_t ResolveRva(uintptr_t rva1021) const {
        if (!m_Bridge.exeBase || !m_Bridge.rva) return 0;
        return m_Bridge.exeBase + m_Bridge.rva(rva1021);
    }

    int GetNativeOpt(uintptr_t rva1021, int fallback = 0) const {
        return SafeSwgGetIntSEH(ResolveRva(rva1021), fallback);
    }

    void SetNativeOpt(uintptr_t rva1021, int val) const {
        SafeSwgSetIntSEH(ResolveRva(rva1021), val);
    }

    void ShowToast(const char* msg) {
        m_StatusToast = msg;
        m_StatusToastTimer = 1.6f;
    }

    // Read/write helper for sibling INI files (acz_hdtex.ini, memdump.ini)
    static int ReadIniInt(const std::wstring& path, const wchar_t* sec, const wchar_t* key, int defVal) {
        return static_cast<int>(GetPrivateProfileIntW(sec, key, defVal, path.c_str()));
    }

    static std::string ReadIniStr(const std::wstring& path, const wchar_t* sec, const wchar_t* key, const char* defVal) {
        wchar_t wdef[64]{};
        MultiByteToWideChar(CP_UTF8, 0, defVal, -1, wdef, 64);
        wchar_t buf[128]{};
        GetPrivateProfileStringW(sec, key, wdef, buf, 128, path.c_str());
        char out[128]{};
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, 128, nullptr, nullptr);
        return std::string(out);
    }

    static void WriteIniInt(const std::wstring& path, const wchar_t* sec, const wchar_t* key, int val) {
        wchar_t buf[32]{};
        swprintf_s(buf, L"%d", val);
        WritePrivateProfileStringW(sec, key, buf, path.c_str());
    }

    static void WriteIniStr(const std::wstring& path, const wchar_t* sec, const wchar_t* key, const wchar_t* val) {
        WritePrivateProfileStringW(sec, key, val, path.c_str());
    }

    void ReloadExternalInis() {
        // acz_hdtex.ini
        std::string g = ReadIniStr(m_HdTexIniPath, L"ACZ_HDTex", L"GamepadGlyphs", "AUTO");
        if (_stricmp(g.c_str(), "PS") == 0) m_HdGlyphs = 1;
        else if (_stricmp(g.c_str(), "XBOX") == 0) m_HdGlyphs = 2;
        else if (_stricmp(g.c_str(), "KB") == 0) m_HdGlyphs = 3;
        else m_HdGlyphs = 0;

        m_HdDdsCache    = ReadIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"EnableDdsCache", 1) != 0 ? 1 : 0;
        m_HdAutoInstall = ReadIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"AutoInstallTexturePacks", 1) != 0 ? 1 : 0;
        m_HdAutoShare   = ReadIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"AutoShareFontGlyphs", 1) != 0 ? 1 : 0;
        m_HdHotReload   = ReadIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"HotReload", 0) != 0 ? 1 : 0;
        m_HdDumpAll     = ReadIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"DumpAllGims", 0) != 0 ? 1 : 0;

        // memdump.ini
        m_MdEnable     = ReadIniInt(m_MemDumpIniPath, L"MemoryDump", L"EnableMemoryDump", 1) != 0 ? 1 : 0;
        m_MdF2         = ReadIniInt(m_MemDumpIniPath, L"MemoryDump", L"EnableF2MemoryDump", 1) != 0 ? 1 : 0;
        m_MdScreenshot = ReadIniInt(m_MemDumpIniPath, L"MemoryDump", L"SaveScreenshotOnDump", 1) != 0 ? 1 : 0;
        m_MdOnExit     = ReadIniInt(m_MemDumpIniPath, L"MemoryDump", L"DumpMemoryOnExit", 0) != 0 ? 1 : 0;
        m_MdMinidump   = ReadIniInt(m_MemDumpIniPath, L"MemoryDump", L"WriteMinidumpFile", 1) != 0 ? 1 : 0;
        m_MdAutoClean  = ReadIniInt(m_MemDumpIniPath, L"MemoryDump", L"AutoDeleteOldDumps", 1) != 0 ? 1 : 0;

        m_HdTexDirty   = false;
        m_MemDumpDirty = false;
    }

    void SaveExternalInisIfDirty() {
        if (m_HdTexDirty) {
            const wchar_t* glyphStr = (m_HdGlyphs == 1) ? L"PS" : (m_HdGlyphs == 2) ? L"XBOX" : (m_HdGlyphs == 3) ? L"KB" : L"AUTO";
            WriteIniStr(m_HdTexIniPath, L"ACZ_HDTex", L"GamepadGlyphs", glyphStr);
            WriteIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"EnableDdsCache", m_HdDdsCache);
            WriteIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"AutoInstallTexturePacks", m_HdAutoInstall);
            WriteIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"AutoShareFontGlyphs", m_HdAutoShare);
            WriteIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"HotReload", m_HdHotReload);
            WriteIniInt(m_HdTexIniPath, L"ACZ_HDTex", L"DumpAllGims", m_HdDumpAll);
            m_HdTexDirty = false;
            if (m_Bridge.notifyHdTexReload) m_Bridge.notifyHdTexReload();
        }
        if (m_MemDumpDirty) {
            WriteIniInt(m_MemDumpIniPath, L"MemoryDump", L"EnableMemoryDump", m_MdEnable);
            WriteIniInt(m_MemDumpIniPath, L"MemoryDump", L"EnableF2MemoryDump", m_MdF2);
            WriteIniInt(m_MemDumpIniPath, L"MemoryDump", L"SaveScreenshotOnDump", m_MdScreenshot);
            WriteIniInt(m_MemDumpIniPath, L"MemoryDump", L"DumpMemoryOnExit", m_MdOnExit);
            WriteIniInt(m_MemDumpIniPath, L"MemoryDump", L"WriteMinidumpFile", m_MdMinidump);
            WriteIniInt(m_MemDumpIniPath, L"MemoryDump", L"AutoDeleteOldDumps", m_MdAutoClean);
            m_MemDumpDirty = false;
        }
    }

    void BuildTabs() {
        m_Tabs.clear();

        // --------------------------------------------------------------------
        // Tab 0: ENHANCER (enhancer.ini)
        // --------------------------------------------------------------------
        {
            OptionTab t;
            t.title = "ENHANCER";
            t.items.push_back(OptionItem::MakeHeader("RENDERING & VISIBILITY"));
            t.items.push_back(OptionItem::MakeToggle(
                "Extended Draw Distance (2x)",
                "Expands coarse terrain grid to 19x19 with procedural OOB fractal terrain and 524km cloud shadows.",
                ApplyScope::NextMission,
                [this]() { return m_Bridge.getDrawDistance ? m_Bridge.getDrawDistance() : 0; },
                [this](int v) { if (m_Bridge.setDrawDistance) m_Bridge.setDrawDistance(v); if (m_Bridge.saveEnhancerIni) m_Bridge.saveEnhancerIni(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Spherical Radial Fog",
                "Replaces planar Z-depth fog with 3D spherical distance so terrain and trees never pop in at corners.",
                ApplyScope::RestartRequired,
                [this]() { return m_Bridge.getRadialFog ? m_Bridge.getRadialFog() : 1; },
                [this](int v) { if (m_Bridge.setRadialFog) m_Bridge.setRadialFog(v); if (m_Bridge.saveEnhancerIni) m_Bridge.saveEnhancerIni(); }
            ));
            t.items.push_back(OptionItem::MakeHeader("STARTUP, INPUT & UI"));
            t.items.push_back(OptionItem::MakeChoice(
                "Fast Startup",
                "Controls skipping of launcher, pad manual, intro logos, memory card delays, and title screen.",
                ApplyScope::RestartRequired,
                { "OFF", "PRESS TO SKIP", "TO TITLE", "TO MAIN MENU" },
                [this]() { return m_Bridge.getFastStartup ? m_Bridge.getFastStartup() : 0; },
                [this](int v) { if (m_Bridge.setFastStartup) m_Bridge.setFastStartup(v); if (m_Bridge.saveEnhancerIni) m_Bridge.saveEnhancerIni(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "2-Player Co-Op / VS Mode",
                "Unlocks 2P modes: Player 1 uses Gamepad and Player 2 uses Keyboard independently.",
                ApplyScope::Instant,
                [this]() { return m_Bridge.getUnlock2P ? m_Bridge.getUnlock2P() : 0; },
                [this](int v) { if (m_Bridge.setUnlock2P) m_Bridge.setUnlock2P(v); if (m_Bridge.saveEnhancerIni) m_Bridge.saveEnhancerIni(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Custom D3D12 UI Menus",
                "Replaces legacy SWG/SWAD Option screens with this native-resolution D3D12 UI.",
                ApplyScope::Instant,
                [this]() { return m_Bridge.getEnableCustomUI ? m_Bridge.getEnableCustomUI() : 1; },
                [this](int v) { if (m_Bridge.setEnableCustomUI) m_Bridge.setEnableCustomUI(v); if (m_Bridge.saveEnhancerIni) m_Bridge.saveEnhancerIni(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Legacy Widescreen Menu Overrides",
                "Loads custom 16:9 / 21:9 .swa layouts for menus not yet migrated to the D3D12 UI engine.",
                ApplyScope::Instant,
                [this]() { return m_Bridge.getEnableMenuOverrides ? m_Bridge.getEnableMenuOverrides() : 1; },
                [this](int v) { if (m_Bridge.setEnableMenuOverrides) m_Bridge.setEnableMenuOverrides(v); if (m_Bridge.saveEnhancerIni) m_Bridge.saveEnhancerIni(); }
            ));
            t.items.push_back(OptionItem::MakeChoice(
                "Legacy Menu Aspect Mode",
                "Forces aspect ratio tier for legacy .swa menu overrides (Auto follows Screen Aspect).",
                ApplyScope::Instant,
                { "AUTO", "FORCE 16:9", "FORCE 4:3", "FORCE 21:9" },
                [this]() { return m_Bridge.getMenuAspectMode ? m_Bridge.getMenuAspectMode() : 0; },
                [this](int v) { if (m_Bridge.setMenuAspectMode) m_Bridge.setMenuAspectMode(v); if (m_Bridge.saveEnhancerIni) m_Bridge.saveEnhancerIni(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Enhancer Logging",
                "Writes [Enhancer] runtime diagnostics to mods\\enhancer\\enhancer.log.",
                ApplyScope::Instant,
                [this]() { return m_Bridge.getEnableLogging ? m_Bridge.getEnableLogging() : 1; },
                [this](int v) { if (m_Bridge.setEnableLogging) m_Bridge.setEnableLogging(v); if (m_Bridge.saveEnhancerIni) m_Bridge.saveEnhancerIni(); }
            ));
            m_Tabs.push_back(std::move(t));
        }

        // --------------------------------------------------------------------
        // Tab 1: DISPLAY (Screen Aspect + Native Display/HUD options)
        // --------------------------------------------------------------------
        {
            OptionTab t;
            t.title = "DISPLAY";
            t.items.push_back(OptionItem::MakeHeader("SCREEN & ASPECT RATIO"));
            t.items.push_back(OptionItem::MakeChoice(
                "Screen Aspect Ratio",
                "Save-safe runtime aspect ratio (4:3, 16:9, or Hor+ 21:9 Ultrawide). Stored in enhancer.ini.",
                ApplyScope::Instant,
                { "4:3 STANDARD", "16:9 WIDESCREEN", "21:9 ULTRAWIDE" },
                [this]() { return m_Bridge.getScreenAspect ? m_Bridge.getScreenAspect() : 1; },
                [this](int v) { if (m_Bridge.setScreenAspect) m_Bridge.setScreenAspect(v); }
            ));
            t.items.push_back(OptionItem::MakeSlider(
                "Brightness",
                "Adjusts in-game gamma / screen brightness level.",
                ApplyScope::GameSave,
                0, 10, 1, "",
                [this]() { return GetNativeOpt(NativeOptionRva::kGetBrightness, 5); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetBrightness, v); }
            ));
            t.items.push_back(OptionItem::MakeHeader("IN-FLIGHT HUD"));
            t.items.push_back(OptionItem::MakeChoice(
                "HUD Display Mode",
                "Toggles tactical HUD elements shown during flight.",
                ApplyScope::GameSave,
                { "NORMAL", "MINIMAL", "OFF" },
                [this]() { return GetNativeOpt(NativeOptionRva::kGetHUD, 0); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetHUD, v); }
            ));
            t.items.push_back(OptionItem::MakeChoice(
                "Measurement Units",
                "Selects imperial (Feet / Knots) or metric (Meters / km/h) altitude & speed units.",
                ApplyScope::GameSave,
                { "FEET / KNOTS", "METERS / KM/H" },
                [this]() { return GetNativeOpt(NativeOptionRva::kGetUnit, 0); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetUnit, v); }
            ));
            t.items.push_back(OptionItem::MakeChoice(
                "Wingman Command Display",
                "Controls visibility of the Wingman tactical command indicator on HUD.",
                ApplyScope::GameSave,
                { "ON", "OFF" },
                [this]() { return GetNativeOpt(NativeOptionRva::kGetCommand, 0); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetCommand, v); }
            ));
            m_Tabs.push_back(std::move(t));
        }

        // --------------------------------------------------------------------
        // Tab 2: CONTROLS (Native ControlSet options)
        // --------------------------------------------------------------------
        {
            OptionTab t;
            t.title = "CONTROLS";
            t.items.push_back(OptionItem::MakeHeader("FLIGHT CONTROLS"));
            t.items.push_back(OptionItem::MakeChoice(
                "Control Scheme",
                "Normal (full roll & pitch freedom) or Novice (auto-leveling simplified controls).",
                ApplyScope::GameSave,
                { "NORMAL", "NOVICE" },
                [this]() { return GetNativeOpt(NativeOptionRva::kGetControl, 0); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetControl, v); }
            ));
            t.items.push_back(OptionItem::MakeChoice(
                "Button Configuration",
                "Selects controller button preset (Type A, B, or C).",
                ApplyScope::GameSave,
                { "TYPE A", "TYPE B", "TYPE C" },
                [this]() { return GetNativeOpt(NativeOptionRva::kGetButton, 0); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetButton, v); }
            ));
            t.items.push_back(OptionItem::MakeChoice(
                "L1/R1 & L2/R2 Swap",
                "Swaps throttle/brake and yaw controls between shoulder buttons and triggers.",
                ApplyScope::GameSave,
                { "NORMAL", "SWAPPED" },
                [this]() { return GetNativeOpt(NativeOptionRva::kGetLR12Button, 0); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetLR12Button, v); }
            ));
            t.items.push_back(OptionItem::MakeChoice(
                "Left Stick Pitch Axis",
                "Normal (pull back to climb) or Reverse pitch control.",
                ApplyScope::GameSave,
                { "NORMAL", "REVERSE" },
                [this]() { return GetNativeOpt(NativeOptionRva::kGetLeftAnalog, 0); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetLeftAnalog, v); }
            ));
            t.items.push_back(OptionItem::MakeChoice(
                "Right Stick Camera",
                "Normal or Reverse camera look axis on the right analog stick.",
                ApplyScope::GameSave,
                { "NORMAL", "REVERSE" },
                [this]() { return GetNativeOpt(NativeOptionRva::kGetRightAnalog, 0); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetRightAnalog, v); }
            ));
            t.items.push_back(OptionItem::MakeHeader("FEEDBACK & HANGAR"));
            t.items.push_back(OptionItem::MakeToggle(
                "Controller Vibration",
                "Enables or disables DualSense / DS4 / XInput vibration feedback.",
                ApplyScope::GameSave,
                [this]() { return GetNativeOpt(NativeOptionRva::kGetVibration, 1) != 0 ? 1 : 0; },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetVibration, v); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Quick Aircraft Select",
                "Streamlines aircraft selection screen in the hangar.",
                ApplyScope::GameSave,
                [this]() { return GetNativeOpt(NativeOptionRva::kGetQuickSelect, 0) != 0 ? 1 : 0; },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetQuickSelect, v); }
            ));
            m_Tabs.push_back(std::move(t));
        }

        // --------------------------------------------------------------------
        // Tab 3: AUDIO (Native SoundSet options)
        // --------------------------------------------------------------------
        {
            OptionTab t;
            t.title = "AUDIO";
            t.items.push_back(OptionItem::MakeHeader("SOUND OUTPUT"));
            t.items.push_back(OptionItem::MakeChoice(
                "Audio Mode",
                "Selects audio channel output configuration.",
                ApplyScope::GameSave,
                { "STEREO", "MONO", "SURROUND" },
                [this]() { return GetNativeOpt(NativeOptionRva::kGetSound, 0); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetSound, v); }
            ));
            t.items.push_back(OptionItem::MakeHeader("VOLUME MIXER"));
            t.items.push_back(OptionItem::MakeSlider(
                "BGM Volume",
                "Background music volume.",
                ApplyScope::GameSave,
                0, 10, 1, "",
                [this]() { return GetNativeOpt(NativeOptionRva::kGetBGM, 8); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetBGM, v); }
            ));
            t.items.push_back(OptionItem::MakeSlider(
                "Radio / Voice Volume",
                "In-flight radio chatter and briefing voice volume.",
                ApplyScope::GameSave,
                0, 10, 1, "",
                [this]() { return GetNativeOpt(NativeOptionRva::kGetVoice, 8); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetVoice, v); }
            ));
            t.items.push_back(OptionItem::MakeSlider(
                "Sound Effects Volume",
                "Jet engines, missiles, explosions, and combat SFX volume.",
                ApplyScope::GameSave,
                0, 10, 1, "",
                [this]() { return GetNativeOpt(NativeOptionRva::kGetSE, 8); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetSE, v); }
            ));
            t.items.push_back(OptionItem::MakeSlider(
                "System SE Volume",
                "Menu navigation and HUD warning tone volume.",
                ApplyScope::GameSave,
                0, 10, 1, "",
                [this]() { return GetNativeOpt(NativeOptionRva::kGetSystemSE, 8); },
                [this](int v) { SetNativeOpt(NativeOptionRva::kSetSystemSE, v); }
            ));
            m_Tabs.push_back(std::move(t));
        }

        // --------------------------------------------------------------------
        // Tab 4: TEXTURES (acz_hdtex.ini)
        // --------------------------------------------------------------------
        {
            OptionTab t;
            t.title = "TEXTURES";
            t.items.push_back(OptionItem::MakeHeader("HD TEXTURE ENGINE (ACZ_HDTEX.ASI)"));
            t.items.push_back(OptionItem::MakeInfo(
                "Plugin Status",
                "Indicates whether acz_hdtex.asi is currently loaded alongside enhancer.asi.",
                [this]() { return (m_Bridge.isHdTexLoaded && m_Bridge.isHdTexLoaded()) ? "LOADED" : "NOT LOADED"; }
            ));
            t.items.push_back(OptionItem::MakeChoice(
                "Gamepad Button Glyphs",
                "Controller prompt icon pack (Auto detects DualSense/DS4, Xbox, or Keyboard live).",
                ApplyScope::Instant,
                { "AUTO", "PLAYSTATION", "XBOX", "KEYBOARD" },
                [this]() { return m_HdGlyphs; },
                [this](int v) { m_HdGlyphs = v; m_HdTexDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "BC7 / BGRA8 DDS Cache",
                "Compiles replacement PNG/JPG textures into .dds_cache for instant loading and smooth frame times.",
                ApplyScope::RestartRequired,
                [this]() { return m_HdDdsCache; },
                [this](int v) { m_HdDdsCache = v; m_HdTexDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Auto-Install Texture Packs",
                "Automatically unpacks and installs OstrichMods HUD & custom textures.zip from mods\\ on startup.",
                ApplyScope::RestartRequired,
                [this]() { return m_HdAutoInstall; },
                [this](int v) { m_HdAutoInstall = v; m_HdTexDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Auto-Share Master Font Glyphs",
                "Shares HD font glyphs across matching dynamic font atlases automatically.",
                ApplyScope::RestartRequired,
                [this]() { return m_HdAutoShare; },
                [this](int v) { m_HdAutoShare = v; m_HdTexDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeHeader("MODDING TOOLS"));
            t.items.push_back(OptionItem::MakeToggle(
                "Hot Reload Textures",
                "Re-reads PNG/JPG files from disk whenever a texture is recreated.",
                ApplyScope::Instant,
                [this]() { return m_HdHotReload; },
                [this](int v) { m_HdHotReload = v; m_HdTexDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Dump All Game Textures",
                "Exports loaded textures and font atlases to mods\\textures_dump\\ for modding.",
                ApplyScope::Instant,
                [this]() { return m_HdDumpAll; },
                [this](int v) { m_HdDumpAll = v; m_HdTexDirty = true; SaveExternalInisIfDirty(); }
            ));
            m_Tabs.push_back(std::move(t));
        }

        // --------------------------------------------------------------------
        // Tab 5: DIAGNOSTICS (memdump.ini)
        // --------------------------------------------------------------------
        {
            OptionTab t;
            t.title = "DIAGNOSTICS";
            t.items.push_back(OptionItem::MakeHeader("CRASH & MEMORY DUMPER (MEMDUMP.ASI)"));
            t.items.push_back(OptionItem::MakeInfo(
                "Plugin Status",
                "Indicates whether memdump.asi is currently loaded.",
                [this]() { return (m_Bridge.isMemDumpLoaded && m_Bridge.isMemDumpLoaded()) ? "LOADED" : "NOT LOADED"; }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Enable Memory Dump Engine",
                "Master switch for diagnostic memory dumps in mods\\memory_dumps\\.",
                ApplyScope::RestartRequired,
                [this]() { return m_MdEnable; },
                [this](int v) { m_MdEnable = v; m_MemDumpDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "F2 Quick Diagnostic Dump",
                "Press F2 at any moment in-game to capture state summary, telemetry, and screenshot.",
                ApplyScope::RestartRequired,
                [this]() { return m_MdF2; },
                [this](int v) { m_MdF2 = v; m_MemDumpDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Save D3D12 Screenshot on Dump",
                "Captures the rendered D3D12 backbuffer right before Present.",
                ApplyScope::RestartRequired,
                [this]() { return m_MdScreenshot; },
                [this](int v) { m_MdScreenshot = v; m_MemDumpDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Dump Memory on Game Exit",
                "Automatically writes a diagnostic dump folder when acz.exe exits.",
                ApplyScope::RestartRequired,
                [this]() { return m_MdOnExit; },
                [this](int v) { m_MdOnExit = v; m_MemDumpDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Write Windows Minidump (.dmp)",
                "Generates a standard acz_process.dmp minidump file inside each dump folder.",
                ApplyScope::RestartRequired,
                [this]() { return m_MdMinidump; },
                [this](int v) { m_MdMinidump = v; m_MemDumpDirty = true; SaveExternalInisIfDirty(); }
            ));
            t.items.push_back(OptionItem::MakeToggle(
                "Auto-Clean Old Dumps (24h)",
                "Automatically deletes dump_* folders older than 24 hours on startup.",
                ApplyScope::RestartRequired,
                [this]() { return m_MdAutoClean; },
                [this](int v) { m_MdAutoClean = v; m_MemDumpDirty = true; SaveExternalInisIfDirty(); }
            ));
            m_Tabs.push_back(std::move(t));
        }
    }

    OptionsHostBridge       m_Bridge;
    std::vector<OptionTab>  m_Tabs;
    int                     m_ActiveTab = 0;
    bool                    m_Open = false;
    bool                    m_OpenedFromSwgSubmenu = false;
    float                   m_FadeAlpha = 0.0f;
    float                   m_TimeSec = 0.0f;
    std::string             m_StatusToast;
    float                   m_StatusToastTimer = 0.0f;

    // Cached sibling INI state
    std::wstring m_HdTexIniPath;
    std::wstring m_MemDumpIniPath;
    int  m_HdGlyphs      = 0;
    int  m_HdDdsCache    = 1;
    int  m_HdAutoInstall = 1;
    int  m_HdAutoShare   = 1;
    int  m_HdHotReload   = 0;
    int  m_HdDumpAll     = 0;
    bool m_HdTexDirty    = false;

    int  m_MdEnable      = 1;
    int  m_MdF2          = 1;
    int  m_MdScreenshot  = 1;
    int  m_MdOnExit      = 0;
    int  m_MdMinidump    = 1;
    int  m_MdAutoClean   = 1;
    bool m_MemDumpDirty  = false;
};

} // namespace acz_ui
