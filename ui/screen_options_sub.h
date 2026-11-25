#pragma once
// ============================================================================
// screen_options_sub.h - Custom RmlUi Options Submenus
// Replaces legacy SWG/SWA screens:
//   - ControlSet.swa  (MAIN MENU > OPTIONS > CONTROLLER SETTINGS)
//   - LanguageSet.swa (MAIN MENU > OPTIONS > LANGUAGE SETTINGS)
//   - SoundSet.swa    (MAIN MENU > OPTIONS > SOUND SETTINGS)
//   - Brightness.swa  (OPTIONS > SCREEN SETTINGS > BRIGHTNESS)
//   - ScrPosition.swa (SCREEN POSITION fullscreen calibration)
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "acz_rmlui_backend.h"
#include "screen_options.h"

namespace acz_ui {

class OptionsSubScreen {
public:
    enum class Kind : uint8_t {
        None        = 0,
        ControlSet  = 1,
        LanguageSet = 2,
        SoundSet    = 3,
        Brightness  = 4,
        ScrPosition = 5,
    };

    static Kind MapStemToKind(const std::string& stem) {
        if (stem == "ControlSet")  return Kind::ControlSet;
        if (stem == "LanguageSet") return Kind::LanguageSet;
        if (stem == "SoundSet")    return Kind::SoundSet;
        if (stem == "Brightness")  return Kind::Brightness;
        if (stem == "ScrPosition") return Kind::ScrPosition;
        return Kind::None;
    }

    void SetNativeResolver(uintptr_t exeBase, std::function<uintptr_t(uintptr_t)> rvaFn) {
        m_ExeBase = exeBase;
        m_RvaFn   = std::move(rvaFn);
    }

    void SetSwgSoundObject(void* soundObj) {
        if (soundObj) m_SwgSoundObj = soundObj;
    }

    bool IsActive() const { return m_Active; }
    bool IsLeaving() const { return m_Leaving; }
    Kind ActiveKind() const { return m_Kind; }

    void Begin(Kind kind) {
        if (kind == Kind::None) return;
        if (m_Active && m_Kind == kind) return;

        m_Kind          = kind;
        m_Active        = true;
        m_Leaving       = false;
        m_RowIdx        = 0;
        m_EditingValue  = false;
        m_EditVal       = 0;
        m_EditInitVal   = 0;
        m_DomDirty      = true;
        m_NeedDocReload = false;

        LoadInitialValues();
        PlaySound("NUD_COMMON_SYS_WINDOW_OPEN");
    }

    void End(RmlOptionsController& rml) {
        if (!m_Active) return;
        m_Active  = false;
        m_Leaving = false;
        m_Kind    = Kind::None;
        if (m_Doc) {
            m_Doc->Hide();
        }
        if (rml.IsMainMenuMode()) {
            rml.SetMainDocVisible(true);
        }
    }

    void Shutdown() {
        if (m_Doc) {
            m_Doc->Close();
            m_Doc = nullptr;
        }
        m_Active  = false;
        m_Leaving = false;
        m_Kind    = Kind::None;
    }

    void Update(const UiInputSnapshot& snap, float dtSec, bool f5Edge) {
        if (!m_Active) return;
        (void)dtSec;

        if (f5Edge) {
            m_NeedDocReload = true;
            m_DomDirty      = true;
        }

        if (m_Leaving) return;

        switch (m_Kind) {
            case Kind::ControlSet:  UpdateSplit(snap, 6); break;
            case Kind::LanguageSet: UpdateSplit(snap, 2); break;
            case Kind::SoundSet:    UpdateSound(snap);    break;
            case Kind::Brightness:  UpdateBrightness(snap); break;
            case Kind::ScrPosition: UpdateScrPosition(snap); break;
            default: break;
        }
    }

    void Render(RmlOptionsController& rml, UiRenderer& renderer, InputDeviceKind activeDev) {
        if (!m_Active || !rml.IsReady()) return;
        Rml::Context* ctx = rml.GetContext();
        if (!ctx) return;

        rml.SetMainDocVisible(false);

        if (!m_Doc || m_NeedDocReload) {
            m_NeedDocReload = false;
            if (m_Doc) {
                m_Doc->Close();
                m_Doc = nullptr;
            }
            rml.ReloadStrings();
            Rml::Factory::ClearStyleSheetCache();
            m_Doc = ctx->LoadDocument("options_sub.rml");
            if (!m_Doc) return;
            m_Doc->Show();
            m_DomDirty = true;
            m_LastW    = 0;
            m_LastH    = 0;
        } else if (!m_Doc->IsVisible()) {
            m_Doc->Show();
            m_DomDirty = true;
        }

        const int w = static_cast<int>(renderer.ScreenWidth());
        const int h = static_cast<int>(renderer.ScreenHeight());
        if (w <= 0 || h <= 0) return;

        const float uiScale = (std::min)(static_cast<float>(w) / 1920.0f, static_cast<float>(h) / 1080.0f);
        const int virtW = (std::max)(640, static_cast<int>(std::lround(static_cast<float>(w) / uiScale)));
        const int virtH = (std::max)(480, static_cast<int>(std::lround(static_cast<float>(h) / uiScale)));
        if (auto* ri = rml.GetRenderInterface()) {
            ri->SetUiScale(uiScale);
        }
        if (w != m_LastW || h != m_LastH) {
            m_LastW = w;
            m_LastH = h;
            ctx->SetDimensions(Rml::Vector2i(virtW, virtH));
            ctx->SetDensityIndependentPixelRatio(1.0f);
            rml.ApplyAspectLayoutToDoc(m_Doc, virtW, virtH, 1.0f);
            m_DomDirty = true;
        }

        if (m_DomDirty || activeDev != m_LastDev) {
            m_LastDev = activeDev;
            RebuildDom(rml.GetStrings(), activeDev);
            m_DomDirty = false;
        }

        // Layer 1: Shared ACZ crimson + Command Center + tactical grid
        rml.RenderSharedMenuBackdropBase();

        // Layer 1.5: Native D3D12 fullscreen golden calibration grid for Screen Position
        if (m_Kind == Kind::ScrPosition) {
            RenderScreenPositionGrid(renderer);
        }

        // Layer 2: RmlUi DOM (prompts, folder frame, widgets, footer)
        renderer.SetPremultipliedBlend(true);
        ctx->Update();
        ctx->Render();
        renderer.SetPremultipliedBlend(false);

        // Layer 3: Additive scanline + glowing ember sparks
        rml.RenderSharedMenuBackdropOverlay();
    }

private:
    uintptr_t ResolveRva(uintptr_t rva) const {
        if (!m_ExeBase || !m_RvaFn) return 0;
        return m_ExeBase + m_RvaFn(rva);
    }

