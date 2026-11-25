#pragma once
// ============================================================================
// ACZero Enhancer — Native D3D12 Title Screen (ui/screen_title.h)
// Replaces Title.swa (AVCModeTitle, US version) with a crisp, aspect-independent
// D3D12 presentation across 4:3, 16:9, 21:9, and 32:9:
//   - Base crimson fire backdrop + 3-layer phase-shifted additive cloud zoom
//     faithfully matching Title.swa (Dict[10] + Dict[17]) in seamless HD
//   - Cinematic "ACE COMBAT ZERO: THE BELKAN WAR" reveal modeled on Op Us-1.mp4:
//     fast zoom-out convergence from camera with 4 translucent motion-echo layers,
//     white-gold additive impact bloom, and ember burst at lock-in (~1.02s)
//   - GPU Pixel-Shader Mode 2 burning embers (DrawEmberQuad) igniting organically
//     from zero when the Title Screen opens (no prewarm) and swelling into a full
//     4-depth-layer ember stream with wind gusts
//   - Rendered localized "[ PRESS ANY BUTTON ]" prompt (strings_en.ini [Title]),
//     HD Namco Bandai copyright, and ACZero Enhancer version credit below it
//   - Frame-exact synchronization with native Title.swa _root "out" fade (frames 1..19)
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>

#include "acz_ui_renderer.h"
#include "acz_ui_font.h"
#ifdef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

namespace acz_ui {

class TitleScreen {
public:
    void SetAssetsRoot(const std::filesystem::path& gameDir) {
        m_GameDir     = gameDir;
        m_AssetsDir   = gameDir / "mods" / "enhancer" / "ui" / "assets" / "title";
        m_StringsPath = gameDir / "mods" / "enhancer" / "ui" / "strings_en.ini";
        ReloadStrings();
    }

    void ReloadStrings() {
        m_PressButtonText = "[ PRESS ANY BUTTON ]";
        m_ModInfoText     = "ACZERO ENHANCER v0.25";
        if (m_StringsPath.empty()) return;

        std::ifstream f(m_StringsPath, std::ios::binary);
        if (!f.is_open()) return;

        bool inTitle = false;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Trim leading spaces
            size_t s = 0;
            while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
            if (s >= line.size() || line[s] == ';' || line[s] == '#') continue;
            if (line[s] == '[') {
                size_t e = line.find(']', s + 1);
                if (e != std::string::npos) {
                    std::string sec = line.substr(s + 1, e - s - 1);
                    inTitle = (sec == "Title");
                }
                continue;
            }
            if (!inTitle) continue;
            size_t eq = line.find('=', s);
            if (eq == std::string::npos) continue;
            std::string key = TrimCopy(line.substr(s, eq - s));
            std::string val = TrimCopy(line.substr(eq + 1));
            if (key == "title.press_any_button" && !val.empty()) {
                m_PressButtonText = val;
            } else if (key == "title.mod_info" && !val.empty()) {
                m_ModInfoText = val;
            }
        }
    }

    bool IsActive() const { return m_Active; }

    void Begin(bool fromWakeLogo = false) {
        if (!m_Active) {
            m_Active          = true;
            m_FromWakeLogo    = fromWakeLogo;
            m_ClockArmed      = false;
            m_ElapsedSec      = 0.0f;
            m_NativeOutFrame  = 0;
            m_OutTriggerSec   = -1.0f;
            m_BurstFired      = false;
            m_SparksAdopted   = false;
            m_Sparks.clear();
            m_Sparks.reserve(1152);
            m_SpawnAccum      = 0.0f;
            m_Gust            = 0.0f;
            m_GustTarget      = 0.0f;
            m_GustTimer       = 2.6f;
            m_Rng             = 0xAC02006u;
        }
    }

    // Seamlessly carry over living ambient embers from WakeLogoScreen so none vanish on transition
    template<typename TSpark>
    void AdoptAmbientSparks(const std::vector<TSpark>& src) {
        if (m_SparksAdopted || src.empty()) return;
        m_SparksAdopted = true;
        for (const TSpark& w : src) {
            if (w.fuel <= 0.02f) continue;
            TitleSpark s{};
            s.x           = w.x;
            s.y           = w.y;
            s.vx          = w.vx;
            s.vy          = w.vy;
            s.liftBase    = w.liftBase;
            s.windFactor  = w.windFactor;
            s.drag        = w.drag;
            s.initialSize = w.initialSize;
            s.aspect      = w.aspect;
            s.rotAngle    = w.rotAngle;
            s.rotSpeed    = w.rotSpeed;
            s.fuel        = w.fuel;
            s.burnRate    = w.burnRate;
            s.age         = w.age;
            s.phase       = w.phase;
            s.freq1       = w.freq1;
            s.freq2       = w.freq2;
            s.heat        = w.heat;
            s.heatTarget  = w.heat;
            s.flareTimer  = 0.25f + NextRand() * 0.45f;
            s.seed01      = w.seed01;
            s.bokehBlur   = w.bokehBlur;
            s.layer       = w.layer;
            s.isBurst     = false;
            m_Sparks.push_back(s);
        }
    }

    // Incrementally preloads 1 texture per call (during WakeLogo) so Title frame 0 has zero decode stutter
    bool PreloadOneAsset(UiRenderer& r) {
        if (!r.IsReady() || m_TexturesAttempted) return false;
        switch (m_PreloadStage) {
            case 0: LoadTex(r, m_TexLogoTop,    "logo_top.png",    1); break;
            case 1: LoadTex(r, m_TexLogoBottom, "logo_bottom.png", 2); break;
            case 2:
                LoadTex(r, m_TexCopyright, "copyright.png", 0);
                m_TexturesAttempted = true;
                break;
            default:
                m_TexturesAttempted = true;
                return false;
        }
        ++m_PreloadStage;
        return true;
    }

