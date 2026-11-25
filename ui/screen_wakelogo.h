#pragma once
// ============================================================================
// ACZero Enhancer — Native D3D12 WakeLogo Intro Screen (ui/screen_wakelogo.h)
// Replaces WakeLogo.swa (AVCModeWakeLogo, 60 FPS / 969 frames) with a crisp,
// aspect-independent D3D12 presentation across 4:3, 16:9, 21:9, and 32:9:
//   - Phase 0 (0.00s - 4.20s):  BANDAI NAMCO Games on full-bleed white backdrop
//   - Phase 1 (4.20s - 10.60s): PROJECT ACES ("New Blood. New Battles.")
//     modeled on Op Us-1(1).mp4 over the shared procedural Belkan storm clouds:
//       * Mode 6 GPU shader diagonal glint sweep & molten center-out ignition
//         on the Diamond-X emblem (logo_project_aces_emblem.png)
//       * Live FontRole::Title "PROJECT ACES" with wide tracking & warm glow
//       * Live FontRole::Serif "\"New Blood. New Battles.\"" with soft
//         left-to-right feathered reveal
//       * Mode 2 GPU burning embers (3 crisp depth layers, no giant bokeh)
//   - Phase 2 (10.60s - 16.15s): Aircraft Manufacturer Licenses (HD PNG) +
//     live FontRole::Serif legal copyright text over the shared procedural clouds
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

#include "acz_ui_renderer.h"
#include "acz_ui_font.h"
#ifdef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

namespace acz_ui {

class WakeLogoScreen {
public:
    struct WakeSpark {
        float x, y;
        float vx, vy;
        float liftBase;
        float windFactor;
        float drag;
        float initialSize;
        float aspect;
        float rotAngle;
        float rotSpeed;
        float fuel;
        float burnRate;
        float age;
        float phase;
        float freq1, freq2;
        float heat;
        float seed01;
        float bokehBlur;
        int   layer; // 0=far, 1=mid, 2=near bokeh (no layer 3 blobs)
    };

    const std::vector<WakeSpark>& ActiveSparks() const { return m_Sparks; }

    void SetAssetsRoot(const std::filesystem::path& gameDir) {
        m_AssetsDir = gameDir / "mods" / "enhancer" / "ui" / "assets" / "wakelogo";
    }

    bool IsActive() const { return m_Active; }

    bool ShouldFinish() const {
        return m_Active && m_ClockArmed && (m_ElapsedSec >= kTotalDurationSec);
    }

    float ElapsedSec() const { return m_ElapsedSec; }

    void Begin() {
        if (!m_Active) {
            m_Active      = true;
            m_ClockArmed  = false;
            m_ElapsedSec  = 0.0f;
            m_NativeFrame = 0;
            m_Sparks.clear();
            m_Sparks.reserve(96);
            m_SpawnAccum  = 0.0f;
            m_Rng         = 0xAC02006u;
        }
    }

    void SetNativeFrame(int32_t rootFrame) {
        if (rootFrame > 0) {
            m_NativeFrame = rootFrame;
        }
    }

    void ResetAndRelease(UiRenderer& r) {
        m_Active      = false;
        m_ClockArmed  = false;
        m_ElapsedSec  = 0.0f;
        m_NativeFrame = 0;
        m_Sparks.clear();
        m_SpawnAccum  = 0.0f;
        ReleaseTex(r, m_TexNamco);
        ReleaseTex(r, m_TexEmblem);
        ReleaseTex(r, m_TexLicenses);
        m_TexturesAttempted = false;
    }