    int GetOpt(uintptr_t rva, int fallback) const {
        return SafeSwgGetIntSEH(ResolveRva(rva), fallback);
    }

    void SetOpt(uintptr_t rva, int val) const {
        SafeSwgSetIntSEH(ResolveRva(rva), val);
    }

    void EndNativeSubMenu() {
        m_Leaving = true;
        SafeSwgCallVoidSEH(ResolveRva(NativeOptionRva::kMainEndSubMenu));
    }

    void PlaySound(const char* name) {
        if (m_SwgSoundObj && name && name[0]) {
            SafeSwgOnVoice2DSEH(m_SwgSoundObj, name);
        }
    }

    void LoadInitialValues() {
        if (m_Kind == Kind::ControlSet) {
            m_CtrlVals[0] = (std::max)(0, (std::min)(1, GetOpt(NativeOptionRva::kGetControl, 0)));
            m_CtrlVals[1] = (std::max)(0, (std::min)(1, GetOpt(NativeOptionRva::kGetButton, 0)));
            m_CtrlVals[2] = (std::max)(0, (std::min)(1, GetOpt(NativeOptionRva::kGetLR12Button, 0)));
            m_CtrlVals[3] = (std::max)(0, (std::min)(1, GetOpt(NativeOptionRva::kGetLeftAnalog, 0)));
            m_CtrlVals[4] = (std::max)(0, (std::min)(1, GetOpt(NativeOptionRva::kGetRightAnalog, 0)));
            m_CtrlVals[5] = (std::max)(0, (std::min)(1, GetOpt(NativeOptionRva::kGetVibration, 0)));
        } else if (m_Kind == Kind::LanguageSet) {
            m_LangVals[0] = (std::max)(0, (std::min)(1, GetOpt(NativeOptionRva::kGetLanguage, 1)));
            m_LangVals[1] = (std::max)(0, (std::min)(7, GetOpt(NativeOptionRva::kGetSubLanguage, 1)));
        } else if (m_Kind == Kind::SoundSet) {
            m_SoundMode    = (std::max)(0, (std::min)(2, GetOpt(NativeOptionRva::kGetSound, 1)));
            m_VolVals[0]   = (std::max)(0, (std::min)(15, GetOpt(NativeOptionRva::kGetBGM, 12)));
            m_VolVals[1]   = (std::max)(0, (std::min)(15, GetOpt(NativeOptionRva::kGetVoice, 12)));
            m_VolVals[2]   = (std::max)(0, (std::min)(15, GetOpt(NativeOptionRva::kGetSE, 12)));
            m_VolVals[3]   = (std::max)(0, (std::min)(15, GetOpt(NativeOptionRva::kGetSystemSE, 12)));
            m_InitSoundMode = m_SoundMode;
            for (int i = 0; i < 4; ++i) m_InitVolVals[i] = m_VolVals[i];
        } else if (m_Kind == Kind::Brightness) {
            m_Brightness     = (std::max)(-3, (std::min)(3, GetOpt(NativeOptionRva::kGetBrightness, 0)));
            m_InitBrightness = m_Brightness;
        } else if (m_Kind == Kind::ScrPosition) {
            m_ScreenX     = (std::max)(-16, (std::min)(16, GetOpt(NativeOptionRva::kGetScreenX, 0)));
            m_ScreenY     = (std::max)(-16, (std::min)(16, GetOpt(NativeOptionRva::kGetScreenY, 0)));
            m_InitScreenX = m_ScreenX;
            m_InitScreenY = m_ScreenY;
        }
    }

    int GetSplitChoiceCount(int row) const {
        if (m_Kind == Kind::ControlSet) return 2;
        if (m_Kind == Kind::LanguageSet) return (row == 0) ? 2 : 8;
        return 2;
    }

    int GetSplitValue(int row) const {
        if (m_Kind == Kind::ControlSet && row >= 0 && row < 6) return m_CtrlVals[row];
        if (m_Kind == Kind::LanguageSet && row >= 0 && row < 2) return m_LangVals[row];
        return 0;
    }