    void SetNativeOutFrame(int32_t rootFrame) {
        if (rootFrame < 0) rootFrame = 0;
        if (rootFrame > 0 && m_NativeOutFrame == 0) {
            m_OutTriggerSec = m_ElapsedSec;
        }
        m_NativeOutFrame = rootFrame;
    }

    void ResetAndRelease(UiRenderer& r) {
        m_Active          = false;
        m_ClockArmed      = false;
        m_ElapsedSec      = 0.0f;
        m_NativeOutFrame  = 0;
        m_OutTriggerSec   = -1.0f;
        m_BurstFired      = false;
        m_SparksAdopted   = false;
        m_Sparks.clear();
        m_LogoMaskTopPts.clear();
        m_LogoMaskBottomPts.clear();
        m_SpawnAccum      = 0.0f;
        ReleaseTex(r, m_TexLogoTop);
        ReleaseTex(r, m_TexLogoBottom);
        ReleaseTex(r, m_TexCopyright);
        m_TexturesAttempted = false;
        m_PreloadStage      = 0;
    }

    void Update(float dtSec) {
        if (!m_Active || !m_ClockArmed) return;
        if (dtSec < 0.0f || dtSec > 0.05f) dtSec = 1.0f / 60.0f;
        m_ElapsedSec += dtSec;

        // Global wind gust simulation (oxygen flares matching MainSelect UiSparkSystem)
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

        // Organic ignition ("burning up from zero") + dense ember cloud across exact letter glyphs:
        //   0.00s..0.35s : clean crimson clouds before first ambient embers ignite
        //   1.40s        : 1024 embers spawn directly inside the exact glyph silhouette of the logo,
        //                  slowly ramping up randomized upward lift before wind sweeps them away
        //   1.45s..6.20s : smooth ramp up to steady ~60 living 3-plane ambient embers (no giant bokeh)
        if (!m_BurstFired && m_ElapsedSec >= kLogoJoinSec) {
            m_BurstFired = true;
            m_Gust       = 0.0f;
            m_GustTarget = 0.0f;
            m_GustTimer  = 2.6f;
            SpawnImpactBurst(1024);
        }

        if (m_ElapsedSec >= 0.35f && m_NativeOutFrame == 0) {
            const float ramp = Clamp01((m_ElapsedSec - 0.35f) / 4.4f);
            const int desiredAmbient = static_cast<int>(8.0f + ramp * 52.0f);
            const float spawnRate = 3.0f + ramp * 15.0f;
            m_SpawnAccum += dtSec * spawnRate;
            while (m_SpawnAccum >= 1.0f) {
                m_SpawnAccum -= 1.0f;
                if (CountAmbientSparks() < desiredAmbient && m_Sparks.size() < 1120u) {
                    TitleSpark s{};
                    SpawnAmbientSpark(s);
                    m_Sparks.push_back(s);
                }
            }
        }

        // Step all active embers
        for (size_t i = 0; i < m_Sparks.size(); ) {
            TitleSpark& s = m_Sparks[i];
            StepSpark(s, dtSec);
            if (s.fuel <= 0.0f || s.x < -0.12f || s.x > 1.14f || s.y < -0.14f || s.y > 1.16f) {
                if (!s.isBurst && m_NativeOutFrame == 0) {
                    // Recycle ambient ember naturally from lower-right ignition zone
                    SpawnAmbientSpark(s);
                    ++i;
                } else {
                    m_Sparks[i] = m_Sparks.back();
                    m_Sparks.pop_back();
                }
            } else {
                ++i;
            }
        }
    }