    void Update(float dtSec) {
        if (!m_Active || !m_ClockArmed) return;
        if (dtSec < 0.0f || dtSec > 0.05f) dtSec = 1.0f / 60.0f;
        m_ElapsedSec += dtSec;

        // Gentle atmospheric embers during Phase 1 (PROJECT ACES) & Phase 2 (Licenses)
        const float t = EffectiveTimeSec();
        if (t >= kPhase1StartSec + 0.15f) {
            const float targetCount = (t < kPhase2StartSec) ? 26.0f : 18.0f;
            const float spawnRate   = (t < kPhase2StartSec) ? 8.5f  : 5.5f;
            m_SpawnAccum += dtSec * spawnRate;
            while (m_SpawnAccum >= 1.0f) {
                m_SpawnAccum -= 1.0f;
                if (m_Sparks.size() < static_cast<size_t>(targetCount)) {
                    WakeSpark s{};
                    SpawnSpark(s);
                    m_Sparks.push_back(s);
                }
            }
        }

        for (size_t i = 0; i < m_Sparks.size(); ) {
            WakeSpark& s = m_Sparks[i];
            StepSpark(s, dtSec);
            if (s.fuel <= 0.0f || s.x < -0.10f || s.x > 1.12f || s.y < -0.12f || s.y > 1.14f) {
                m_Sparks[i] = m_Sparks.back();
                m_Sparks.pop_back();
            } else {
                ++i;
            }
        }
    }