    void ApplySplitValue(int row, int val) {
        if (m_Kind == Kind::ControlSet) {
            static constexpr uintptr_t kSetRvas[6] = {
                NativeOptionRva::kSetControl,
                NativeOptionRva::kSetButton,
                NativeOptionRva::kSetLR12Button,
                NativeOptionRva::kSetLeftAnalog,
                NativeOptionRva::kSetRightAnalog,
                NativeOptionRva::kSetVibration,
            };
            if (row >= 0 && row < 6) {
                m_CtrlVals[row] = val;
                SetOpt(kSetRvas[row], val);
            }
        } else if (m_Kind == Kind::LanguageSet) {
            if (row == 0) {
                m_LangVals[0] = val;
                SetOpt(NativeOptionRva::kSetLanguage, val);
            } else if (row == 1) {
                m_LangVals[1] = val;
                SetOpt(NativeOptionRva::kSetSubLanguage, val);
            }
        }
    }

    void UpdateSplit(const UiInputSnapshot& snap, int rowCount) {
        if (!m_EditingValue) {
            if (snap.up) {
                m_RowIdx = (m_RowIdx - 1 + rowCount) % rowCount;
                PlaySound("NUD_COMMON_SYS_CURSOR");
                m_DomDirty = true;
            } else if (snap.down) {
                m_RowIdx = (m_RowIdx + 1) % rowCount;
                PlaySound("NUD_COMMON_SYS_CURSOR");
                m_DomDirty = true;
            } else if (snap.confirm) {
                m_EditingValue = true;
                m_EditVal      = GetSplitValue(m_RowIdx);
                m_EditInitVal  = m_EditVal;
                PlaySound("NUD_COMMON_SYS_DECIDE");
                m_DomDirty = true;
            } else if (snap.cancel) {
                PlaySound("NUD_COMMON_SYS_CANCEL");
                EndNativeSubMenu();
            }
        } else {
            const int cnt = GetSplitChoiceCount(m_RowIdx);
            if (snap.up) {
                m_EditVal = (m_EditVal - 1 + cnt) % cnt;
                ApplySplitValue(m_RowIdx, m_EditVal);
                PlaySound("NUD_COMMON_SYS_CURSOR");
                m_DomDirty = true;
            } else if (snap.down) {
                m_EditVal = (m_EditVal + 1) % cnt;
                ApplySplitValue(m_RowIdx, m_EditVal);
                PlaySound("NUD_COMMON_SYS_CURSOR");
                m_DomDirty = true;
            } else if (snap.confirm) {
                ApplySplitValue(m_RowIdx, m_EditVal);
                m_EditingValue = false;
                PlaySound("NUD_COMMON_SYS_DECIDE");
                m_DomDirty = true;
            } else if (snap.cancel) {
                ApplySplitValue(m_RowIdx, m_EditInitVal);
                m_EditingValue = false;
                PlaySound("NUD_COMMON_SYS_CANCEL");
                m_DomDirty = true;
            }
        }
    }

    void UpdateSound(const UiInputSnapshot& snap) {
        constexpr int kRows = 5; // 0=SOUND, 1=BGM, 2=VOICE, 3=GAME SE, 4=SYSTEM SE
        if (snap.up) {
            m_RowIdx = (m_RowIdx - 1 + kRows) % kRows;
            PlaySound("NUD_COMMON_SYS_CURSOR");
            m_DomDirty = true;
        } else if (snap.down) {
            m_RowIdx = (m_RowIdx + 1) % kRows;
            PlaySound("NUD_COMMON_SYS_CURSOR");
            m_DomDirty = true;
        } else if (snap.left || snap.right) {
            const int delta = snap.right ? 1 : -1;
            if (m_RowIdx == 0) {
                const int next = (std::max)(0, (std::min)(2, m_SoundMode + delta));
                if (next != m_SoundMode) {
                    m_SoundMode = next;
                    SetOpt(NativeOptionRva::kSetSound, m_SoundMode);
                    PlaySound("NUD_COMMON_SYS_CURSOR");
                    m_DomDirty = true;
                }
            } else {
                static constexpr uintptr_t kVolSetRvas[4] = {
                    NativeOptionRva::kSetBGM,
                    NativeOptionRva::kSetVoice,
                    NativeOptionRva::kSetSE,
                    NativeOptionRva::kSetSystemSE,
                };
                const int vi = m_RowIdx - 1;
                const int next = (std::max)(0, (std::min)(15, m_VolVals[vi] + delta));
                if (next != m_VolVals[vi]) {
                    m_VolVals[vi] = next;
                    SetOpt(kVolSetRvas[vi], m_VolVals[vi]);
                    PlaySound("NUD_COMMON_SYS_CURSOR");
                    m_DomDirty = true;
                }
            }
        } else if (snap.resetDef) {
            m_SoundMode  = (std::max)(0, (std::min)(2, GetOpt(NativeOptionRva::kDefSound, 1)));
            m_VolVals[0] = (std::max)(0, (std::min)(15, GetOpt(NativeOptionRva::kDefBGM, 12)));
            m_VolVals[1] = (std::max)(0, (std::min)(15, GetOpt(NativeOptionRva::kDefVoice, 12)));
            m_VolVals[2] = (std::max)(0, (std::min)(15, GetOpt(NativeOptionRva::kDefSE, 12)));
            m_VolVals[3] = (std::max)(0, (std::min)(15, GetOpt(NativeOptionRva::kDefSystemSE, 12)));
            SetOpt(NativeOptionRva::kSetSound, m_SoundMode);
            SetOpt(NativeOptionRva::kSetBGM, m_VolVals[0]);
            SetOpt(NativeOptionRva::kSetVoice, m_VolVals[1]);
            SetOpt(NativeOptionRva::kSetSE, m_VolVals[2]);
            SetOpt(NativeOptionRva::kSetSystemSE, m_VolVals[3]);
            PlaySound("NUD_COMMON_SYS_DECIDE");
            m_DomDirty = true;
        } else if (snap.confirm) {
            PlaySound("NUD_COMMON_SYS_DECIDE");
            EndNativeSubMenu();
        } else if (snap.cancel) {
            // Revert to values upon entry
            SetOpt(NativeOptionRva::kSetSound, m_InitSoundMode);
            SetOpt(NativeOptionRva::kSetBGM, m_InitVolVals[0]);
            SetOpt(NativeOptionRva::kSetVoice, m_InitVolVals[1]);
            SetOpt(NativeOptionRva::kSetSE, m_InitVolVals[2]);
            SetOpt(NativeOptionRva::kSetSystemSE, m_InitVolVals[3]);
            PlaySound("NUD_COMMON_SYS_CANCEL");
            EndNativeSubMenu();
        }
    }