    void Render(UiRenderer& r, UiFontManager& fonts, float sharedBackdropTimeSec = -1.0f) {
        if (!m_Active || !r.IsReady()) return;
        const bool firstLoad = !m_TexturesAttempted;
        EnsureTexturesLoaded(r);
        if (firstLoad || !m_ClockArmed) {
            // Start animation clock cleanly AFTER texture decode completes so frame 0.00s is never skipped
            m_ElapsedSec = 0.0f;
            m_ClockArmed = true;
        }

        const float vw = r.VirtualWidth();
        const float vh = r.VirtualHeight();
        const float cx = vw * 0.5f;
        const float t  = m_ElapsedSec;

        // Compute smooth master fade-out synchronized 1:1 with native Title.swa _root "out" (frames 1..19)
        float outU = 0.0f;
        if (m_NativeOutFrame >= 1) {
            outU = Clamp01(static_cast<float>(m_NativeOutFrame - 1) / 18.0f);
        }
        const float masterFade = 1.0f - SmoothStep(0.0f, 1.0f, outU);

        // Solid black base
        r.SetPremultipliedBlend(false);
        r.SetAdditiveAlphaBlend(false);
        r.DrawRectFilled(0.0f, 0.0f, vw, vh, Color(0, 0, 0, 255));

        if (masterFade <= 0.002f) {
            return;
        }

        // Impact flash curve shared between procedural backdrop reflection & logo bloom
        float flash = 0.0f;
        if (t >= kLogoJoinSec - 0.12f && t < kLogoJoinSec + 1.38f) {
            const float rise = SmoothStep(kLogoJoinSec - 0.12f, kLogoJoinSec + 0.03f, t);
            const float fall = 1.0f - SmoothStep(kLogoJoinSec + 0.03f, kLogoJoinSec + 1.38f, t);
            flash = rise * fall * fall;
        }

        // --------------------------------------------------------------------
        // 1. Procedural GPU Backdrop (Pixel Shader Mode 4):
        //    - Endless forward camera fly-through into dark crimson storm clouds
        //    - 3 staggered forward-zooming light-gold mist cloud layers matching
        //      vanilla Title.swa Dict[17] color & subtle additive translucency
        //    - Warm atmospheric reflection from the logo collision flash
        // --------------------------------------------------------------------
        const float bgStart    = m_FromWakeLogo ? 0.94f : 0.45f;
        const float cloudStart = m_FromWakeLogo ? 0.86f : 0.55f;
        const float bgFade     = (bgStart + (1.0f - bgStart) * SmoothStep(0.0f, 0.35f, t)) * masterFade;
        const float cloudIntro = cloudStart + (1.0f - cloudStart) * SmoothStep(0.0f, 0.65f, t);
        const float bgTime     = (sharedBackdropTimeSec >= 0.0f) ? sharedBackdropTimeSec : t;
        if (bgFade > 0.001f) {
            r.DrawTitleBackdrop(bgTime, cloudIntro, flash, bgFade);
        }

        // --------------------------------------------------------------------
        // 2. Far & Mid depth-layer burning embers (behind logo for 3D depth)
        // --------------------------------------------------------------------
        RenderSparksByLayers(r, masterFade, true);

        // --------------------------------------------------------------------
        // 3. 3D Perspective Split Logo Reveal (Op Us-1.mp4 — crisp, ZERO ghosting):
        //    - 0.00s..1.40s: "ACE COMBAT ZERO" rotates in from bold 3D yaw (+60 deg:
        //                    right end much closer to camera, left start farther),
        //                    while "THE BELKAN WAR" rotates in from opposite yaw
        //                    (-60 deg: left start closer, right end farther),
        //                    both clearly sweeping & flattening into plane at 1.40s
        //    - 1.40s       : Both lines lock seamlessly into unified logo -> pure
        //                    optical Gaussian/anamorphic bloom flash (ZERO text
        //                    duplication!) + 260 embers across entire text volume
        //    - 1.40s..4.40s: Unified logo slowly & smoothly shrinks (1.18x -> 1.00x)
        //    - On Exit     : Smoothly recedes and dissolves cleanly
        // --------------------------------------------------------------------
        const bool hasTop    = (m_TexLogoTop.slot != 0);
        const bool hasBottom = (m_TexLogoBottom.slot != 0);
        if (hasTop || hasBottom) {
            const float logoCy   = vh * 0.445f;
            const float baseW    = (std::min)(vw * 0.84f, 1180.0f);
            // Reference 1792px virtual logo width so logo_top.png and logo_bottom.png
            // preserve exact 1:1 pixel proportions regardless of their independent padding
            const float pxScale  = baseW / 1792.0f;
            const float topBaseW = hasTop    ? (static_cast<float>(m_TexLogoTop.w)    * pxScale) : 0.0f;
            const float topBaseH = hasTop    ? (static_cast<float>(m_TexLogoTop.h)    * pxScale) : 0.0f;
            const float botBaseW = hasBottom ? (static_cast<float>(m_TexLogoBottom.w) * pxScale) : 0.0f;
            const float botBaseH = hasBottom ? (static_cast<float>(m_TexLogoBottom.h) * pxScale) : 0.0f;

            // Vertical center offsets (in reference 1792x320 logo pixels) matching original lock-up:
            //   "ACE COMBAT ZERO" center = -25.5 px, "THE BELKAN WAR" center = +95.5 px
            const float topBaseOffY = -25.5f * pxScale;
            const float botBaseOffY = +95.5f * pxScale;

            // Cache exact normalized join-time rects (1.18x) for pixel-accurate glyph ember spawning
            const float joinTopW = topBaseW * kLogoJoinScale;
            const float joinTopH = topBaseH * kLogoJoinScale;
            const float joinTopCy = logoCy + topBaseOffY * kLogoJoinScale;
            m_TopJoinLeft   = (cx - joinTopW * 0.5f) / vw;
            m_TopJoinWidth  = joinTopW / vw;
            m_TopJoinTop    = (joinTopCy - joinTopH * 0.5f) / vh;
            m_TopJoinHeight = joinTopH / vh;

            const float joinBotW = botBaseW * kLogoJoinScale;
            const float joinBotH = botBaseH * kLogoJoinScale;
            const float joinBotCy = logoCy + botBaseOffY * kLogoJoinScale;
            m_BotJoinLeft   = (cx - joinBotW * 0.5f) / vw;
            m_BotJoinWidth  = joinBotW / vw;
            m_BotJoinTop    = (joinBotCy - joinBotH * 0.5f) / vh;
            m_BotJoinHeight = joinBotH / vh;

            // Scale curve:
            //   0.00s..1.40s : 3.45x -> 1.18x (bold close-up entry zooming out during 3D rotation)
            //   1.40s..4.40s : 1.18x -> 1.00x (slow, stately, silky-smooth shrink!)
            auto evalLogoScale = [](float ts) -> float {
                if (ts <= 0.0f) return 3.45f;
                if (ts < kLogoJoinSec) {
                    const float u   = ts / kLogoJoinSec;
                    const float inv = 1.0f - u;
                    const float e   = 1.0f - std::pow(inv, 2.15f);
                    return 3.45f - (3.45f - kLogoJoinScale) * e;
                }
                if (ts < kLogoSettleSec) {
                    const float u   = (ts - kLogoJoinSec) / (kLogoSettleSec - kLogoJoinSec);
                    const float inv = 1.0f - u;
                    const float e   = 1.0f - std::pow(inv, 2.2f);
                    return kLogoJoinScale - (kLogoJoinScale - 1.0f) * e;
                }
                return 1.0f;
            };

            // Smooth exit recession when button pressed (outU: 0 -> 1)
            const float exitEase  = SmoothStep(0.0f, 1.0f, outU);
            const float exitScale = 1.0f - 0.09f * exitEase;
            const float exitFade  = 1.0f - exitEase;

            const float mainScale = evalLogoScale(t) * exitScale;
            const float mainW     = baseW * mainScale;
            const float mainH     = (320.0f * pxScale) * mainScale;

            const float topW  = topBaseW * mainScale;
            const float topH  = topBaseH * mainScale;
            const float topCy = logoCy + topBaseOffY * mainScale;
            const float topX  = cx - topW * 0.5f;
            const float topY  = topCy - topH * 0.5f;

            const float botW  = botBaseW * mainScale;
            const float botH  = botBaseH * mainScale;
            const float botCy = logoCy + botBaseOffY * mainScale;
            const float botX  = cx - botW * 0.5f;
            const float botY  = botCy - botH * 0.5f;

            if (t < kLogoJoinSec) {
                // 3A. Bold, unmistakable 3D Y-axis perspective rotation (ZERO ghosting!)
                const float u      = Clamp01(t / kLogoJoinSec);
                const float inv    = 1.0f - u;
                const float rotInv = std::pow(inv, 1.55f);
                const float yawAbs = 1.04f * rotInv;       // ~60 deg -> 0 deg (clearly visible!)
                const float dxAbs  = (vw * 0.22f) * rotInv;
                const float alpha  = SmoothStep(0.0f, 0.18f, t) * exitFade;

                if (alpha > 0.002f) {
                    const float focal = mainW * 0.64f;
                    const Color col(255, 255, 255, ToByte(alpha));
                    // Top ("ACE COMBAT ZERO"): +yawAbs -> right end closer to camera, left start farther
                    if (hasTop) {
                        r.DrawPerspectiveImageY(cx - dxAbs, topCy, topW, topH, +yawAbs, focal, m_TexLogoTop.slot, col, 36);
                    }
                    // Bottom ("THE BELKAN WAR"): -yawAbs -> opposite angle (left start closer, right end farther)
                    if (hasBottom) {
                        r.DrawPerspectiveImageY(cx + dxAbs, botCy, botW, botH, -yawAbs, focal, m_TexLogoBottom.slot, col, 36);
                    }
                }
            } else {
                // 3B. Locked-in split logo parts (slowly & smoothly shrinking 1.18x -> 1.00x)
                if (exitFade > 0.002f) {
                    const Color col(255, 255, 255, ToByte(exitFade));
                    if (hasTop)    r.DrawImage(topX, topY, topW, topH, m_TexLogoTop.slot, col);
                    if (hasBottom) r.DrawImage(botX, botY, botW, botH, m_TexLogoBottom.slot, col);
                }

                // 3C. Intense Full-Logo Optical Bloom Flash (NO scaled duplicates -> ZERO text ghosting!)
                const float idleGlow = (t > kLogoJoinSec + 0.90f)
                    ? (0.04f + 0.04f * (0.5f + 0.5f * std::sin((t - kLogoJoinSec) * 1.45f)))
                    : 0.0f;
                const float exitBloom = (outU > 0.0f) ? (0.42f * std::sin(outU * 3.14159265f)) : 0.0f;
                const float coreBloom = (flash + idleGlow + exitBloom) * exitFade;

                if (coreBloom > 0.004f) {
                    r.SetAdditiveAlphaBlend(true);
                    // Stacked 1:1 exact-aligned letter overexposure (flares ALL letters to brilliant white-gold, ZERO doubling!)
                    const Color b1(255, 250, 224, ToByte((std::min)(1.0f, coreBloom * 1.00f)));
                    if (hasTop)    r.DrawImage(topX, topY, topW, topH, m_TexLogoTop.slot, b1);
                    if (hasBottom) r.DrawImage(botX, botY, botW, botH, m_TexLogoBottom.slot, b1);
                    if (flash > 0.01f) {
                        const Color b2(255, 255, 242, ToByte((std::min)(1.0f, flash * 1.00f * exitFade)));
                        const Color b3(255, 232, 168, ToByte((std::min)(1.0f, flash * 0.85f * exitFade)));
                        if (hasTop) {
                            r.DrawImage(topX, topY, topW, topH, m_TexLogoTop.slot, b2);
                            r.DrawImage(topX, topY, topW, topH, m_TexLogoTop.slot, b3);
                        }
                        if (hasBottom) {
                            r.DrawImage(botX, botY, botW, botH, m_TexLogoBottom.slot, b2);
                            r.DrawImage(botX, botY, botW, botH, m_TexLogoBottom.slot, b3);
                        }
                    }

                    // Even full-width optical aura across ALL letters (no center hotspot)
                    const float optFlash = (flash + exitBloom * 0.65f) * exitFade;
                    if (optFlash > 0.008f) {
                        r.DrawOpticalBloom(cx, logoCy, mainW * 1.36f, mainH * 2.05f,
                                           Color(255, 222, 138, ToByte((std::min)(1.0f, optFlash * 0.56f))));
                    }
                    r.SetAdditiveAlphaBlend(false);
                }
            }
        }

        // --------------------------------------------------------------------
        // 5. Near & Foreground bokeh burning embers (in front of logo)
        // --------------------------------------------------------------------
        RenderSparksByLayers(r, masterFade, false);

        // --------------------------------------------------------------------
        // 6. Bottom UI elements (appear at 2.45s, matching Title.swa frame 147
        //    when Dict[8] button input becomes active)
        // --------------------------------------------------------------------
        if (t >= kPromptAppearSec) {
            fonts.SyncGpuAtlas(r);
            const float promptEnter = SmoothStep(kPromptAppearSec, kPromptAppearSec + 0.50f, t);

            // 6A. Rendered "[ PRESS ANY BUTTON ]" prompt (localized from strings_en.ini)
            const float pulsePhase = (std::max)(0.0f, t - (kPromptAppearSec + 0.50f));
            // 1.0s cycle: 1.0 -> 0.48 -> 1.0 (matching Dict[23] 60-frame repeat loop)
            const float pulse = 0.74f + 0.26f * std::cos(pulsePhase * 6.2831853f);

            // Brief golden confirmation flash when the player presses a button
            float confirmFlash = 0.0f;
            if (m_OutTriggerSec >= 0.0f) {
                const float dtOut = t - m_OutTriggerSec;
                if (dtOut >= 0.0f && dtOut < 0.25f) {
                    confirmFlash = 1.0f - (dtOut / 0.25f);
                }
            }

            const float promptAlpha = promptEnter * (std::min)(1.0f, pulse + confirmFlash * 0.45f) * masterFade;
            if (promptAlpha > 0.004f && !m_PressButtonText.empty()) {
                const float promptY     = vh * 0.816f;
                const float promptSize  = 23.0f;
                const float promptTrack = 2.4f;
                const Color textCol = (confirmFlash > 0.01f)
                    ? Color(255, 246, 218, ToByte(promptAlpha))
                    : Color(248, 208, 140, ToByte(promptAlpha));

                // Soft dark halo + subtle faux-bold pass matching original Title.swa bold sans style
                fonts.DrawText(r, FontRole::Title, promptSize, cx, promptY + 2.0f,
                               m_PressButtonText.c_str(),
                               Color(18, 6, 2, ToByte(promptAlpha * 0.85f)),
                               TextAlign::Center, false, promptTrack);
                fonts.DrawText(r, FontRole::Title, promptSize, cx + 0.7f, promptY,
                               m_PressButtonText.c_str(),
                               Color(textCol.r, textCol.g, textCol.b, ToByte(promptAlpha * 0.65f)),
                               TextAlign::Center, false, promptTrack);
                fonts.DrawText(r, FontRole::Title, promptSize, cx, promptY,
                               m_PressButtonText.c_str(),
                               textCol,
                               TextAlign::Center, true, promptTrack);

                // Subtle straight-alpha additive warm golden glow on pulse peak / confirm flash
                const float glowA = promptEnter * (0.26f * pulse + 0.60f * confirmFlash) * masterFade;
                if (glowA > 0.01f) {
                    r.SetAdditiveAlphaBlend(true);
                    fonts.DrawText(r, FontRole::Title, promptSize, cx, promptY,
                                   m_PressButtonText.c_str(),
                                   Color(255, 192, 98, ToByte((std::min)(1.0f, glowA))),
                                   TextAlign::Center, false, promptTrack);
                    r.SetAdditiveAlphaBlend(false);
                }
            }

            // 6B. HD Namco Bandai Copyright ("TM&©2006 NAMCO BANDAI GAMES INC.")
            if (m_TexCopyright.slot != 0) {
                const float copyAlpha = promptEnter * 0.90f * masterFade;
                if (copyAlpha > 0.004f) {
                    const float copyH = 62.0f;
                    const float copyW = copyH * m_TexCopyright.aspect;
                    const float copyY = vh * 0.854f;
                    r.DrawImage(cx - copyW * 0.5f, copyY, copyW, copyH, m_TexCopyright.slot,
                                Color(255, 226, 178, ToByte(copyAlpha)));
                }
            }

            // 6C. ACZero Enhancer mod info strictly below copyright (matching classic serif style)
            if (!m_ModInfoText.empty()) {
                const float modEnter = SmoothStep(kPromptAppearSec + 0.12f, kPromptAppearSec + 0.65f, t);
                const float modAlpha = modEnter * 0.78f * masterFade;
                if (modAlpha > 0.004f) {
                    const float modY = vh * 0.918f;
                    fonts.DrawText(r, FontRole::Serif, 18.0f, cx, modY,
                                   m_ModInfoText.c_str(),
                                   Color(242, 202, 148, ToByte(modAlpha)),
                                   TextAlign::Center, true, 1.8f);
                }
            }

            // Flush any newly rasterized glyphs on the first frame they appear
            fonts.SyncGpuAtlas(r);
        }
    }

private:
    static constexpr float kLogoJoinSec     = 1.40f; // Moment ACE COMBAT ZERO & THE BELKAN WAR collide
    static constexpr float kLogoJoinScale   = 1.18f; // Logo scale at join moment (before slow settle)
    static constexpr float kLogoSettleSec   = 4.40f; // End of slow, stately logo shrink (1.18x -> 1.00x)
    static constexpr float kPromptAppearSec = 2.45f; // Title.swa frame 147 @ 60fps

