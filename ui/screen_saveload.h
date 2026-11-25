#pragma once
// ============================================================================
// screen_saveload.h - Custom RmlUi Save / Load Screen (replaces SaveLoad.swa)
// Directly interfaces with acz.exe's native CSelectMemorycard vtable (0x31DFC8)
// under SEH protection, preserving 100% native save-file compatibility while
// rendering crisp HD ACZ UI with dynamic PS/Xbox/KB icons and hot-reload (F5).
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "acz_rmlui_backend.h"

namespace acz_ui {

// SEH-safe CSelectMemorycard native vtable helpers (no C++ destructors inside __try)
struct McNativeDispatch {
    uintptr_t receiverAddr = 0; // g_ExeBase + AczRva(0x31DFC8)
    uintptr_t arrayAddr    = 0; // g_ExeBase + AczRva(0x31DFD0), 16 listeners
    uintptr_t stateAddr    = 0; // g_ExeBase + AczRva(0x30F2C4)
};

inline void SafeMcCallVoid0SEH(const McNativeDispatch& d, size_t vtblOff) {
    if (!d.receiverAddr || !d.arrayAddr) return;
    __try {
        using Fn = void(__fastcall*)(void*);
        void** arr = reinterpret_cast<void**>(d.arrayAddr);
        for (int i = 0; i < 16; ++i) {
            void* obj = arr[i];
            if (obj) {
                void** vt = *reinterpret_cast<void***>(obj);
                if (vt) reinterpret_cast<Fn>(vt[vtblOff / 8])(obj);
            }
        }
        void* mainObj = *reinterpret_cast<void**>(d.receiverAddr);
        if (mainObj) {
            void** vt = *reinterpret_cast<void***>(mainObj);
            if (vt) reinterpret_cast<Fn>(vt[vtblOff / 8])(mainObj);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

inline void SafeMcCallVoid1SEH(const McNativeDispatch& d, size_t vtblOff, int arg) {
    if (!d.receiverAddr || !d.arrayAddr) return;
    __try {
        using Fn = void(__fastcall*)(void*, int);
        void** arr = reinterpret_cast<void**>(d.arrayAddr);
        for (int i = 0; i < 16; ++i) {
            void* obj = arr[i];
            if (obj) {
                void** vt = *reinterpret_cast<void***>(obj);
                if (vt) reinterpret_cast<Fn>(vt[vtblOff / 8])(obj, arg);
            }
        }
        void* mainObj = *reinterpret_cast<void**>(d.receiverAddr);
        if (mainObj) {
            void** vt = *reinterpret_cast<void***>(mainObj);
            if (vt) reinterpret_cast<Fn>(vt[vtblOff / 8])(mainObj, arg);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

inline int SafeMcCallInt0SEH(const McNativeDispatch& d, size_t vtblOff, int fallback) {
    if (!d.receiverAddr) return fallback;
    int res = fallback;
    __try {
        using Fn = int(__fastcall*)(void*);
        void* mainObj = *reinterpret_cast<void**>(d.receiverAddr);
        if (mainObj) {
            void** vt = *reinterpret_cast<void***>(mainObj);
            if (vt) res = reinterpret_cast<Fn>(vt[vtblOff / 8])(mainObj);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return res;
}

inline int SafeMcCallInt1SEH(const McNativeDispatch& d, size_t vtblOff, int arg, int fallback) {
    if (!d.receiverAddr) return fallback;
    int res = fallback;
    __try {
        using Fn = int(__fastcall*)(void*, int);
        void* mainObj = *reinterpret_cast<void**>(d.receiverAddr);
        if (mainObj) {
            void** vt = *reinterpret_cast<void***>(mainObj);
            if (vt) res = reinterpret_cast<Fn>(vt[vtblOff / 8])(mainObj, arg);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return res;
}

inline uint32_t SafeMcReadStateSEH(const McNativeDispatch& d) {
    if (!d.stateAddr) return 0u;
    uint32_t st = 0u;
    __try {
        st = *reinterpret_cast<const uint32_t*>(d.stateAddr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return st;
}

class SaveLoadScreen {
public:
    enum class Phase : uint8_t {
        SelectSlot    = 0,
        Confirm       = 1,
        ConfirmCancel = 2,
        Busy          = 3,
        Complete      = 4,
    };

    struct SlotInfo {
        bool isFile     = false;
        int  missionNum = 0;
        int  level      = -1;
        int  hour       = 0;
        int  minute     = 0;
        int  second     = 0;
        bool isClear    = false;
    };

    void SetNativeDispatch(uintptr_t receiverAddr, uintptr_t arrayAddr, uintptr_t stateAddr) {
        m_Dispatch.receiverAddr = receiverAddr;
        m_Dispatch.arrayAddr    = arrayAddr;
        m_Dispatch.stateAddr    = stateAddr;
    }

    void SetSwgSoundObject(void* soundObj) {
        if (soundObj) m_SwgSoundObj = soundObj;
    }

    bool IsActive() const { return m_Active; }
    bool IsLeaving() const { return m_Leaving; }

    void Begin() {
        if (m_Active) return;
        m_Active        = true;
        m_Leaving       = false;
        m_Phase         = Phase::SelectSlot;
        m_ElapsedSec    = 0.0f;
        m_BusyElapsed   = 0.0f;
        m_ConfirmYes    = false;
        m_DomDirty      = true;
        m_NeedDocReload = false;

        // Kick native CSelectMemorycard slot scan & read mode/default slot
        SafeMcCallVoid0SEH(m_Dispatch, kVtblBegin);
        m_IsSaveMode = (SafeMcCallInt0SEH(m_Dispatch, kVtblIsSaveMode, 1) != 0);
        const int defSlot = SafeMcCallInt0SEH(m_Dispatch, kVtblGetDefaultNumber, 0);
        m_SelectedSlot = (defSlot >= 0 && defSlot < 3) ? defSlot : 0;
        RefreshSlots();
        PlaySound("NUD_COMMON_SYS_WINDOW_OPEN");
    }

    void End(RmlOptionsController& rml) {
        if (!m_Active) return;
        m_Active  = false;
        m_Leaving = false;
        if (m_Doc) {
            m_Doc->Hide();
        }
        rml.SetMainDocVisible(true);
    }

    void Shutdown() {
        if (m_Doc) {
            m_Doc->Close();
            m_Doc = nullptr;
        }
        m_Active  = false;
        m_Leaving = false;
    }

    void Update(const UiInputSnapshot& snap, float dtSec, bool f5Edge) {
        if (!m_Active) return;
        const float dt = (std::max)(0.0f, (std::min)(dtSec, 0.1f));
        m_ElapsedSec += dt;

        if (f5Edge) {
            m_NeedDocReload = true;
            m_DomDirty = true;
        }

        // Re-poll slots briefly after entry in case CSelectMemorycard finishes async scan a few frames in
        if (m_Phase == Phase::SelectSlot && m_ElapsedSec < 0.6f) {
            if (RefreshSlots()) {
                m_DomDirty = true;
            }
        }

        if (m_Leaving) {
            return;
        }

        if (m_Phase == Phase::SelectSlot) {
            if (snap.up) {
                m_SelectedSlot = (m_SelectedSlot + 2) % 3;
                PlaySound("NUD_COMMON_SYS_CURSOR");
                m_DomDirty = true;
            } else if (snap.down) {
                m_SelectedSlot = (m_SelectedSlot + 1) % 3;
                PlaySound("NUD_COMMON_SYS_CURSOR");
                m_DomDirty = true;
            } else if (snap.confirm) {
                if (!m_IsSaveMode && !m_Slots[m_SelectedSlot].isFile) {
                    // Cannot load an empty slot
                    PlaySound("NUD_COMMON_SYS_CANCEL");
                } else {
                    PlaySound("NUD_COMMON_SYS_DECIDE");
                    m_Phase = Phase::Confirm;
                    // Default to NO on overwrite for safety, YES on new save / load
                    m_ConfirmYes = !(m_IsSaveMode && m_Slots[m_SelectedSlot].isFile);
                    m_DomDirty = true;
                }
            } else if (snap.cancel) {
                PlaySound("NUD_COMMON_SYS_CANCEL");
                m_Phase      = Phase::ConfirmCancel;
                m_ConfirmYes = false; // Default to NO, matching vanilla ACZ
                m_DomDirty   = true;
            }
            return;
        }

        if (m_Phase == Phase::ConfirmCancel) {
            if (snap.up || snap.down) {
                m_ConfirmYes = !m_ConfirmYes;
                PlaySound("NUD_COMMON_SYS_CURSOR");
                m_DomDirty = true;
            } else if (snap.cancel) {
                PlaySound("NUD_COMMON_SYS_CANCEL");
                m_Phase    = Phase::SelectSlot;
                m_DomDirty = true;
            } else if (snap.confirm) {
                if (!m_ConfirmYes) {
                    PlaySound("NUD_COMMON_SYS_CANCEL");
                    m_Phase    = Phase::SelectSlot;
                    m_DomDirty = true;
                } else {
                    PlaySound("NUD_COMMON_SYS_DECIDE");
                    m_Leaving = true;
                    SafeMcCallVoid0SEH(m_Dispatch, kVtblCancelMode);
                    SafeMcCallVoid0SEH(m_Dispatch, kVtblEndMode);
                }
            }
            return;
        }

        if (m_Phase == Phase::Confirm) {
            if (snap.up || snap.down) {
                m_ConfirmYes = !m_ConfirmYes;
                PlaySound("NUD_COMMON_SYS_CURSOR");
                m_DomDirty = true;
            } else if (snap.cancel) {
                PlaySound("NUD_COMMON_SYS_CANCEL");
                m_Phase = Phase::SelectSlot;
                m_DomDirty = true;
            } else if (snap.confirm) {
                if (!m_ConfirmYes) {
                    PlaySound("NUD_COMMON_SYS_CANCEL");
                    m_Phase = Phase::SelectSlot;
                    m_DomDirty = true;
                } else {
                    PlaySound("NUD_COMMON_SYS_DECIDE");
                    m_Phase       = Phase::Busy;
                    m_BusyElapsed = 0.0f;
                    m_DomDirty    = true;
                    if (m_IsSaveMode) {
                        SafeMcCallVoid1SEH(m_Dispatch, kVtblBeginSave, m_SelectedSlot);
                    } else {
                        SafeMcCallVoid1SEH(m_Dispatch, kVtblBeginLoad, m_SelectedSlot);
                    }
                }
            }
            return;
        }

        if (m_Phase == Phase::Busy) {
            m_BusyElapsed += dt;
            const uint32_t mcState = SafeMcReadStateSEH(m_Dispatch);
            // Wait at least 0.40s for visual feedback & native 30-frame MC timer, or finish on state 2/6 / 2.2s timeout
            if ((m_BusyElapsed >= 0.45f && (mcState == 2u || mcState == 6u)) || m_BusyElapsed >= 2.2f) {
                RefreshSlots();
                m_Phase    = Phase::Complete;
                m_DomDirty = true;
                PlaySound("NUD_COMMON_SYS_DECIDE");
            }
            return;
        }

        if (m_Phase == Phase::Complete) {
            if (snap.confirm || snap.cancel) {
                PlaySound("NUD_COMMON_SYS_DECIDE");
                m_Leaving = true;
                SafeMcCallVoid1SEH(m_Dispatch, kVtblInput, 1);
                SafeMcCallVoid0SEH(m_Dispatch, kVtblEndMode);
            }
        }
    }

    void Render(RmlOptionsController& rml, UiRenderer& renderer, InputDeviceKind activeDev) {
        if (!m_Active || !rml.IsReady()) return;

        Rml::Context* ctx = rml.GetContext();
        if (!ctx) return;

        // Hide MainMenu document while SaveLoad is active so both never overlap in the shared context
        rml.SetMainDocVisible(false);

        if (!m_Doc || m_NeedDocReload) {
            m_NeedDocReload = false;
            if (m_Doc) {
                m_Doc->Close();
                m_Doc = nullptr;
            }
            rml.ReloadStrings();
            Rml::Factory::ClearStyleSheetCache();
            m_Doc = ctx->LoadDocument("saveload.rml");
            if (!m_Doc) return;
            m_Doc->Show();
            m_DomDirty = true;
            m_LastW = 0;
            m_LastH = 0;
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

        // 1. Shared authentic crimson + Command Center + tactical grid backdrop
        rml.RenderSharedMenuBackdropBase();

        // 2. Render SaveLoad RML document
        renderer.SetPremultipliedBlend(true);
        ctx->Update();
        ctx->Render();
        renderer.SetPremultipliedBlend(false);

        // 3. Shared scanline sweep + embers over the frame
        rml.RenderSharedMenuBackdropOverlay();
    }

private:
    static constexpr size_t kVtblIsSaveMode       = 0x2D8;
    static constexpr size_t kVtblBegin            = 0x2E0;
    static constexpr size_t kVtblBeginSave        = 0x2F0;
    static constexpr size_t kVtblBeginLoad        = 0x2F8;
    static constexpr size_t kVtblCancelMode       = 0x300;
    static constexpr size_t kVtblEndMode          = 0x308;
    static constexpr size_t kVtblInput            = 0x310;
    static constexpr size_t kVtblIsFile           = 0x318;
    static constexpr size_t kVtblGetMissionNum    = 0x320;
    static constexpr size_t kVtblGetLevel         = 0x328;
    static constexpr size_t kVtblGetHour          = 0x330;
    static constexpr size_t kVtblGetMinuts        = 0x338;
    static constexpr size_t kVtblGetSecond        = 0x340;
    static constexpr size_t kVtblGetClear         = 0x348;
    static constexpr size_t kVtblGetDefaultNumber = 0x350;

    void PlaySound(const char* nudName) {
        if (m_SwgSoundObj && nudName && nudName[0]) {
            SafeSwgOnVoice2DSEH(m_SwgSoundObj, nudName);
        }
    }

    bool RefreshSlots() {
        bool changed = false;
        const bool newSaveMode = (SafeMcCallInt0SEH(m_Dispatch, kVtblIsSaveMode, m_IsSaveMode ? 1 : 0) != 0);
        if (newSaveMode != m_IsSaveMode) {
            m_IsSaveMode = newSaveMode;
            changed = true;
        }
        for (int i = 0; i < 3; ++i) {
            SlotInfo next;
            next.isFile = (SafeMcCallInt1SEH(m_Dispatch, kVtblIsFile, i, 0) != 0);
            if (next.isFile) {
                next.missionNum = SafeMcCallInt1SEH(m_Dispatch, kVtblGetMissionNum, i, 0);
                next.level      = SafeMcCallInt1SEH(m_Dispatch, kVtblGetLevel, i, -1);
                next.hour       = SafeMcCallInt1SEH(m_Dispatch, kVtblGetHour, i, 0);
                next.minute     = SafeMcCallInt1SEH(m_Dispatch, kVtblGetMinuts, i, 0);
                next.second     = SafeMcCallInt1SEH(m_Dispatch, kVtblGetSecond, i, 0);
                next.isClear    = (SafeMcCallInt1SEH(m_Dispatch, kVtblGetClear, i, 0) != 0);
            }
            const SlotInfo& cur = m_Slots[i];
            if (cur.isFile != next.isFile || cur.missionNum != next.missionNum ||
                cur.level != next.level || cur.hour != next.hour ||
                cur.minute != next.minute || cur.second != next.second ||
                cur.isClear != next.isClear) {
                m_Slots[i] = next;
                changed = true;
            }
        }
        return changed;
    }

    static std::string FormatMission(const UiStringTable& strings, const SlotInfo& s) {
        if (!s.isFile || s.missionNum <= 0 || s.missionNum > 18 || (s.isClear && s.missionNum >= 18)) {
            return "-- / --------";
        }
        static const char* kMissionFallbacks[19] = {
            "--------",
            "GLACIAL SKIES",
            "ANNEX",
            "THE ROUND TABLE",
            "JUGGERNAUT",
            "FLICKER OF HOPE",
            "DIAPASON",
            "BASTION",
            "MERLON",
            "SWORD OF ANNIHILATION",
            "MAYHEM",
            "THE INFERNO",
            "THE STAGE OF APOCALYPSE",
            "LYING IN DECEIT",
            "THE FINAL OVERTURE",
            "THE TALON OF RUIN",
            "THE DEMON OF THE ROUND TABLE",
            "THE VALLEY OF KINGS",
            "ZERO"
        };
        char key[32];
        std::snprintf(key, sizeof(key), "sl.mission.%02d", s.missionNum);
        const std::string name = strings.Get(key, kMissionFallbacks[s.missionNum]);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%02d / %s", s.missionNum, name.c_str());
        return std::string(buf);
    }

    static std::string FormatDifficulty(const UiStringTable& strings, const SlotInfo& s) {
        if (!s.isFile) return "----";
        switch (s.level) {
            case 0: return strings.Get("sl.diff.0", "VERY EASY");
            case 1: return strings.Get("sl.diff.1", "EASY");
            case 2: return strings.Get("sl.diff.2", "NORMAL");
            case 3: return strings.Get("sl.diff.3", "HARD");
            case 4: return strings.Get("sl.diff.4", "EXPERT");
            case 5: return strings.Get("sl.diff.5", "ACE");
            default: return "----";
        }
    }

    static std::string FormatFlightTime(const SlotInfo& s) {
        if (!s.isFile) return "----:--:--";
        const int hh = (std::max)(0, (std::min)(s.hour, 9999));
        const int mm = (std::max)(0, (std::min)(s.minute, 59));
        const int ss = (std::max)(0, (std::min)(s.second, 59));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d:%02d:%02d", hh, mm, ss);
        return std::string(buf);
    }

    void ApplyAspectLayout(int virtW) {
        if (!m_Doc) return;
        auto* topEl    = m_Doc->GetElementById("top-prompts");
        auto* frameEl  = m_Doc->GetElementById("menu-frame");
        auto* footerEl = m_Doc->GetElementById("footer-desc");
        if (virtW > 1920) {
            const int sidePx = (virtW - 1736) / 2;
            const std::string sideStr = std::to_string(sidePx) + "px";
            if (topEl)    { topEl->SetProperty("left", sideStr); topEl->SetProperty("right", sideStr); }
            if (frameEl)  { frameEl->SetProperty("left", sideStr); frameEl->SetProperty("right", sideStr); }
            if (footerEl) { footerEl->SetProperty("padding-left", sideStr); footerEl->SetProperty("padding-right", sideStr); }
        } else {
            if (topEl)    { topEl->RemoveProperty("left"); topEl->RemoveProperty("right"); }
            if (frameEl)  { frameEl->RemoveProperty("left"); frameEl->RemoveProperty("right"); }
            if (footerEl) { footerEl->RemoveProperty("padding-left"); footerEl->RemoveProperty("padding-right"); }
        }
    }

    void RebuildDom(const UiStringTable& strings, InputDeviceKind dev) {
        if (!m_Doc) return;

        // 1. Dynamic button icons + localized labels
        if (auto* el = m_Doc->GetElementById("sl-ico-ok")) {
            el->SetAttribute("src", dev == InputDeviceKind::Keyboard ? "assets/icons/kb/Enter_Alt_Key_Dark.png"
                                  : (dev == InputDeviceKind::Xbox    ? "assets/icons/xbox/XboxSeriesX_A.png"
                                                                     : "assets/icons/ps/PS5_Cross.png"));
        }
        if (auto* el = m_Doc->GetElementById("sl-ico-cancel")) {
            el->SetAttribute("src", dev == InputDeviceKind::Keyboard ? "assets/icons/kb/Esc_Key_Dark.png"
                                  : (dev == InputDeviceKind::Xbox    ? "assets/icons/xbox/XboxSeriesX_B.png"
                                                                     : "assets/icons/ps/PS5_Circle.png"));
        }
        if (auto* el = m_Doc->GetElementById("sl-lbl-ok")) {
            el->SetInnerRML(strings.Get("sl.ok", "OK"));
        }
        if (auto* el = m_Doc->GetElementById("sl-lbl-cancel")) {
            el->SetInnerRML(strings.Get("sl.cancel", "CANCEL"));
        }

        // 2. Header, mode tag & footer prompt
        if (auto* el = m_Doc->GetElementById("sl-header-text")) {
            el->SetInnerRML(strings.Get("sl.header", "GAME DATA"));
        }
        if (auto* el = m_Doc->GetElementById("sl-mode-tag")) {
            el->SetInnerRML(m_IsSaveMode ? strings.Get("sl.mode.save", "SAVE")
                                         : strings.Get("sl.mode.load", "LOAD"));
        }
        if (auto* el = m_Doc->GetElementById("footer-desc")) {
            el->SetInnerRML(m_IsSaveMode ? strings.Get("sl.footer.save", "Select file to save.")
                                         : strings.Get("sl.footer.load", "Select file to load."));
        }

        // 3. 3 Save Slots
        const std::string fileFmt  = strings.Get("sl.file_fmt", "FILE %02d");
        const std::string lblMiss  = strings.Get("sl.field.mission", "[MISSION No./NAME]");
        const std::string lblDiff  = strings.Get("sl.field.difficulty", "[DIFFICULTY LEVEL]");
        const std::string lblTime  = strings.Get("sl.field.time", "[CAMPAIGN FLIGHT TIME]");
        const std::string lblClear = strings.Get("sl.clear", "CLEAR");

        for (int i = 0; i < 3; ++i) {
            const std::string idxStr = std::to_string(i);
            if (auto* slotEl = m_Doc->GetElementById("sl-slot-" + idxStr)) {
                slotEl->SetClass("selected", i == m_SelectedSlot);
            }
            if (auto* titleEl = m_Doc->GetElementById("sl-title-" + idxStr)) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), fileFmt.c_str(), i + 1);
                titleEl->SetInnerRML(buf);
            }
            if (auto* km = m_Doc->GetElementById("sl-km-" + idxStr)) km->SetInnerRML(lblMiss);
            if (auto* kd = m_Doc->GetElementById("sl-kd-" + idxStr)) kd->SetInnerRML(lblDiff);
            if (auto* kt = m_Doc->GetElementById("sl-kt-" + idxStr)) kt->SetInnerRML(lblTime);
            if (auto* mEl = m_Doc->GetElementById("sl-m-" + idxStr)) {
                mEl->SetInnerRML(FormatMission(strings, m_Slots[i]));
            }
            if (auto* dEl = m_Doc->GetElementById("sl-d-" + idxStr)) {
                dEl->SetInnerRML(FormatDifficulty(strings, m_Slots[i]));
            }
            if (auto* tEl = m_Doc->GetElementById("sl-t-" + idxStr)) {
                tEl->SetInnerRML(FormatFlightTime(m_Slots[i]));
            }
            if (auto* cEl = m_Doc->GetElementById("sl-c-" + idxStr)) {
                cEl->SetInnerRML(lblClear);
                cEl->SetClass("hidden", !(m_Slots[i].isFile && m_Slots[i].isClear));
            }
        }

        // 4. Modal overlay (Confirm / Busy / Complete)
        auto* ov     = m_Doc->GetElementById("sl-modal-overlay");
        auto* msgEl  = m_Doc->GetElementById("sl-modal-msg");
        auto* subEl  = m_Doc->GetElementById("sl-modal-sub");
        auto* btnsEl = m_Doc->GetElementById("sl-modal-btns");
        auto* yesEl  = m_Doc->GetElementById("sl-btn-yes");
        auto* noEl   = m_Doc->GetElementById("sl-btn-no");
        if (!ov) return;

        if (m_Phase == Phase::SelectSlot) {
            ov->SetClass("hidden", true);
            return;
        }

        ov->SetClass("hidden", false);
        char slotLabel[32];
        std::snprintf(slotLabel, sizeof(slotLabel), fileFmt.c_str(), m_SelectedSlot + 1);

        if (m_Phase == Phase::Confirm) {
            std::string fmt;
            if (m_IsSaveMode) {
                fmt = m_Slots[m_SelectedSlot].isFile
                    ? strings.Get("sl.confirm.overwrite", "Overwrite %s?")
                    : strings.Get("sl.confirm.save", "Save game data to %s?");
            } else {
                fmt = strings.Get("sl.confirm.load", "Load %s?");
            }
            char msgBuf[160];
            std::snprintf(msgBuf, sizeof(msgBuf), fmt.c_str(), slotLabel);
            if (msgEl)  msgEl->SetInnerRML(msgBuf);
            const bool showSub = (m_IsSaveMode && m_Slots[m_SelectedSlot].isFile);
            if (subEl) {
                subEl->SetInnerRML(showSub ? strings.Get("sl.confirm.overwrite_sub", "Existing campaign progress in this slot will be replaced.") : "");
                subEl->SetClass("hidden", !showSub);
            }
            if (btnsEl) btnsEl->SetClass("hidden", false);
            if (yesEl)  { yesEl->SetInnerRML(strings.Get("sl.btn.yes", "YES")); yesEl->SetClass("hidden", false); yesEl->SetClass("focused", m_ConfirmYes); }
            if (noEl)   { noEl->SetInnerRML(strings.Get("sl.btn.no", "NO"));    noEl->SetClass("hidden", false);  noEl->SetClass("focused", !m_ConfirmYes); }
        } else if (m_Phase == Phase::ConfirmCancel) {
            if (msgEl) {
                msgEl->SetInnerRML(m_IsSaveMode ? strings.Get("sl.confirm.cancel_save", "Cancel save?")
                                                : strings.Get("sl.confirm.cancel_load", "Cancel load?"));
            }
            if (subEl)  { subEl->SetInnerRML(""); subEl->SetClass("hidden", true); }
            if (btnsEl) btnsEl->SetClass("hidden", false);
            if (yesEl)  { yesEl->SetInnerRML(strings.Get("sl.btn.yes", "YES")); yesEl->SetClass("hidden", false); yesEl->SetClass("focused", m_ConfirmYes); }
            if (noEl)   { noEl->SetInnerRML(strings.Get("sl.btn.no", "NO"));    noEl->SetClass("hidden", false);  noEl->SetClass("focused", !m_ConfirmYes); }
        } else if (m_Phase == Phase::Busy) {
            if (msgEl)  msgEl->SetInnerRML(m_IsSaveMode ? strings.Get("sl.busy.save", "Saving game data...")
                                                        : strings.Get("sl.busy.load", "Loading game data..."));
            if (subEl)  { subEl->SetInnerRML(strings.Get("sl.busy.sub", "Do not close the game while accessing save data.")); subEl->SetClass("hidden", false); }
            if (btnsEl) btnsEl->SetClass("hidden", true);
        } else if (m_Phase == Phase::Complete) {
            if (msgEl)  msgEl->SetInnerRML(m_IsSaveMode ? strings.Get("sl.done.save", "Save complete.")
                                                        : strings.Get("sl.done.load", "Load complete."));
            if (subEl)  { subEl->SetInnerRML(""); subEl->SetClass("hidden", true); }
            if (btnsEl) btnsEl->SetClass("hidden", false);
            if (yesEl)  { yesEl->SetInnerRML(strings.Get("sl.btn.ok", "OK")); yesEl->SetClass("hidden", false); yesEl->SetClass("focused", true); }
            if (noEl)   { noEl->SetClass("hidden", true); }
        }
    }

    McNativeDispatch       m_Dispatch;
    void*                  m_SwgSoundObj   = nullptr;
    Rml::ElementDocument*  m_Doc           = nullptr;
    bool                   m_Active        = false;
    bool                   m_Leaving       = false;
    bool                   m_IsSaveMode    = true;
    int                    m_SelectedSlot  = 0;
    Phase                  m_Phase         = Phase::SelectSlot;
    bool                   m_ConfirmYes    = false;
    float                  m_ElapsedSec    = 0.0f;
    float                  m_BusyElapsed   = 0.0f;
    SlotInfo               m_Slots[3];
    bool                   m_DomDirty      = true;
    bool                   m_NeedDocReload = false;
    int                    m_LastW         = 0;
    int                    m_LastH         = 0;
    InputDeviceKind        m_LastDev       = InputDeviceKind::PlayStation;
};

} // namespace acz_ui