    void UpdateBrightness(const UiInputSnapshot& snap) {
        if (snap.left || snap.right) {
            const int next = (std::max)(-3, (std::min)(3, m_Brightness + (snap.right ? 1 : -1)));
            if (next != m_Brightness) {
                m_Brightness = next;
                SetOpt(NativeOptionRva::kSetBrightness, m_Brightness);
                PlaySound("NUD_COMMON_SYS_CURSOR");
                m_DomDirty = true;
            }
        } else if (snap.resetDef) {
            m_Brightness = (std::max)(-3, (std::min)(3, GetOpt(NativeOptionRva::kDefBrightness, 0)));
            SetOpt(NativeOptionRva::kSetBrightness, m_Brightness);
            PlaySound("NUD_COMMON_SYS_DECIDE");
            m_DomDirty = true;
        } else if (snap.confirm) {
            PlaySound("NUD_COMMON_SYS_DECIDE");
            EndNativeSubMenu();
        } else if (snap.cancel) {
            SetOpt(NativeOptionRva::kSetBrightness, m_InitBrightness);
            PlaySound("NUD_COMMON_SYS_CANCEL");
            EndNativeSubMenu();
        }
    }

    void UpdateScrPosition(const UiInputSnapshot& snap) {
        bool moved = false;
        if (snap.left  && m_ScreenX > -16) { --m_ScreenX; moved = true; }
        if (snap.right && m_ScreenX <  16) { ++m_ScreenX; moved = true; }
        if (snap.up    && m_ScreenY > -16) { --m_ScreenY; moved = true; }
        if (snap.down  && m_ScreenY <  16) { ++m_ScreenY; moved = true; }
        if (moved) {
            SetOpt(NativeOptionRva::kSetScreenX, m_ScreenX);
            SetOpt(NativeOptionRva::kSetScreenY, m_ScreenY);
            PlaySound("NUD_COMMON_SYS_CURSOR");
            m_DomDirty = true;
        } else if (snap.resetDef) {
            m_ScreenX = GetOpt(NativeOptionRva::kDefScreenX, 0);
            m_ScreenY = GetOpt(NativeOptionRva::kDefScreenY, 0);
            SetOpt(NativeOptionRva::kSetScreenX, m_ScreenX);
            SetOpt(NativeOptionRva::kSetScreenY, m_ScreenY);
            PlaySound("NUD_COMMON_SYS_DECIDE");
            m_DomDirty = true;
        } else if (snap.confirm) {
            PlaySound("NUD_COMMON_SYS_DECIDE");
            EndNativeSubMenu();
        } else if (snap.cancel) {
            SetOpt(NativeOptionRva::kSetScreenX, m_InitScreenX);
            SetOpt(NativeOptionRva::kSetScreenY, m_InitScreenY);
            PlaySound("NUD_COMMON_SYS_CANCEL");
            EndNativeSubMenu();
        }
    }