    struct LoadedTex {
        uint32_t slot   = 0;
        uint32_t w      = 0;
        uint32_t h      = 0;
        float    aspect = 1.0f;
    };

    struct TitleSpark {
        float x, y;          // normalized [0..1]
        float vx, vy;        // normalized velocity / sec
        float liftBase;
        float liftDelay;     // per-ember random delay before upward lift ramps in
        float liftRampDur;   // per-ember random duration of smooth lift acceleration
        float windDelay;     // per-ember random delay before horizontal wind sweeps in
        float windFactor;
        float drag;
        float initialSize;   // virtual 1080p px
        float aspect;
        float rotAngle;
        float rotSpeed;
        float fuel;          // 1.0 -> 0.0
        float burnRate;
        float age;
        float phase;
        float freq1, freq2;
        float heat;
        float heatTarget;
        float flareTimer;
        float seed01;
        float bokehBlur;
        int   layer;         // 0=far, 1=mid, 2=near bokeh, 3=foreground bokeh
        bool  isBurst;
    };

    static std::string TrimCopy(const std::string& s) {
        size_t a = 0;
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
        size_t b = s.size();
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
        return s.substr(a, b - a);
    }

    static float Clamp01(float v) {
        return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
    }

    static float SmoothStep(float e0, float e1, float x) {
        if (e1 <= e0) return (x >= e1) ? 1.0f : 0.0f;
        const float u = Clamp01((x - e0) / (e1 - e0));
        return u * u * (3.0f - 2.0f * u);
    }