    void Render(UiRenderer& r, UiFontManager& fonts, float sharedBackdropTimeSec) {
        if (!m_Active || !r.IsReady()) return;
        const bool firstLoad = !m_TexturesAttempted;
        EnsureTexturesLoaded(r);
        if (firstLoad || !m_ClockArmed) {
            m_ElapsedSec = 0.0f;
            m_ClockArmed = true;
        }

        const float vw = r.VirtualWidth();
        const float vh = r.VirtualHeight();
        const float cx = vw * 0.5f;
        const float cy = vh * 0.5f;
        const float t  = EffectiveTimeSec();
        const float bgTime = (sharedBackdropTimeSec >= 0.0f) ? sharedBackdropTimeSec : t;

        // Solid black base
        r.SetPremultipliedBlend(false);
        r.SetAdditiveAlphaBlend(false);
        r.DrawRectFilled(0.0f, 0.0f, vw, vh, Color(0, 0, 0, 255));

        // --------------------------------------------------------------------
        // Phase 0 (0.00s - 4.00s): BANDAI NAMCO Games on full-bleed white
        // Matches vanilla WakeLogo.swa: white backdrop fades in from black,
        // logo fades in gently over white, then whole frame fades to black
        // followed by a brief 0.35s black pause before crimson clouds appear.
        // --------------------------------------------------------------------
        if (t < kPhase1StartSec) {
            const float bgFade   = SmoothFade(t, -0.001f, 0.45f, kPhase1StartSec - 0.80f, kPhase1StartSec - 0.35f);
            const float logoFade = SmoothStep(0.30f, 0.80f, t);
            if (bgFade > 0.002f) {
                r.DrawRectFilled(0.0f, 0.0f, vw, vh, Color(255, 255, 255, 255));
                if (m_TexNamco.slot != 0 && logoFade > 0.002f) {
                    const float h = 640.0f;
                    const float w = h * m_TexNamco.aspect;
                    r.DrawImage(cx - w * 0.5f, cy - h * 0.5f, w, h, m_TexNamco.slot,
                                Color(255, 255, 255, ToByte(logoFade)));
                }
                if (bgFade < 0.998f) {
                    r.DrawRectFilled(0.0f, 0.0f, vw, vh, Color(0, 0, 0, ToByte(1.0f - bgFade)));
                }
            }
            return;
        }

        // --------------------------------------------------------------------
        // Continuous shared procedural storm clouds across Phase 1 & Phase 2:
        //   Full vibrant Belkan crimson brightness matching vanilla WakeLogo.swa
        //   and seamlessly matching TitleScreen on handoff (bg=0.94, cloud=0.86)
        // --------------------------------------------------------------------
        const float stormT     = t - kPhase1StartSec;
        const float bgEnter    = SmoothStep(0.0f, 0.70f, stormT);
        const float inPhase2   = SmoothStep(kPhase2StartSec - 0.35f, kPhase2StartSec + 0.45f, t);
        const float titleRamp  = SmoothStep(kTotalDurationSec - 0.65f, kTotalDurationSec, t);
        const float bgLevel    = bgEnter * (0.94f - 0.04f * inPhase2 + 0.04f * titleRamp);
        const float cloudLevel = SmoothStep(0.05f, 0.85f, stormT) * (0.86f - 0.08f * inPhase2 + 0.08f * titleRamp);

        // --------------------------------------------------------------------
        // Phase 1 (4.00s - 10.30s, 6.30s): PROJECT ACES ("New Blood. New Battles.")
        // Matches vanilla WakeLogo.swa: vibrant crimson clouds, clean luminous
        // emblem (no rim-fire), crisp cream typography, then brief live clouds
        // beat (~0.75s) before Licenses fade in.
        // --------------------------------------------------------------------
        if (t < kPhase2StartSec) {
            const float lt   = t - kPhase1StartSec;
            const float dur  = kPhase2StartSec - kPhase1StartSec; // 6.30s
            const float fade = SmoothFade(lt, 0.55f, 1.45f, dur - 1.45f, dur - 0.75f);
            const float corePulse = 0.50f + 0.50f * std::sin(lt * 2.2f);

            // 1. Shared procedural Belkan crimson storm clouds at full vibrant brightness
            r.DrawTitleBackdrop(bgTime, cloudLevel, 0.0f, bgLevel);

            // 2. Far/mid embers behind emblem
            RenderSparks(r, bgEnter, true);

            if (fade <= 0.002f) {
                RenderSparks(r, bgEnter * 0.75f, false);
                return;
            }

            // 3. Project Aces Diamond-X Emblem — Standard RGBA image (no custom shader)
            if (m_TexEmblem.slot != 0) {
                const float embH = 760.0f;
                const float embW = embH * m_TexEmblem.aspect;
                const float embX = cx - embW * 0.5f;
                const float embY = vh * 0.415f - embH * 0.574f;

                r.DrawImage(embX, embY, embW, embH, m_TexEmblem.slot,
                            Color(255, 255, 255, ToByte(fade)));
            }

            // 4. Live Typography: "PROJECT ACES" & "\"New Blood. New Battles.\""
            //    Clean pale cream/silver-white with soft drop shadow (matching vanilla WakeLogo.swa)
            fonts.SyncGpuAtlas(r);
            {
                const char* kStudio    = "PROJECT ACES";
                const float titleY     = vh * 0.702f;
                const float titleSize  = 24.0f;
                const float titleTrack = 13.0f;

                fonts.DrawText(r, FontRole::Title, titleSize, cx, titleY + 2.0f,
                               kStudio, Color(20, 4, 2, ToByte(fade * 0.82f)),
                               TextAlign::Center, false, titleTrack);
                fonts.DrawText(r, FontRole::Title, titleSize, cx, titleY,
                               kStudio, Color(238, 224, 206, ToByte(fade * 0.94f)),
                               TextAlign::Center, true, titleTrack);

                const char* kSlogan     = "\"New Blood. New Battles.\"";
                const float sloganY     = vh * 0.756f;
                const float sloganSize  = 23.0f;
                const float sloganTrack = 1.4f;

                fonts.DrawText(r, FontRole::Serif, sloganSize, cx, sloganY + 2.0f,
                               kSlogan, Color(18, 4, 2, ToByte(fade * 0.80f)),
                               TextAlign::Center, false, sloganTrack);
                fonts.DrawText(r, FontRole::Serif, sloganSize, cx, sloganY,
                               kSlogan, Color(226, 212, 196, ToByte(fade * 0.90f)),
                               TextAlign::Center, true, sloganTrack);
            }
            fonts.SyncGpuAtlas(r);

            // 5. Near embers in front
            RenderSparks(r, bgEnter * 0.80f, false);
            return;
        }

        // --------------------------------------------------------------------
        // Phase 2 (10.30s - 15.70s, 5.40s): Aircraft Manufacturer Licenses & Legal Text
        // Smooth fade-out of logos/text over LIVE continuous clouds
        // -> seamless zero-cut handoff directly into Title!
        // --------------------------------------------------------------------
        {
            const float lt   = t - kPhase2StartSec;
            const float dur  = kTotalDurationSec - kPhase2StartSec; // 5.40s
            const float fade = SmoothFade(lt, -0.001f, 0.50f, dur - 0.65f, dur - 0.15f);

            // Continuous vibrant procedural backdrop right up until Title takes over
            r.DrawTitleBackdrop(bgTime, cloudLevel, 0.0f, bgLevel);
            RenderSparks(r, 0.72f, true);
            if (fade <= 0.002f) {
                RenderSparks(r, 0.72f, false);
                return;
            }

            // Top: HD Aircraft Manufacturer Licenses (untouched texture per user preference)
            if (m_TexLicenses.slot != 0) {
                const float maxW = (std::min)(vw * 0.70f, 1260.0f);
                const float h    = (std::min)(295.0f, maxW / m_TexLicenses.aspect);
                const float w    = h * m_TexLicenses.aspect;
                r.DrawImage(cx - w * 0.5f, vh * 0.250f - h * 0.5f, w, h, m_TexLicenses.slot,
                            Color(255, 255, 255, ToByte(fade * 0.96f)));
            }

            // Bottom: Live crisp Serif legal copyright lines (uniform tight spacing like vanilla)
            static const char* const kLegalLines[] = {
                "All trademarks and copyrights associated with the manufacturers, aircraft,",
                "models, trade names, brands and visual images depicted in this game are",
                "the property of their respective owners, and used with such permissions.",
                "Copyright and other rights owned by BAE SYSTEMS PLC and its partners and",
                "are not to be reproduced without the prior written consent of BAE SYSTEMS PLC.",
                "Produced under license from Boeing Management Company.",
                "MIRAGE and RAFALE are aircraft and registered trademarks of DASSAULT AVIATION.",
                "Copyright and other rights owned by GRIPEN INTERNATIONAL and its partners and",
                "are not to be reproduced without the prior written consent of GRIPEN INTERNATIONAL.",
                "Produced in cooperation with Japan Air Self-Defense Force.",
                "Lockheed Martin Trademarks used under license to Namco Ltd.",
                "Produced under a license from Northrop Grumman Systems Corporation."
            };

            fonts.SyncGpuAtlas(r);
            const float fontSize   = 21.0f;
            const float lineStep   = 30.0f;
            const float startY     = vh * 0.432f;
            const Color textCol(244, 220, 184, ToByte(fade * 0.94f));
            const Color shadowCol(12, 4, 2, ToByte(fade * 0.85f));

            for (size_t i = 0; i < sizeof(kLegalLines) / sizeof(kLegalLines[0]); ++i) {
                const float y = startY + static_cast<float>(i) * lineStep;
                fonts.DrawText(r, FontRole::Serif, fontSize, cx, y + 1.5f,
                               kLegalLines[i], shadowCol, TextAlign::Center, false, 0.35f);
                fonts.DrawText(r, FontRole::Serif, fontSize, cx, y,
                               kLegalLines[i], textCol, TextAlign::Center, true, 0.35f);
            }
            fonts.SyncGpuAtlas(r);
            RenderSparks(r, 0.72f, false);
        }
    }

private:
    static constexpr float kPhase1StartSec   = 4.00f;  // BANDAI NAMCO Games (4.00s incl. 0.35s black beat)
    static constexpr float kPhase2StartSec   = 10.30f; // PROJECT ACES (6.30s incl. 0.75s live clouds beat)
    static constexpr float kTotalDurationSec = 15.70f; // Aircraft Licenses (5.40s) -> seamless handoff to Title!