    void ApplyAspectLayout(int virtW) {
        if (!m_Doc) return;
        const float safeW = (std::min)(static_cast<float>(virtW) - 96.0f, 1736.0f);
        const float sidePx = (std::max)(48.0f, (static_cast<float>(virtW) - safeW) * 0.5f);
        char pxBuf[32];
        std::snprintf(pxBuf, sizeof(pxBuf), "%.0fpx", sidePx);

        if (auto* el = m_Doc->GetElementById("top-prompts")) {
            el->SetProperty("left", pxBuf);
            el->SetProperty("right", pxBuf);
        }
        if (auto* el = m_Doc->GetElementById("menu-frame")) {
            el->SetProperty("left", pxBuf);
            el->SetProperty("right", pxBuf);
        }
        if (auto* el = m_Doc->GetElementById("footer-desc")) {
            el->SetProperty("padding-left", pxBuf);
            el->SetProperty("padding-right", pxBuf);
        }
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

    void SetElText(const char* id, const std::string& txt) {
        if (!m_Doc) return;
        if (auto* el = m_Doc->GetElementById(id)) {
            el->SetInnerRML(EscapeHtml(txt));
        }
    }

    void SetElHidden(const char* id, bool hidden) {
        if (!m_Doc) return;
        if (auto* el = m_Doc->GetElementById(id)) {
            el->SetClass("hidden", hidden);
        }
    }

    void UpdatePromptIcons(const UiStringTable& strings, InputDeviceKind dev) {
        const char* okSrc  = "assets/icons/ps/PS5_Cross.png";
        const char* canSrc = "assets/icons/ps/PS5_Circle.png";
        const char* defSrc = "assets/icons/ps/PS5_Triangle.png";
        if (dev == InputDeviceKind::Xbox) {
            okSrc  = "assets/icons/xbox/XboxSeriesX_A.png";
            canSrc = "assets/icons/xbox/XboxSeriesX_B.png";
            defSrc = "assets/icons/xbox/XboxSeriesX_Y.png";
        } else if (dev == InputDeviceKind::Keyboard) {
            okSrc  = "assets/icons/kb/Enter_Alt_Key_Dark.png";
            canSrc = "assets/icons/kb/Esc_Key_Dark.png";
            defSrc = "assets/icons/kb/R_Key_Dark.png";
        }
        if (auto* el = m_Doc->GetElementById("opt-ico-ok"))     el->SetAttribute("src", okSrc);
        if (auto* el = m_Doc->GetElementById("opt-ico-cancel")) el->SetAttribute("src", canSrc);
        if (auto* el = m_Doc->GetElementById("opt-ico-def"))    el->SetAttribute("src", defSrc);

        SetElText("opt-lbl-ok",     strings.Get("optsub.ok", "OK"));
        SetElText("opt-lbl-cancel", strings.Get("optsub.cancel", "CANCEL"));
        SetElText("opt-lbl-def",    strings.Get("optsub.default", "DEFAULT"));

        const bool hasDefault = (m_Kind == Kind::SoundSet || m_Kind == Kind::Brightness || m_Kind == Kind::ScrPosition);
        SetElHidden("opt-sep-def", !hasDefault);
        SetElHidden("opt-ico-def", !hasDefault);
        SetElHidden("opt-lbl-def", !hasDefault);
    }

    void RebuildDom(const UiStringTable& strings, InputDeviceKind dev) {
        if (!m_Doc) return;
        UpdatePromptIcons(strings, dev);

        const bool hideFrame = (m_Kind == Kind::ScrPosition);
        SetElHidden("menu-frame", hideFrame);

        auto* bodyEl = m_Doc->GetElementById("opt-body");
        if (!bodyEl) return;

        if (m_Kind == Kind::ControlSet) {
            SetElText("opt-crumb-text", strings.Get("optsub.ctrl.crumb", "MAIN MENU > OPTIONS > CONTROLLER SETTINGS"));
            static const char* kRowLblKeys[6] = {
                "optsub.ctrl.0.label", "optsub.ctrl.1.label", "optsub.ctrl.2.label",
                "optsub.ctrl.3.label", "optsub.ctrl.4.label", "optsub.ctrl.5.label"
            };
            static const char* kRowLblDef[6] = {
                "CONTROL TYPE", "RADAR/SWITCH WEAPONS", "ACCELERATION/YAW",
                "LEFT STICK SETTINGS", "RIGHT STICK SETTINGS", "VIBRATION"
            };
            static const char* kRowDescKeys[6] = {
                "optsub.ctrl.0.desc", "optsub.ctrl.1.desc", "optsub.ctrl.2.desc",
                "optsub.ctrl.3.desc", "optsub.ctrl.4.desc", "optsub.ctrl.5.desc"
            };
            static const char* kRowDescDef[6] = {
                "Switch between Normal and Novice controls.",
                "Switch button functions for Radar and Switch Weapons.",
                "Switch button functions for Acceleration/Deceleration and Yaw.",
                "Switch vertical pitch control on the left stick.",
                "Switch vertical camera control on the right stick.",
                "Toggle controller vibration function on or off."
            };
            static const char* kValKeys[6][2] = {
                { "optsub.ctrl.0.v0", "optsub.ctrl.0.v1" },
                { "optsub.ctrl.1.v0", "optsub.ctrl.1.v1" },
                { "optsub.ctrl.2.v0", "optsub.ctrl.2.v1" },
                { "optsub.ctrl.3.v0", "optsub.ctrl.3.v1" },
                { "optsub.ctrl.4.v0", "optsub.ctrl.4.v1" },
                { "optsub.ctrl.5.v0", "optsub.ctrl.5.v1" },
            };
            static const char* kValDef[6][2] = {
                { "NORMAL", "NOVICE" },
                { "NORMAL", "INVERTED" },
                { "NORMAL", "INVERTED" },
                { "NORMAL", "REVERSE" },
                { "NORMAL", "REVERSE" },
                { "ON", "OFF" },
            };

            std::string html = "<div class=\"opt-split-left\">";
            for (int r = 0; r < 6; ++r) {
                std::string cls = "opt-left-row";
                if (r == m_RowIdx) cls += m_EditingValue ? " editing" : " selected";
                html += "<div class=\"" + cls + "\"><span>" + EscapeHtml(strings.Get(kRowLblKeys[r], kRowLblDef[r])) + "</span></div>";
            }
            html += "</div><div class=\"opt-split-right\">";
            for (int r = 0; r < 6; ++r) {
                const bool activeGrp = (r == m_RowIdx);
                html += "<div class=\"opt-right-group";
                if (activeGrp) html += " active";
                html += "\">";
                for (int c = 0; c < 2; ++c) {
                    const bool isCur = (m_CtrlVals[r] == c);
                    const bool isFoc = (activeGrp && m_EditingValue && m_EditVal == c);
                    html += "<div class=\"opt-right-choice";
                    if (isFoc) html += " focused";
                    html += "\"><span class=\"opt-sq\"></span><span class=\"opt-caret\">";
                    html += isCur ? "&gt;" : " ";
                    html += "</span><span>" + EscapeHtml(strings.Get(kValKeys[r][c], kValDef[r][c])) + "</span></div>";
                }
                html += "</div>";
            }
            html += "</div>";
            bodyEl->SetInnerRML(html);

            const int safeRow = (std::max)(0, (std::min)(5, m_RowIdx));
            SetElText("footer-desc", strings.Get(kRowDescKeys[safeRow], kRowDescDef[safeRow]));
            return;
        }

        if (m_Kind == Kind::LanguageSet) {
            SetElText("opt-crumb-text", strings.Get("optsub.lang.crumb", "MAIN MENU > OPTIONS > LANGUAGE SETTINGS"));
            const char* rowLbls[2] = {
                "optsub.lang.0.label", "optsub.lang.1.label"
            };
            const char* rowDefs[2] = { "VOICE LANGUAGE", "SUBTITLE LANGUAGE" };
            const char* voiceKeys[2] = { "voice.0.label", "voice.1.label" };
            const char* voiceDefs[2] = { "JAPANESE", "ENGLISH" };
            const char* subKeys[8] = {
                "sub.0.label", "sub.1.label", "sub.2.label", "sub.3.label",
                "sub.4.label", "sub.5.label", "sub.6.label", "sub.7.label"
            };
            const char* subDefs[8] = {
                "JAPANESE", "ENGLISH(US)", "ENGLISH(UK)", "GERMAN",
                "FRENCH", "ITALIAN", "SPANISH", "NO SUB"
            };

            std::string html = "<div class=\"opt-split-left\">";
            for (int r = 0; r < 2; ++r) {
                std::string cls = "opt-left-row";
                if (r == m_RowIdx) cls += m_EditingValue ? " editing" : " selected";
                html += "<div class=\"" + cls + "\"><span>" + EscapeHtml(strings.Get(rowLbls[r], rowDefs[r])) + "</span></div>";
            }
            html += "</div><div class=\"opt-split-right\">";
            for (int r = 0; r < 2; ++r) {
                const bool activeGrp = (r == m_RowIdx);
                const int cnt = (r == 0) ? 2 : 8;
                html += "<div class=\"opt-right-group";
                if (activeGrp) html += " active";
                html += "\">";
                for (int c = 0; c < cnt; ++c) {
                    const bool isCur = (m_LangVals[r] == c);
                    const bool isFoc = (activeGrp && m_EditingValue && m_EditVal == c);
                    const std::string lbl = (r == 0)
                        ? strings.Get(voiceKeys[c], voiceDefs[c])
                        : strings.Get(subKeys[c], subDefs[c]);
                    html += "<div class=\"opt-right-choice";
                    if (isFoc) html += " focused";
                    html += "\"><span class=\"opt-sq\"></span><span class=\"opt-caret\">";
                    html += isCur ? "&gt;" : " ";
                    html += "</span><span>" + EscapeHtml(lbl) + "</span></div>";
                }
                html += "</div>";
            }
            html += "</div>";
            bodyEl->SetInnerRML(html);

            if (m_RowIdx == 0) {
                SetElText("footer-desc", strings.Get("optsub.lang.0.desc", "Set voiceover language."));
            } else {
                SetElText("footer-desc", strings.Get("optsub.lang.1.desc", "Set subtitle language."));
            }
            return;
        }

        if (m_Kind == Kind::SoundSet) {
            SetElText("opt-crumb-text", strings.Get("optsub.sound.crumb", "MAIN MENU > OPTIONS > SOUND SETTINGS"));
            std::string html = "<div class=\"opt-sound-wrap\">";

            // Row 0: SOUND MODE (MONO | STEREO | SURROUND)
            const char* modeKeys[3] = { "optsub.sound.mono", "optsub.sound.stereo", "optsub.sound.surround" };
            const char* modeDefs[3] = { "MONO", "STEREO", "SURROUND" };
            html += std::string("<div class=\"opt-sound-section") + (m_RowIdx == 0 ? " focused" : "") + "\">";
            html += "<div class=\"opt-sound-label\">" + EscapeHtml(strings.Get("optsub.sound.mode", "SOUND")) + "</div>";
            html += "<div class=\"opt-mode-row\">";
            for (int m = 0; m < 3; ++m) {
                if (m > 0) {
                    const bool showLeftArrow  = (m_RowIdx == 0 && m_SoundMode == m);
                    const bool showRightArrow = (m_RowIdx == 0 && m_SoundMode == m - 1);
                    html += "<span class=\"opt-arrow-gap";
                    if (showLeftArrow || showRightArrow) html += " visible";
                    html += "\">";
                    if (showLeftArrow) html += "&lt;";
                    else if (showRightArrow) html += "&gt;";
                    else html += " ";
                    html += "</span>";
                }
                html += std::string("<div class=\"opt-mode-btn") + (m_SoundMode == m ? " selected" : "") + "\">";
                html += EscapeHtml(strings.Get(modeKeys[m], modeDefs[m])) + "</div>";
            }
            html += "</div></div>";

            // Rows 1..4: BGM / VOICE / GAME SE / SYSTEM SE (0..15)
            static const char* kVolKeys[4] = { "optsub.sound.bgm", "optsub.sound.voice", "optsub.sound.se", "optsub.sound.sys" };
            static const char* kVolDefs[4] = { "BGM VOLUME", "VOICE VOLUME", "GAME SE VOLUME", "SYSTEM SE VOLUME" };
            for (int v = 0; v < 4; ++v) {
                const bool foc = (m_RowIdx == v + 1);
                const int val = m_VolVals[v];
                char numBuf[16];
                std::snprintf(numBuf, sizeof(numBuf), "[%2d]", val);
                html += std::string("<div class=\"opt-sound-section") + (foc ? " focused" : "") + "\">";
                html += "<div class=\"opt-sound-label\">" + EscapeHtml(strings.Get(kVolKeys[v], kVolDefs[v])) + "</div>";
                html += "<div class=\"opt-vol-row\">";
                html += "<div class=\"opt-vol-num\">" + EscapeHtml(numBuf) + "</div>";
                html += "<div class=\"opt-cells\">";
                for (int c = 0; c < 15; ++c) {
                    const bool filled = (c < val);
                    const bool tip    = (foc && val > 0 && c == val - 1);
                    std::string cls = "opt-cell";
                    if (filled) cls += " filled";
                    if (tip)    cls += " active-tip";
                    html += "<div class=\"" + cls + "\">" + (filled ? "X" : " ") + "</div>";
                }
                html += "</div></div></div>";
            }
            html += "</div>";
            bodyEl->SetInnerRML(html);

            static const char* kDescKeys[5] = {
                "sound.mode.desc", "sound.bgm.desc", "sound.radio.desc", "sound.se.desc", "sound.sys.desc"
            };
            static const char* kDescDefs[5] = {
                "Use the left stick to switch between Mono, Stereo, and\nSurround settings.",
                "Use the left stick to set BGM volume.\n[0]: Mute   [15]: Max volume\nDoes not apply to some scenes.",
                "Use the left stick to set radio transmissions volume.\n[0]: Mute   [15]: Max volume\nDoes not apply to some scenes.",
                "Use the left stick to set game sound effect volume.\n[0]: Mute   [15]: Max volume\nDoes not apply to some scenes.",
                "Use the left stick to set system sound effect volume.\n[0]: Mute   [15]: Max volume\nDoes not apply to some scenes.",
            };
            const int sr = (std::max)(0, (std::min)(4, m_RowIdx));
            SetElText("footer-desc", strings.Get(kDescKeys[sr], kDescDefs[sr]));
            return;
        }

        if (m_Kind == Kind::Brightness) {
            SetElText("opt-crumb-text", strings.Get("optsub.bright.crumb", "OPTIONS > SCREEN SETTINGS > BRIGHTNESS"));
            char valBuf[24];
            if (m_Brightness == 0) std::snprintf(valBuf, sizeof(valBuf), "[+-0]");
            else std::snprintf(valBuf, sizeof(valBuf), "[%+d]", m_Brightness);

            // Use authentic ± character for zero (UTF-8: \xC2\xB1)
            std::string valStr = (m_Brightness == 0) ? "[\xC2\xB1" "0]" : std::string(valBuf);

            std::string html = "<div class=\"opt-bright-wrap\">";
            html += "<div class=\"opt-bright-top\">";
            html += "<div class=\"opt-bright-title\">" + EscapeHtml(strings.Get("optsub.bright.label", "BRIGHTNESS LEVEL")) + "</div>";
            html += "<div class=\"opt-bright-ctrl\">";
            html += "<div class=\"opt-bright-val\">" + EscapeHtml(valStr) + "</div>";
            html += "<div class=\"opt-bright-cells\">";
            for (int lv = -3; lv <= 3; ++lv) {
                const bool isAct = (lv == m_Brightness);
                const bool isFilled = (lv <= m_Brightness);
                if (isAct && lv > -3) html += "<span class=\"opt-barrow\">&lt;</span>";
                html += std::string("<div class=\"opt-bcell") + (isAct ? " active" : "") + "\">";
                html += isFilled ? "X" : " ";
                html += "</div>";
                if (isAct && lv < 3) html += "<span class=\"opt-barrow\">&gt;</span>";
            }
            html += "</div></div></div>";

            // 4x10 calibration swatch grid (White, Red, Green, Blue)
            html += "<div class=\"opt-bright-box\">";
            const float brightBias = static_cast<float>(m_Brightness) * 18.0f;
            for (int row = 0; row < 4; ++row) {
                html += "<div class=\"opt-swatch-row\">";
                for (int col = 0; col < 10; ++col) {
                    const float base = 245.0f * (1.0f - static_cast<float>(col) * 0.095f);
                    const int v = (std::max)(8, (std::min)(255, static_cast<int>(std::lround(base + brightBias))));
                    int r = (row == 0 || row == 1) ? v : 0;
                    int g = (row == 0 || row == 2) ? v : 0;
                    int b = (row == 0 || row == 3) ? v : 0;
                    char styleBuf[64];
                    std::snprintf(styleBuf, sizeof(styleBuf), "background-color: rgb(%d,%d,%d);", r, g, b);
                    html += std::string("<div class=\"opt-swatch\" style=\"") + styleBuf + "\"></div>";
                }
                html += "</div>";
            }
            html += "</div></div>";
            bodyEl->SetInnerRML(html);

            SetElText("footer-desc", strings.Get("optsub.bright.desc", "Use the left stick to adjust screen brightness."));
            return;
        }

        if (m_Kind == Kind::ScrPosition) {
            bodyEl->SetInnerRML("");
            char posDesc[256];
            const std::string baseDesc = strings.Get("optsub.pos.desc", "Use the left stick to adjust screen position.");
            std::snprintf(posDesc, sizeof(posDesc), "%s  [X:%+d  Y:%+d]", baseDesc.c_str(), m_ScreenX, m_ScreenY);
            SetElText("footer-desc", posDesc);
        }
    }

    void RenderScreenPositionGrid(UiRenderer& r) {
        const float vw = r.VirtualWidth();
        const float vh = r.VirtualHeight();
        const float offX = static_cast<float>(m_ScreenX) * 3.0f;
        const float offY = static_cast<float>(m_ScreenY) * 3.0f;

        const Color lineGold(210, 152, 24, 185);
        const Color dimGold(180, 126, 20, 115);
        const Color brightGold(255, 208, 42, 245);

        const float ox = 64.0f + offX;
        const float oy = 38.0f + offY;
        const float ow = vw - 128.0f;
        const float oh = vh - 76.0f;

        const float ix = 220.0f + offX;
        const float iy = 122.0f + offY;
        const float iw = vw - 440.0f;
        const float ih = vh - 244.0f;

        // Outer and inner calibration frames
        r.DrawRectOutline(ox, oy, ow, oh, 2.5f, lineGold);
        r.DrawRectOutline(ix, iy, iw, ih, 2.5f, lineGold);

        // Fullscreen margin guide ticks extending to screen edges
        r.DrawLine(ix, 0.0f, ix, vh, 1.5f, dimGold);
        r.DrawLine(ix + iw, 0.0f, ix + iw, vh, 1.5f, dimGold);
        r.DrawLine(0.0f, iy, vw, iy, 1.5f, dimGold);
        r.DrawLine(0.0f, iy + ih, vw, iy + ih, 1.5f, dimGold);

        // Full X diagonals from outer corners through center
        r.DrawLine(ox, oy, ox + ow, oy + oh, 2.2f, lineGold);
        r.DrawLine(ox + ow, oy, ox, oy + oh, 2.2f, lineGold);

        // 4 directional golden arrows around center
        const float cx = ox + ow * 0.5f;
        const float cy = oy + oh * 0.5f;
        const float gap = 58.0f;
        const float aw  = 20.0f;
        const float ah  = 16.0f;

        // Up arrow
        for (int i = 0; i < static_cast<int>(ah); ++i) {
            const float t = static_cast<float>(i) / ah;
            const float hw = aw * t;
            r.DrawRectFilled(cx - hw, cy - gap - ah + static_cast<float>(i), hw * 2.0f + 1.0f, 1.5f, brightGold);
        }
        // Down arrow
        for (int i = 0; i < static_cast<int>(ah); ++i) {
            const float t = 1.0f - (static_cast<float>(i) / ah);
            const float hw = aw * t;
            r.DrawRectFilled(cx - hw, cy + gap + static_cast<float>(i), hw * 2.0f + 1.0f, 1.5f, brightGold);
        }
        // Left arrow
        for (int i = 0; i < static_cast<int>(ah); ++i) {
            const float t = static_cast<float>(i) / ah;
            const float hh = aw * t;
            r.DrawRectFilled(cx - gap - ah + static_cast<float>(i), cy - hh, 1.5f, hh * 2.0f + 1.0f, brightGold);
        }
        // Right arrow
        for (int i = 0; i < static_cast<int>(ah); ++i) {
            const float t = 1.0f - (static_cast<float>(i) / ah);
            const float hh = aw * t;
            r.DrawRectFilled(cx + gap + static_cast<float>(i), cy - hh, 1.5f, hh * 2.0f + 1.0f, brightGold);
        }
    }

    uintptr_t                           m_ExeBase       = 0;
    std::function<uintptr_t(uintptr_t)> m_RvaFn;
    void*                               m_SwgSoundObj   = nullptr;
    Rml::ElementDocument*               m_Doc           = nullptr;

    Kind            m_Kind           = Kind::None;
    bool            m_Active         = false;
    bool            m_Leaving        = false;
    bool            m_DomDirty       = true;
    bool            m_NeedDocReload  = false;
    int             m_LastW          = 0;
    int             m_LastH          = 0;
    InputDeviceKind m_LastDev        = InputDeviceKind::PlayStation;

    int  m_RowIdx       = 0;
    bool m_EditingValue = false;
    int  m_EditVal      = 0;
    int  m_EditInitVal  = 0;

    int  m_CtrlVals[6]     = { 0, 0, 0, 0, 0, 0 };
    int  m_LangVals[2]     = { 1, 1 };
    int  m_SoundMode       = 1;
    int  m_InitSoundMode   = 1;
    int  m_VolVals[4]      = { 12, 12, 12, 12 };
    int  m_InitVolVals[4]  = { 12, 12, 12, 12 };
    int  m_Brightness      = 0;
    int  m_InitBrightness  = 0;
    int  m_ScreenX         = 0;
    int  m_ScreenY         = 0;
    int  m_InitScreenX     = 0;
    int  m_InitScreenY     = 0;
};

} // namespace acz_ui