    static uint8_t ToByte(float a) {
        const int v = static_cast<int>(Clamp01(a) * 255.0f + 0.5f);
        return static_cast<uint8_t>((v < 0) ? 0 : ((v > 255) ? 255 : v));
    }

    float NextRand() {
        m_Rng = m_Rng * 1664525u + 1013904223u;
        return static_cast<float>((m_Rng >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }

    int CountAmbientSparks() const {
        int c = 0;
        for (const TitleSpark& s : m_Sparks) {
            if (!s.isBurst) ++c;
        }
        return c;
    }

    void SpawnAmbientSpark(TitleSpark& s) {
        // 3 crisp depth planes (layer 3 giant bokeh blobs removed for clean Title presentation)
        const float rl = NextRand();
        if (rl < 0.42f)      { s.layer = 0; s.bokehBlur = 0.20f; }
        else if (rl < 0.84f) { s.layer = 1; s.bokehBlur = 0.00f; }
        else                 { s.layer = 2; s.bokehBlur = 0.46f + NextRand() * 0.16f; }

        // Spawn along bottom & lower-right edges so embers visibly ignite and rise across screen
        if (NextRand() < 0.72f) {
            s.x = 0.22f + NextRand() * 0.84f;
            s.y = 0.96f + NextRand() * 0.12f;
        } else {
            s.x = 0.94f + NextRand() * 0.10f;
            s.y = 0.54f + NextRand() * 0.48f;
        }

        s.liftBase    = 0.022f + NextRand() * 0.028f;
        s.windFactor  = 0.58f  + NextRand() * 1.05f;
        s.drag        = 1.15f  + NextRand() * 1.25f;
        s.vx          = -(0.015f + NextRand() * 0.028f);
        s.vy          = -s.liftBase;

        const float rSz = NextRand();
        if (s.layer == 0)      s.initialSize = 8.0f   + rSz * 7.0f;
        else if (s.layer == 1) s.initialSize = 22.0f  + (rSz * rSz) * 20.0f;
        else                   s.initialSize = 44.0f  + rSz * 24.0f;

        s.aspect      = 0.90f + NextRand() * 0.45f;
        s.rotAngle    = NextRand() * 6.28318f;
        s.rotSpeed    = (NextRand() - 0.5f) * 2.4f;
        s.seed01      = NextRand();
        s.fuel        = 1.0f;
        s.burnRate    = 0.045f + NextRand() * 0.045f;
        s.age         = 0.0f;
        s.phase       = NextRand() * 6.28318f;
        s.freq1       = 2.6f + NextRand() * 5.2f;
        s.freq2       = 1.3f + NextRand() * 3.4f;
        s.heat        = 0.58f + NextRand() * 0.42f;
        s.heatTarget  = s.heat;
        s.flareTimer  = 0.15f + NextRand() * 0.45f;
        s.isBurst     = false;
    }

    void SpawnImpactBurst(int count) {
        // Dense 512-ember cloud spawned directly inside the exact opaque glyph silhouette
        // of "ACE COMBAT ZERO" + "THE BELKAN WAR" at their exact join-time screen rect:
        //   1) Materializes across the exact letters & lingers briefly
        //   2) Each ember slowly, smoothly ramps up its own randomized upward lift speed
        //   3) Then a gradually building wind sweeps the rising cloud across the screen
        const size_t topCount = m_LogoMaskTopPts.size();
        const size_t botCount = m_LogoMaskBottomPts.size();
        const size_t totalMask = topCount + botCount;
        const bool hasMask = (totalMask > 0);

        for (int i = 0; i < count; ++i) {
            TitleSpark s{};
            const float rl = NextRand();
            if (rl < 0.46f)      { s.layer = 0; s.bokehBlur = 0.18f; s.initialSize = 4.5f  + NextRand() * 5.0f; }
            else if (rl < 0.90f) { s.layer = 1; s.bokehBlur = 0.00f; s.initialSize = 8.5f  + NextRand() * 12.0f; }
            else                 { s.layer = 2; s.bokehBlur = 0.46f + NextRand() * 0.16f; s.initialSize = 22.0f + NextRand() * 16.0f; }

            if (hasMask) {
                const size_t idx = static_cast<size_t>(NextRand() * static_cast<float>(totalMask)) % totalMask;
                if (idx < topCount) {
                    const LogoMaskPt& pt = m_LogoMaskTopPts[idx];
                    s.x = m_TopJoinLeft + pt.u * m_TopJoinWidth  + (NextRand() - 0.5f) * 0.008f;
                    s.y = m_TopJoinTop  + pt.v * m_TopJoinHeight + (NextRand() - 0.5f) * 0.010f;
                } else {
                    const LogoMaskPt& pt = m_LogoMaskBottomPts[idx - topCount];
                    s.x = m_BotJoinLeft + pt.u * m_BotJoinWidth  + (NextRand() - 0.5f) * 0.008f;
                    s.y = m_BotJoinTop  + pt.v * m_BotJoinHeight + (NextRand() - 0.5f) * 0.010f;
                }
            } else {
                s.x = m_TopJoinLeft + NextRand() * m_TopJoinWidth;
                s.y = m_TopJoinTop  + NextRand() * (m_TopJoinHeight + m_BotJoinHeight);
            }

            // Almost stationary at birth so they clearly materialize right out of the text
            s.vx = (NextRand() - 0.5f) * 0.009f;
            s.vy = -(0.001f + NextRand() * 0.005f);

            // Slower, majestic per-ember randomized lift-speed ramp BEFORE wind kicks in
            s.liftBase    = 0.016f + NextRand() * 0.036f;
            s.liftDelay   = 0.15f  + NextRand() * 0.85f;
            s.liftRampDur = 1.10f  + NextRand() * 1.70f;
            s.windDelay   = 1.60f  + NextRand() * 1.50f;

            s.windFactor  = 0.55f  + NextRand() * 1.05f;
            s.drag        = 1.15f  + NextRand() * 0.95f;
            s.aspect      = 1.00f  + NextRand() * 0.52f;
            s.rotAngle    = NextRand() * 6.28318f;
            s.rotSpeed    = (NextRand() - 0.5f) * 3.2f;
            s.seed01      = NextRand();
            s.fuel        = 0.92f  + NextRand() * 0.08f;
            s.burnRate    = 0.065f + NextRand() * 0.090f; // longer life to match slower majestic rise
            s.age         = 0.0f;
            s.phase       = NextRand() * 6.28318f;
            s.freq1       = 3.0f   + NextRand() * 5.2f;
            s.freq2       = 1.4f   + NextRand() * 3.4f;
            s.heat        = 0.88f  + NextRand() * 0.12f;
            s.heatTarget  = 0.72f  + NextRand() * 0.28f;
            s.flareTimer  = 0.16f  + NextRand() * 0.32f;
            s.isBurst     = true;
            m_Sparks.push_back(s);
        }
    }

    void StepSpark(TitleSpark& s, float dtSec) {
        s.age += dtSec;
        s.rotAngle += s.rotSpeed * (1.0f + m_Gust * 0.7f) * dtSec;

        s.flareTimer -= dtSec;
        if (s.flareTimer <= 0.0f) {
            if (NextRand() < 0.34f) {
                s.heatTarget = 0.85f + NextRand() * 0.15f;
                s.flareTimer = 0.16f + NextRand() * 0.32f;
            } else {
                s.heatTarget = 0.20f + NextRand() * 0.45f;
                s.flareTimer = 0.24f + NextRand() * 0.58f;
            }
        }
        s.heat += (s.heatTarget - s.heat) * (std::min)(1.0f, dtSec * 5.2f);
        s.fuel -= s.burnRate * (0.72f + 0.55f * s.heat + 0.25f * m_Gust) * dtSec;

        const float depthSpeed = (s.layer == 0) ? 0.36f
                               : ((s.layer == 2) ? 1.85f
                               : ((s.layer == 3) ? 2.85f : 1.0f));
        const float altitude = Clamp01(1.0f - s.y);
        const float swirlX = std::sin(m_ElapsedSec * 1.15f + s.phase + s.y * 4.5f) * 0.018f;
        const float swirlY = std::cos(m_ElapsedSec * 0.95f + s.phase * 1.3f + s.x * 3.8f) * 0.012f;

        if (s.isBurst) {
            // Phase 1: Per-ember slow smooth randomized upward lift acceleration while near text
            const float liftRamp  = SmoothStep(s.liftDelay, s.liftDelay + s.liftRampDur, s.age);
            // Phase 2: Gradually building horizontal wind sweeps the rising embers across the screen
            const float windRamp  = SmoothStep(s.windDelay, s.windDelay + 3.00f, s.age);
            const float activeSwirl = 0.10f + 0.90f * (std::max)(liftRamp, windRamp);

            const float windPushX = -(0.032f + altitude * 0.082f + m_Gust * 0.065f) * s.windFactor * windRamp;
            const float liftPushY = -(s.liftBase * (0.04f + 0.96f * liftRamp) + m_Gust * 0.014f * windRamp);
            const float targetVx  = (windPushX + swirlX * activeSwirl) * depthSpeed;
            const float targetVy  = (liftPushY + swirlY * activeSwirl) * depthSpeed;
            const float damp      = (std::min)(1.0f, s.drag * (0.55f + 0.65f * (std::max)(liftRamp, windRamp)) * dtSec);
            s.vx += (targetVx - s.vx) * damp;
            s.vy += (targetVy - s.vy) * damp;
        } else {
            const float gustPushX = -m_Gust * 0.055f * s.windFactor;
            const float gustPushY = -m_Gust * 0.018f;
            const float targetVx = (-(0.018f + altitude * 0.068f) * s.windFactor + gustPushX + swirlX) * depthSpeed;
            const float targetVy = (-(s.liftBase * (0.65f + 0.35f * s.fuel)) + gustPushY + swirlY) * depthSpeed;
            const float damp = (std::min)(1.0f, s.drag * dtSec);
            s.vx += (targetVx - s.vx) * damp;
            s.vy += (targetVy - s.vy) * damp;
        }

        s.x += s.vx * dtSec;
        s.y += s.vy * dtSec;
    }

    void RenderSparksByLayers(UiRenderer& r, float masterFade, bool backgroundPass) const {
        if (m_Sparks.empty() || masterFade <= 0.004f) return;
        const float vw = r.VirtualWidth();
        const float vh = r.VirtualHeight();

        // Dual Premultiplied-Ash + Additive-Fire PSO (Pixel Shader Mode 2)
        r.SetPremultipliedBlend(true);
        for (const TitleSpark& s : m_Sparks) {
            const bool isBg = (s.layer <= 1);
            if (isBg != backgroundPass) continue;
            if (s.fuel <= 0.005f) continue;

            const float spawnFade = (std::min)(1.0f, s.age / (s.isBurst ? 0.08f : 0.48f));
            const float fuelFade  = (s.fuel < 0.28f) ? (s.fuel / 0.28f) : 1.0f;

            const float tumble = 0.65f + 0.35f * std::fabs(std::cos(s.rotAngle * 0.9f));
            const float osc    = 0.5f + 0.5f * std::sin(m_ElapsedSec * s.freq1 + s.phase)
                                             * std::cos(m_ElapsedSec * s.freq2 + s.phase * 1.6f);
            const float gustBoost = m_Gust * 0.34f;
            const float heat   = (std::max)(0.14f, (std::min)(1.0f,
                (s.heat * 0.65f + osc * 0.35f + gustBoost) * tumble * (0.42f + 0.58f * s.fuel)));

            const float layerAlpha = (s.layer == 0) ? 0.54f
                                   : ((s.layer == 2) ? 0.62f
                                   : ((s.layer == 3) ? 0.16f : 1.0f));
            const float alpha = Clamp01(spawnFade * fuelFade * layerAlpha * (0.45f + 0.55f * heat) * masterFade);
            if (alpha <= 0.015f) continue;

            const float burnScale = (s.layer == 3) ? 1.0f : (0.26f + 0.74f * std::sqrt(s.fuel));
            const float curSize   = s.initialSize * burnScale * (0.90f + 0.22f * heat);
            const float w         = curSize * s.aspect;
            const float h         = curSize;

            const float crust        = (std::max)(0.02f, (std::min)(0.92f, 1.0f - s.fuel));
            const float seedAndCrust = std::floor(1.0f + s.seed01 * 28.0f) + crust;

            r.DrawEmberQuad(s.x * vw, s.y * vh, w, h, s.rotAngle, heat, s.bokehBlur, seedAndCrust, alpha);
        }
        r.SetPremultipliedBlend(false);
    }

    static void DrawCoverImage(UiRenderer& r, const LoadedTex& tex, float zoom, float offsetX, float offsetY, Color tint) {
        const float vw = r.VirtualWidth();
        const float vh = r.VirtualHeight();
        const float screenAspect = (vh > 0.0f) ? (vw / vh) : (16.0f / 9.0f);
        float drawW = vw;
        float drawH = vh;
        if (screenAspect > tex.aspect) {
            drawW = vw;
            drawH = vw / tex.aspect;
        } else {
            drawH = vh;
            drawW = vh * tex.aspect;
        }
        drawW *= zoom;
        drawH *= zoom;
        const float x = (vw - drawW) * 0.5f + offsetX;
        const float y = (vh - drawH) * 0.5f + offsetY;
        r.DrawImage(x, y, drawW, drawH, tex.slot, tint);
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

    void LoadTex(UiRenderer& r, LoadedTex& out, const char* filename, int logoMaskTarget = 0) {
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
        if (logoMaskTarget == 1 || logoMaskTarget == 2) {
            std::vector<LogoMaskPt>& dstMask = (logoMaskTarget == 1) ? m_LogoMaskTopPts : m_LogoMaskBottomPts;
            dstMask.clear();
            const int step = 4;
            dstMask.reserve(static_cast<size_t>((w / step) * (h / step) / 2));
            const float invW = 1.0f / static_cast<float>(w);
            const float invH = 1.0f / static_cast<float>(h);
            for (int y = 0; y < h; y += step) {
                const uint8_t* row = rgba + static_cast<size_t>(y) * static_cast<size_t>(w) * 4u;
                for (int x = 0; x < w; x += step) {
                    if (row[static_cast<size_t>(x) * 4u + 3u] >= 48u) {
                        dstMask.push_back(LogoMaskPt{
                            (static_cast<float>(x) + 0.5f) * invW,
                            (static_cast<float>(y) + 0.5f) * invH
                        });
                    }
                }
            }
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
        LoadTex(r, m_TexLogoTop,    "logo_top.png",    1);
        LoadTex(r, m_TexLogoBottom, "logo_bottom.png", 2);
        LoadTex(r, m_TexCopyright,  "copyright.png",   0);
    }

    struct LogoMaskPt {
        float u;
        float v;
    };

    std::filesystem::path m_GameDir;
    std::filesystem::path m_AssetsDir;
    std::filesystem::path m_StringsPath;
    std::string           m_PressButtonText = "[ PRESS ANY BUTTON ]";
    std::string           m_ModInfoText     = "ACZERO ENHANCER v0.25";

    bool    m_Active            = false;
    bool    m_FromWakeLogo      = false;
    bool    m_ClockArmed        = false;
    bool    m_TexturesAttempted = false;
    int     m_PreloadStage      = 0;
    bool    m_BurstFired        = false;
    bool    m_SparksAdopted     = false;
    float   m_ElapsedSec        = 0.0f;
    int32_t m_NativeOutFrame    = 0;
    float   m_OutTriggerSec     = -1.0f;
    float   m_TopJoinLeft       = 0.14f;
    float   m_TopJoinWidth      = 0.72f;
    float   m_TopJoinTop        = 0.34f;
    float   m_TopJoinHeight     = 0.14f;
    float   m_BotJoinLeft       = 0.18f;
    float   m_BotJoinWidth      = 0.64f;
    float   m_BotJoinTop        = 0.46f;
    float   m_BotJoinHeight     = 0.10f;

    LoadedTex m_TexLogoTop{};
    LoadedTex m_TexLogoBottom{};
    LoadedTex m_TexCopyright{};

    std::vector<LogoMaskPt> m_LogoMaskTopPts;
    std::vector<LogoMaskPt> m_LogoMaskBottomPts;
    std::vector<TitleSpark> m_Sparks;
    float    m_SpawnAccum = 0.0f;
    float    m_Gust       = 0.0f;
    float    m_GustTarget = 0.0f;
    float    m_GustTimer  = 2.6f;
    uint32_t m_Rng        = 0xAC02006u;
};

} // namespace acz_ui