    struct LoadedTex {
        uint32_t slot   = 0;
        uint32_t w      = 0;
        uint32_t h      = 0;
        float    aspect = 1.0f;
    };

    float EffectiveTimeSec() const {
        // Always drive WakeLogo from our monotonic elapsed clock (WakeLogo.swa uses
        // _root.gotoAndStop so m_NativeFrame can remain constant on frame 1!).
        return m_ElapsedSec;
    }

    static float Clamp01(float v) {
        return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
    }

    static float SmoothStep(float e0, float e1, float x) {
        if (e1 <= e0) return (x >= e1) ? 1.0f : 0.0f;
        const float u = Clamp01((x - e0) / (e1 - e0));
        return u * u * (3.0f - 2.0f * u);
    }

    static float SmoothFade(float t, float inStart, float inEnd, float outStart, float outEnd) {
        if (t <= inStart || t >= outEnd) return 0.0f;
        float a = 1.0f;
        if (t < inEnd)       a = SmoothStep(inStart, inEnd, t);
        else if (t > outStart) a = 1.0f - SmoothStep(outStart, outEnd, t);
        return Clamp01(a);
    }

    static uint8_t ToByte(float a) {
        const int v = static_cast<int>(Clamp01(a) * 255.0f + 0.5f);
        return static_cast<uint8_t>((v < 0) ? 0 : ((v > 255) ? 255 : v));
    }

    float NextRand() {
        m_Rng = m_Rng * 1664525u + 1013904223u;
        return static_cast<float>((m_Rng >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }

    void SpawnSpark(WakeSpark& s) {
        const float rl = NextRand();
        if (rl < 0.45f)      { s.layer = 0; s.bokehBlur = 0.20f; s.initialSize = 6.0f  + NextRand() * 6.0f; }
        else if (rl < 0.88f) { s.layer = 1; s.bokehBlur = 0.00f; s.initialSize = 14.0f + NextRand() * 14.0f; }
        else                 { s.layer = 2; s.bokehBlur = 0.46f + NextRand() * 0.16f; s.initialSize = 30.0f + NextRand() * 18.0f; }

        if (NextRand() < 0.65f) {
            s.x = 0.15f + NextRand() * 0.85f;
            s.y = 0.72f + NextRand() * 0.34f;
        } else {
            s.x = 0.88f + NextRand() * 0.16f;
            s.y = 0.25f + NextRand() * 0.65f;
        }

        s.liftBase   = 0.018f + NextRand() * 0.026f;
        s.windFactor = 0.55f  + NextRand() * 0.90f;
        s.drag       = 1.15f  + NextRand() * 1.10f;
        s.vx         = -(0.018f + NextRand() * 0.025f);
        s.vy         = -s.liftBase;
        s.aspect     = 0.90f + NextRand() * 0.45f;
        s.rotAngle   = NextRand() * 6.28318f;
        s.rotSpeed   = (NextRand() - 0.5f) * 2.2f;
        s.seed01     = NextRand();
        s.fuel       = 1.0f;
        s.burnRate   = 0.08f + NextRand() * 0.08f;
        s.age        = 0.0f;
        s.phase      = NextRand() * 6.28318f;
        s.freq1      = 2.6f + NextRand() * 4.5f;
        s.freq2      = 1.3f + NextRand() * 2.8f;
        s.heat       = 0.65f + NextRand() * 0.35f;
    }

    void StepSpark(WakeSpark& s, float dtSec) {
        s.age += dtSec;
        s.rotAngle += s.rotSpeed * dtSec;
        s.fuel -= s.burnRate * dtSec;

        const float depthSpeed = (s.layer == 0) ? 0.40f : ((s.layer == 2) ? 1.75f : 1.0f);
        const float swirlX = std::sin(m_ElapsedSec * 1.1f + s.phase + s.y * 4.0f) * 0.015f;
        const float swirlY = std::cos(m_ElapsedSec * 0.9f + s.phase * 1.3f + s.x * 3.5f) * 0.010f;
        const float targetVx = (-(0.022f + (1.0f - s.y) * 0.045f) * s.windFactor + swirlX) * depthSpeed;
        const float targetVy = (-s.liftBase + swirlY) * depthSpeed;
        const float damp = (std::min)(1.0f, s.drag * dtSec);
        s.vx += (targetVx - s.vx) * damp;
        s.vy += (targetVy - s.vy) * damp;
        s.x  += s.vx * dtSec;
        s.y  += s.vy * dtSec;
    }

    void RenderSparks(UiRenderer& r, float masterFade, bool backgroundPass) const {
        if (m_Sparks.empty() || masterFade <= 0.004f) return;
        const float vw = r.VirtualWidth();
        const float vh = r.VirtualHeight();

        r.SetPremultipliedBlend(true);
        for (const WakeSpark& s : m_Sparks) {
            const bool isBg = (s.layer <= 1);
            if (isBg != backgroundPass) continue;
            if (s.fuel <= 0.005f) continue;

            const float spawnFade = (std::min)(1.0f, s.age / 0.45f);
            const float fuelFade  = (s.fuel < 0.30f) ? (s.fuel / 0.30f) : 1.0f;
            const float osc       = 0.5f + 0.5f * std::sin(m_ElapsedSec * s.freq1 + s.phase)
                                                * std::cos(m_ElapsedSec * s.freq2 + s.phase * 1.5f);
            const float heat      = Clamp01((s.heat * 0.65f + osc * 0.35f) * (0.45f + 0.55f * s.fuel));
            const float layerAlpha = (s.layer == 0) ? 0.52f : ((s.layer == 2) ? 0.58f : 0.92f);
            const float alpha     = Clamp01(spawnFade * fuelFade * layerAlpha * (0.45f + 0.55f * heat) * masterFade);
            if (alpha <= 0.015f) continue;

            const float curSize = s.initialSize * (0.30f + 0.70f * std::sqrt(s.fuel));
            const float crust   = (std::max)(0.02f, (std::min)(0.90f, 1.0f - s.fuel));
            const float seedAndCrust = std::floor(1.0f + s.seed01 * 28.0f) + crust;
            r.DrawEmberQuad(s.x * vw, s.y * vh, curSize * s.aspect, curSize, s.rotAngle,
                            heat, s.bokehBlur, seedAndCrust, alpha);
        }
        r.SetPremultipliedBlend(false);
    }

    static void ReleaseTex(UiRenderer& r, LoadedTex& t) {
        if (t.slot != 0) {
            r.ReleaseDynamicTexture(t.slot);
            t.slot = 0;
        }
        t.w = 0;
        t.h = 0;
        t.aspect = 1.0f;
    }

    void LoadTex(UiRenderer& r, LoadedTex& out, const char* filename) {
        if (out.slot != 0 || m_AssetsDir.empty()) return;
        const std::filesystem::path fullPath = m_AssetsDir / filename;
        FILE* f = _wfopen(fullPath.c_str(), L"rb");
        if (!f) return;
        fseek(f, 0, SEEK_END);
        const long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 32 * 1024 * 1024) {
            fclose(f);
            return;
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(sz));
        const size_t rd = fread(bytes.data(), 1, bytes.size(), f);
        fclose(f);
        if (rd != bytes.size()) return;

        int w = 0, h = 0, ch = 0;
        uint8_t* rgba = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &ch, 4);
        if (!rgba || w <= 0 || h <= 0) {
            if (rgba) stbi_image_free(rgba);
            return;
        }
        const uint32_t slot = r.CreateDynamicTexture(
            static_cast<uint32_t>(w), static_cast<uint32_t>(h), DXGI_FORMAT_R8G8B8A8_UNORM, 4u);
        if (slot != 0) {
            r.UpdateTexturePixels(slot, rgba, static_cast<uint32_t>(w) * 4u);
            out.slot   = slot;
            out.w      = static_cast<uint32_t>(w);
            out.h      = static_cast<uint32_t>(h);
            out.aspect = static_cast<float>(w) / static_cast<float>(h);
        }
        stbi_image_free(rgba);
    }

    void EnsureTexturesLoaded(UiRenderer& r) {
        if (m_TexturesAttempted) return;
        m_TexturesAttempted = true;
        LoadTex(r, m_TexNamco,    "logo_namco.png");
        LoadTex(r, m_TexEmblem,   "logo_project_aces_emblem.png");
        LoadTex(r, m_TexLicenses, "logo_licenses.png");
    }

    std::filesystem::path m_AssetsDir;
    bool    m_Active            = false;
    bool    m_ClockArmed        = false;
    bool    m_TexturesAttempted = false;
    float   m_ElapsedSec        = 0.0f;
    int32_t m_NativeFrame       = 0;

    LoadedTex m_TexNamco{};
    LoadedTex m_TexEmblem{};
    LoadedTex m_TexLicenses{};

    std::vector<WakeSpark> m_Sparks;
    float    m_SpawnAccum = 0.0f;
    uint32_t m_Rng        = 0xAC02006u;
};

} // namespace acz_ui
