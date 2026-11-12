// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — ACZero Enhancer Core Plugin
// File: enhancer.cpp (v0.23.6 — Widescreen 16:9 & 21:9 Menus/HUD/Subtitles, 21:9 3D FOV, 60 FPS Unlocker & DirectInput/XInput/KB Input Engine)
// Build: Visual Studio 2026 / 2022 x64 (Release) -> enhancer.asi
// Usage: Place enhancer.asi into "<GameDir>\mods\enhancer\enhancer.asi" (loaded by version.dll)
//        Configuration is stored in "<GameDir>\mods\enhancer\enhancer.ini"
//        Operates 100% independently of memdump.dll and acz_hdtex.dll.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <filesystem>
#include <share.h>

#if defined(_MSC_VER)
#pragma warning(disable: 26117 26110 26115) // Suppress MSVC SAL false-positive concurrency warnings in std::lock_guard scopes
#endif


#pragma comment(lib, "user32.lib")

namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
// Configuration & Constants
// ----------------------------------------------------------------------------
static constexpr const char* ENHANCER_VERSION_STR = "v0.23.6";

// ----------------------------------------------------------------------------
// Dual-Version Executable Detection & RVA Mapper (v1.0.2.1 & v2.0.0.1)
// ----------------------------------------------------------------------------
enum class AczExeVersion : uint32_t {
    V1_0_2_1 = 0, // Initial Release (TimeDateStamp = 0x6A9E70E0, SizeOfImage = 0x0385D000)
    V2_0_0_1 = 1  // Standalone Steam Release (TimeDateStamp = 0x6AC61130, SizeOfImage = 0x0389B000)
};

static AczExeVersion g_AczVersion = AczExeVersion::V1_0_2_1;

static AczExeVersion DetectAczExeVersion(uintptr_t exe_base) {
    if (exe_base == 0) return AczExeVersion::V1_0_2_1;
    __try {
        if (*reinterpret_cast<const uint16_t*>(exe_base) == 0x5A4Du) {
            const uint32_t lfanew = *reinterpret_cast<const uint32_t*>(exe_base + 0x3Cu);
            if (lfanew >= 0x40u && lfanew < 0x1000u) {
                const uintptr_t nt = exe_base + lfanew;
                const uint32_t ts = *reinterpret_cast<const uint32_t*>(nt + 0x08u);
                const uint32_t img_size = *reinterpret_cast<const uint32_t*>(nt + 0x50u);
                if (ts == 0x6AC61130u || img_size >= 0x03890000u) return AczExeVersion::V2_0_0_1;
                if (ts == 0x6A9E70E0u || img_size == 0x0385D000u) return AczExeVersion::V1_0_2_1;
            }
        }
        const uint8_t* p2001 = reinterpret_cast<const uint8_t*>(exe_base + 0x00203F40u);
        if (p2001[0] == 0x40u && p2001[1] == 0x53u && p2001[2] == 0x48u && p2001[3] == 0x89u) {
            return AczExeVersion::V2_0_0_1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return AczExeVersion::V1_0_2_1;
}

static const char* GetAczExeVersionString(AczExeVersion ver) {
    return (ver == AczExeVersion::V2_0_0_1) ? "2.0.0.1 (Standalone Steam)" : "1.0.2.1 (Initial Release)";
}

static uintptr_t AczRva(uintptr_t rva_1021) {
    if (g_AczVersion != AczExeVersion::V2_0_0_1) return rva_1021;
    if (rva_1021 < 0x004CA0u) return rva_1021;
    if (rva_1021 < 0x004E80u) return rva_1021 + 0x0010u;
    if (rva_1021 < 0x18AD20u) return rva_1021 + 0x0040u;
    if (rva_1021 < 0x18D7A4u) return rva_1021 + 0x0680u;
    if (rva_1021 < 0x18E520u) return rva_1021 + 0x0685u;
    if (rva_1021 < 0x21E068u) return rva_1021 + 0x0680u;
    if (rva_1021 < 0x27B000u) return rva_1021 + 0x1900u;
    if (rva_1021 == 0x0027B2E8u) return 0x0027D2F8u;
    if (rva_1021 == 0x0027B6F0u) return 0x0027D6E8u;
    if (rva_1021 < 0x002CEEF0u) return rva_1021 + 0x2040u;
    if (rva_1021 >= 0x00308F00u && rva_1021 < 0x00309000u) return rva_1021 + 0x2600u;
    if (rva_1021 < 0x00309000u) return rva_1021 + 0x2238u;
    if (rva_1021 < 0x00324600u) return rva_1021 + 0x3000u; // Initialized .data
    return rva_1021 + 0x3020u;
}

// Official version.dll (ACZ Mod Loader 1.2) Add-on API structure passed to AczAddonInit
using FnModLoaderLog = void (__cdecl*)(const char* fmt, ...);

struct AczAddonApi {
    uint32_t       struct_size;  // +0x00 (0x30 = 48 bytes)
    uint32_t       api_version;  // +0x04 (1)
    HMODULE        addon_module; // +0x08
    HMODULE        exe_module;   // +0x10
    const wchar_t* game_dir;     // +0x18
    const wchar_t* mods_dir;     // +0x20
    FnModLoaderLog log_fn;       // +0x28 (writes directly to modloader.log + newline)
};

// ----------------------------------------------------------------------------
// Global State
// ----------------------------------------------------------------------------
static FnModLoaderLog        g_ModLoaderLogFn               = nullptr;
static FILE*                 g_FallbackLogFile              = nullptr;
static std::atomic<bool>     g_PluginInitialized{false};
static HMODULE               g_SelfModule                   = nullptr;
static uintptr_t             g_ExeBase                      = 0;

static fs::path              g_GameDir;
static fs::path              g_ModsDir;
static fs::path              g_EnhancerDir;
static fs::path              g_EnhancerIniPath;

// Configuration options loaded from mods\enhancer\enhancer.ini
static bool                  g_EnableLogging                = true;
static bool                  g_Unlock2PlayerCampaign        = false; // 0 = Single-Player (merge KB+Pad into P1, lock 2P), 1 = Split P1 Pad + P2 KB
static bool                  g_EnableMenuOverrides          = true;
static int                   g_MenuAspectMode               = 0;     // 0 = Auto, 1 = Force 16:9, 2 = Force 4:3, 3 = Force 21:9
static int                   g_ScreenAspect                 = 1;     // 0 = 4:3, 1 = 16:9, 2 = 21:9 (Active aspect ratio; save file always writes 16:9)

static void SaveScreenAspectToIni(int aspect) {
    if (aspect < 0 || aspect > 2) return;
    g_ScreenAspect = aspect;
    const std::string ini_str = g_EnhancerIniPath.string();
    const std::string val_str = std::to_string(aspect);
    WritePrivateProfileStringA("Widescreen", "ScreenAspect", val_str.c_str(), ini_str.c_str());
}

// ----------------------------------------------------------------------------
// Logging Helper (Writes directly to modloader.log via version.dll API or shared FILE*)
// ----------------------------------------------------------------------------
static void LogMsg(const char* fmt, ...) {
    if (!g_EnableLogging) return;
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';

    if (g_ModLoaderLogFn != nullptr) {
        g_ModLoaderLogFn("%s", buf);
    } else if (g_FallbackLogFile != nullptr) {
        fprintf(g_FallbackLogFile, "%s\n", buf);
        fflush(g_FallbackLogFile);
    }
}

// ----------------------------------------------------------------------------
// Minimal x64 Detour & Memory Patch Helpers
// ----------------------------------------------------------------------------
static void WriteAbsoluteJump64(void* dst, const void* target) {
    if (!dst) return;
    uint8_t jmp[14];
    jmp[0] = 0xFF;
    jmp[1] = 0x25;
    const uint32_t rel_offset = 0;
    std::memcpy(jmp + 2, &rel_offset, sizeof(rel_offset));
    const uint64_t target_u64 = reinterpret_cast<uint64_t>(target);
    std::memcpy(jmp + 6, &target_u64, sizeof(target_u64));
    std::memcpy(dst, jmp, sizeof(jmp));
}

#pragma warning(push)
#pragma warning(disable: 6385 6386)
static bool InstallDetour64(
    const char* name,
    uintptr_t target_addr,
    const uint8_t* expected_prologue,
    size_t prologue_len,
    const void* hook_fn,
    void** out_trampoline
) {
    constexpr size_t kTrampAllocSize = 64;
    if (prologue_len < 14 || prologue_len > 32 || (prologue_len + 14) > kTrampAllocSize) return false;

    const uint8_t* target_bytes = reinterpret_cast<const uint8_t*>(target_addr);
    const bool has_rip_mov_rax_at_11 = (prologue_len == 18 &&
                                        expected_prologue[11] == 0x48 &&
                                        expected_prologue[12] == 0x8B &&
                                        expected_prologue[13] == 0x05);
    const size_t verify_len = has_rip_mov_rax_at_11 ? 14u : prologue_len;
    if (std::memcmp(target_bytes, expected_prologue, verify_len) != 0) {
        LogMsg("[Enhancer] WARNING: Prologue mismatch at %s (%p)!", name, reinterpret_cast<void*>(target_addr));
        return false;
    }

    uint8_t* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, kTrampAllocSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)
    );
    if (!tramp) return false;

    std::memset(tramp, 0xCC, kTrampAllocSize);
    if (has_rip_mov_rax_at_11) {
        std::memcpy(tramp, target_bytes, 11);
        int32_t disp32 = *reinterpret_cast<const int32_t*>(target_bytes + 14);
        uint64_t abs_target = static_cast<uint64_t>((target_addr + 18) + static_cast<intptr_t>(disp32));
        tramp[11] = 0x48;
        tramp[12] = 0xB8; // movabs rax, imm64
        std::memcpy(tramp + 13, &abs_target, sizeof(abs_target));
        tramp[21] = 0x48;
        tramp[22] = 0x8B;
        tramp[23] = 0x00; // mov rax, [rax]
        WriteAbsoluteJump64(tramp + 24, reinterpret_cast<const void*>(target_addr + prologue_len));
    } else {
        std::memcpy(tramp, target_bytes, prologue_len);
        WriteAbsoluteJump64(tramp + prologue_len, reinterpret_cast<const void*>(target_addr + prologue_len));
    }
    FlushInstructionCache(GetCurrentProcess(), tramp, kTrampAllocSize);

    DWORD old_protect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(target_addr), prologue_len, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    WriteAbsoluteJump64(reinterpret_cast<void*>(target_addr), hook_fn);
    for (size_t i = 14; i < prologue_len; ++i) {
        reinterpret_cast<uint8_t*>(target_addr)[i] = 0x90;
    }

    VirtualProtect(reinterpret_cast<void*>(target_addr), prologue_len, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(target_addr), prologue_len);

    *out_trampoline = tramp;
    LogMsg("[Enhancer] Installed hook '%s' at %p (trampoline at %p)",
           name, reinterpret_cast<void*>(target_addr), tramp);
    return true;
}
#pragma warning(pop)


static bool PatchMemoryBytes(
    uintptr_t target_addr,
    const uint8_t* patch_bytes,
    size_t len,
    const uint8_t* expected_orig = nullptr
) {
    if (g_ExeBase != 0 && target_addr > g_ExeBase && target_addr < g_ExeBase + 0x03850000u) {
        uintptr_t rva_1021 = target_addr - g_ExeBase;
        target_addr = g_ExeBase + AczRva(rva_1021);
    }
    if (!target_addr || !patch_bytes || len == 0) return false;
    if (expected_orig != nullptr) {
        if (std::memcmp(reinterpret_cast<const void*>(target_addr), expected_orig, len) != 0) {
            return false;
        }
    }
    DWORD old_protect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(target_addr), len, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(target_addr), patch_bytes, len);
    VirtualProtect(reinterpret_cast<void*>(target_addr), len, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(target_addr), len);
    return true;
}

// ----------------------------------------------------------------------------
// Load mods\enhancer\enhancer.ini Configuration
// ----------------------------------------------------------------------------
static int ReadEnhancerIniInt(const char* key, int default_val) {
    const std::string ini_str = g_EnhancerIniPath.string();
    int val = GetPrivateProfileIntA("Enhancer", key, -99999, ini_str.c_str());
    if (val != -99999) return val;
    val = GetPrivateProfileIntA("Settings", key, -99999, ini_str.c_str());
    if (val != -99999) return val;
    val = GetPrivateProfileIntA("General", key, -99999, ini_str.c_str());
    if (val != -99999) return val;
    return default_val;
}

static void LoadEnhancerConfigIni() {
    std::error_code ec;
    fs::create_directories(g_EnhancerDir, ec);

    if (!fs::exists(g_EnhancerIniPath, ec)) {
        FILE* f = nullptr;
        fopen_s(&f, g_EnhancerIniPath.string().c_str(), "w");
        if (f) {
            fprintf(f,
                "; ============================================================================\n"
                "; Ace Combat Zero: The Belkan War — ACZero Enhancer Configuration (%s)\n"
                "; Used by: mods\\enhancer\\enhancer.asi\n"
                "; ============================================================================\n\n"
                "[Enhancer]\n"
                "; EnableLogging: Write [Enhancer] events directly into mods\\enhancer\\enhancer.log (1 = Yes, 0 = No)\n"
                "EnableLogging=1\n\n"
                "; EnableMenuOverrides: Load custom 16:9 / 21:9 widescreen menu layouts from mods\\textures\\menus\\ (1 = Yes, 0 = No)\n"
                "EnableMenuOverrides=1\n\n"
                "; MenuAspectMode: Widescreen menu aspect mode (0 = Auto from game Screen Size, 1 = Force 16:9, 2 = Force 4:3, 3 = Force 21:9)\n"
                "MenuAspectMode=0\n\n"
                "; Unlock2PlayerCampaign: Two-player Co-Op / VS Mode input behavior:\n"
                ";   0 = Single-Player mode (merges Gamepad and Keyboard into Player 1; keeps 2P NEW GAME & VS MODE locked)\n"
                ";   1 = Two-Player mode (unlocks 2P NEW GAME & VS MODE; Player 1 = Gamepad, Player 2 = Keyboard)\n"
                "Unlock2PlayerCampaign=0\n\n"
                "[Widescreen]\n"
                "; ScreenAspect: Active aspect ratio stored independently of the save file (100%% save-safe):\n"
                ";   0 = 4:3 Standard\n"
                ";   1 = 16:9 Widescreen (Default)\n"
                ";   2 = 21:9 Ultrawide\n"
                "; The save file on disk always retains standard 16:9 to guarantee clean compatibility.\n"
                "; Can be toggled in-game via OPTIONS -> DISPLAY SETTINGS -> SCREEN SIZE or edited directly here.\n"
                "ScreenAspect=1\n",
                ENHANCER_VERSION_STR
            );
            fclose(f);
        }
    }

    g_EnableLogging         = ReadEnhancerIniInt("EnableLogging",         1) != 0;
    g_Unlock2PlayerCampaign = ReadEnhancerIniInt("Unlock2PlayerCampaign", 0) != 0;

    // Read Widescreen Menu Override settings from enhancer.ini (with fallback to acz_hdtex.ini for compatibility)
    fs::path hd_ini = g_EnhancerDir / "acz_hdtex.ini";
    std::error_code hd_ec;
    if (!fs::exists(hd_ini, hd_ec)) hd_ini = g_ModsDir / "acz_hdtex.ini";
    int default_menu_ovr  = 1;
    int default_menu_mode = 0;
    if (fs::exists(hd_ini, hd_ec)) {
        default_menu_ovr  = GetPrivateProfileIntA("ACZ_HDTex", "EnableMenuOverrides", 1, hd_ini.string().c_str());
        default_menu_mode = GetPrivateProfileIntA("ACZ_HDTex", "MenuAspectMode",      0, hd_ini.string().c_str());
    }
    g_EnableMenuOverrides   = ReadEnhancerIniInt("EnableMenuOverrides", default_menu_ovr) != 0;

    const std::string ini_str = g_EnhancerIniPath.string();
    g_ScreenAspect = GetPrivateProfileIntA("Widescreen", "ScreenAspect", -1, ini_str.c_str());
    if (g_ScreenAspect < 0 || g_ScreenAspect > 2) {
        g_ScreenAspect = GetPrivateProfileIntA("Enhancer", "ScreenAspect", 1, ini_str.c_str());
        if (g_ScreenAspect < 0 || g_ScreenAspect > 2) g_ScreenAspect = 1;
    }
    g_MenuAspectMode        = ReadEnhancerIniInt("MenuAspectMode",      default_menu_mode);
}

// ============================================================================
// Widescreen 16:9 / 21:9 UI, HUD, Subtitles, Menus, Video & 3D FOV Engine
// ============================================================================
// Hook 10..12: Widescreen 16:9/21:9 Menu Override Engine (CSwgInstance::Init, CSwgInstance::Update, CSwadMenu::Draw)
static constexpr uintptr_t RVA_CSwgInstanceInit            = 0x001F7BE0; // +0x1F7BE0 (v1.0.2.1) / +0x1F8260 (v2.0.0.1)
static constexpr uintptr_t RVA_CSwgInstanceUpdate          = 0x001F8180; // +0x1F8180 (v1.0.2.1) / +0x1F8800 (v2.0.0.1)
static constexpr uintptr_t RVA_CSwadMenuDraw               = 0x001CBB70; // +0x1CBB70 (v1.0.2.1) / +0x1CC1F0 (v2.0.0.1)
static constexpr uintptr_t RVA_MissionTitleDraw            = 0x00173120; // AVModeMissionTitle::Draw (+0x173160 in v2.0.0.1)
static constexpr uintptr_t RVA_MissionTitleVirtualWLoad    = 0x00173254; // vmovss xmm0, [0x1402d291c] (512.0f) in AVModeMissionTitle::Draw (+0x173294 in v2.0.0.1)
static constexpr uintptr_t RVA_LoadingScreenDraw           = 0x00205050; // CLoadingScreen::Draw (+0x2056D0 in v2.0.0.1)
static constexpr uintptr_t RVA_LoadingScreenVirtualWLoad   = 0x002050EE; // vmovss xmm0, [0x1402d291c] (512.0f) in CLoadingScreen::Draw (+0x20576E in v2.0.0.1)
static constexpr uintptr_t RVA_CutsceneSubFontDraw         = 0x0000B410; // CCutsceneSubFont::Draw (+0x00B450 in v2.0.0.1)
static constexpr uintptr_t RVA_CutsceneSubFontDrawEmblem   = 0x0000B810; // CCutsceneSubFont::DrawEmblemGim (+0x00B850 in v2.0.0.1)
static constexpr uintptr_t RVA_CutsceneFontDraw            = 0x001FCBA0; // CCutsceneFont::Draw (+0x1FD220 in v2.0.0.1)
static constexpr uintptr_t RVA_GsBuildProjectionMatrices   = 0x001FBEC0; // GsBuildProjectionMatrices (+0x1FC540 in v2.0.0.1)
static constexpr uintptr_t RVA_GetAspectScaleY             = 0x001FD130; // GetAspectScaleY (+0x1FD7B0 in v2.0.0.1)
static constexpr uintptr_t RVA_RadioSubFontDraw            = 0x001A5100; // CRadioSubFont::DrawRadioSub (+0x1A5780 in v2.0.0.1)
static constexpr uintptr_t RVA_CHudManagerDraw             = 0x000AE510; // CHudManager::Draw (+0x0AE550 in v2.0.0.1)
static constexpr uintptr_t RVA_PauseMenuDraw               = 0x000ED0E0; // CPauseMenu::Draw (+0x0ED120 in v2.0.0.1)
static constexpr uintptr_t RVA_PauseSubMenuDraw            = 0x000EFDC0; // CPauseSubMenu::Draw (+0x0EFE00 in v2.0.0.1)
static constexpr uintptr_t RVA_DebriefingDraw              = 0x00016E30; // AVModeDebriefing::Draw (+0x016E70 in v2.0.0.1)
static constexpr uintptr_t RVA_ScissorRadarBypass          = 0x0019451B; // UpdateGsFrameAndScissor hardcoded 16:9 radar scissor branch (+0x194B9B in v2.0.0.1)
static constexpr uintptr_t RVA_HudDrawTextLeft             = 0x000BAF60; // CHudManager::DrawTargetTextLeft (+0x0BAFA0 in v2.0.0.1)
static constexpr uintptr_t RVA_HudDrawTextRight            = 0x000BB1C0; // CHudManager::DrawTargetTextRight (+0x0BB200 in v2.0.0.1)
static constexpr uintptr_t RVA_HudStatusAlerts             = 0x000B1B60; // CHudManager::DrawStatusAlerts (+0x0B1BA0 in v2.0.0.1)
static constexpr uintptr_t RVA_HudBottomMessage            = 0x000B2950; // CHudManager::DrawBottomMessage (+0x0B2990 in v2.0.0.1)
static constexpr uintptr_t RVA_HudCountdown                = 0x000B1680; // CHudManager::DrawCountdown (+0x0B16C0 in v2.0.0.1)
static constexpr uintptr_t RVA_HudDrawTextFloat            = 0x000BB3E0; // CHudManager::DrawTextFloat (+0x0BB420 in v2.0.0.1)
static constexpr uintptr_t RVA_IsMovieCutsceneFontActive   = 0x00326F85; // Set to 1 inside CMovieCutsceneFont::Draw (+0x329FA5 in v2.0.0.1)
static constexpr uintptr_t RVA_IsFmvWidescreen             = 0x0032C92A; // Set to 1 during 16:9 FMV cutscenes in DrawMovieQuad (+0x32F94A in v2.0.0.1)
static constexpr uintptr_t RVA_CSysDataIsWidescreen        = 0x0031EEF8; // CSysData+0x28: 0 = Menu, 1 = Mission Widescreen (+0x321EF8 in v2.0.0.1)
static constexpr uintptr_t RVA_ScreenSizeOption            = 0x00310E8C; // CSysData+0x2C: 0 = 4:3, 1 = 16:9 (+0x313E8C in v2.0.0.1)
static constexpr uintptr_t RVA_ScissorBranch1              = 0x001943C4; // UpdateGsFrameAndScissor 4:3 pillarbox je (+0x194A44 in v2.0.0.1)
static constexpr uintptr_t RVA_ScissorBranch2              = 0x001944BB; // UpdateGsFrameAndScissor 4:3 pillarbox jne (+0x194B3B in v2.0.0.1)
static constexpr uintptr_t RVA_RenderWidth                 = 0x0030F274; // Active render width (+0x312274 in v2.0.0.1)
static constexpr uintptr_t RVA_RenderHeight                = 0x0030F278; // Active render height (+0x312278 in v2.0.0.1)
static constexpr uintptr_t RVA_GsVirtualWidth              = 0x003566C0; // GS virtual screen width: 682 for 16:9, 896 for 21:9 4:3 pillarbox (+0x3596E0 in v2.0.0.1)
static constexpr uintptr_t RVA_ScissorRightMul             = 0x00194410; // vmulss xmm0, xmm1, [0x1402d2240] (+0x194A90 in v2.0.0.1)
static constexpr uintptr_t RVA_ScissorLeftMul1             = 0x00194418; // vmulss xmm1, xmm1, [0x1402d2018] (+0x194A98 in v2.0.0.1)
static constexpr uintptr_t RVA_ScissorLeftMul2             = 0x001944C9; // vmulss xmm1, xmm0, [0x1402d2018] (+0x194B49 in v2.0.0.1)
static constexpr uintptr_t RVA_PillarboxDataCave           = 0x00308F40; // Zero-padded float cave at end of .rdata (+0x30B540 in v2.0.0.1; +0x00..+0x1C reserved for enhancer.dll)
static constexpr uintptr_t RVA_SpiderCenterXLoad           = 0x001C530E; // vmovss xmm4, [0x1402d2b1c] (28672.0f) in CSwgSpider::GenerateVerts (+0x1C598E in v2.0.0.1)
static constexpr uintptr_t RVA_AssaultModelCenterXLoad     = 0x00149FAB; // mov eax, [rsi+0x130C] (1900.0f) in AssaultRecordModel::Update (+0x149FEB in v2.0.0.1)
static constexpr uintptr_t RVA_DrawMovieOrthoHLoad         = 0x00181E31; // vmovss xmm0, [0x1402d2d9c] (-2.0f) in DrawMovieQuad (+0x1824B1 in v2.0.0.1)
static constexpr uintptr_t RVA_DrawMovieOrthoWLoad         = 0x00181E47; // vmovss xmm1, [0x1402d23c0] (+2.0f) in DrawMovieQuad (+0x1824C7 in v2.0.0.1)


static constexpr uintptr_t RVA_GameFontInit_Fallback       = 0x001FFBD0;
static const uint8_t PROLOGUE_GameFontInit_Fallback[16] = {
    0x48, 0x83, 0xEC, 0x08,             // sub rsp, 8
    0x48, 0x8B, 0x02,                   // mov rax, [rdx]
    0x4C, 0x8B, 0xCA,                   // mov r9, rdx
    0x4C, 0x8B, 0xC1,                   // mov r8, rcx
    0x48, 0x85, 0xC0                    // test rax, rax
};

static const uint8_t PROLOGUE_CSwgInstanceInit[15] = {
    0x48, 0x89, 0x5C, 0x24, 0x08,       // mov [rsp+08h], rbx
    0x48, 0x89, 0x6C, 0x24, 0x10,       // mov [rsp+10h], rbp
    0x48, 0x89, 0x74, 0x24, 0x18        // mov [rsp+18h], rsi
};

static const uint8_t PROLOGUE_CSwgInstanceUpdate[16] = {
    0x40, 0x53,                                     // push rbx
    0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00,       // sub rsp, 90h
    0x80, 0x79, 0x01, 0x00,                         // cmp byte ptr [rcx+1], 0
    0x48, 0x8B, 0xD9                                // mov rbx, rcx
};

static const uint8_t PROLOGUE_CSwadMenuDraw[14] = {
    0x40, 0x53,                                     // push rbx
    0x48, 0x83, 0xEC, 0x20,                         // sub rsp, 20h
    0x48, 0x83, 0x79, 0x08, 0x00,                   // cmp qword ptr [rcx+8], 0
    0x48, 0x8B, 0xD9                                // mov rbx, rcx
};

static const uint8_t PROLOGUE_MissionTitleDraw[23] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,                   // mov [rsp+10h], rbx
    0x48, 0x89, 0x6C, 0x24, 0x18,                   // mov [rsp+18h], rbp
    0x48, 0x89, 0x74, 0x24, 0x20,                   // mov [rsp+20h], rsi
    0x57,                                           // push rdi
    0x48, 0x81, 0xEC, 0xF0, 0x01, 0x00, 0x00        // sub rsp, 1F0h
};

static const uint8_t PROLOGUE_LoadingScreenDraw[18] = {
    0x40, 0x53,                                     // push rbx
    0x56,                                           // push rsi
    0x57,                                           // push rdi
    0x48, 0x81, 0xEC, 0x40, 0x02, 0x00, 0x00,       // sub rsp, 240h
    0x48, 0x8B, 0x05, 0xDE, 0x3F, 0x10, 0x00        // mov rax, [rip + __security_cookie] (relocated in trampoline)
};


static const uint8_t PROLOGUE_CutsceneSubFontDraw[17] = {
    0x48, 0x89, 0x74, 0x24, 0x20,                   // mov [rsp+20h], rsi
    0x57,                                           // push rdi
    0x41, 0x56,                                     // push r14
    0x41, 0x57,                                     // push r15
    0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00        // sub rsp, 80h
};

static const uint8_t PROLOGUE_CutsceneSubFontDrawEmblem[19] = {
    0x48, 0x8B, 0xC4,                               // mov rax, rsp
    0x48, 0x89, 0x68, 0x20,                         // mov [rax+20h], rbp
    0x56,                                           // push rsi
    0x57,                                           // push rdi
    0x41, 0x54,                                     // push r12
    0x41, 0x55,                                     // push r13
    0x41, 0x57,                                     // push r15
    0x48, 0x83, 0xEC, 0x70                          // sub rsp, 70h
};

static const uint8_t PROLOGUE_CutsceneFontDraw[16] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,                   // mov [rsp+10h], rbx
    0x55,                                           // push rbp
    0x56,                                           // push rsi
    0x57,                                           // push rdi
    0x41, 0x56,                                     // push r14
    0x41, 0x57,                                     // push r15
    0x48, 0x83, 0xEC, 0x50                          // sub rsp, 50h
};

static const uint8_t PROLOGUE_GsBuildProjectionMatrices[19] = {
    0x48, 0x8B, 0xC4,                               // mov rax, rsp
    0x48, 0x89, 0x58, 0x08,                         // mov [rax+08h], rbx
    0x48, 0x89, 0x70, 0x10,                         // mov [rax+10h], rsi
    0x48, 0x89, 0x78, 0x18,                         // mov [rax+18h], rdi
    0x4C, 0x89, 0x70, 0x20                          // mov [rax+20h], r14
};

static const uint8_t PROLOGUE_RadioSubFontDraw[16] = {
    0x4C, 0x89, 0x4C, 0x24, 0x20,                   // mov [rsp+20h], r9
    0x4C, 0x89, 0x44, 0x24, 0x18,                   // mov [rsp+18h], r8
    0x55,                                           // push rbp
    0x53,                                           // push rbx
    0x56,                                           // push rsi
    0x57,                                           // push rdi
    0x41, 0x54                                      // push r12
};

static const uint8_t PROLOGUE_CHudManagerDraw[16] = {
    0x40, 0x53,                                     // push rbx
    0x57,                                           // push rdi
    0x41, 0x54,                                     // push r12
    0x41, 0x55,                                     // push r13
    0x41, 0x56,                                     // push r14
    0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00        // sub rsp, 80h
};

static const uint8_t PROLOGUE_PauseMenuDraw[17] = {
    0x40, 0x55,                                     // push rbp
    0x56,                                           // push rsi
    0x41, 0x56,                                     // push r14
    0x48, 0x8D, 0x6C, 0x24, 0xE0,                   // lea rbp, [rsp-20h]
    0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00        // sub rsp, 120h
};

static const uint8_t PROLOGUE_PauseSubMenuDraw[18] = {
    0x40, 0x56,                                     // push rsi
    0x41, 0x54,                                     // push r12
    0x48, 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00,       // sub rsp, 0F8h
    0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00        // mov rax, [rip + __security_cookie] (relocated in trampoline)
};

static const uint8_t PROLOGUE_DebriefingDraw[16] = {
    0x40, 0x53,                                     // rex push rbx
    0x48, 0x83, 0xEC, 0x40,                         // sub rsp, 40h
    0x80, 0xB9, 0xB5, 0xD2, 0x00, 0x00, 0x00,       // cmp byte ptr [rcx+0D2B5h], 0
    0x48, 0x8B, 0xD9                                // mov rbx, rcx
};

static const uint8_t PROLOGUE_HudDrawTextLeft[21] = {
    0x40, 0x55,                                     // push rbp
    0x56,                                           // push rsi
    0x41, 0x54,                                     // push r12
    0x41, 0x56,                                     // push r14
    0x41, 0x57,                                     // push r15
    0x48, 0x8D, 0x6C, 0x24, 0xF1,                   // lea rbp, [rsp-0Fh]
    0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00        // sub rsp, 0F0h
};

static const uint8_t PROLOGUE_HudDrawTextRight[21] = {
    0x40, 0x55,                                     // push rbp
    0x53,                                           // push rbx
    0x56,                                           // push rsi
    0x57,                                           // push rdi
    0x41, 0x56,                                     // push r14
    0x41, 0x57,                                     // push r15
    0x48, 0x8D, 0x6C, 0x24, 0xF9,                   // lea rbp, [rsp-07h]
    0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00        // sub rsp, 0D8h
};

static const uint8_t PROLOGUE_HudStatusAlerts[16] = {
    0x48, 0x8B, 0xC4,                               // mov rax, rsp
    0x53,                                           // push rbx
    0x57,                                           // push rdi
    0x48, 0x83, 0xEC, 0x78,                         // sub rsp, 78h
    0x48, 0x89, 0x70, 0x10,                         // mov [rax+10h], rsi
    0x4C, 0x8B, 0xDA                                // mov r11, rdx
};

static const uint8_t PROLOGUE_HudBottomMessage[17] = {
    0x4C, 0x89, 0x44, 0x24, 0x18,                   // mov [rsp+18h], r8
    0x53,                                           // push rbx
    0x48, 0x83, 0xEC, 0x40,                         // sub rsp, 40h
    0x80, 0xB9, 0x75, 0x01, 0x00, 0x00, 0x00        // cmp byte ptr [rcx+175h], 0
};

static const uint8_t PROLOGUE_HudCountdown[16] = {
    0x4C, 0x8B, 0xDC,                               // mov r11, rsp
    0x55,                                           // push rbp
    0x56,                                           // push rsi
    0x49, 0x8D, 0x6B, 0xA1,                         // lea rbp, [r11-5Fh]
    0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00        // sub rsp, 0A8h
};

static const uint8_t PROLOGUE_HudDrawTextFloat[19] = {
    0x40, 0x55,                                     // push rbp
    0x53,                                           // push rbx
    0x56,                                           // push rsi
    0x57,                                           // push rdi
    0x41, 0x56,                                     // push r14
    0x48, 0x8D, 0x6C, 0x24, 0xE0,                   // lea rbp, [rsp-20h]
    0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00        // sub rsp, 120h
};


using FnCSwgInstanceInit           = void*     (__fastcall*)(
    void* this_ptr,
    void* a2,
    uint8_t* swg_data,
    void* renderer,
    void* a5,
    void* a6,
    void* a7
);
using FnCSwgInstanceUpdate         = void      (__fastcall*)(void* this_ptr);
using FnCSwadMenuDraw              = void*     (__fastcall*)(void* this_ptr, void* a2);
using FnGameFontInitFallback       = bool      (__fastcall*)(uint8_t* font_obj, uint8_t** data_ptr_in_out);

static FnCSwgInstanceInit           g_OrigCSwgInstanceInit           = nullptr;
static FnCSwgInstanceUpdate         g_OrigCSwgInstanceUpdate         = nullptr;
static FnCSwadMenuDraw              g_OrigCSwadMenuDraw              = nullptr;
static FnGameFontInitFallback       g_OrigGameFontInitFallback       = nullptr;

static fs::path                     g_TexturesDir;


// ----------------------------------------------------------------------------
// Optional Cross-DLL Bridge to acz_hdtex.dll (Graceful Fallback when Standalone)
// ----------------------------------------------------------------------------
using FnAczHdTexGetTim2AspectMask            = uint8_t (__fastcall*)(const uint8_t* tim2_base, uint32_t tim2_size);
using FnAczHdTexResolveGimDescriptorTier     = int     (__fastcall*)(uint8_t* gim_desc, const char* caller_tag);
using FnAczHdTexGetActiveTextureAspectTier   = int     (__fastcall*)();

static FnAczHdTexGetTim2AspectMask          g_BridgeGetTim2AspectMask        = nullptr;
static FnAczHdTexResolveGimDescriptorTier   g_BridgeResolveGimAspectTier     = nullptr;
static FnAczHdTexGetActiveTextureAspectTier g_BridgeGetActiveTexAspectTier   = nullptr;

static void ResolveHdTexBridgeIfNeeded() {
    if (g_BridgeGetTim2AspectMask != nullptr && g_BridgeResolveGimAspectTier != nullptr) {
        return;
    }
    HMODULE hMod = GetModuleHandleA("acz_hdtex.dll");
    if (!hMod) hMod = GetModuleHandleA("acz_hdtex.asi");
    if (!hMod) return;

    if (!g_BridgeGetTim2AspectMask) {
        g_BridgeGetTim2AspectMask = reinterpret_cast<FnAczHdTexGetTim2AspectMask>(
            GetProcAddress(hMod, "AczHdTex_GetTim2AspectMask")
        );
    }
    if (!g_BridgeResolveGimAspectTier) {
        g_BridgeResolveGimAspectTier = reinterpret_cast<FnAczHdTexResolveGimDescriptorTier>(
            GetProcAddress(hMod, "AczHdTex_ResolveGimDescriptorAspectTier")
        );
    }
    if (!g_BridgeGetActiveTexAspectTier) {
        g_BridgeGetActiveTexAspectTier = reinterpret_cast<FnAczHdTexGetActiveTextureAspectTier>(
            GetProcAddress(hMod, "AczHdTex_GetActiveTextureAspectTier")
        );
    }
}

static uint8_t QueryHdTexTim2AspectMask(const uint8_t* tim2_base, uint32_t tim2_size) {
    ResolveHdTexBridgeIfNeeded();
    if (g_BridgeGetTim2AspectMask != nullptr) {
        return g_BridgeGetTim2AspectMask(tim2_base, tim2_size);
    }
    return 0u; // Standalone fallback: no HD _16x9/_21x9 texture variants loaded -> revert 512x448 bg quads to 4:3
}

static int QueryHdTexGimAspectTier(uint8_t* gim_desc, const char* caller_tag) {
    ResolveHdTexBridgeIfNeeded();
    if (g_BridgeResolveGimAspectTier != nullptr) {
        return g_BridgeResolveGimAspectTier(gim_desc, caller_tag);
    }
    return 0; // Standalone fallback: 4:3 native GIM texture
}

static int GetGameAspectOption() {
    return (g_ScreenAspect >= 0 && g_ScreenAspect <= 2) ? g_ScreenAspect : 1;
}

static bool IsGameWidescreenEnabled() {
    return GetGameAspectOption() != 0;
}

static int GetActiveTextureAspectTier() {
    ResolveHdTexBridgeIfNeeded();
    if (g_BridgeGetActiveTexAspectTier != nullptr) {
        return g_BridgeGetActiveTexAspectTier();
    }
    if (!g_EnableMenuOverrides || g_MenuAspectMode == 2) return 0;
    if (g_MenuAspectMode == 3) return 2;
    if (g_MenuAspectMode == 1) return 1;
    return GetGameAspectOption();
}

// Standalone Fallback for select.msa 21:9 Menu Strings (when acz_hdtex.dll is not installed)
static void PatchSelectMsaPoolInMemoryFallback(uint8_t* start_ptr) {
    if (!start_ptr) return;
    __try {
        if (start_ptr[0] == 'G' && start_ptr[1] == 'I' && start_ptr[2] == 'M' && start_ptr[3] == 0) {
            return;
        }
        uint32_t num_strs   = *reinterpret_cast<const uint32_t*>(start_ptr + 0x00);
        uint16_t num_glyphs = *reinterpret_cast<const uint16_t*>(start_ptr + 0x04);
        uint16_t glyph_dim  = *reinterpret_cast<const uint16_t*>(start_ptr + 0x08);

        if (num_strs == 140u && num_glyphs == 48u && glyph_dim == 32u) {
            uint32_t rdi_off  = 16u * (48u + 1u);
            uint32_t r10_off  = ((rdi_off + 2u * 140u) & ~3u) + 4u;
            uint32_t idx_base = r10_off + 4u * 140u;
            int32_t  soff22   = *reinterpret_cast<const int32_t*>(start_ptr + r10_off + 4u * 22u);
            if (soff22 == 158) {
                *reinterpret_cast<uint16_t*>(start_ptr + rdi_off + 2u * 22u) = 4u;
                uint16_t* gidx = reinterpret_cast<uint16_t*>(start_ptr + idx_base + 2u * soff22);
                gidx[0] = 10u; gidx[1] = 9u; gidx[2] = 18u; gidx[3] = 17u;
            }
        } else if (num_strs == 123u && num_glyphs == 38u && glyph_dim == 16u) {
            uint32_t rdi_off     = 16u * (38u + 1u);
            uint32_t r10_off     = ((rdi_off + 2u * 123u) & ~3u) + 4u;
            uint32_t idx_base    = r10_off + 4u * 123u;
            uint16_t last_len    = *reinterpret_cast<const uint16_t*>(start_ptr + rdi_off + 2u * 122u);
            int32_t  last_off    = *reinterpret_cast<const int32_t*>(start_ptr + r10_off + 4u * 122u);
            uint32_t end_idx_off = r10_off + 2u * (static_cast<uint32_t>(last_off) + 2u * 123u + static_cast<uint32_t>(last_len));
            uint32_t pal_off     = (end_idx_off & ~15u) + 16u;
            uint32_t bmp_off     = pal_off + 0x40u;

            uint16_t* gm36_cc = reinterpret_cast<uint16_t*>(start_ptr + 16u + 36u * 16u + 8u);
            if (*gm36_cc == 90u) {
                *gm36_cc = 50u;
                std::memcpy(start_ptr + bmp_off + 36u * 128u, start_ptr + bmp_off + 5u * 128u, 40u);
            }
            int32_t soff31 = *reinterpret_cast<const int32_t*>(start_ptr + r10_off + 4u * 31u);
            if (soff31 == 266) {
                *reinterpret_cast<uint16_t*>(start_ptr + rdi_off + 2u * 31u) = 5u;
                uint16_t* gidx = reinterpret_cast<uint16_t*>(start_ptr + idx_base + 2u * soff31);
                gidx[0] = 0u; gidx[1] = 36u; gidx[2] = 4u; gidx[3] = 10u; gidx[4] = 9u;
            }
            int32_t soff50 = *reinterpret_cast<const int32_t*>(start_ptr + r10_off + 4u * 50u);
            if (soff50 == 442) {
                *reinterpret_cast<uint16_t*>(start_ptr + rdi_off + 2u * 50u) = 5u;
                uint16_t* gidx = reinterpret_cast<uint16_t*>(start_ptr + idx_base + 2u * soff50);
                gidx[0] = 11u; gidx[1] = 36u; gidx[2] = 4u; gidx[3] = 10u; gidx[4] = 9u;
            }
        } else if (num_strs == 874u && num_glyphs == 92u && glyph_dim == 32u) {
            uint32_t rdi_off  = 16u * (92u + 1u);
            uint32_t r10_off  = ((rdi_off + 2u * 874u) & ~3u) + 4u;
            uint32_t idx_base = r10_off + 4u * 874u;
            int32_t  soff830  = *reinterpret_cast<const int32_t*>(start_ptr + r10_off + 4u * 830u);
            if (soff830 == 78482) {
                static const uint16_t kDesc21x9[41] = {
                    34, 69, 83, 84, 0, 83, 69, 84, 84, 73, 78, 71, 0, 70, 79, 82, 0,
                    85, 76, 84, 82, 65, 87, 73, 68, 69, 0, 18, 17, 26, 25, 0,
                    77, 79, 78, 73, 84, 79, 82, 83, 14
                };
                *reinterpret_cast<uint16_t*>(start_ptr + rdi_off + 2u * 830u) = 41u;
                std::memcpy(start_ptr + idx_base + 2u * soff830, kDesc21x9, sizeof(kDesc21x9));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool __fastcall Hook_FallbackGameFontInit(uint8_t* font_obj, uint8_t** data_ptr_in_out) {
    if (data_ptr_in_out != nullptr && *data_ptr_in_out != nullptr) {
        PatchSelectMsaPoolInMemoryFallback(*data_ptr_in_out);
    }
    return g_OrigGameFontInitFallback(font_obj, data_ptr_in_out);
}

// ----------------------------------------------------------------------------
// Hooks 10..12: Widescreen 16:9 Menu Override Engine
//   - Hook 10: CSwgInstance::Init (+0x1F7BE0 in v1.0.2.1 / +0x1F8260 in v2.0.0.1)
//   - Hook 11: CSwgRenderer::UploadSwadTextures (+0x1ECBE0 in v1.0.2.1 / +0x1ED260 in v2.0.0.1)
//   - Hook 12: CSwgInstance::Draw (+0x1F8310 in v1.0.2.1 / +0x1F8990 in v2.0.0.1)
// Replaces menu SWAD (.swa) / SWG (chunk00_layout.swg) layouts from
// mods\textures\menus\<menu_name>\ while routing embedded TIM2 textures
// through the standard FNV-1a HD texture replacement pipeline and dynamically
// toggling the D3D12 16:9 full-screen scissor and layout aspect state in real time.
// ----------------------------------------------------------------------------
struct SwgWordDiff {
    uint32_t offset;
    uint32_t val_4_3;
    uint32_t val_16_9;
    uint32_t val_21_9;
};

static std::mutex                                                 g_MenuOverrideMutex;
static std::unordered_map<std::string, std::vector<SwgWordDiff>>  g_SwgAspectDiffs;
static std::unordered_map<std::string, bool>                      g_HasWidescreenMenuOverride;
static std::atomic<int>                                           g_WidescreenScissorState{-1};
static std::atomic<bool>                                          g_CurrentMenuHasWsOverride{true};

// Returns 0 = 4:3 (disabled), 1 = 16:9 Widescreen, 2 = 21:9 Ultrawide
static int GetTargetMenuAspectTier() {
    if (!g_EnableMenuOverrides || g_MenuAspectMode == 2) {
        return 0;
    }
    // Rule 3: Completely disable widescreen menu hook when the game is in 4:3 mode
    if (!IsGameWidescreenEnabled()) {
        return 0;
    }
    if (g_MenuAspectMode == 3) {
        return 2;
    }
    if (g_MenuAspectMode == 1) {
        return 1;
    }
    int tex_tier = GetActiveTextureAspectTier();
    return (tex_tier >= 2) ? 2 : 1;
}

// If the user deletes a 512x448 _16x9.png or _21x9.png texture variant from disk,
// automatically reverts that Type 0 Shape quad in swg_16 / swg_21 back to 4:3 (width=512.0f, midx=0)
// so the 4:3 texture is displayed centered without horizontal stretching.
static void RevertMissingWidescreenBgShapes(
    const std::vector<uint8_t>& swad_archive,
    const uint8_t* swg_4x3,
    uint8_t* swg_16x9,
    uint8_t* swg_21x9,
    size_t swg_size
) {
    if (swad_archive.size() < 0xE0 || std::memcmp(swad_archive.data(), "SWAD", 4) != 0) return;
    if (!swg_4x3 || !swg_16x9 || !swg_21x9 || swg_size < 0xE0) return;

    uint32_t chunk_cnt = *reinterpret_cast<const uint32_t*>(swad_archive.data() + 4);
    uint32_t t5_off    = *reinterpret_cast<const uint32_t*>(swg_4x3 + 0x7C);
    uint32_t t5_cnt    = *reinterpret_cast<const uint32_t*>(swg_4x3 + 0x80);
    if (t5_off + t5_cnt * 8u > swg_size) return;

    for (uint32_t di = 0; di < t5_cnt; ++di) {
        int32_t dtype = *reinterpret_cast<const int32_t*>(swg_4x3 + t5_off + di * 8);
        int32_t doff  = *reinterpret_cast<const int32_t*>(swg_4x3 + t5_off + di * 8 + 4);
        if (dtype != 0 || doff < 0 || static_cast<size_t>(doff + 4) > swg_size) continue;

        int32_t cnt  = *reinterpret_cast<const int32_t*>(swg_4x3 + doff);
        int32_t roff = doff + 4;
        for (int32_t r = 0; r < cnt; ++r) {
            if (roff < 0 || static_cast<size_t>(roff + 36) > swg_size) break;
            int32_t noff    = *reinterpret_cast<const int32_t*>(swg_4x3 + roff + 4);
            int32_t midx_43 = *reinterpret_cast<const int32_t*>(swg_4x3 + roff + 8);
            int32_t tex_idx = *reinterpret_cast<const int32_t*>(swg_4x3 + roff + 12);
            float   w_43    = *reinterpret_cast<const float*>(swg_4x3 + roff + 28);
            float   h_43    = *reinterpret_cast<const float*>(swg_4x3 + roff + 32);

            if (std::fabs(w_43 - 16.0f) < 1e-2f && std::fabs(h_43 - 128.0f) < 1e-2f && midx_43 == 0 && tex_idx > 5) {
                *reinterpret_cast<float*>(swg_16x9 + roff + 28) = 16.0f;
                *reinterpret_cast<float*>(swg_21x9 + roff + 28) = 16.0f;
            }

            uint32_t chunk_idx = static_cast<uint32_t>(tex_idx) + 1u;
            if (std::fabs(w_43 - 512.0f) < 1e-2f && std::fabs(h_43 - 448.0f) < 1e-2f &&
                tex_idx >= 0 && chunk_idx < chunk_cnt &&
                8u + (chunk_idx + 1u) * 8u <= swad_archive.size()) {
                uint32_t c_off = *reinterpret_cast<const uint32_t*>(swad_archive.data() + 8 + chunk_idx * 8);
                uint32_t c_sz  = *reinterpret_cast<const uint32_t*>(swad_archive.data() + 12 + chunk_idx * 8);
                if (c_sz >= 0x40 && static_cast<uint64_t>(c_off) + c_sz <= swad_archive.size()) {
                    const uint8_t* tim2_base = swad_archive.data() + c_off;
                    uint8_t avail_mask = QueryHdTexTim2AspectMask(tim2_base, c_sz);
                    bool has_16 = (avail_mask & (1u << 1)) != 0;
                    bool has_21 = (avail_mask & (1u << 2)) != 0;
                    if (!has_16) {
                        *reinterpret_cast<int32_t*>(swg_16x9 + roff + 8) = (tex_idx == 6 && midx_43 == 2) ? 1 : midx_43;
                        *reinterpret_cast<float*>(swg_16x9 + roff + 28)  = (tex_idx == 6 && midx_43 == 2) ? 592.0f : w_43;
                    }
                    if (!has_21 && !has_16) {
                        *reinterpret_cast<int32_t*>(swg_21x9 + roff + 8) = (tex_idx == 6 && midx_43 == 2) ? 1 : midx_43;
                        *reinterpret_cast<float*>(swg_21x9 + roff + 28)  = (tex_idx == 6 && midx_43 == 2) ? 592.0f : w_43;
                    }
                }
            }
            roff = noff;
        }
    }
}

static void BuildSwgAspectDiffTable(
    const std::string& stem,
    const uint8_t* swg_4x3,
    const uint8_t* swg_16x9,
    const uint8_t* swg_21x9,
    size_t swg_size
) {
    if (!swg_4x3 || !swg_16x9 || !swg_21x9 || swg_size < 0xE0) return;
    if (std::memcmp(swg_4x3, "SWG\0", 4) != 0 ||
        std::memcmp(swg_16x9, "SWG\0", 4) != 0 ||
        std::memcmp(swg_21x9, "SWG\0", 4) != 0) {
        return;
    }

    std::vector<SwgWordDiff> diffs;
    diffs.reserve(2048);
    for (size_t off = 0; off + 4 <= swg_size; off += 4) {
        uint32_t v_4_3  = *reinterpret_cast<const uint32_t*>(swg_4x3 + off);
        uint32_t v_16_9 = *reinterpret_cast<const uint32_t*>(swg_16x9 + off);
        uint32_t v_21_9 = *reinterpret_cast<const uint32_t*>(swg_21x9 + off);
        if (v_4_3 != v_16_9 || v_4_3 != v_21_9) {
            diffs.push_back({static_cast<uint32_t>(off), v_4_3, v_16_9, v_21_9});
        }
    }
    g_SwgAspectDiffs[stem] = std::move(diffs);
}

// ----------------------------------------------------------------------------
// Dynamic 4:3 Pillarbox & GS Virtual Width Manager (for 16:9 and 21:9)
// ----------------------------------------------------------------------------
// Note on D3D12 GS Offscreen Architecture:
//   - The offscreen D3D12 GS RenderTargets (RT #0 / RT #1) are allocated at startup
//     with dimensions [RVA_ViewportWidth x RVA_ViewportHeight] = (height * 16 / 9) x height
//     (e.g. 1920x1080) and must NEVER have RVA_ViewportWidth overwritten.
//   - In 21:9 mode, ResizeSwapChain sets the final presentation SwapChain viewport to 21:9
//     (e.g. 2520x1080), stretching the 1920x1080 offscreen GS RenderTarget across 21:9.
//   - Inside the GS emulator, [RVA_GsVirtualWidth] (+0x3566C0, default 682) controls
//     3D/2D vertex scaling (0x140193F07), sub-screen GS_SCISSOR scaling (0x14019461E),
//     offscreen framebuffer UVs (0x140192A3F), and 4:3 FMV scaling (0x140193F68) whenever
//     CSysData::isWidescreen ([rax+0x28]) == 0 (all menus, hangar, briefing).
//   - Setting [RVA_GsVirtualWidth] = 896 (512 * 21 / 12) and scissor fractions to
//     [3/14 .. 11/14] (0.2142857f .. 0.7857143f of 1920 = 411..1508) when a 4:3 menu is
//     active in 21:9 mode scales both 3D Hangar and 2D UI to exact 4:3 centered on 21:9!
//   - When a custom widescreen menu (_16x9.swa / _21x9.swa, authored on a 682-wide canvas)
//     is active (ws_menu_active == true), [RVA_GsVirtualWidth] is kept at 682 so its
//     -85..597 canvas fills 0..1920 (100% of the display).
static void UpdateDynamicPillarboxAndAspect(bool ws_menu_active) {
    if (!g_ExeBase) return;
    __try {
        float*    pLeft               = reinterpret_cast<float*>(g_ExeBase + AczRva(RVA_PillarboxDataCave + 0x00));
        float*    pRight              = reinterpret_cast<float*>(g_ExeBase + AczRva(RVA_PillarboxDataCave + 0x04));
        float*    pSpiderCenter       = reinterpret_cast<float*>(g_ExeBase + AczRva(RVA_PillarboxDataCave + 0x08));
        float*    pAssaultModelCenter = reinterpret_cast<float*>(g_ExeBase + AczRva(RVA_PillarboxDataCave + 0x0C));
        uint32_t* pGsVirtualW         = reinterpret_cast<uint32_t*>(g_ExeBase + AczRva(RVA_GsVirtualWidth));

        int aspect_opt = GetGameAspectOption();
        if (aspect_opt == 2 && !ws_menu_active) {
            // 21:9 Ultrawide active on a non-overridden 4:3 menu (Briefing, etc.):
            uint32_t render_w = *reinterpret_cast<const uint32_t*>(g_ExeBase + AczRva(RVA_RenderWidth));
            uint32_t render_h = *reinterpret_cast<const uint32_t*>(g_ExeBase + AczRva(RVA_RenderHeight));
            float aspect = (render_w > 0 && render_h > 0)
                ? (static_cast<float>(render_w) / static_cast<float>(render_h))
                : (21.0f / 9.0f);
            if (aspect < 1.8f) {
                aspect = 21.0f / 9.0f;
            }

            // Fraction of the ultrawide display occupied by a 4:3 pillarbox (4/7 = 0.57142857f for 21:9)
            float scale = (4.0f / 3.0f) / aspect;
            uint32_t gs_virtual_w = static_cast<uint32_t>((512.0f / scale) + 0.5f); // 896 for 21:9
            float left_frac  = (1.0f - scale) * 0.5f; // 0.21428572f (411 / 1920)
            float right_frac = 1.0f - left_frac;      // 0.78571427f (1508 / 1920)

            *pGsVirtualW         = gs_virtual_w;
            *pLeft               = left_frac;
            *pRight              = right_frac;
            *pSpiderCenter       = 28672.0f; // Stock 4:3 spider chart X center (1792.0 * 16.0)
            *pAssaultModelCenter = 1900.0f;  // Stock 4:3 AssaultRecord 3D miniature aircraft X center
        } else if (ws_menu_active) {
            int target_tier = GetTargetMenuAspectTier();
            if (target_tier == 2) {
                // 21:9 Ultrawide custom menu override (896-wide canvas, dx_left = -184.0 SWG px):
                *pGsVirtualW         = 896u;
                *pLeft               = 0.0f;
                *pRight              = 1.0f;
                *pSpiderCenter       = 28672.0f + (-184.0f * 16.0f); // 25728.0f (aligns with shifted [STATISTICS] hexagon)
                *pAssaultModelCenter = 1900.0f - 184.0f;             // 1716.0f  (aligns 3D miniature aircraft with [PILOT DATA] pane)
            } else {
                // 16:9 Widescreen custom menu override (682-wide canvas, dx_left = -77.0 SWG px):
                *pGsVirtualW         = 682u;
                *pLeft               = 0.0f;
                *pRight              = 1.0f;
                *pSpiderCenter       = 28672.0f + (-77.0f * 16.0f);  // 27440.0f (aligns with shifted [STATISTICS] hexagon)
                *pAssaultModelCenter = 1900.0f - 77.0f;              // 1823.0f  (aligns 3D miniature aircraft with [PILOT DATA] pane)
            }
        } else {
            // Stock 16:9 / 4:3 mode (pillarbox):
            *pGsVirtualW         = 682u;
            *pLeft               = 0.125f;
            *pRight              = 0.875f;
            *pSpiderCenter       = 28672.0f;
            *pAssaultModelCenter = 1900.0f;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void SetWidescreenMenuScissorOverride(bool enable) {
    UpdateDynamicPillarboxAndAspect(enable);

    int target_state = enable ? 1 : 0;
    if (g_WidescreenScissorState.load(std::memory_order_relaxed) == target_state) {
        return;
    }

    uint8_t* branch1 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(RVA_ScissorBranch1));
    uint8_t* branch2 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(RVA_ScissorBranch2));
    DWORD old1 = 0, old2 = 0;

    if (VirtualProtect(branch1, 2, PAGE_EXECUTE_READWRITE, &old1)) {
        if (enable) {
            branch1[0] = 0x90; branch1[1] = 0x90; // NOP out je +0x17 -> full widescreen scissor (0..1920)
        } else {
            branch1[0] = 0x74; branch1[1] = 0x17; // Restore je +0x17 -> 4:3 pillarbox scissor
        }
        VirtualProtect(branch1, 2, old1, &old1);
        FlushInstructionCache(GetCurrentProcess(), branch1, 2);
    }

    if (VirtualProtect(branch2, 2, PAGE_EXECUTE_READWRITE, &old2)) {
        if (enable) {
            branch2[0] = 0xEB; branch2[1] = 0x35; // jmp +0x35 -> skip 4:3 pillarbox shrink
        } else {
            branch2[0] = 0x75; branch2[1] = 0x35; // Restore jne +0x35
        }
        VirtualProtect(branch2, 2, old2, &old2);
        FlushInstructionCache(GetCurrentProcess(), branch2, 2);
    }

    g_WidescreenScissorState.store(target_state, std::memory_order_relaxed);
    LogMsg("[Enhancer] [MenuOverride] D3D12 widescreen menu scissor override: %s",
           enable ? "ENABLED (full viewport)" : "DISABLED (4:3 pillarbox)");
}

// Recomputes cached 2D world transform matrices (mc + 0x140..0x17F) across the active
// CMovieClipInstance hierarchy so live aspect ratio changes in Options take effect immediately.
static void RefreshMovieClipWorldMatrices(uint8_t* mc, const float* parent_world, int depth = 0) {
    if (!mc || depth > 32) return;

    const float* local_mat = *reinterpret_cast<const float* const*>(mc + 0x138);
    float* world = reinterpret_cast<float*>(mc + 0x140); // [0]=m00, [1]=m01, [4]=m10, [5]=m11, [12]=tx, [13]=ty

    if (local_mat != nullptr) {
        const float l_m00 = local_mat[0];
        const float l_m01 = local_mat[1];
        const float l_m10 = local_mat[4];
        const float l_m11 = local_mat[5];
        const float l_tx  = local_mat[12];
        const float l_ty  = local_mat[13];

        if (parent_world != nullptr) {
            const float p_m00 = parent_world[0];
            const float p_m01 = parent_world[1];
            const float p_m10 = parent_world[4];
            const float p_m11 = parent_world[5];
            const float p_tx  = parent_world[12];
            const float p_ty  = parent_world[13];

            world[0]  = p_m00 * l_m00 + p_m10 * l_m01;
            world[1]  = p_m01 * l_m00 + p_m11 * l_m01;
            world[4]  = p_m00 * l_m10 + p_m10 * l_m11;
            world[5]  = p_m01 * l_m10 + p_m11 * l_m11;
            world[12] = p_m00 * l_tx  + p_m10 * l_ty + p_tx;
            world[13] = p_m01 * l_tx  + p_m11 * l_ty + p_ty;
        } else {
            world[0]  = l_m00;
            world[1]  = l_m01;
            world[4]  = l_m10;
            world[5]  = l_m11;
            world[12] = l_tx;
            world[13] = l_ty;
        }
    } else if (parent_world != nullptr) {
        std::memcpy(world, parent_world, 64);
    }

    // Apply ActionScript local matrix override (mc + 0x80 flag, matrix at +0x84..+0xB8) if active
    if (*reinterpret_cast<const uint8_t*>(mc + 0x80) != 0) {
        const float m00 = world[0];
        const float m01 = world[1];
        const float m10 = world[4];
        const float m11 = world[5];
        const float tx  = world[12];
        const float ty  = world[13];

        const float a00 = *reinterpret_cast<const float*>(mc + 0x84);
        const float a01 = *reinterpret_cast<const float*>(mc + 0x88);
        const float a10 = *reinterpret_cast<const float*>(mc + 0x94);
        const float a11 = *reinterpret_cast<const float*>(mc + 0x98);
        const float atx = *reinterpret_cast<const float*>(mc + 0xB4);
        const float aty = *reinterpret_cast<const float*>(mc + 0xB8);

        world[0]  = m00 * a00 + m10 * a01;
        world[1]  = m01 * a00 + m11 * a01;
        world[4]  = m00 * a10 + m10 * a11;
        world[5]  = m01 * a10 + m11 * a11;
        world[12] = m00 * atx + m10 * aty + tx;
        world[13] = m01 * atx + m11 * aty + ty;
    }

    // Traverse active child MovieClip linked list (head at mc + 0x108, next at child + 0x30)
    for (uint8_t* child = *reinterpret_cast<uint8_t* const*>(mc + 0x108);
         child != nullptr;
         child = *reinterpret_cast<uint8_t* const*>(child + 0x30)) {
        RefreshMovieClipWorldMatrices(child, world, depth + 1);
    }
}

// Dynamically switches an active SWG buffer in RAM between 4:3 (512x448), 16:9 (682x448),
// and 21:9 (896x448) and refreshes active MovieClip world transforms.
static std::string ResolveSwgMenuStem(const uint8_t* swg_buf) {
    if (!swg_buf || std::memcmp(swg_buf, "SWG\0", 4) != 0) return {};
    char name_buf[65] = {};
    std::memcpy(name_buf, swg_buf + 8, 64);
    std::string stem(name_buf);
    if (stem == "AircraftSelect_") {
        stem = "AircraftSelect_Q";
    } else if (stem == "ControlSet") {
        uint32_t t0_cnt = *reinterpret_cast<const uint32_t*>(swg_buf + 0x58);
        if (t0_cnt == 541u) {
            stem = "LanguageSet";
        }
    }
    return stem;
}

#if defined(_MSC_VER)
__declspec(noinline) static void CallRefreshMovieClipWorldMatricesSEH(uint8_t* root_mc) {
    __try {
        RefreshMovieClipWorldMatrices(root_mc, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
#endif

static void SafeRefreshSwgInstanceMatrices(void* swg_instance) {
    if (swg_instance == nullptr) return;
    uint8_t* root_mc = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(swg_instance) + 0x1E0);
    if (root_mc != nullptr) {
#if defined(_MSC_VER)
        CallRefreshMovieClipWorldMatricesSEH(root_mc);
#else
        RefreshMovieClipWorldMatrices(root_mc, nullptr, 0);
#endif
    }
}

static void ApplySwgDynamicAspectState(void* swg_instance, uint8_t* swg_buf, int target_tier) {
    if (!swg_buf || std::memcmp(swg_buf, "SWG\0", 4) != 0) {
        return;
    }

    uint16_t cur_width = *reinterpret_cast<const uint16_t*>(swg_buf + 0x50);
    uint16_t target_width = (target_tier == 2) ? 896u : ((target_tier == 1) ? 682u : 512u);

    if (cur_width != target_width) {
        std::string stem = ResolveSwgMenuStem(swg_buf);

        auto it = g_SwgAspectDiffs.find(stem);
        if (it != g_SwgAspectDiffs.end() && !it->second.empty()) {
            for (const SwgWordDiff& d : it->second) {
                uint32_t val = (target_tier == 2) ? d.val_21_9 : ((target_tier == 1) ? d.val_16_9 : d.val_4_3);
                *reinterpret_cast<uint32_t*>(swg_buf + d.offset) = val;
            }
            *reinterpret_cast<uint16_t*>(swg_buf + 0x50) = target_width;

            SafeRefreshSwgInstanceMatrices(swg_instance);

            LogMsg("[Enhancer] [MenuOverride] Live-switched '%s' layout aspect to %s (canvas_width=%u, %zu word diffs applied)",
                   stem.c_str(),
                   (target_tier == 2) ? "21:9 Ultrawide" : ((target_tier == 1) ? "16:9 Widescreen" : "4:3 Standard"),
                   static_cast<unsigned>(target_width), it->second.size());
        }
    }
}

static bool ReadFileBytes(const fs::path& p, std::vector<uint8_t>& out_buf) {
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) return false;
    FILE* f = nullptr;
    fopen_s(&f, p.string().c_str(), "rb");
    if (!f) return false;
    _fseeki64(f, 0, SEEK_END);
    int64_t fsize = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (fsize < 0xE0) {
        fclose(f);
        return false;
    }
    out_buf.resize(static_cast<size_t>(fsize));
    size_t nread = fread(out_buf.data(), 1, out_buf.size(), f);
    fclose(f);
    return (nread == out_buf.size());
}

static const uint8_t* ExtractSwgFromFileBuffer(const std::vector<uint8_t>& buf, size_t* out_swg_size, bool* out_is_swad) {
    if (buf.size() >= 0xE0 && std::memcmp(buf.data(), "SWAD", 4) == 0) {
        uint32_t chunk_cnt = *reinterpret_cast<const uint32_t*>(buf.data() + 4);
        uint32_t off0      = *reinterpret_cast<const uint32_t*>(buf.data() + 8);
        uint32_t sz0       = *reinterpret_cast<const uint32_t*>(buf.data() + 12);
        if (chunk_cnt >= 1 && static_cast<uint64_t>(off0) + sz0 <= buf.size() &&
            sz0 >= 0xE0 && std::memcmp(buf.data() + off0, "SWG\0", 4) == 0) {
            if (out_swg_size) *out_swg_size = sz0;
            if (out_is_swad)  *out_is_swad  = true;
            return buf.data() + off0;
        }
    } else if (buf.size() >= 0xE0 && std::memcmp(buf.data(), "SWG\0", 4) == 0) {
        if (out_swg_size) *out_swg_size = buf.size();
        if (out_is_swad)  *out_is_swad  = false;
        return buf.data();
    }
    return nullptr;
}

static void ProcessSwgMenuOverride(const std::string& stem, uint8_t* swg_data) {
    std::lock_guard<std::mutex> lock(g_MenuOverrideMutex);
    bool has_ws_override = false;

    if (g_EnableMenuOverrides && g_MenuAspectMode != 2) {
        fs::path menu_dir = g_TexturesDir / "menus" / stem;
        std::error_code dir_ec;
        if (!fs::exists(menu_dir, dir_ec)) {
            fs::path alt_dir = g_EnhancerDir / "menus" / stem;
            if (fs::exists(alt_dir, dir_ec)) {
                menu_dir = alt_dir;
            }
        }
        std::vector<uint8_t> raw_4x3, raw_16x9, raw_21x9, raw_base;

        // Load 4:3 base (<stem>.swa), 16:9 (<stem>_16x9.swa), and 21:9 (<stem>_21x9.swa)
        ReadFileBytes(menu_dir / (stem + ".swa"), raw_base);
        ReadFileBytes(menu_dir / (stem + "_16x9.swa"), raw_16x9);
        ReadFileBytes(menu_dir / (stem + "_21x9.swa"), raw_21x9);

        if (!raw_base.empty()) {
            size_t base_sz = 0;
            bool base_swad = false;
            const uint8_t* base_swg = ExtractSwgFromFileBuffer(raw_base, &base_sz, &base_swad);
            if (base_swg != nullptr) {
                uint16_t base_w = *reinterpret_cast<const uint16_t*>(base_swg + 0x50);
                if (base_w <= 512u) {
                    raw_4x3 = std::move(raw_base);
                } else if (raw_16x9.empty() && raw_21x9.empty()) {
                    raw_16x9 = std::move(raw_base);
                }
            }
        }

        if (!raw_16x9.empty() && raw_21x9.empty()) raw_21x9 = raw_16x9;
        if (!raw_21x9.empty() && raw_16x9.empty()) raw_16x9 = raw_21x9;

        size_t sz_4 = 0, sz_16 = 0, sz_21 = 0;
        bool is_swad_4 = false, is_swad_16 = false, is_swad_21 = false;
        const uint8_t* swg_4  = ExtractSwgFromFileBuffer(raw_4x3,  &sz_4,  &is_swad_4);
        const uint8_t* swg_16 = ExtractSwgFromFileBuffer(raw_16x9, &sz_16, &is_swad_16);
        const uint8_t* swg_21 = ExtractSwgFromFileBuffer(raw_21x9, &sz_21, &is_swad_21);

        if (swg_16 != nullptr && swg_21 != nullptr && sz_16 == sz_21) {
            const uint8_t* base_4x3_ptr = (swg_4 != nullptr && sz_4 == sz_16) ? swg_4 : swg_data;
            has_ws_override = true;

            // If user deleted any _16x9.png / _21x9.png 512x448 texture variants, revert their quads to 4:3
            std::vector<uint8_t> raw_swad_archive;
            if (is_swad_4) {
                RevertMissingWidescreenBgShapes(raw_4x3, base_4x3_ptr, const_cast<uint8_t*>(swg_16), const_cast<uint8_t*>(swg_21), sz_16);
            } else if (is_swad_16) {
                RevertMissingWidescreenBgShapes(raw_16x9, base_4x3_ptr, const_cast<uint8_t*>(swg_16), const_cast<uint8_t*>(swg_21), sz_16);
            } else if (ReadFileBytes(menu_dir / (stem + ".swa"), raw_swad_archive) ||
                       ReadFileBytes(menu_dir / (stem + "_16x9.swa"), raw_swad_archive)) {
                RevertMissingWidescreenBgShapes(raw_swad_archive, base_4x3_ptr, const_cast<uint8_t*>(swg_16), const_cast<uint8_t*>(swg_21), sz_16);
            }

            // Runtime sanitization for scrollbar thumbs (16x128 shapes with tex_idx > 5) and DetailMap branch titles:
            {
                uint32_t t5_off = *reinterpret_cast<const uint32_t*>(base_4x3_ptr + 0x7C);
                uint32_t t5_cnt = *reinterpret_cast<const uint32_t*>(base_4x3_ptr + 0x80);
                if (t5_off + t5_cnt * 8u <= sz_16) {
                    for (uint32_t di = 0; di < t5_cnt; ++di) {
                        int32_t dtype = *reinterpret_cast<const int32_t*>(base_4x3_ptr + t5_off + di * 8);
                        int32_t doff  = *reinterpret_cast<const int32_t*>(base_4x3_ptr + t5_off + di * 8 + 4);
                        if (dtype != 0 || doff < 0 || static_cast<size_t>(doff + 4) > sz_16) continue;
                        int32_t cnt  = *reinterpret_cast<const int32_t*>(base_4x3_ptr + doff);
                        int32_t roff = doff + 4;
                        for (int32_t r = 0; r < cnt; ++r) {
                            if (roff < 0 || static_cast<size_t>(roff + 36) > sz_16) break;
                            int32_t noff    = *reinterpret_cast<const int32_t*>(base_4x3_ptr + roff + 4);
                            int32_t midx_43 = *reinterpret_cast<const int32_t*>(base_4x3_ptr + roff + 8);
                            int32_t tex_idx = *reinterpret_cast<const int32_t*>(base_4x3_ptr + roff + 12);
                            float   w_43    = *reinterpret_cast<const float*>(base_4x3_ptr + roff + 28);
                            float   h_43    = *reinterpret_cast<const float*>(base_4x3_ptr + roff + 32);
                            if (std::fabs(w_43 - 16.0f) < 1e-2f && std::fabs(h_43 - 128.0f) < 1e-2f && midx_43 == 0 && tex_idx > 5) {
                                *reinterpret_cast<float*>(const_cast<uint8_t*>(swg_16) + roff + 28) = 16.0f;
                                *reinterpret_cast<float*>(const_cast<uint8_t*>(swg_21) + roff + 28) = 16.0f;
                            }
                            roff = noff;
                        }
                    }
                }
                {
                    uint32_t t0_off = *reinterpret_cast<const uint32_t*>(base_4x3_ptr + 0x54);
                    uint32_t t0_cnt = *reinterpret_cast<const uint32_t*>(base_4x3_ptr + 0x58);
                    if (t0_off + t0_cnt * 64u <= sz_16) {
                        auto set_mat_tx = [&](uint32_t m, float tx_43, float tx_16, float tx_21) {
                            if (m < t0_cnt) {
                                uint32_t tx_off = t0_off + m * 64u + 48u;
                                *reinterpret_cast<float*>(const_cast<uint8_t*>(base_4x3_ptr) + tx_off) = tx_43;
                                *reinterpret_cast<float*>(swg_data + tx_off) = tx_43;
                                *reinterpret_cast<float*>(const_cast<uint8_t*>(swg_16) + tx_off) = tx_16;
                                *reinterpret_cast<float*>(const_cast<uint8_t*>(swg_21) + tx_off) = tx_21;
                            }
                        };

                        if (stem == "DetailMap") {
                            for (uint32_t m : { 156u, 164u, 165u, 166u }) {
                                if (m < t0_cnt) {
                                    float tx_43 = *reinterpret_cast<const float*>(base_4x3_ptr + t0_off + m * 64u + 48u);
                                    set_mat_tx(m, tx_43, tx_43, tx_43);
                                }
                            }
                        } else if (stem == "AircraftSelect") {
                            // Normal hangar mode scrollbar thumb (Mat[127], vanilla 4:3 tx = 483.50f)
                            set_mat_tx(127u, 485.00f, 561.833336f, 669.00f);
                            // DISPLAY camera mode scrollbar thumb (Mat[305..326])
                            struct DispThumbMat { uint32_t idx; float vtx; };
                            const DispThumbMat disp_mats[] = {
                                { 305u, 491.25f }, { 311u, 489.75f }, { 312u, 488.50f }, { 314u, 487.30f },
                                { 315u, 486.25f }, { 317u, 485.45f }, { 319u, 484.75f }, { 321u, 484.20f },
                                { 323u, 483.80f }, { 325u, 483.60f }, { 326u, 483.50f }
                            };
                            for (const auto& dm : disp_mats) {
                                set_mat_tx(dm.idx, dm.vtx + 1.50f, dm.vtx + 86.833336f, dm.vtx + 193.625f);
                            }
                        } else if (stem == "AircraftData") {
                            // Normal mode scrollbar thumb (Mat[94], vanilla 4:3 tx = 483.50f)
                            set_mat_tx(94u, 485.00f, 561.833336f, 669.00f);
                        } else if (stem == "Shop") {
                            // Normal mode scrollbar thumb (Mat[337], vanilla 4:3 tx = 483.50f)
                            set_mat_tx(337u, 485.00f, 561.833336f, 669.00f);
                        }
                    }
                }
            }

            BuildSwgAspectDiffTable(stem, base_4x3_ptr, swg_16, swg_21, sz_16);

            // Original game SWAD textures are preserved; HD textures are supplied via acz_hdtex PNG overrides.

            // Mutate game-allocated swg_data in-place (preserves contiguous SWAD heap layout & avoids dangling vector pointers)
            if (base_4x3_ptr != swg_data) {
                std::memcpy(swg_data, base_4x3_ptr, sz_16);
            }
            int target_tier = GetTargetMenuAspectTier();
            ApplySwgDynamicAspectState(nullptr, swg_data, target_tier);
            LogMsg("[Enhancer] [MenuOverride] Loaded widescreen menu override for '%s' (%zu bytes, active_tier=%d)",
                   stem.c_str(), sz_16, target_tier);
        }
    }

    g_HasWidescreenMenuOverride[stem] = has_ws_override;
    g_CurrentMenuHasWsOverride.store(has_ws_override, std::memory_order_relaxed);
    int target_tier = GetTargetMenuAspectTier();
    SetWidescreenMenuScissorOverride(has_ws_override && (target_tier > 0));
}

static void* __fastcall Hook_CSwgInstanceInit(
    void* this_ptr,
    void* a2,
    uint8_t* swg_data,
    void* renderer,
    void* a5,
    void* a6,
    void* a7
) {
    if (swg_data != nullptr && std::memcmp(swg_data, "SWG\0", 4) == 0) {
        std::string stem = ResolveSwgMenuStem(swg_data);
        if (!stem.empty()) {
            ProcessSwgMenuOverride(stem, swg_data);
        }
    }

    return g_OrigCSwgInstanceInit(this_ptr, a2, swg_data, renderer, a5, a6, a7);
}

using FnDrawMovieQuad = void* (__fastcall*)(void* a1, void* a2, void* a3, void* a4);
static FnDrawMovieQuad g_OrigDrawMovieQuad = nullptr;

static void* __fastcall Hook_DrawMovieQuad(void* a1, void* a2, void* a3, void* a4) {
    // Dynamic independent X/Y orthographic projection scaling for video & briefing playback:
    //   - Note on RenderTarget 0 (1920x1080) decoding at 0x140215ED7..0x1402160CC:
    //       * Briefing videos (b01..b17.smv, where BYTE PTR [a1 + 0x57A] != 0) are 1440x1080 (4:3)
    //         and are decoded into the CENTER 0.75 width of RenderTarget 0 (dst_x = 240..1680)!
    //       * Cutscene / interview videos (s01..v4la3.smv, where BYTE PTR [a1 + 0x57A] == 0) are
    //         1920x1080 (16:9 with baked-in top/bottom black letterbox bars, active height 397/440)
    //         and span 100% width (0..1920) of RenderTarget 0.
    //   - RVA 0x181E47 (xmm1 -> RVA_PillarboxDataCave + 0x10): Ortho Width  (+2.0f / scale_x)
    //   - RVA 0x181E31 (xmm0 -> RVA_PillarboxDataCave + 0x14): Ortho Height (-2.0f / scale_y)
    if (g_ExeBase) {
        float* pVideoOrthoW = reinterpret_cast<float*>(g_ExeBase + AczRva(RVA_PillarboxDataCave + 0x10));
        float* pVideoOrthoH = reinterpret_cast<float*>(g_ExeBase + AczRva(RVA_PillarboxDataCave + 0x14));
        uint8_t is_4x3_movie = 0;
        if (a1 != nullptr) {
            is_4x3_movie = *reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(a1) + 0x57A);
        }
        int aspect_opt = GetGameAspectOption();
        float scale_x = 1.0f;
        float scale_y = 1.0f;

        if (aspect_opt == 2) {
            // 21:9 Ultrawide mode:
            uint32_t render_w = *reinterpret_cast<const uint32_t*>(g_ExeBase + AczRva(RVA_RenderWidth));
            uint32_t render_h = *reinterpret_cast<const uint32_t*>(g_ExeBase + AczRva(RVA_RenderHeight));
            float aspect = (render_w > 0 && render_h > 0)
                ? (static_cast<float>(render_w) / static_cast<float>(render_h))
                : (21.0f / 9.0f);
            if (aspect < 1.8f) {
                aspect = 21.0f / 9.0f;
            }
            if (is_4x3_movie != 0) {
                // Briefing (already 0.75 wide in RT 0): scale RT 0 by (16/9)/aspect (0.7619048f)
                // so 0.75 * 16/21 = 4/7 (0.5714286f), exact 4:3 proportions inside 21:9!
                scale_x = (16.0f / 9.0f) / aspect;
                scale_y = 1.0f;
            } else {
                // Cutscene / Interview (16:9 with baked-in top/bottom black bars, active height 397/440):
                // Zoom vertically by 440/397 (1.1083123f) to eliminate the baked-in top/bottom black bars,
                // and compress horizontally by scale_y * ((16/9)/aspect) (0.8444284f) for true 1:1 proportions!
                scale_y = 440.0f / 397.0f;
                scale_x = scale_y * ((16.0f / 9.0f) / aspect);
            }
        } else if (aspect_opt == 1) {
            // 16:9 Widescreen mode:
            // Both Briefing (already 0.75 pillarboxed in RT 0) and Cutscene (1.0 in RT 0) use 1:1 RT 0 mapping.
            scale_x = 1.0f;
            scale_y = 1.0f;
        } else {
            // 4:3 Standard mode (SwapChain Scissor clips output to middle 0.75 width):
            if (is_4x3_movie != 0) {
                // Briefing is already 0.75 wide in RT 0, matching the 0.75-wide 4:3 SwapChain Scissor 1:1!
                scale_x = 1.0f;
                scale_y = 1.0f;
            } else {
                // Cutscene is 1.0 wide in RT 0: scale by 0.75 in both X and Y so it fits inside the
                // 0.75-wide 4:3 SwapChain Scissor with proper 16:9 letterboxing!
                scale_x = 0.75f;
                scale_y = 0.75f;
            }
        }

        if (pVideoOrthoW) *pVideoOrthoW = 2.0f / scale_x;
        if (pVideoOrthoH) *pVideoOrthoH = -2.0f / scale_y;

        // Ensure 4:3 pillarbox scissor & GsVirtualWidth (896 in 21:9, 682 in 16:9/4:3) are active
        // for Briefing overlays and subtitles:
        g_CurrentMenuHasWsOverride.store(false, std::memory_order_relaxed);
        SetWidescreenMenuScissorOverride(false);
    }
    return g_OrigDrawMovieQuad(a1, a2, a3, a4);
}

static void SyncSwgInstanceAspect(void* swg_instance, bool update_scissor) {
    if (g_ExeBase != 0) {
        int32_t* pScreenSize = reinterpret_cast<int32_t*>(g_ExeBase + AczRva(RVA_ScreenSizeOption));
        if (*pScreenSize != g_ScreenAspect) {
            *pScreenSize = g_ScreenAspect;
        }
    }
    if (swg_instance == nullptr) return;
    if (!update_scissor) {
        UpdateDynamicPillarboxAndAspect(g_WidescreenScissorState.load(std::memory_order_relaxed) == 1);
    }
    uint8_t* swg_data = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(swg_instance) + 0x08);
    if (swg_data == nullptr || std::memcmp(swg_data, "SWG\0", 4) != 0) return;

    std::string stem = ResolveSwgMenuStem(swg_data);

    int target_tier = GetTargetMenuAspectTier();
    bool has_ws_override = false;
    {
        std::lock_guard<std::mutex> lock(g_MenuOverrideMutex);
        auto it = g_HasWidescreenMenuOverride.find(stem);
        if (it != g_HasWidescreenMenuOverride.end() && it->second) {
            has_ws_override = true;
            ApplySwgDynamicAspectState(swg_instance, swg_data, target_tier);
        }
    }
    g_CurrentMenuHasWsOverride.store(has_ws_override, std::memory_order_relaxed);
    if (update_scissor) {
        SetWidescreenMenuScissorOverride(has_ws_override && (target_tier > 0));
    }
}


static void __fastcall Hook_CSwgInstanceUpdate(void* this_ptr) {
    SyncSwgInstanceAspect(this_ptr, true);
    g_OrigCSwgInstanceUpdate(this_ptr);
}

static void* __fastcall Hook_CSwadMenuDraw(void* this_ptr, void* a2) {
    if (this_ptr != nullptr) {
        void* swg_instance = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(this_ptr) + 0x08);
        SyncSwgInstanceAspect(swg_instance, true);
    }
    return g_OrigCSwadMenuDraw(this_ptr, a2);
}

using FnMissionTitleDraw = void (__fastcall*)(void* this_ptr);
static FnMissionTitleDraw g_OrigMissionTitleDraw = nullptr;

// ----------------------------------------------------------------------------
// Hook 14: AVModeMissionTitle::Draw (+0x173120)
// Enables full-viewport 16:9 (682x448) and 21:9 (896x448) widescreen rendering
// for data/mistitle/m01..m19.gim when _16x9.png / _21x9.png variants exist in
// mods	extures\, and restores 4:3 pillarbox state immediately after drawing!
// ----------------------------------------------------------------------------
static void __fastcall Hook_MissionTitleDraw(void* this_ptr) {
    float* pMissionTitleVirtualW = g_ExeBase
        ? reinterpret_cast<float*>(g_ExeBase + AczRva(RVA_PillarboxDataCave + 0x18))
        : nullptr;

    int eff_tier = 0;
    uint32_t state = 0;
    if (this_ptr != nullptr) {
        state = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(this_ptr) + 0x08);
        if (state == 1u && g_EnableMenuOverrides) {
            int target_tier = GetTargetMenuAspectTier();
            if (target_tier > 0) {
                uint8_t* gim_desc = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(this_ptr) + 0x20);
                if (gim_desc != nullptr) {
                    eff_tier = QueryHdTexGimAspectTier(gim_desc, "MissionTitleDraw");
                }
            }
        }
    }

    if (state == 1u) {
        // AVModeMissionTitle::Draw queues a GIF DMA packet into the global DMA chain (0x14032c8b0),
        // which is flushed to D3D12 at the end of the frame by GsTransferGifPacket -> UpdateGsFrameAndScissor:
        //   - SetWidescreenMenuScissorOverride(eff_tier > 0) MUST remain active through the end-of-frame DMA flush!
        //   - pMissionTitleVirtualW (read synchronously at RVA 0x173254 inside g_OrigMissionTitleDraw) controls
        //     env.scale_x = 512.0f / virtual_w so the sprite quad X1..X2 spans 682 GS px (16:9) or 896 GS px (21:9)
        //     instead of being squished into 512 GS px (4:3)!
        if (pMissionTitleVirtualW) {
            if (eff_tier == 2) {
                *pMissionTitleVirtualW = 512.0f * (512.0f / 896.0f); // 292.571429f -> quad width = 896.0f (21:9)
            } else if (eff_tier == 1) {
                *pMissionTitleVirtualW = 512.0f * (512.0f / 682.0f); // 384.398827f -> quad width = 682.0f (16:9)
            } else {
                *pMissionTitleVirtualW = 512.0f;                     // 512.0f      -> quad width = 512.0f (4:3)
            }
        }
        SetWidescreenMenuScissorOverride(eff_tier > 0);
        g_OrigMissionTitleDraw(this_ptr);
        if (pMissionTitleVirtualW) {
            *pMissionTitleVirtualW = 512.0f;
        }
    } else {
        // Title card finished (state == 2): restore 4:3 pillarbox state before transitioning to gameplay/briefing
        if (pMissionTitleVirtualW) {
            *pMissionTitleVirtualW = 512.0f;
        }
        SetWidescreenMenuScissorOverride(false);
        g_OrigMissionTitleDraw(this_ptr);
    }
}

using FnLoadingScreenDraw = uint8_t* (__fastcall*)(void* this_ptr, uint8_t* gif_buf);
static FnLoadingScreenDraw g_OrigLoadingScreenDraw = nullptr;

// ----------------------------------------------------------------------------
// Hook 15: CLoadingScreen::Draw (+0x205050 / +0x2056D0)
// Unstretches data/select/loading.bin sprites (loading_00 [ LOADING ] badge and
// loading_01..05 mission opening epigraph quotes) in 16:9 and 21:9 widescreen:
//   - During mission start (AVModeMission::Init -> CLoadingScreen::StartQuote),
//     CSysData::isWidescreen (+0x31EEF8) is already set to 1, which causes
//     GsFlushDrawBatch (+0x193FA4) to set the GS projection width [rbx+0x73c] to
//     512.0f across the entire 16:9 / 21:9 viewport instead of 682 / 896.
//   - Redirecting RVA 0x2050EE (vmovss xmm0, [0x1402d291c]) to RVA_PillarboxDataCave + 0x1C
//     (pLoadingScreenVirtualW) and setting it to 682.666687f (16:9) or 896.0f (21:9)
//     scales env.scale_x = 512.0f / virtual_w around screen center (0x8000), rendering
//     all 5 epigraph quotes and [ LOADING ] at true 1:1 unstretched proportions (while
//     also supporting pre-widened _16x9 / _21x9 quote textures if provided).
// ----------------------------------------------------------------------------
static uint8_t* __fastcall Hook_LoadingScreenDraw(void* this_ptr, uint8_t* gif_buf) {
    float* pLoadingScreenVirtualW = g_ExeBase
        ? reinterpret_cast<float*>(g_ExeBase + AczRva(RVA_PillarboxDataCave + 0x1C))
        : nullptr;

    if (this_ptr != nullptr && g_ExeBase != 0) {
        const uint8_t* ls = reinterpret_cast<const uint8_t*>(this_ptr);
        uint8_t is_active = ls[0x18];
        int32_t timer     = *reinterpret_cast<const int32_t*>(ls + 0x1C);
        if (is_active != 0 || timer != 0) {
            uint8_t is_ws = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
            if (is_ws != 0 && g_WidescreenScissorState.load(std::memory_order_relaxed) != 0) {
                SetWidescreenMenuScissorOverride(false);
            }

            uint8_t* desc_array = *reinterpret_cast<uint8_t* const*>(ls + 0x08);
            int32_t  quote_idx  = *reinterpret_cast<const int32_t*>(ls + 0x20);

            if (desc_array != nullptr) {
                QueryHdTexGimAspectTier(desc_array, "LoadingScreen_Badge");
                if (quote_idx > 0 && quote_idx < 6) {
                    uint8_t* q_desc = desc_array + static_cast<size_t>(quote_idx) * 0x48u;
                    QueryHdTexGimAspectTier(q_desc, "LoadingScreen_Quote");
                }
            }

            int aspect_opt = GetGameAspectOption();
            float true_virtual_w = 512.0f;
            if (aspect_opt == 2) {
                uint32_t render_w = *reinterpret_cast<const uint32_t*>(g_ExeBase + AczRva(RVA_RenderWidth));
                uint32_t render_h = *reinterpret_cast<const uint32_t*>(g_ExeBase + AczRva(RVA_RenderHeight));
                float aspect = (render_w > 0 && render_h > 0)
                    ? (static_cast<float>(render_w) / static_cast<float>(render_h))
                    : (21.0f / 9.0f);
                if (aspect < 1.8f) {
                    aspect = 21.0f / 9.0f;
                }
                true_virtual_w = 512.0f * (aspect / (4.0f / 3.0f)); // 896.0f for 21:9
            } else if (aspect_opt == 1) {
                true_virtual_w = 512.0f * ((16.0f / 9.0f) / (4.0f / 3.0f)); // 682.666687f for 16:9
            }

            float target_virtual_w = 512.0f;
            if (is_ws != 0) {
                target_virtual_w = true_virtual_w;
            } else if (aspect_opt == 2) {
                uint32_t gs_vw = *reinterpret_cast<const uint32_t*>(g_ExeBase + AczRva(RVA_GsVirtualWidth));
                if (gs_vw >= 512u && gs_vw < 750u) {
                    target_virtual_w = 512.0f * (true_virtual_w / static_cast<float>(gs_vw));
                }
            }

            if (pLoadingScreenVirtualW) {
                *pLoadingScreenVirtualW = target_virtual_w;
            }
            uint8_t* ret = g_OrigLoadingScreenDraw(this_ptr, gif_buf);
            if (pLoadingScreenVirtualW) {
                *pLoadingScreenVirtualW = 512.0f;
            }
            return ret;
        }
    }

    return g_OrigLoadingScreenDraw(this_ptr, gif_buf);
}


// ----------------------------------------------------------------------------
// Hooks 17..19: Cutscene Subtitles, Location/Time Captions, Teletype & Squadron Intros
//   - CCutsceneSubFont::Draw (+0x00B410): Real-time 3D cutscene dialogue subtitles
//     (align_mode 0, 1, 2), location/time captions (align_mode 3, e.g. "Valais Air Base"),
//     and Squadron Intro titles/subtitles (align_mode 4 "GALM", align_mode 5 "Ustio Air Force...").
//   - CCutsceneSubFont::DrawEmblemGim (+0x00B810): Squadron Intro emblem (align_mode 6).
//   - CCutsceneFont::Draw (+0x1FCBA0): FMV movie/interview subtitles (CMovieCutsceneFont)
//     and FMV interviewee green teletype captions (CCutsceneTeletypeFont).
//
// Why cutscene fonts are stretched in 16:9 and 21:9:
//   - In widescreen (16:9 / 21:9), GetAspectScaleY (+0x1FD130 / +0x17F6D0) multiplies
//     glyph quad Y-height by 7/6 (1.166667f), making 1 vertical texel equal 1/384 of
//     screen height, while GetVirtualScreenScale (+0x1FCFA0 / +0x17F7C0) sets out_scale_x = 0.75f.
//   - However, DrawGlyph2D (+0x00B0E0 / +0x1FC8D0) and GetGlyphAdvanceX (+0x1FD170) never
//     multiply horizontal glyph advance (adv_x) or quad X-width by 0.75f (16:9) or 4/7 (21:9),
//     causing every character and inter-character step to stretch horizontally by 4/3 (+33.3%)
//     in 16:9 and 7/4 (+75%) in 21:9.
//   - By scaling emitted GS_REG_XYZ2 (0x05) X coordinates directly in 12.4 fixed-point
//     subpixel space (where 16 * 0.75 = 12 subpixels exact):
//       1) Centered subtitles & location captions (center_flag == 1 or align_mode <= 3,
//          plus << >> and [ ] speaker brackets) scale around screen center (offset_x * 16)
//          by ws_4x3_to_actual_x (0.75f in 16:9, 0.75f * (16/9)/aspect in 21:9).
//       2) Left-aligned Squadron Intro fonts (align_mode 4 & 5) and Teletype captions
//          already have their starting X anchor (pos_x * 0.75f - width/2) fitted for 16:9
//          relative to the Squadron Emblem, so their starting X anchor is kept at 1.0x in
//          16:9 and mapped to the 16:9 frame (ws_16x9_to_actual_x) in 21:9 (along with the
//          Squadron Emblem in DrawEmblemGim), while character advances/widths extending
//          from the anchor scale by ws_4x3_to_actual_x (0.75f in 16:9, 4/7 in 21:9).
// ----------------------------------------------------------------------------
using FnCutsceneSubFontDraw       = uint8_t (__fastcall*)(uint8_t* this_ptr, int32_t str_idx, uint8_t** gif_buf_ptr, const uint8_t* gs_env, float cur_time);
using FnCutsceneSubFontDrawEmblem = uint8_t (__fastcall*)(uint8_t* this_ptr, void* gim_desc, uint8_t** gif_buf_ptr, const uint8_t* gs_env, float cur_time);
using FnCutsceneFontDraw          = uint8_t (__fastcall*)(uint8_t* this_ptr, int32_t str_idx, uint8_t** gif_buf_ptr, const uint8_t* gs_env, float cur_time);

static FnCutsceneSubFontDraw       g_OrigCutsceneSubFontDraw       = nullptr;
static FnCutsceneSubFontDrawEmblem g_OrigCutsceneSubFontDrawEmblem = nullptr;
static FnCutsceneFontDraw          g_OrigCutsceneFontDraw          = nullptr;

static void ComputeCutsceneWidescreenScales(int aspect_opt, float* out_ws_16x9_to_actual_x, float* out_ws_4x3_to_actual_x) {
    float ws_16x9 = 1.0f;
    if (aspect_opt == 2 && g_ExeBase != 0) {
        uint32_t render_w = *reinterpret_cast<const uint32_t*>(g_ExeBase + AczRva(RVA_RenderWidth));
        uint32_t render_h = *reinterpret_cast<const uint32_t*>(g_ExeBase + AczRva(RVA_RenderHeight));
        float aspect = (render_w > 0 && render_h > 0)
            ? (static_cast<float>(render_w) / static_cast<float>(render_h))
            : (21.0f / 9.0f);
        if (aspect < 1.8f) {
            aspect = 21.0f / 9.0f;
        }
        ws_16x9 = (16.0f / 9.0f) / aspect;
    }
    if (out_ws_16x9_to_actual_x) *out_ws_16x9_to_actual_x = ws_16x9;
    if (out_ws_4x3_to_actual_x)  *out_ws_4x3_to_actual_x  = 0.75f * ws_16x9;
}

static void ScaleCutsceneGifPacketsX(
    uint8_t*       gif_start,
    uint8_t*       gif_end,
    float          orig_anchor_subpx,
    float          new_anchor_subpx,
    float          span_scale_x
) {
    if (!gif_start || !gif_end || gif_end <= gif_start) return;
    __try {
        uint8_t* p = gif_start;
        while (p + 32 <= gif_end) {
            // IMPORTANT: DrawGlyph2D (+0x00B0E0 / +0x1FC8D0), DrawEmblem (+0x00B810), and
            // DrawCursor (+0x006020 / +0x014210) write GIF packet fields and A+D register IDs
            // using 32-bit DWORD stores (`mov DWORD PTR [r9+0x68], 5`) into a non-zeroed GS
            // scratch ring buffer, leaving the upper 32 bits at [ad + 0x0C] with random leftover
            // bytes from previous frames! Always read 32-bit DWORDs here (never uint64_t).
            const uint32_t dma_lo = *reinterpret_cast<const uint32_t*>(p + 0x00);
            const uint32_t dma_id = (dma_lo >> 28) & 0x7u;
            if (dma_id == 3u || dma_id == 4u) { // DMA REF / REFE tag (16 bytes only; payload is external)
                p += 16u;
                continue;
            }
            const uint32_t gif_lo = *reinterpret_cast<const uint32_t*>(p + 0x10);
            const uint32_t gif_hi = *reinterpret_cast<const uint32_t*>(p + 0x18);

            const uint32_t qwc   = dma_lo & 0xFFFFu;
            const uint32_t nloop = gif_lo & 0x7FFFu;

            if (qwc == 0 || nloop == 0 || qwc != nloop + 1u || (gif_hi & 0xFFu) != 0x0Eu) {
                p += 16;
                continue;
            }

            uint8_t* pkt_end = p + 16u + qwc * 16u;
            if (pkt_end > gif_end) {
                break;
            }

            uint8_t* ad = p + 32u;
            for (uint32_t i = 0; i < nloop; ++i, ad += 16u) {
                const uint32_t reg = *reinterpret_cast<const uint32_t*>(ad + 0x08);
                if (reg == 0x05u) { // GS_REG_XYZ2
                    const uint16_t x_orig    = *reinterpret_cast<const uint16_t*>(ad + 0x00);
                    const float    dx        = static_cast<float>(x_orig) - orig_anchor_subpx;
                    const int32_t  x_new     = static_cast<int32_t>(std::lroundf(new_anchor_subpx + dx * span_scale_x));
                    const uint16_t x_clamped = static_cast<uint16_t>(std::max(0, std::min(65535, x_new)));
                    *reinterpret_cast<uint16_t*>(ad + 0x00) = x_clamped;
                }
            }

            p = pkt_end;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static uint8_t __fastcall Hook_CutsceneSubFontDraw(
    uint8_t*       this_ptr,
    int32_t        str_idx,
    uint8_t**      gif_buf_ptr,
    const uint8_t* gs_env,
    float          cur_time
) {
    uint8_t* gif_before = (gif_buf_ptr != nullptr) ? *gif_buf_ptr : nullptr;
    uint8_t  ret        = g_OrigCutsceneSubFontDraw(this_ptr, str_idx, gif_buf_ptr, gs_env, cur_time);
    uint8_t* gif_after  = (gif_buf_ptr != nullptr) ? *gif_buf_ptr : nullptr;

    if (ret != 0 && gif_before != nullptr && gif_after > gif_before && this_ptr != nullptr && gs_env != nullptr && g_ExeBase != 0) {
        const uint8_t is_ws = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt > 0) {
            float ws_16x9_to_actual_x = 1.0f;
            float ws_4x3_to_actual_x  = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, &ws_16x9_to_actual_x, &ws_4x3_to_actual_x);

            const float   offset_x     = *reinterpret_cast<const float*>(gs_env + 0x190);
            const float   center_subpx = offset_x * 16.0f;
            const uint8_t align_mode   = this_ptr[0x86];
            const uint8_t center_flag  = this_ptr[0x87];

            float orig_anchor_subpx = center_subpx;
            float new_anchor_subpx  = center_subpx;
            float span_scale_x      = ws_4x3_to_actual_x;

            if (center_flag == 0 && align_mode >= 4) {
                // Squadron Intro title (align_mode 4 "GALM") & subtitle (align_mode 5 "Ustio Air Force..."):
                // Starting X (pos_x * 0.75f - env_w/2) is already fitted for 16:9 next to the emblem;
                // in 21:9 map the starting anchor into the 16:9 frame, and scale character advances by ws_4x3_to_actual_x.
                const int16_t pos_x       = *reinterpret_cast<const int16_t*>(this_ptr + 0x7C);
                const int16_t env_w       = *reinterpret_cast<const int16_t*>(gs_env + 0x28);
                const int32_t start_x_rel = static_cast<int32_t>(static_cast<float>(pos_x) * 0.75f - static_cast<float>(env_w / 2));
                orig_anchor_subpx         = static_cast<float>(static_cast<int32_t>(offset_x + static_cast<float>(start_x_rel)) << 4);
                new_anchor_subpx          = center_subpx + (orig_anchor_subpx - center_subpx) * ws_16x9_to_actual_x;
            }

            ScaleCutsceneGifPacketsX(gif_before, gif_after, orig_anchor_subpx, new_anchor_subpx, span_scale_x);
        }
    }
    return ret;
}

// ----------------------------------------------------------------------------
// Hooks 20..29: Gameplay HUD, Mission Radio Subtitles & 21:9 3D FOV Engine
//   1. Hook_GsBuildProjectionMatrices (+0x1FBEC0 / +0x1FC540) & Hook_GetAspectScaleY (+0x1FD130 / +0x1FD7B0):
//      - In 21:9 (aspect_opt == 2), expands GsScreenConfig virtual width from 640.0f (16:9) to
//        640.0f / ws_16x9_to_actual_x (840.0f for 21:9) and updates scale_x (+0x198) to 512.0f / 840.0f.
//      - Produces true Hor+ 21:9 3D camera FOV, 21:9 NDC frustum clipping planes, 21:9 3D billboard/sprite
//        aspect scaling, and automatically unstretches all 3D-linked HUD elements (-o- center reticle,
//        3D target square boxes, locked target reticle, gun pipper circle, velocity vector, pitch ladder lines,
//        and 1P/3P compass).
//      - Hook_GetAspectScaleY preserves the 16:9 Y-scale ratio (1.5555556f) in 21:9 so 3D cutscene
//        subtitles keep their exact 16:9 vertical height.
//   2. Universal HUD & Radio Subtitle GIF Packet X-Scaler (ScaleHudGifPacketsX):
//      - Handles all 4 PS2 GIF packet formats used by ACZ HUD & Radio Subtitles:
//        * PACKED NREG=1 (0x0E), NREG=2 (0xEE), NREG=3 (0xEEE) with GS_REG_XYZ2 (0x05) / GS_REG_XYZF2 (0x04)
//          and optional GS_REG_SCISSOR_1 (0x40) scaling for Radar clip rectangles.
//        * REGLIST (FLG=1) NREG=3 (0x412 / 0x413, 24 bytes/vertex) and NREG=2 (0x41, 16 bytes/vertex).
//      - Unstretches Mission Radio Subtitles (CMissionRadioSubManager::Draw), fixed-pixel HUD widgets
//        (SCORE, TIME, SPEED, ALT, Climb, Range, Heading, Weapons + Aircraft/Missile icons, COVER + D-pad,
//        SpWeapon), centered HUD status alerts/messages/countdowns, and 3D target text labels ("FRND", "TGT",
//        target aircraft type/name, distance) in both 16:9 and 21:9, and scales Radar & Pause Menu in 21:9.
// ----------------------------------------------------------------------------
using FnGsBuildProjectionMatrices = void     (__fastcall*)(uint8_t* gs_cfg);
using FnGetAspectScaleY           = float    (__fastcall*)(void* self, const uint8_t* gs_cfg);
using FnRadioSubFontDraw          = uint8_t  (__fastcall*)(void* self, void* sub_entry, const int32_t* x_ptr, const int32_t* y_ptr, uint8_t** gif_buf_ptr, const uint8_t* gs_env);
using FnCHudManagerDraw           = uint8_t* (__fastcall*)(uint8_t* hud_mgr, uint8_t* gif_buf, uint8_t* gs_env, void* a4, void* a5);
using FnHudWidgetVirtualDraw      = uint8_t* (__fastcall*)(void* self, uint8_t* gif_buf, void* gs_env, uint32_t flag);
using FnPauseMenuDraw             = uint8_t* (__fastcall*)(void* self, uint8_t* gif_buf, void* gs_env, void* a4, uintptr_t a5);
using FnPauseSubMenuDraw          = uint8_t* (__fastcall*)(void* self, uint8_t* gif_buf, void* gs_env);
using FnDebriefingDraw            = uint8_t* (__fastcall*)(uint8_t* debrief_obj, uint8_t* gif_buf);
using FnHudDrawTextAnchored       = uint8_t* (__fastcall*)(void* font_mgr, uint8_t* gif_buf, uintptr_t a3, int32_t anchor_x, int32_t anchor_y, int32_t spacing, void* tex_desc, const uint8_t* rgba_ptr, const char* str);
using FnHudStatusAlerts           = uint8_t* (__fastcall*)(void* hud_mgr, uint8_t* gif_buf, void* gs_env);
using FnHudBottomMessage          = uint8_t* (__fastcall*)(void* hud_mgr, uint8_t* gif_buf, void* gs_env);
using FnHudCountdown              = uint8_t* (__fastcall*)(void* hud_mgr, uint8_t* gif_buf);
using FnHudDrawTextFloat          = uint8_t* (__fastcall*)(void* font_mgr, uint8_t* gif_buf, uintptr_t a3, float x_f, float y_f, int32_t spacing, void* tex_desc, const uint8_t* rgba_ptr, const char* str);

static FnGsBuildProjectionMatrices g_OrigGsBuildProjectionMatrices = nullptr;
static FnRadioSubFontDraw          g_OrigRadioSubFontDraw          = nullptr;
static FnCHudManagerDraw           g_OrigCHudManagerDraw           = nullptr;
static FnPauseMenuDraw             g_OrigPauseMenuDraw             = nullptr;
static FnPauseSubMenuDraw          g_OrigPauseSubMenuDraw          = nullptr;
static FnDebriefingDraw            g_OrigDebriefingDraw            = nullptr;
static FnHudDrawTextAnchored       g_OrigHudDrawTextLeft           = nullptr;
static FnHudDrawTextAnchored       g_OrigHudDrawTextRight          = nullptr;
static FnHudStatusAlerts           g_OrigHudStatusAlerts           = nullptr;
static FnHudBottomMessage          g_OrigHudBottomMessage          = nullptr;
static FnHudCountdown              g_OrigHudCountdown              = nullptr;
static FnHudDrawTextFloat          g_OrigHudDrawTextFloat          = nullptr;

static thread_local int   g_HudWidgetScaleDepth     = 0;
static thread_local bool  g_InsideCHudManagerDraw   = false;
static thread_local bool  g_InsideDebriefingDraw    = false;
static thread_local float g_CurrentRadarLeftSubpx   = 29168.0f; // (31.0f + 1792.0f) * 16.0f

static void ScaleHudGifPacketsX(
    uint8_t* gif_start,
    uint8_t* gif_end,
    float    orig_anchor_subpx,
    float    new_anchor_subpx,
    float    span_scale_x,
    bool     scale_scissor = false
) {
    if (!gif_start || !gif_end || gif_end <= gif_start) return;
    __try {
        const float orig_anchor_px = orig_anchor_subpx * (1.0f / 16.0f) - 1792.0f;
        const float new_anchor_px  = new_anchor_subpx  * (1.0f / 16.0f) - 1792.0f;

        auto scale_subpx_u16 = [&](uint8_t* x_ptr) {
            const uint16_t x_orig = *reinterpret_cast<const uint16_t*>(x_ptr);
            const float    dx     = static_cast<float>(x_orig) - orig_anchor_subpx;
            const int32_t  x_new  = static_cast<int32_t>(std::lroundf(new_anchor_subpx + dx * span_scale_x));
            *reinterpret_cast<uint16_t*>(x_ptr) = static_cast<uint16_t>(std::max(0, std::min(65535, x_new)));
        };

        uint8_t* p = gif_start;
        while (p + 32 <= gif_end) {
            const uint32_t dma_lo = *reinterpret_cast<const uint32_t*>(p + 0x00);
            const uint32_t dma_id = (dma_lo >> 28) & 0x7u;
            if (dma_id == 3u || dma_id == 4u) { // DMA REF / REFE tag (16 bytes only; payload is external)
                p += 16u;
                continue;
            }
            const uint32_t gif_lo    = *reinterpret_cast<const uint32_t*>(p + 0x10);
            const uint32_t gif_lo_hi = *reinterpret_cast<const uint32_t*>(p + 0x14);
            const uint32_t gif_hi    = *reinterpret_cast<const uint32_t*>(p + 0x18);

            const uint32_t qwc   = dma_lo & 0xFFFFu;
            const uint32_t nloop = gif_lo & 0x7FFFu;
            const uint32_t flg   = (gif_lo_hi >> 26) & 0x3u;
            const uint32_t nreg  = (gif_lo_hi >> 28) & 0xFu;

            if (qwc == 0 || nloop == 0) {
                p += 16;
                continue;
            }
            uint8_t* pkt_end = p + 16u + qwc * 16u;
            if (pkt_end > gif_end) {
                break;
            }

            if (flg == 0u && (gif_hi & 0xFu) == 0x0Eu && qwc >= 2u) {
                const uint32_t num_ad = qwc - 1u;
                uint8_t* ad = p + 32u;
                for (uint32_t i = 0; i < num_ad; ++i, ad += 16u) {
                    const uint32_t reg = *reinterpret_cast<const uint32_t*>(ad + 0x08);
                    if (reg == 0x05u || reg == 0x04u) { // GS_REG_XYZ2 or GS_REG_XYZF2
                        scale_subpx_u16(ad + 0x00);
                    } else if (scale_scissor && reg == 0x40u) { // GS_REG_SCISSOR_1
                        const uint32_t sc_x  = *reinterpret_cast<const uint32_t*>(ad + 0x00);
                        const uint32_t scax0 = sc_x & 0x7FFu;
                        const uint32_t scax1 = (sc_x >> 16) & 0x7FFu;
                        if (!(scax0 == 0u && scax1 >= 0x1FFu)) {
                            const int32_t nx0 = std::max(0, std::min(511, static_cast<int32_t>(std::lroundf(new_anchor_px + (static_cast<float>(scax0) - orig_anchor_px) * span_scale_x))));
                            const int32_t nx1 = std::max(0, std::min(511, static_cast<int32_t>(std::lroundf(new_anchor_px + (static_cast<float>(scax1) - orig_anchor_px) * span_scale_x))));
                            *reinterpret_cast<uint32_t*>(ad + 0x00) = (static_cast<uint32_t>(nx1) << 16) | static_cast<uint32_t>(nx0);
                        }
                    }
                }
            } else if (flg == 1u) {
                const uint32_t regs = gif_hi & 0xFFFFu;
                if (nreg == 3u && (regs == 0x0413u || regs == 0x0412u) && qwc == ((nloop * 3u + 1u) / 2u + 1u)) {
                    uint8_t* v = p + 32u;
                    for (uint32_t i = 0; i < nloop; ++i, v += 24u) {
                        scale_subpx_u16(v + 16u);
                    }
                } else if (nreg == 2u && (regs & 0xFFu) == 0x41u && qwc == (nloop + 1u)) {
                    uint8_t* v = p + 32u;
                    for (uint32_t i = 0; i < nloop; ++i, v += 16u) {
                        scale_subpx_u16(v + 8u);
                    }
                }
            }

            p = pkt_end;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool ComputeHudGifBoundsX(
    uint8_t*  gif_start,
    uint8_t*  gif_end,
    uint16_t* out_min_x,
    uint16_t* out_max_x
) {
    if (!gif_start || !gif_end || gif_end <= gif_start || !out_min_x || !out_max_x) return false;
    uint16_t min_x = 0xFFFFu;
    uint16_t max_x = 0u;
    bool     found = false;
    __try {
        auto accum_u16 = [&](const uint8_t* x_ptr) {
            const uint16_t x = *reinterpret_cast<const uint16_t*>(x_ptr);
            if (x >= 0x4000u && x <= 0xC000u) {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                found = true;
            }
        };

        uint8_t* p = gif_start;
        while (p + 32 <= gif_end) {
            const uint32_t dma_lo = *reinterpret_cast<const uint32_t*>(p + 0x00);
            const uint32_t dma_id = (dma_lo >> 28) & 0x7u;
            if (dma_id == 3u || dma_id == 4u) { // DMA REF / REFE tag (16 bytes only; payload is external)
                p += 16u;
                continue;
            }
            const uint32_t gif_lo    = *reinterpret_cast<const uint32_t*>(p + 0x10);
            const uint32_t gif_lo_hi = *reinterpret_cast<const uint32_t*>(p + 0x14);
            const uint32_t gif_hi    = *reinterpret_cast<const uint32_t*>(p + 0x18);

            const uint32_t qwc   = dma_lo & 0xFFFFu;
            const uint32_t nloop = gif_lo & 0x7FFFu;
            const uint32_t flg   = (gif_lo_hi >> 26) & 0x3u;
            const uint32_t nreg  = (gif_lo_hi >> 28) & 0xFu;

            if (qwc == 0 || nloop == 0) {
                p += 16;
                continue;
            }
            uint8_t* pkt_end = p + 16u + qwc * 16u;
            if (pkt_end > gif_end) {
                break;
            }

            if (flg == 0u && (gif_hi & 0xFu) == 0x0Eu && qwc >= 2u) {
                const uint32_t num_ad = qwc - 1u;
                const uint8_t* ad = p + 32u;
                for (uint32_t i = 0; i < num_ad; ++i, ad += 16u) {
                    const uint32_t reg = *reinterpret_cast<const uint32_t*>(ad + 0x08);
                    if (reg == 0x05u || reg == 0x04u) {
                        accum_u16(ad + 0x00);
                    }
                }
            } else if (flg == 1u) {
                const uint32_t regs = gif_hi & 0xFFFFu;
                if (nreg == 3u && (regs == 0x0413u || regs == 0x0412u) && qwc == ((nloop * 3u + 1u) / 2u + 1u)) {
                    const uint8_t* v = p + 32u;
                    for (uint32_t i = 0; i < nloop; ++i, v += 24u) {
                        accum_u16(v + 16u);
                    }
                } else if (nreg == 2u && (regs & 0xFFu) == 0x41u && qwc == (nloop + 1u)) {
                    const uint8_t* v = p + 32u;
                    for (uint32_t i = 0; i < nloop; ++i, v += 16u) {
                        accum_u16(v + 8u);
                    }
                }
            }

            p = pkt_end;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (found) {
        *out_min_x = min_x;
        *out_max_x = max_x;
    }
    return found;
}

// Hook 20: GsBuildProjectionMatrices (+0x1FBEC0 in v1.0.2.1 / +0x1FC540 in v2.0.0.1)
static void __fastcall Hook_GsBuildProjectionMatrices(uint8_t* gs_cfg) {
    if (gs_cfg != nullptr && g_ExeBase != 0) {
        __try {
            const float   h    = *reinterpret_cast<const float*>(gs_cfg + 0x04);
            const int16_t gs_w = *reinterpret_cast<const int16_t*>(gs_cfg + 0x28);
            if ((h == 360.0f && gs_w == 512) || (h == 180.0f && gs_w == 256)) {
                const uint8_t is_ws      = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
                const int     aspect_opt = GetGameAspectOption();
                const float   base_16x9_w = h * (16.0f / 9.0f); // 640.0f or 320.0f
                float         target_w    = base_16x9_w;
                if (is_ws != 0 && aspect_opt == 2) {
                    float ws_16x9_to_actual_x = 1.0f;
                    ComputeCutsceneWidescreenScales(aspect_opt, &ws_16x9_to_actual_x, nullptr);
                    if (ws_16x9_to_actual_x > 0.1f) {
                        target_w = base_16x9_w / ws_16x9_to_actual_x; // 840.0f for 21:9
                    }
                }
                *reinterpret_cast<float*>(gs_cfg + 0x00)   = target_w;
                *reinterpret_cast<float*>(gs_cfg + 0x198)  = static_cast<float>(gs_w) / target_w;
            } else if (g_InsideDebriefingDraw && h == 448.0f && gs_w == 512) {
                // AVModeDebriefing::Draw 3D Tactical Map (debrief_obj + 0x3B68):
                // Expand the 3D frustum clip planes ([gs_cfg + 0x150]) from 512.0f (4:3 pillarbox)
                // to the full 16:9 (684.0f) or 21:9 (898.0f) viewport width while preserving 1:1
                // unstretched camera projection (scale_x = 1.0f, scale_y = 1.0f) so 3D terrain
                // wireframe grid lines, flight paths, and unit icons extend edge-to-edge across the screen.
                const int target_tier = g_EnableMenuOverrides ? GetTargetMenuAspectTier() : GetGameAspectOption();
                float target_frustum_w = 512.0f;
                if (target_tier == 2) {
                    float ws_16x9_to_actual_x = 1.0f;
                    ComputeCutsceneWidescreenScales(2, &ws_16x9_to_actual_x, nullptr);
                    target_frustum_w = (ws_16x9_to_actual_x > 0.1f) ? (684.0f / ws_16x9_to_actual_x) : 898.0f;
                } else if (target_tier == 1) {
                    target_frustum_w = 684.0f;
                }
                *reinterpret_cast<uint32_t*>(gs_cfg + 0x1A0) &= ~1u; // Keep scale_x = 1.0f independent of proj_width
                *reinterpret_cast<float*>(gs_cfg + 0x198) = 1.0f;
                *reinterpret_cast<float*>(gs_cfg + 0x19C) = 1.0f;
                *reinterpret_cast<float*>(gs_cfg + 0x00)  = target_frustum_w;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_OrigGsBuildProjectionMatrices(gs_cfg);
}

// Hook 21: GetAspectScaleY (+0x1FD130 in v1.0.2.1 / +0x1FD7B0 in v2.0.0.1)
//   Keeps 3D cutscene subtitle Y-scale at the 16:9 value (1.5555556f) even when
//   GsBuildProjectionMatrices sets [gs_cfg + 0x198] to 512.0f / 840.0f for 21:9 3D FOV.
static float __fastcall Hook_GetAspectScaleY(void* self, const uint8_t* gs_cfg) {
    (void)self;
    if (gs_cfg != nullptr && g_ExeBase != 0) {
        __try {
            const uint8_t is_ws = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
            if (is_ws != 0) {
                const float   scale_y = *reinterpret_cast<const float*>(gs_cfg + 0x19C);
                const int16_t gs_w    = *reinterpret_cast<const int16_t*>(gs_cfg + 0x28);
                const float   scale_x_16x9 = (gs_w == 256) ? (256.0f / 320.0f) : (512.0f / 640.0f);
                return scale_y / scale_x_16x9;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return 1.0f;
}

// Vtable Hook Registry for HUD Widgets & Radar
struct VtableHookEntry {
    void**               vtable      = nullptr;
    size_t               slot_offset = 0;
    FnHudWidgetVirtualDraw orig_fn   = nullptr;
};

static constexpr size_t MAX_VTABLE_HOOKS = 32;
static VtableHookEntry  g_VtableHooks[MAX_VTABLE_HOOKS]{};
static size_t           g_NumVtableHooks = 0;

static FnHudWidgetVirtualDraw FindOrigVtableDraw(void* self, size_t slot_offset) {
    if (!self) return nullptr;
    void** vtable = *reinterpret_cast<void***>(self);
    for (size_t i = 0; i < g_NumVtableHooks; ++i) {
        if (g_VtableHooks[i].vtable == vtable && g_VtableHooks[i].slot_offset == slot_offset) {
            return g_VtableHooks[i].orig_fn;
        }
    }
    return nullptr;
}

static void EnsureVtableHookInstalled(void* obj, size_t slot_offset, FnHudWidgetVirtualDraw hook_fn) {
    if (!obj) return;
    __try {
        void** vtable = *reinterpret_cast<void***>(obj);
        if (!vtable) return;
        void** slot_ptr = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(vtable) + slot_offset);
        if (*slot_ptr == reinterpret_cast<void*>(hook_fn)) {
            return;
        }
        for (size_t i = 0; i < g_NumVtableHooks; ++i) {
            if (g_VtableHooks[i].vtable == vtable && g_VtableHooks[i].slot_offset == slot_offset) {
                return;
            }
        }
        if (g_NumVtableHooks >= MAX_VTABLE_HOOKS) return;

        FnHudWidgetVirtualDraw orig_fn = reinterpret_cast<FnHudWidgetVirtualDraw>(*slot_ptr);
        DWORD old_prot = 0;
        if (VirtualProtect(slot_ptr, sizeof(void*), PAGE_READWRITE, &old_prot)) {
            g_VtableHooks[g_NumVtableHooks].vtable      = vtable;
            g_VtableHooks[g_NumVtableHooks].slot_offset = slot_offset;
            g_VtableHooks[g_NumVtableHooks].orig_fn     = orig_fn;
            ++g_NumVtableHooks;
            *slot_ptr = reinterpret_cast<void*>(hook_fn);
            VirtualProtect(slot_ptr, sizeof(void*), old_prot, &old_prot);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Virtual Hook: Fixed-Pixel HUD Widgets (vtable + 0x40: SCORE, Weapons + Aircraft/Missiles, SpWeapon, ALT, SPEED, Climb, Range)
static uint8_t* __fastcall Hook_HudFixedWidget_VirtualDraw40(void* self, uint8_t* gif_buf, void* gs_env, uint32_t flag) {
    FnHudWidgetVirtualDraw orig_fn = FindOrigVtableDraw(self, 0x40u);
    if (!orig_fn) return gif_buf;

    const uint8_t is_ws      = (g_ExeBase != 0) ? *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen)) : 0u;
    const int     aspect_opt = GetGameAspectOption();
    const bool    do_scale   = (is_ws != 0 && aspect_opt > 0 && self != nullptr);

    uint8_t orig_ws_byte = 0;
    if (do_scale) {
        orig_ws_byte = *(reinterpret_cast<uint8_t*>(self) + 0x09);
        // Temporarily clear widget[+0x09] so internal elements (like aircraft/missile icons in Widget5)
        // draw with uniform 4:3 relative geometry before uniform X scaling:
        *(reinterpret_cast<uint8_t*>(self) + 0x09) = 0;
    }

    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = orig_fn(self, gif_buf, gs_env, flag);
    --g_HudWidgetScaleDepth;

    if (do_scale) {
        *(reinterpret_cast<uint8_t*>(self) + 0x09) = orig_ws_byte;
        if (gif_buf != nullptr && gif_end > gif_buf) {
            uint16_t min_x = 0, max_x = 0;
            if (ComputeHudGifBoundsX(gif_buf, gif_end, &min_x, &max_x)) {
                float ws_4x3_to_actual_x = 0.75f;
                ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
                const float anchor_subpx = 0.5f * (static_cast<float>(min_x) + static_cast<float>(max_x));
                ScaleHudGifPacketsX(gif_buf, gif_end, anchor_subpx, anchor_subpx, ws_4x3_to_actual_x, false);
            }
        }
    }
    return gif_end;
}

// Hook 32: AVModeDebriefing::Draw (+0x016E30 in v1.0.2.1 / +0x016E70 in v2.0.0.1)
//   Enables full-viewport 16:9 (682x448) and 21:9 (896x448) widescreen rendering for the
//   Post-Mission Debriefing (MISSION RESULT) screen:
//     - Activates SetWidescreenMenuScissorOverride(target_tier > 0) so the D3D12 viewport scissor
//       opens to [0.0 .. 1.0] and RVA_GsVirtualWidth is set to 682 (16:9) or 896 (21:9), keeping
//       all 2D MISSION RESULT tables, text, gamepad glyphs, and subtitles centered at 1:1 proportions.
//     - Sets g_InsideDebriefingDraw = true so Hook_GsBuildProjectionMatrices expands the 3D Tactical Map
//       frustum clip planes from 512.0f (4:3) to 684.0f (16:9) / 898.0f (21:9) edge-to-edge.
static uint8_t* __fastcall Hook_DebriefingDraw(uint8_t* debrief_obj, uint8_t* gif_buf) {
    if (debrief_obj != nullptr) {
        const uint8_t is_active = debrief_obj[0xD2B5];
        if (is_active != 0) {
            const int target_tier = g_EnableMenuOverrides ? GetTargetMenuAspectTier() : GetGameAspectOption();
            SetWidescreenMenuScissorOverride(target_tier > 0);
        } else {
            SetWidescreenMenuScissorOverride(false);
        }
    }
    g_InsideDebriefingDraw = true;
    uint8_t* ret = g_OrigDebriefingDraw(debrief_obj, gif_buf);
    g_InsideDebriefingDraw = false;
    return ret;
}

// Virtual Hook: Destroyed-Target Score Breakdown List (Widget7, vtable + 0x40, under SCORE)
//   Scales around the exact same horizontal center anchor as Widget3 (SCORE: 100.5 + dx GS px = 30280.0f + dx * 16.0f subpx)
//   so the right-aligned points column stays aligned with the right edge of the SCORE box regardless of target name length.
static uint8_t* __fastcall Hook_HudScorePopupWidget_VirtualDraw40(void* self, uint8_t* gif_buf, void* gs_env, uint32_t flag) {
    FnHudWidgetVirtualDraw orig_fn = FindOrigVtableDraw(self, 0x40u);
    if (!orig_fn) return gif_buf;

    const uint8_t is_ws      = (g_ExeBase != 0) ? *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen)) : 0u;
    const int     aspect_opt = GetGameAspectOption();
    const bool    do_scale   = (is_ws != 0 && aspect_opt > 0 && self != nullptr);

    uint8_t orig_ws_byte = 0;
    if (do_scale) {
        orig_ws_byte = *(reinterpret_cast<uint8_t*>(self) + 0x09);
        *(reinterpret_cast<uint8_t*>(self) + 0x09) = 0;
    }

    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = orig_fn(self, gif_buf, gs_env, flag);
    --g_HudWidgetScaleDepth;

    if (do_scale) {
        *(reinterpret_cast<uint8_t*>(self) + 0x09) = orig_ws_byte;
        if (gif_buf != nullptr && gif_end > gif_buf) {
            float ws_4x3_to_actual_x = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
            const int32_t dx           = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(self) + 0xE0);
            const float   anchor_subpx = 30280.0f + static_cast<float>(dx * 16);
            ScaleHudGifPacketsX(gif_buf, gif_end, anchor_subpx, anchor_subpx, ws_4x3_to_actual_x, false);
        }
    }
    return gif_end;
}

// Virtual Hook: Inactive Wingman Command Submenu (Widget6_COVER, vtable + 0x40)
//   Places the inactive Wingman Command submenu directly above the minimap (left-aligned with the minimap).
static uint8_t* __fastcall Hook_HudCoverWidget_VirtualDraw40(void* self, uint8_t* gif_buf, void* gs_env, uint32_t flag) {
    FnHudWidgetVirtualDraw orig_fn = FindOrigVtableDraw(self, 0x40u);
    if (!orig_fn) return gif_buf;

    const uint8_t is_ws      = (g_ExeBase != 0) ? *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen)) : 0u;
    const int     aspect_opt = GetGameAspectOption();
    const bool    do_scale   = (is_ws != 0 && aspect_opt > 0 && self != nullptr);

    uint8_t orig_ws_byte = 0;
    if (do_scale) {
        orig_ws_byte = *(reinterpret_cast<uint8_t*>(self) + 0x09);
        *(reinterpret_cast<uint8_t*>(self) + 0x09) = 0;
    }

    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = orig_fn(self, gif_buf, gs_env, flag);
    --g_HudWidgetScaleDepth;

    if (do_scale) {
        *(reinterpret_cast<uint8_t*>(self) + 0x09) = orig_ws_byte;
        if (gif_buf != nullptr && gif_end > gif_buf) {
            uint16_t min_x = 0, max_x = 0;
            if (ComputeHudGifBoundsX(gif_buf, gif_end, &min_x, &max_x)) {
                float ws_4x3_to_actual_x = 0.75f;
                ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
                const float orig_left_subpx = static_cast<float>(min_x);
                ScaleHudGifPacketsX(gif_buf, gif_end, orig_left_subpx, g_CurrentRadarLeftSubpx, ws_4x3_to_actual_x, false);
            }
        }
    }
    return gif_end;
}

// Virtual Hook: SubWidget3 Weapon Range Bracket & [ SHOOT ] Prompt (w2 + 0x48, vtable + 0x40)
//   1. Clamps self[+0x34] (active weapon max lock range) to 32768.0f (matching self[+0x30] target distance clamp
//      at +0x0DBFFF) so long-range weapons like XLAA never overflow the top of the 216-px right ']' bracket.
//   2. Leaves the 1-px ']' bracket, vertical weapon range bar, and '>' chevron unscaled at exact integer GS pixels
//      (preventing 1-px horizontal separation), and only scales the trailing [ SHOOT ] prompt around screen center.
static uint8_t* __fastcall Hook_HudRangeWidget_VirtualDraw40(void* self, uint8_t* gif_buf, void* gs_env, uint32_t flag) {
    FnHudWidgetVirtualDraw orig_fn = FindOrigVtableDraw(self, 0x40u);
    if (!orig_fn) return gif_buf;

    uint8_t* self_u8 = reinterpret_cast<uint8_t*>(self);
    float orig_weapon_range = 0.0f;
    if (self_u8 != nullptr) {
        orig_weapon_range = *reinterpret_cast<const float*>(self_u8 + 0x34);
        if (orig_weapon_range > 32768.0f) {
            *reinterpret_cast<float*>(self_u8 + 0x34) = 32768.0f;
        }
    }

    const uint8_t is_ws      = (g_ExeBase != 0) ? *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen)) : 0u;
    const int     aspect_opt = GetGameAspectOption();
    const bool    do_scale   = (is_ws != 0 && aspect_opt > 0 && self_u8 != nullptr);

    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = orig_fn(self, gif_buf, gs_env, flag);
    --g_HudWidgetScaleDepth;

    if (self_u8 != nullptr && orig_weapon_range > 32768.0f) {
        *reinterpret_cast<float*>(self_u8 + 0x34) = orig_weapon_range;
    }

    if (do_scale && gif_buf != nullptr && gif_end > gif_buf) {
        // Only scale the trailing [ SHOOT ] box & text when active (self[+0x39] != 0 && self[+0x3A] == 0):
        if (*(self_u8 + 0x08) != 0 && *(self_u8 + 0x39) != 0 && *(self_u8 + 0x3A) == 0) {
            size_t prefix_bytes = 3u * 0x60u; // 3 line sprites for ']' bracket
            if (*(self_u8 + 0x38) != 0) {
                prefix_bytes += 2u * 0x60u;   // 2 line sprites for vertical weapon range bar + top tick
            }
            if (*reinterpret_cast<const float*>(self_u8 + 0x30) != -1.0f) {
                prefix_bytes += 0x80u;        // 1 triangle-strip packet for '>' target range chevron
            }
            if (gif_buf + prefix_bytes < gif_end) {
                float ws_4x3_to_actual_x = 0.75f;
                ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
                ScaleHudGifPacketsX(gif_buf + prefix_bytes, gif_end, 32768.0f, 32768.0f, ws_4x3_to_actual_x, false);
            }
        }
    }
    return gif_end;
}

// Virtual Hook: SubWidget4 Heading Tape (vtable + 0x40, anchored at compass center X)
static uint8_t* __fastcall Hook_HudHeadingWidget_VirtualDraw40(void* self, uint8_t* gif_buf, void* gs_env, uint32_t flag) {
    FnHudWidgetVirtualDraw orig_fn = FindOrigVtableDraw(self, 0x40u);
    if (!orig_fn) return gif_buf;

    const uint8_t is_ws      = (g_ExeBase != 0) ? *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen)) : 0u;
    const int     aspect_opt = GetGameAspectOption();
    const bool    do_scale   = (is_ws != 0 && aspect_opt > 0 && self != nullptr);

    uint8_t orig_ws_byte = 0;
    if (do_scale) {
        orig_ws_byte = *(reinterpret_cast<uint8_t*>(self) + 0x09);
        *(reinterpret_cast<uint8_t*>(self) + 0x09) = 0;
    }

    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = orig_fn(self, gif_buf, gs_env, flag);
    --g_HudWidgetScaleDepth;

    if (do_scale) {
        *(reinterpret_cast<uint8_t*>(self) + 0x09) = orig_ws_byte;
        if (gif_buf != nullptr && gif_end > gif_buf) {
            float ws_4x3_to_actual_x = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
            const int32_t base_x     = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(self) + 0x10);
            const int32_t yaw_lag_x  = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(self) + 0x20);
            const float   center_sub = static_cast<float>((base_x + yaw_lag_x + 0x700) * 16);
            ScaleHudGifPacketsX(gif_buf, gif_end, center_sub, center_sub, ws_4x3_to_actual_x, false);
        }
    }
    return gif_end;
}

// Virtual Hook: Widget4 TIME (vtable + 0x48)
static uint8_t* __fastcall Hook_HudFixedWidget_VirtualDraw48(void* self, uint8_t* gif_buf, void* gs_env, uint32_t flag) {
    FnHudWidgetVirtualDraw orig_fn = FindOrigVtableDraw(self, 0x48u);
    if (!orig_fn) return gif_buf;

    const uint8_t is_ws      = (g_ExeBase != 0) ? *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen)) : 0u;
    const int     aspect_opt = GetGameAspectOption();
    const bool    do_scale   = (is_ws != 0 && aspect_opt > 0 && self != nullptr);

    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = orig_fn(self, gif_buf, gs_env, flag);
    --g_HudWidgetScaleDepth;

    if (do_scale && gif_buf != nullptr && gif_end > gif_buf) {
        uint16_t min_x = 0, max_x = 0;
        if (ComputeHudGifBoundsX(gif_buf, gif_end, &min_x, &max_x)) {
            float ws_4x3_to_actual_x = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
            const float anchor_subpx = 0.5f * (static_cast<float>(min_x) + static_cast<float>(max_x));
            ScaleHudGifPacketsX(gif_buf, gif_end, anchor_subpx, anchor_subpx, ws_4x3_to_actual_x, false);
        }
    }
    return gif_end;
}

// Virtual Hook: Radar0 / Radar1 (vtable + 0x40)
//   In 16:9, Radar is already 1:1 square (rx = 31, rw = 81 GS px).
//   In 21:9 (aspect_opt == 2), scales Radar vertices and its GS_REG_SCISSOR_1 clip box
//   around the Radar center by ws_16x9_to_actual_x (16/21) so Radar is 1:1 square in 21:9 too.
static uint8_t* __fastcall Hook_Radar_VirtualDraw40(void* self, uint8_t* gif_buf, void* gs_env, uint32_t flag) {
    FnHudWidgetVirtualDraw orig_fn = FindOrigVtableDraw(self, 0x40u);
    if (!orig_fn) return gif_buf;

    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = orig_fn(self, gif_buf, gs_env, flag);
    --g_HudWidgetScaleDepth;

    if (self != nullptr && gif_buf != nullptr && gif_end > gif_buf && g_ExeBase != 0) {
        const uint8_t is_ws      = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int     aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt == 2) {
            float ws_16x9_to_actual_x = 1.0f;
            ComputeCutsceneWidescreenScales(aspect_opt, &ws_16x9_to_actual_x, nullptr);
            const int32_t rx = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(self) + 0x20);
            const int32_t rw = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(self) + 0x28);
            const float   radar_center_subpx = (static_cast<float>(rx) + static_cast<float>(rw) * 0.5f + 1792.0f) * 16.0f;
            ScaleHudGifPacketsX(gif_buf, gif_end, radar_center_subpx, radar_center_subpx, ws_16x9_to_actual_x, true);
        }
    }
    return gif_end;
}

// Hook 22: CRadioSubFont::DrawRadioSub (+0x1A5100 in v1.0.2.1 / +0x1A5780 in v2.0.0.1)
//   Unstretches in-flight Mission Radio Subtitles (speaker name, underline, triangle pointer,
//   << >> brackets, and subtitle text) around screen center (32768.0f) in 16:9 and 21:9.
static uint8_t __fastcall Hook_RadioSubFontDraw(
    void*          self,
    void*          sub_entry,
    const int32_t* x_ptr,
    const int32_t* y_ptr,
    uint8_t**      gif_buf_ptr,
    const uint8_t* gs_env
) {
    uint8_t* gif_before = (gif_buf_ptr != nullptr) ? *gif_buf_ptr : nullptr;
    ++g_HudWidgetScaleDepth;
    uint8_t  ret        = g_OrigRadioSubFontDraw(self, sub_entry, x_ptr, y_ptr, gif_buf_ptr, gs_env);
    --g_HudWidgetScaleDepth;
    uint8_t* gif_after  = (gif_buf_ptr != nullptr) ? *gif_buf_ptr : nullptr;

    if (ret != 0 && gif_before != nullptr && gif_after > gif_before && g_ExeBase != 0) {
        const uint8_t is_ws      = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int     aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt > 0) {
            float ws_4x3_to_actual_x = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
            ScaleHudGifPacketsX(gif_before, gif_after, 32768.0f, 32768.0f, ws_4x3_to_actual_x, false);
        }
    }
    return ret;
}

// Hook 23: CHudManager::Draw (+0x0AE510 in v1.0.2.1 / +0x0AE550 in v2.0.0.1)
static uint8_t* __fastcall Hook_CHudManagerDraw(uint8_t* hud_mgr, uint8_t* gif_buf, uint8_t* gs_env, void* a4, void* a5) {
    if (hud_mgr != nullptr) {
        __try {
            uint8_t* r0 = *reinterpret_cast<uint8_t**>(hud_mgr + 0x00);
            uint8_t* r1 = *reinterpret_cast<uint8_t**>(hud_mgr + 0x08);
            uint8_t* w2 = *reinterpret_cast<uint8_t**>(hud_mgr + 0x10);
            uint8_t* w3 = *reinterpret_cast<uint8_t**>(hud_mgr + 0x18); // SCORE
            uint8_t* w4 = *reinterpret_cast<uint8_t**>(hud_mgr + 0x20); // TIME
            uint8_t* w5 = *reinterpret_cast<uint8_t**>(hud_mgr + 0x28); // Weapons + Aircraft/Missiles
            uint8_t* w6 = *reinterpret_cast<uint8_t**>(hud_mgr + 0x30); // Inactive Wingman Command (COVER + D-pad)
            uint8_t* w7 = *reinterpret_cast<uint8_t**>(hud_mgr + 0x38); // Destroyed-Target Score Breakdown List (under SCORE)

            EnsureVtableHookInstalled(r0, 0x40u, &Hook_Radar_VirtualDraw40);
            EnsureVtableHookInstalled(r1, 0x40u, &Hook_Radar_VirtualDraw40);
            EnsureVtableHookInstalled(w3, 0x40u, &Hook_HudFixedWidget_VirtualDraw40);
            EnsureVtableHookInstalled(w4, 0x48u, &Hook_HudFixedWidget_VirtualDraw48);
            EnsureVtableHookInstalled(w5, 0x40u, &Hook_HudFixedWidget_VirtualDraw40);
            EnsureVtableHookInstalled(w6, 0x40u, &Hook_HudCoverWidget_VirtualDraw40);
            EnsureVtableHookInstalled(w7, 0x40u, &Hook_HudScorePopupWidget_VirtualDraw40);

            if (w2 != nullptr) {
                EnsureVtableHookInstalled(*reinterpret_cast<void**>(w2 + 0x30), 0x40u, &Hook_HudFixedWidget_VirtualDraw40);   // SPEED
                EnsureVtableHookInstalled(*reinterpret_cast<void**>(w2 + 0x38), 0x40u, &Hook_HudFixedWidget_VirtualDraw40);   // ALT
                // Note: w2 + 0x40 (Climb bracket '[') is intentionally left unhooked so its 1-px vertical lines stay crisp.
                EnsureVtableHookInstalled(*reinterpret_cast<void**>(w2 + 0x48), 0x40u, &Hook_HudRangeWidget_VirtualDraw40);   // Range bracket ']' & [ SHOOT ]
                EnsureVtableHookInstalled(*reinterpret_cast<void**>(w2 + 0x50), 0x40u, &Hook_HudHeadingWidget_VirtualDraw40); // Heading
            }

            // Adjust widescreen HUD layout in singleplayer (hud_mgr[+0x3CB] == 0):
            // 1. Shift SCORE + Score Breakdown List (left) and TIME (right) closer to the screen corners/edges
            //    instead of their stock +28/-28 inward 4:3 offsets.
            // 2. Shift SPEED/Climb (left) and ALT/Range (right) outward (-24 / +24) ONLY in 3rd-person view
            //    (sub_speed[+0x34] == 0); in 1st-person HUD / cockpit views, preserve stock (+28 / -28)
            //    next to the vertical speed/altitude tape ladders and pitch-ladder brackets.
            // 3. Place the inactive Wingman Command submenu (Widget6_COVER) directly above the minimap (y = 248..282).
            const uint8_t is_split  = *(hud_mgr + 0x3CB);
            const uint8_t is_ws     = (g_ExeBase != 0) ? *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen)) : 0u;
            const int     aspect_opt = GetGameAspectOption();
            if (is_split == 0) {
                if (is_ws != 0 && aspect_opt > 0) {
                    float rx = 31.0f;
                    float rw = 81.0f;
                    if (r0 != nullptr) {
                        rx = static_cast<float>(*reinterpret_cast<const int32_t*>(r0 + 0x20));
                        rw = static_cast<float>(*reinterpret_cast<const int32_t*>(r0 + 0x28));
                    }
                    if (aspect_opt == 2) {
                        float ws_16x9_to_actual_x = 1.0f;
                        ComputeCutsceneWidescreenScales(aspect_opt, &ws_16x9_to_actual_x, nullptr);
                        const float radar_center_px = rx + rw * 0.5f;
                        const float radar_left_px   = radar_center_px - (rw * 0.5f) * ws_16x9_to_actual_x;
                        g_CurrentRadarLeftSubpx     = (radar_left_px + 1792.0f) * 16.0f;
                    } else {
                        g_CurrentRadarLeftSubpx     = (rx + 1792.0f) * 16.0f;
                    }

                    if (w3 != nullptr) {
                        *reinterpret_cast<int32_t*>(w3 + 0x20) = -24; // SCORE X closer to left edge (stock widescreen was +28)
                        *reinterpret_cast<int32_t*>(w3 + 0x24) = -10; // SCORE Y closer to top edge
                    }
                    if (w7 != nullptr) {
                        *reinterpret_cast<int32_t*>(w7 + 0xE0) = -24; // Destroyed-target score popup X (matches SCORE)
                        *reinterpret_cast<int32_t*>(w7 + 0xE4) = -10; // Destroyed-target score popup Y (matches SCORE)
                    }
                    if (w4 != nullptr) {
                        *reinterpret_cast<int32_t*>(w4 + 0x20) = +24; // TIME X closer to right edge (stock widescreen was -28)
                        *reinterpret_cast<int32_t*>(w4 + 0x24) = -10; // TIME Y closer to top edge
                    }
                    if (w2 != nullptr) {
                        uint8_t* sub_speed = *reinterpret_cast<uint8_t**>(w2 + 0x30);
                        uint8_t* sub_alt   = *reinterpret_cast<uint8_t**>(w2 + 0x38);
                        uint8_t* sub_climb = *reinterpret_cast<uint8_t**>(w2 + 0x40);
                        uint8_t* sub_range = *reinterpret_cast<uint8_t**>(w2 + 0x48);
                        const bool is_third_person = (sub_speed != nullptr && *(sub_speed + 0x34) == 0);
                        const int32_t left_x  = is_third_person ? -24 : +28;
                        const int32_t right_x = is_third_person ? +24 : -28;
                        if (sub_speed != nullptr) *reinterpret_cast<int32_t*>(sub_speed + 0x28) = left_x;
                        if (sub_climb != nullptr) *reinterpret_cast<int32_t*>(sub_climb + 0x28) = left_x;
                        if (sub_alt   != nullptr) *reinterpret_cast<int32_t*>(sub_alt   + 0x28) = right_x;
                        if (sub_range != nullptr) *reinterpret_cast<int32_t*>(sub_range + 0x28) = right_x;
                    }
                    if (w6 != nullptr) {
                        *reinterpret_cast<int32_t*>(w6 + 0x20) = 0;
                        *reinterpret_cast<int32_t*>(w6 + 0x24) = -138; // Place COVER + D-pad directly above the minimap (y = 248..282)
                    }
                } else {
                    if (w3 != nullptr) {
                        *reinterpret_cast<int32_t*>(w3 + 0x20) = 0;
                        *reinterpret_cast<int32_t*>(w3 + 0x24) = 0;
                    }
                    if (w7 != nullptr) {
                        *reinterpret_cast<int32_t*>(w7 + 0xE0) = 0;
                        *reinterpret_cast<int32_t*>(w7 + 0xE4) = 0;
                    }
                    if (w4 != nullptr) {
                        *reinterpret_cast<int32_t*>(w4 + 0x20) = 0;
                        *reinterpret_cast<int32_t*>(w4 + 0x24) = 0;
                    }
                    if (w2 != nullptr) {
                        uint8_t* sub_speed = *reinterpret_cast<uint8_t**>(w2 + 0x30);
                        uint8_t* sub_alt   = *reinterpret_cast<uint8_t**>(w2 + 0x38);
                        uint8_t* sub_climb = *reinterpret_cast<uint8_t**>(w2 + 0x40);
                        uint8_t* sub_range = *reinterpret_cast<uint8_t**>(w2 + 0x48);
                        if (sub_speed != nullptr) *reinterpret_cast<int32_t*>(sub_speed + 0x28) = 0;
                        if (sub_climb != nullptr) *reinterpret_cast<int32_t*>(sub_climb + 0x28) = 0;
                        if (sub_alt   != nullptr) *reinterpret_cast<int32_t*>(sub_alt   + 0x28) = 0;
                        if (sub_range != nullptr) *reinterpret_cast<int32_t*>(sub_range + 0x28) = 0;
                    }
                    if (w6 != nullptr) {
                        *reinterpret_cast<int32_t*>(w6 + 0x20) = 0;
                        *reinterpret_cast<int32_t*>(w6 + 0x24) = 0;
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    const bool prev_inside = g_InsideCHudManagerDraw;
    g_InsideCHudManagerDraw = true;
    uint8_t* ret = g_OrigCHudManagerDraw(hud_mgr, gif_buf, gs_env, a4, a5);
    g_InsideCHudManagerDraw = prev_inside;
    return ret;
}

// Hook 24: CPauseMenu::Draw (+0x0ED0E0 in v1.0.2.1 / +0x0ED120 in v2.0.0.1)
//   Pause menu and its right-hand tactical map already scale by 0.75f in 16:9;
//   in 21:9 (aspect_opt == 2), scale vertices and tactical map scissor around screen center by 16/21.
static uint8_t* __fastcall Hook_PauseMenuDraw(void* self, uint8_t* gif_buf, void* gs_env, void* a4, uintptr_t a5) {
    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = g_OrigPauseMenuDraw(self, gif_buf, gs_env, a4, a5);
    --g_HudWidgetScaleDepth;

    if (gif_buf != nullptr && gif_end > gif_buf && g_ExeBase != 0) {
        const uint8_t is_ws      = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int     aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt == 2) {
            float ws_16x9_to_actual_x = 1.0f;
            ComputeCutsceneWidescreenScales(aspect_opt, &ws_16x9_to_actual_x, nullptr);
            ScaleHudGifPacketsX(gif_buf, gif_end, 32768.0f, 32768.0f, ws_16x9_to_actual_x, true);
        }
    }
    return gif_end;
}

// Hook 31: CPauseSubMenu::Draw (+0x0EFDC0 in v1.0.2.1 / +0x0EFE00 in v2.0.0.1)
static uint8_t* __fastcall Hook_PauseSubMenuDraw(void* self, uint8_t* gif_buf, void* gs_env) {
    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = g_OrigPauseSubMenuDraw(self, gif_buf, gs_env);
    --g_HudWidgetScaleDepth;

    if (gif_buf != nullptr && gif_end > gif_buf && g_ExeBase != 0) {
        const uint8_t is_ws      = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int     aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt == 2) {
            float ws_16x9_to_actual_x = 1.0f;
            ComputeCutsceneWidescreenScales(aspect_opt, &ws_16x9_to_actual_x, nullptr);
            ScaleHudGifPacketsX(gif_buf, gif_end, 32768.0f, 32768.0f, ws_16x9_to_actual_x, true);
        }
    }
    return gif_end;
}

static thread_local int32_t g_LastTargetBoxY0        = 0;
static thread_local bool    g_LastTargetHasPilotLine = false;

// Hook 25: CHudManager::DrawTargetTextLeft (+0x0BAF60 in v1.0.2.1 / +0x0BAFA0 in v2.0.0.1)
//   Draws left-aligned target marker labels on the right side of the target box:
//     - Line 1 (Target Type, e.g. "B-52", caller ret RVA 0x0B6D07): stock anchor_y = Y0 - 0x90
//     - Line 2 (Ace Pilot Name, caller ret RVA 0x0B6D7E):           stock anchor_y = Y0 + 0x10
//     - Line 3 (Target Distance, e.g. "18200", caller ret RVA 0x0B6F5E): stock anchor_y = Y1 + 0x10
//   In widescreen, brings Line 1 down (Y0 - 0x30) and Line 3 up (Y0 + 0x70 when no pilot name, or Y0 + 0xD0
//   when pilot name is present) so the labels sit compactly alongside the target box without a large vertical gap.
static uint8_t* __fastcall Hook_HudDrawTextLeft(
    void*          font_mgr,
    uint8_t*       gif_buf,
    uintptr_t      a3,
    int32_t        anchor_x,
    int32_t        anchor_y,
    int32_t        spacing,
    void*          tex_desc,
    const uint8_t* rgba_ptr,
    const char*    str
) {
    const bool in_target_hud = (g_InsideCHudManagerDraw && g_HudWidgetScaleDepth == 0 && g_ExeBase != 0);
    bool do_scale = false;
    int  aspect_opt = 0;
    if (in_target_hud) {
        const uint8_t is_ws = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt > 0) {
            do_scale = true;
            const uintptr_t ret_rva = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_ExeBase;
            if (ret_rva == AczRva(0x0B6D07)) {
                // Line 1: Target Type ("B-52", "UNKNOWN", "ALLY") — stock anchor_y = Y0 - 0x90
                const int32_t box_y0 = anchor_y + 0x90;
                g_LastTargetBoxY0        = box_y0;
                g_LastTargetHasPilotLine = false;
                anchor_y                 = box_y0 - 0x30;
            } else if (ret_rva == AczRva(0x0B6D7E)) {
                // Line 2: Ace / Pilot Name — stock anchor_y = Y0 + 0x10
                const int32_t box_y0 = anchor_y - 0x10;
                g_LastTargetBoxY0        = box_y0;
                g_LastTargetHasPilotLine = true;
                anchor_y                 = box_y0 + 0x50;
            } else if (ret_rva == AczRva(0x0B6F5E)) {
                // Line 3: Target Distance ("18200") — stock anchor_y = Y1 + 0x10
                if (g_LastTargetBoxY0 != 0) {
                    anchor_y = g_LastTargetHasPilotLine ? (g_LastTargetBoxY0 + 0xD0) : (g_LastTargetBoxY0 + 0x70);
                } else {
                    anchor_y -= 0x60;
                }
            }
        }
    }

    uint8_t* gif_end = g_OrigHudDrawTextLeft(font_mgr, gif_buf, a3, anchor_x, anchor_y, spacing, tex_desc, rgba_ptr, str);
    if (do_scale && gif_buf != nullptr && gif_end > gif_buf) {
        float ws_4x3_to_actual_x = 0.75f;
        ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
        const float anchor_subpx = static_cast<float>(anchor_x);
        ScaleHudGifPacketsX(gif_buf, gif_end, anchor_subpx, anchor_subpx, ws_4x3_to_actual_x, false);
    }
    return gif_end;
}

// Hook 26: CHudManager::DrawTargetTextRight (+0x0BB1C0 in v1.0.2.1 / +0x0BB200 in v2.0.0.1)
//   Draws right-aligned "FRND" and "TGT" labels anchored at anchor_x = box_left_x - 0x20, stock anchor_y = Y0 - 0x80.
//   In widescreen, shifts anchor_y down by +0x50 (to Y0 - 0x30) so "TGT"/"FRND" aligns horizontally with Line 1 ("B-52").
static uint8_t* __fastcall Hook_HudDrawTextRight(
    void*          font_mgr,
    uint8_t*       gif_buf,
    uintptr_t      a3,
    int32_t        anchor_x,
    int32_t        anchor_y,
    int32_t        spacing,
    void*          tex_desc,
    const uint8_t* rgba_ptr,
    const char*    str
) {
    const bool in_target_hud = (g_InsideCHudManagerDraw && g_HudWidgetScaleDepth == 0 && g_ExeBase != 0);
    bool do_scale = false;
    int  aspect_opt = 0;
    if (in_target_hud) {
        const uint8_t is_ws = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt > 0) {
            do_scale = true;
            const uintptr_t ret_rva = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_ExeBase;
            if (ret_rva == AczRva(0x0BA5CC) || ret_rva == AczRva(0x0BA964)) {
                // "TGT" / "FRND" — stock anchor_y = Y0 - 0x80 -> shift to Y0 - 0x30 to align with Line 1 ("B-52")
                anchor_y += 0x50;
            }
        }
    }

    uint8_t* gif_end = g_OrigHudDrawTextRight(font_mgr, gif_buf, a3, anchor_x, anchor_y, spacing, tex_desc, rgba_ptr, str);
    if (do_scale && gif_buf != nullptr && gif_end > gif_buf) {
        float ws_4x3_to_actual_x = 0.75f;
        ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
        const float anchor_subpx = static_cast<float>(anchor_x);
        ScaleHudGifPacketsX(gif_buf, gif_end, anchor_subpx, anchor_subpx, ws_4x3_to_actual_x, false);
    }
    return gif_end;
}

// Hook 27: CHudManager::DrawStatusAlerts (+0x0B1B60 in v1.0.2.1 / +0x0B1BA0 in v2.0.0.1)
static uint8_t* __fastcall Hook_HudStatusAlerts(void* hud_mgr, uint8_t* gif_buf, void* gs_env) {
    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = g_OrigHudStatusAlerts(hud_mgr, gif_buf, gs_env);
    --g_HudWidgetScaleDepth;

    if (gif_buf != nullptr && gif_end > gif_buf && g_ExeBase != 0) {
        const uint8_t is_ws      = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int     aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt > 0) {
            float ws_4x3_to_actual_x = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
            ScaleHudGifPacketsX(gif_buf, gif_end, 32768.0f, 32768.0f, ws_4x3_to_actual_x, false);
        }
    }
    return gif_end;
}

// Hook 28: CHudManager::DrawBottomMessage (+0x0B2950 in v1.0.2.1 / +0x0B2990 in v2.0.0.1)
static uint8_t* __fastcall Hook_HudBottomMessage(void* hud_mgr, uint8_t* gif_buf, void* gs_env) {
    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = g_OrigHudBottomMessage(hud_mgr, gif_buf, gs_env);
    --g_HudWidgetScaleDepth;

    if (gif_buf != nullptr && gif_end > gif_buf && g_ExeBase != 0) {
        const uint8_t is_ws      = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int     aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt > 0) {
            float ws_4x3_to_actual_x = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
            ScaleHudGifPacketsX(gif_buf, gif_end, 32768.0f, 32768.0f, ws_4x3_to_actual_x, false);
        }
    }
    return gif_end;
}

// Hook 29: CHudManager::DrawCountdown (+0x0B1680 in v1.0.2.1 / +0x0B16C0 in v2.0.0.1)
static uint8_t* __fastcall Hook_HudCountdown(void* hud_mgr, uint8_t* gif_buf) {
    ++g_HudWidgetScaleDepth;
    uint8_t* gif_end = g_OrigHudCountdown(hud_mgr, gif_buf);
    --g_HudWidgetScaleDepth;

    if (gif_buf != nullptr && gif_end > gif_buf && g_ExeBase != 0) {
        const uint8_t is_ws      = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int     aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt > 0) {
            float ws_4x3_to_actual_x = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
            ScaleHudGifPacketsX(gif_buf, gif_end, 32768.0f, 32768.0f, ws_4x3_to_actual_x, false);
        }
    }
    return gif_end;
}

// Hook 30: CHudManager::DrawTextFloat (+0x0BB3E0 in v1.0.2.1 / +0x0BB420 in v2.0.0.1)
//   Unstretches standalone HUD float-positioned text & degree numbers (1P/3P Pitch Ladder degree numbers)
//   when called inside CHudManager::Draw outside already-scaled HUD widgets.
static uint8_t* __fastcall Hook_HudDrawTextFloat(
    void*          font_mgr,
    uint8_t*       gif_buf,
    uintptr_t      a3,
    float          x_f,
    float          y_f,
    int32_t        spacing,
    void*          tex_desc,
    const uint8_t* rgba_ptr,
    const char*    str
) {
    uint8_t* gif_end = g_OrigHudDrawTextFloat(font_mgr, gif_buf, a3, x_f, y_f, spacing, tex_desc, rgba_ptr, str);
    if (g_InsideCHudManagerDraw && g_HudWidgetScaleDepth == 0 && gif_buf != nullptr && gif_end > gif_buf && g_ExeBase != 0) {
        const uint8_t is_ws      = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int     aspect_opt = GetGameAspectOption();
        if (is_ws != 0 && aspect_opt > 0) {
            uint16_t min_x = 0, max_x = 0;
            if (ComputeHudGifBoundsX(gif_buf, gif_end, &min_x, &max_x)) {
                float ws_4x3_to_actual_x = 0.75f;
                ComputeCutsceneWidescreenScales(aspect_opt, nullptr, &ws_4x3_to_actual_x);
                const float anchor_subpx = 0.5f * (static_cast<float>(min_x) + static_cast<float>(max_x));
                ScaleHudGifPacketsX(gif_buf, gif_end, anchor_subpx, anchor_subpx, ws_4x3_to_actual_x, false);
            }
        }
    }
    return gif_end;
}

static uint8_t __fastcall Hook_CutsceneSubFontDrawEmblem(
    uint8_t*       this_ptr,
    void*          gim_desc,
    uint8_t**      gif_buf_ptr,
    const uint8_t* gs_env,
    float          cur_time
) {
    uint8_t* gif_before = (gif_buf_ptr != nullptr) ? *gif_buf_ptr : nullptr;
    uint8_t  ret        = g_OrigCutsceneSubFontDrawEmblem(this_ptr, gim_desc, gif_buf_ptr, gs_env, cur_time);
    uint8_t* gif_after  = (gif_buf_ptr != nullptr) ? *gif_buf_ptr : nullptr;

    if (ret != 0 && gif_before != nullptr && gif_after > gif_before && this_ptr != nullptr && gs_env != nullptr && g_ExeBase != 0) {
        const uint8_t is_ws = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const int aspect_opt = GetGameAspectOption();
        // Squadron Emblem (align_mode 6) is already fitted for 16:9; in 21:9 (aspect_opt == 2),
        // map its X coordinates into the 16:9 frame so it stays circular and aligned with GALM / Ustio Air Force:
        if (is_ws != 0 && aspect_opt == 2) {
            float ws_16x9_to_actual_x = 1.0f;
            float ws_4x3_to_actual_x  = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, &ws_16x9_to_actual_x, &ws_4x3_to_actual_x);

            const float offset_x     = *reinterpret_cast<const float*>(gs_env + 0x190);
            const float center_subpx = offset_x * 16.0f;
            ScaleCutsceneGifPacketsX(gif_before, gif_after, center_subpx, center_subpx, ws_16x9_to_actual_x);
        }
    }
    return ret;
}

static uint8_t __fastcall Hook_CutsceneFontDraw(
    uint8_t*       this_ptr,
    int32_t        str_idx,
    uint8_t**      gif_buf_ptr,
    const uint8_t* gs_env,
    float          cur_time
) {
    uint8_t* gif_before = (gif_buf_ptr != nullptr) ? *gif_buf_ptr : nullptr;
    uint8_t  ret        = g_OrigCutsceneFontDraw(this_ptr, str_idx, gif_buf_ptr, gs_env, cur_time);
    uint8_t* gif_after  = (gif_buf_ptr != nullptr) ? *gif_buf_ptr : nullptr;

    if (ret != 0 && gif_before != nullptr && gif_after > gif_before && this_ptr != nullptr && gs_env != nullptr && g_ExeBase != 0) {
        const uint8_t is_ws     = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_CSysDataIsWidescreen));
        const uint8_t is_fmv_ws = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_IsFmvWidescreen));
        const int aspect_opt    = GetGameAspectOption();
        if ((is_ws != 0 || is_fmv_ws != 0) && aspect_opt > 0) {
            float ws_16x9_to_actual_x = 1.0f;
            float ws_4x3_to_actual_x  = 0.75f;
            ComputeCutsceneWidescreenScales(aspect_opt, &ws_16x9_to_actual_x, &ws_4x3_to_actual_x);

            const float   offset_x    = *reinterpret_cast<const float*>(gs_env + 0x190);
            const float   center_subpx = offset_x * 16.0f;
            const uint8_t center_flag = this_ptr[0x87];

            float orig_anchor_subpx = center_subpx;
            float new_anchor_subpx  = center_subpx;
            float span_scale_x      = ws_4x3_to_actual_x;

            if (center_flag == 0) {
                const uint8_t is_movie_sub = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_IsMovieCutsceneFontActive));
                const float   out_scale_x  = (is_ws != 0 || is_movie_sub != 0)
                    ? 0.75f
                    : *reinterpret_cast<const float*>(gs_env + 0x198);
                const int16_t pos_x       = *reinterpret_cast<const int16_t*>(this_ptr + 0x7C);
                const int16_t env_w       = *reinterpret_cast<const int16_t*>(gs_env + 0x28);
                const int32_t start_x_rel = static_cast<int32_t>(static_cast<float>(pos_x) * out_scale_x - static_cast<float>(env_w / 2));
                orig_anchor_subpx         = static_cast<float>(static_cast<int32_t>(offset_x + static_cast<float>(start_x_rel)) << 4);
                new_anchor_subpx          = center_subpx + (orig_anchor_subpx - center_subpx) * ws_16x9_to_actual_x;
            }

            ScaleCutsceneGifPacketsX(gif_before, gif_after, orig_anchor_subpx, new_anchor_subpx, span_scale_x);
        }
    }
    return ret;
}


// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Engine 21:9 Aspect Ratio Patches (SWG.option_setAspect(2) & Widescreen Checks)
//   1. RVA 0x189C3A (OptionSetAspectImpl): maps edx = 0 -> 12 (4:3), 1 -> 16 (16:9), 2 -> 21 (21:9)
//      so D3D12 viewport & window width = height * 21 / 9 when 21:9 is selected.
//   2. RVA 0x1C2D2D (SaveLoadPreserveAspect): NOPs out the 6-byte overwrite of ScreenSizeOption
//      after save-file deserialization so saved 21:9 (2) is preserved across game restarts.
//   3. RVA 0x16E146, 0x17F6E0, 0x17F704, 0x17F7C0, 0x181CDB, 0x193F41: changes ScreenSizeOption == 1
//      checks to ScreenSizeOption != 0 so both 16:9 (1) and 21:9 (2) activate widescreen rendering.
// ----------------------------------------------------------------------------
static void ApplyEngine21x9AspectPatches() {
    if (!g_ExeBase) return;

    auto patch_bytes = [](uintptr_t rva, const uint8_t* new_bytes, size_t len) -> bool {
        uint8_t* addr = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(rva));
        DWORD old_prot = 0;
        if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &old_prot)) return false;
        std::memcpy(addr, new_bytes, len);
        VirtualProtect(addr, len, old_prot, &old_prot);
        FlushInstructionCache(GetCurrentProcess(), addr, len);
        return true;
    };

    // 1. RVA 0x189C3A (19 bytes): mov [rcx+0x24], edx; lea ecx, [rdx*4+12]; cmp edx, 2; sete al; add cl, al; nop
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x189C3A));
        if ((p[0] == 0x83 && p[1] == 0xFA && p[2] == 0x01) || (p[0] == 0xE9)) {
            static const uint8_t kSetAspect21x9[19] = {
                0x89, 0x51, 0x24,                               // mov [rcx+0x24], edx
                0x8D, 0x0C, 0x95, 0x0C, 0x00, 0x00, 0x00,       // lea ecx, [rdx*4 + 12]
                0x83, 0xFA, 0x02,                               // cmp edx, 2
                0x0F, 0x94, 0xC0,                               // sete al
                0x00, 0xC1,                                     // add cl, al
                0x90                                            // nop
            };
            patch_bytes(0x189C3A, kSetAspect21x9, sizeof(kSetAspect21x9));
        }
    }

    // 2. RVA 0x1C2D2D (6 bytes): NOP out mov [ScreenSizeOption], eax after save load so saved 21:9 (2) is preserved
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x1C2D2D));
        if (p[-3] == 0x0F && p[-2] == 0x94 && p[-1] == 0xC0 && p[0] == 0x89 && p[1] == 0x05) {
            static const uint8_t kNop6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
            patch_bytes(0x1C2D2D, kNop6, sizeof(kNop6));
        }
    }

    // 3. RVA 0x16E146: cmp [ScreenSizeOption], 1; sete bl -> cmp [ScreenSizeOption], 0; setne bl
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x16E146));
        if (p[0] == 0x83 && p[1] == 0x3D && p[6] == 0x01 && p[7] == 0x0F && p[8] == 0x94 && p[9] == 0xC3) {
            DWORD old_prot = 0;
            if (VirtualProtect(p + 6, 3, PAGE_EXECUTE_READWRITE, &old_prot)) {
                p[6] = 0x00;
                p[8] = 0x95;
                VirtualProtect(p + 6, 3, old_prot, &old_prot);
                FlushInstructionCache(GetCurrentProcess(), p + 6, 3);
            }
        }
    }

    // 4. RVA 0x17F6E0: mov eax, 1; vmovd xmm1, eax; vpcmpeqd xmm3, xmm0, xmm1 -> mov eax, 0; ...; vpcmpgtd
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x17F6E0));
        if (p[0] == 0xB8 && p[1] == 0x01 && p[10] == 0xF9 && p[11] == 0x76 && p[12] == 0xD9) {
            DWORD old_prot = 0;
            if (VirtualProtect(p, 13, PAGE_EXECUTE_READWRITE, &old_prot)) {
                p[1]  = 0x00;
                p[11] = 0x66;
                VirtualProtect(p, 13, old_prot, &old_prot);
                FlushInstructionCache(GetCurrentProcess(), p, 13);
            }
        }
    }

    // 5. RVA 0x17F704, 0x17F7C0, 0x193F41: cmp [ScreenSizeOption], 1; jne -> cmp [ScreenSizeOption], 0; je
    for (uintptr_t rva : { static_cast<uintptr_t>(0x17F704), static_cast<uintptr_t>(0x17F7C0), static_cast<uintptr_t>(0x193F41) }) {
        uint8_t* p = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(rva));
        if (p[0] == 0x83 && p[1] == 0x3D && p[6] == 0x01 && p[7] == 0x75) {
            DWORD old_prot = 0;
            if (VirtualProtect(p + 6, 2, PAGE_EXECUTE_READWRITE, &old_prot)) {
                p[6] = 0x00;
                p[7] = 0x74;
                VirtualProtect(p + 6, 2, old_prot, &old_prot);
                FlushInstructionCache(GetCurrentProcess(), p + 6, 2);
            }
        }
    }

    // 6. RVA 0x181CDB (9 bytes): vmovd xmm1, r15d; vpcmpeqd xmm2, xmm0, xmm1 -> vpcmpeqd xmm2, xmm2, xmm2; nop x5
    //    Makes xmm2 unconditionally all-1s across 4:3, 16:9, and 21:9 so xmm1 (0x181CE4 = ScaleX)
    //    and xmm4 (0x181CAE = ScaleY) are 100% independent!
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x181CDB));
        if ((p[0] == 0xC4 && p[1] == 0xC1 && p[2] == 0x79 && p[3] == 0x6E && p[4] == 0xCF) ||
            (p[0] == 0xC5 && p[1] == 0xF9 && p[2] == 0xEF && p[3] == 0xC9)) {
            static const uint8_t kWsCheck5[9] = { 0xC5, 0xE9, 0x76, 0xD2, 0x90, 0x90, 0x90, 0x90, 0x90 };
            patch_bytes(0x181CDB, kWsCheck5, sizeof(kWsCheck5));
        }
    }

    // 7. Dynamic 4:3 Pillarbox Scissor Patches:
    //    RVA 0x194410: vmulss xmm0, xmm1, [0x1402d2240] (0.875f) -> redirect disp32 to RVA_PillarboxDataCave + 0x04
    //    RVA 0x194418: vmulss xmm1, xmm1, [0x1402d2018] (0.125f) -> redirect disp32 to RVA_PillarboxDataCave + 0x00
    //    RVA 0x1944C9: vmulss xmm1, xmm0, [0x1402d2018] (0.125f) -> redirect disp32 to RVA_PillarboxDataCave + 0x00
    auto redirect_disp32 = [](uintptr_t ins_rva, uintptr_t target_rva) {
        uint8_t* ins_ptr = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(ins_rva));
        uint8_t* next_rip = ins_ptr + 8;
        uint8_t* target_ptr = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(target_rva));
        int32_t disp32 = static_cast<int32_t>(target_ptr - next_rip);

        DWORD old_prot = 0;
        if (VirtualProtect(ins_ptr + 4, 4, PAGE_EXECUTE_READWRITE, &old_prot)) {
            *reinterpret_cast<int32_t*>(ins_ptr + 4) = disp32;
            VirtualProtect(ins_ptr + 4, 4, old_prot, &old_prot);
            FlushInstructionCache(GetCurrentProcess(), ins_ptr, 8);
        }
    };

    float* pLeft = reinterpret_cast<float*>(g_ExeBase + AczRva(RVA_PillarboxDataCave));
    DWORD old_data_prot = 0;
    if (VirtualProtect(pLeft, 32, PAGE_READWRITE, &old_data_prot)) {
        pLeft[0] = 0.125f;    // Initial default: stock scissor left fraction
        pLeft[1] = 0.875f;    // Initial default: stock scissor right fraction
        pLeft[2] = 28672.0f;  // Initial default: stock spider center X (28672.0f = 1792.0 * 16.0)
        pLeft[3] = 1900.0f;   // Initial default: stock AssaultRecord 3D miniature aircraft center X (2048.0 - 148.0)
        pLeft[4] = 2.0f;      // Initial default: video ortho width  (+2.0f / scale_x at +0x10)
        pLeft[5] = -2.0f;     // Initial default: video ortho height (-2.0f / scale_y at +0x14)
        pLeft[6] = 512.0f;    // Initial default: MissionTitle virtual width (512.0f at +0x18)
        pLeft[7] = 512.0f;    // Initial default: LoadingScreen virtual width (512.0f at +0x1C)
    }

    redirect_disp32(RVA_ScissorRightMul,   RVA_PillarboxDataCave + 0x04);
    redirect_disp32(RVA_ScissorLeftMul1,   RVA_PillarboxDataCave + 0x00);
    redirect_disp32(RVA_ScissorLeftMul2,   RVA_PillarboxDataCave + 0x00);
    redirect_disp32(RVA_SpiderCenterXLoad, RVA_PillarboxDataCave + 0x08);

    // 7b. Redirect AssaultRecordModel::Update 3D miniature aircraft X center load
    //     RVA 0x149FAB (6 bytes): 8B 86 0C 13 00 00 (mov eax, [rsi+0x130C])
    //     -> 8B 05 <disp32> (mov eax, [rip + RVA_PillarboxDataCave + 0x0C])
    {
        uint8_t* ins_ptr = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(RVA_AssaultModelCenterXLoad));
        if (ins_ptr[0] == 0x8B && (ins_ptr[1] == 0x86 || ins_ptr[1] == 0x05)) {
            uint8_t* next_rip = ins_ptr + 6;
            uint8_t* target_ptr = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(RVA_PillarboxDataCave + 0x0C));
            int32_t disp32 = static_cast<int32_t>(target_ptr - next_rip);
            DWORD old_prot = 0;
            if (VirtualProtect(ins_ptr, 6, PAGE_EXECUTE_READWRITE, &old_prot)) {
                ins_ptr[0] = 0x8B;
                ins_ptr[1] = 0x05;
                *reinterpret_cast<int32_t*>(ins_ptr + 2) = disp32;
                VirtualProtect(ins_ptr, 6, old_prot, &old_prot);
                FlushInstructionCache(GetCurrentProcess(), ins_ptr, 6);
            }
        }
    }

    // 8. RVA 0x181E47 (OrthoW = +2.0f / scale_x) & 0x181E31 (OrthoH = -2.0f / scale_y):
    //    Redirect movie quad orthographic projection loads to RVA_PillarboxDataCave + 0x10 and +0x14
    {
        auto redirect_movie_ortho = [](uintptr_t rva, uintptr_t cave_target_rva) {
            uint8_t* ins_ptr = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(rva));
            if (ins_ptr[0] == 0xC5 && ins_ptr[1] == 0xFA && ins_ptr[2] == 0x10) {
                uint8_t* next_rip = ins_ptr + 8;
                uint8_t* target_ptr = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(cave_target_rva));
                int32_t disp32 = static_cast<int32_t>(target_ptr - next_rip);
                DWORD old_prot = 0;
                if (VirtualProtect(ins_ptr + 4, 4, PAGE_EXECUTE_READWRITE, &old_prot)) {
                    *reinterpret_cast<int32_t*>(ins_ptr + 4) = disp32;
                    VirtualProtect(ins_ptr + 4, 4, old_prot, &old_prot);
                    FlushInstructionCache(GetCurrentProcess(), ins_ptr, 8);
                }
            }
        };
        redirect_movie_ortho(RVA_DrawMovieOrthoWLoad,      RVA_PillarboxDataCave + 0x10); // xmm1 = +2.0f / scale_x
        redirect_movie_ortho(RVA_DrawMovieOrthoHLoad,      RVA_PillarboxDataCave + 0x14); // xmm0 = -2.0f / scale_y
        redirect_movie_ortho(RVA_MissionTitleVirtualWLoad,  RVA_PillarboxDataCave + 0x18); // xmm0 = MissionTitle virtual_w
        redirect_movie_ortho(RVA_LoadingScreenVirtualWLoad, RVA_PillarboxDataCave + 0x1C); // xmm0 = LoadingScreen virtual_w
    }

    // 9. Expand Hangar opening fade-in black rectangle X1/X2 from 640px (0x6C00..0x9400) to 928px (0x6300..0x9D00)
    //    so it covers 100% of 16:9 (682.67px) and 21:9 (896px) without leaving narrow side gaps on Hangar entry:
    {
        uint8_t* pX1 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x15B904));
        uint8_t* pX2 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x15B90E));
        if (pX1[0] == 0x00 && pX1[1] == 0x6C && pX2[0] == 0x00 && pX2[1] == 0x94) {
            static const uint8_t kFadeX1[2] = { 0x00, 0x63 };
            static const uint8_t kFadeX2[2] = { 0x00, 0x9D };
            patch_bytes(0x15B904, kFadeX1, sizeof(kFadeX1));
            patch_bytes(0x15B90E, kFadeX2, sizeof(kFadeX2));
        }
    }

    // 10. Expand DetailMap horizontal crosshair line X1/X2 in DrawDetailMapCrosshairAndBox (0x140008660)
    //     from 4:3 screen edges (0x7000..0x9000 = 0..512) to full ultrawide span (0x6300..0x9D00 = -208..+720)
    //     so the horizontal position indicator line reaches the minimap on the right in 16:9 and 21:9:
    {
        uint8_t* pMapX1 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x00878B));
        uint8_t* pMapX2 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x00879C));
        if (pMapX1[0] == 0x00 && pMapX1[1] == 0x70 && pMapX2[0] == 0x00 && pMapX2[1] == 0x90) {
            static const uint8_t kMapX1[2] = { 0x00, 0x63 };
            static const uint8_t kMapX2[2] = { 0x00, 0x9D };
            patch_bytes(0x00878B, kMapX1, sizeof(kMapX1));
            patch_bytes(0x00879C, kMapX2, sizeof(kMapX2));
        }
    }

    // 11. Expand Post-Mission Debriefing (AVModeDebriefing::DrawScreenFade, 0x140017F10) full-screen fade quad
    //     (0x018021 X1=0x7000, 0x01803E X2=0x9000) and opening horizontal scan-bar (0x0180BA X1=0x7000, 0x0180E3 X2=0x9000)
    //     from 4:3 screen edges (0x7000..0x9000 = 0..512) to full ultrawide span (0x6300..0x9D00 = -208..+720):
    {
        uint8_t* pDebFadeX1 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x018021));
        uint8_t* pDebFadeX2 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x01803E));
        if (pDebFadeX1[0] == 0x00 && pDebFadeX1[1] == 0x70 && pDebFadeX2[0] == 0x00 && pDebFadeX2[1] == 0x90) {
            static const uint8_t kDebX1[2] = { 0x00, 0x63 };
            static const uint8_t kDebX2[2] = { 0x00, 0x9D };
            patch_bytes(0x018021, kDebX1, sizeof(kDebX1));
            patch_bytes(0x01803E, kDebX2, sizeof(kDebX2));
        }
        uint8_t* pDebBarX1 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x0180BA));
        uint8_t* pDebBarX2 = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x0180E3));
        if (pDebBarX1[0] == 0x00 && pDebBarX1[1] == 0x70 && pDebBarX2[0] == 0x00 && pDebBarX2[1] == 0x90) {
            static const uint8_t kDebX1[2] = { 0x00, 0x63 };
            static const uint8_t kDebX2[2] = { 0x00, 0x9D };
            patch_bytes(0x0180BA, kDebX1, sizeof(kDebX1));
            patch_bytes(0x0180E3, kDebX2, sizeof(kDebX2));
        }
    }

    // 12. Bypass hardcoded 16:9 radar scissor lookup table in UpdateGsFrameAndScissor (RVA 0x19451B: jne 0x1401945f3 -> jmp 0x1401945f3)
    //     When Radar0/Radar1 sets GS_REG_SCISSOR_1 with SCAY1=420 and SCAX0!=31 (such as SCAX0=40 in 21:9),
    //     the stock branch at 0x194575 fell into splitscreen index 3 (D3D12 Scissor X = 2156..2755 on the right half of the screen),
    //     clipping out 100% of the radar interior! Jumping to 0x1401945F3 uses the dynamic GS_REG_SCISSOR_1 converter for all aspects.
    {
        uint8_t* pSc = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(RVA_ScissorRadarBypass));
        if (pSc[0] == 0x0F && pSc[1] == 0x85 && pSc[2] == 0xD2 && pSc[3] == 0x00 && pSc[4] == 0x00 && pSc[5] == 0x00) {
            static const uint8_t kJmpDynamicScissor[6] = { 0xE9, 0xD3, 0x00, 0x00, 0x00, 0x90 };
            patch_bytes(RVA_ScissorRadarBypass, kJmpDynamicScissor, sizeof(kJmpDynamicScissor));
        }
    }

    UpdateDynamicPillarboxAndAspect(false);

    LogMsg("[Enhancer] Applied 21:9 aspect-ratio engine patches (OptionSetAspect=2 -> 21:9, Dynamic 4:3 Pillarbox).");
}




// ----------------------------------------------------------------------------
// Hook 33: PollDirectInputDevice (+0x21A480 in v1.0.2.1 / +0x21AB00 in v2.0.0.1)
// Full Native PS5 DualSense (054C:0CE6 / 054C:0DF2) & DualShock 4 (054C:05C4 / 09CC / 0BA0)
// DirectInput Support + Universal DirectInput POV Hat (D-Pad) Fix:
//   1. Fixes the stock InitDefaultDinputMapping bug where HatL..HatD (IDs 33..36) were
//      initialized with type = 0 (Button 33..36) instead of type = 1 (POV Hat P0L..P0D).
//   2. Translates DIJOYSTATE2 directly for Sony PlayStation controllers (VID 0x054C) so
//      generic Xbox [00000000] mappings in settings/dinput.ini never scramble Square/Cross/Circle,
//      L3/R3, Options, or Right Stick (Z/Rz), and adds full support for D-Pad (rgdwPOV[0]),
//      Touchpad Click (B14 -> Select/Radar), Create/Share (B9 -> Select/Radar), and both
//      analog (lRx/lRy) and digital (B7/B8) L2/R2 triggers across USB and Bluetooth.
// ----------------------------------------------------------------------------

// ============================================================================
// Unified Sony DualSense / DualShock 4 DirectInput Remap, HID Output Report
// Vibration (USB & Bluetooth), and Simultaneous Gamepad + Keyboard Support
// ============================================================================
struct RawDiJoyState2_Acz {
    int32_t  lX;
    int32_t  lY;
    int32_t  lZ;
    int32_t  lRx;
    int32_t  lRy;
    int32_t  lRz;
    int32_t  rglSlider[2];
    uint32_t rgdwPOV[4];
    uint8_t  rgbButtons[128];
    uint8_t  extra[112];
};

#pragma pack(push, 1)
struct AczHidCaps {
    uint16_t Usage;
    uint16_t UsagePage;
    uint16_t InputReportByteLength;
    uint16_t OutputReportByteLength;
    uint16_t FeatureReportByteLength;
    uint16_t Reserved[17];
    uint16_t NumberLinkCollectionNodes;
    uint16_t NumberInputButtonCaps;
    uint16_t NumberInputValueCaps;
    uint16_t NumberInputDataIndices;
    uint16_t NumberOutputButtonCaps;
    uint16_t NumberOutputValueCaps;
    uint16_t NumberOutputDataIndices;
    uint16_t NumberFeatureButtonCaps;
    uint16_t NumberFeatureValueCaps;
    uint16_t NumberFeatureDataIndices;
};
#pragma pack(pop)

using FnHidD_GetPreparsedData  = BOOLEAN (__stdcall*)(HANDLE HidDeviceObject, void** PreparsedData);
using FnHidD_FreePreparsedData = BOOLEAN (__stdcall*)(void* PreparsedData);
using FnHidP_GetCaps           = long    (__stdcall*)(void* PreparsedData, AczHidCaps* Capabilities);

static uint32_t ComputeSonyBtCrc32(uint8_t seed_byte, const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    auto step_byte = [&crc](uint8_t b) {
        crc ^= static_cast<uint32_t>(b);
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    };
    step_byte(seed_byte);
    for (size_t i = 0; i < len; ++i) {
        step_byte(data[i]);
    }
    return ~crc;
}

static void SendSonyHidVibrationReport(uint8_t left_motor, uint8_t right_motor) {
    static HANDLE     s_hid_handle        = INVALID_HANDLE_VALUE;
    static uint16_t   s_hid_pid           = 0;
    static uint16_t   s_out_report_len    = 0;
    static OVERLAPPED s_hid_ovl           = {};
    static HANDLE     s_hid_event         = nullptr;
    static uint64_t   s_last_enum_ms      = 0;
    static uint64_t   s_last_send_ms      = 0;
    static uint8_t    s_last_left         = 0;
    static uint8_t    s_last_right        = 0;
    static HMODULE    s_hid_dll           = nullptr;
    static FnHidD_GetPreparsedData  s_pGetPreparsed  = nullptr;
    static FnHidD_FreePreparsedData s_pFreePreparsed = nullptr;
    static FnHidP_GetCaps           s_pGetCaps       = nullptr;
    static bool       s_hid_api_resolved  = false;

    uint64_t now_ms = GetTickCount64();
    if (left_motor == s_last_left && right_motor == s_last_right) {
        if (left_motor == 0 && right_motor == 0) {
            return;
        }
        if (now_ms - s_last_send_ms < 90ULL) {
            return;
        }
    }

    if (!s_hid_api_resolved) {
        s_hid_api_resolved = true;
        s_hid_dll = LoadLibraryA("hid.dll");
        if (s_hid_dll) {
            s_pGetPreparsed  = reinterpret_cast<FnHidD_GetPreparsedData>(GetProcAddress(s_hid_dll, "HidD_GetPreparsedData"));
            s_pFreePreparsed = reinterpret_cast<FnHidD_FreePreparsedData>(GetProcAddress(s_hid_dll, "HidD_FreePreparsedData"));
            s_pGetCaps       = reinterpret_cast<FnHidP_GetCaps>(GetProcAddress(s_hid_dll, "HidP_GetCaps"));
        }
        s_hid_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    if (s_hid_handle == INVALID_HANDLE_VALUE && (s_last_enum_ms == 0 || (now_ms - s_last_enum_ms) >= 1500ULL)) {
        s_last_enum_ms = now_ms;
        UINT num_devs = 0;
        if (GetRawInputDeviceList(nullptr, &num_devs, sizeof(RAWINPUTDEVICELIST)) == 0 && num_devs > 0 && num_devs < 512) {
            RAWINPUTDEVICELIST dev_list[128];
            UINT cap = (num_devs > 128) ? 128 : num_devs;
            if (GetRawInputDeviceList(dev_list, &cap, sizeof(RAWINPUTDEVICELIST)) != static_cast<UINT>(-1)) {
                for (UINT i = 0; i < cap; ++i) {
                    if (dev_list[i].dwType != RIM_TYPEHID) continue;
                    RID_DEVICE_INFO rdi{};
                    rdi.cbSize = sizeof(RID_DEVICE_INFO);
                    UINT rdi_sz = sizeof(RID_DEVICE_INFO);
                    if (GetRawInputDeviceInfoA(dev_list[i].hDevice, RIDI_DEVICEINFO, &rdi, &rdi_sz) == static_cast<UINT>(-1)) {
                        continue;
                    }
                    if (rdi.hid.dwVendorId != 0x054Cu) continue;
                    if (rdi.hid.usUsagePage != 0x01u || (rdi.hid.usUsage != 0x05u && rdi.hid.usUsage != 0x04u)) continue;

                    wchar_t dev_path[512] = {};
                    UINT name_len = 511;
                    if (GetRawInputDeviceInfoW(dev_list[i].hDevice, RIDI_DEVICENAME, dev_path, &name_len) == static_cast<UINT>(-1) || dev_path[0] == L'\0') {
                        continue;
                    }
                    HANDLE h = CreateFileW(
                        dev_path,
                        GENERIC_WRITE | GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr,
                        OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED,
                        nullptr
                    );
                    if (h == INVALID_HANDLE_VALUE) {
                        h = CreateFileW(
                            dev_path,
                            GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr,
                            OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED,
                            nullptr
                        );
                    }
                    if (h != INVALID_HANDLE_VALUE) {
                        uint16_t out_len = 0;
                        void* preparsed = nullptr;
                        if (s_pGetPreparsed && s_pFreePreparsed && s_pGetCaps && s_pGetPreparsed(h, &preparsed) && preparsed) {
                            AczHidCaps caps{};
                            if (s_pGetCaps(preparsed, &caps) >= 0) {
                                out_len = caps.OutputReportByteLength;
                            }
                            s_pFreePreparsed(preparsed);
                        }
                        if (out_len == 0) {
                            out_len = (rdi.hid.dwProductId == 0x05C4u || rdi.hid.dwProductId == 0x09CCu) ? 32u : 48u;
                        }
                        s_hid_handle     = h;
                        s_hid_pid        = static_cast<uint16_t>(rdi.hid.dwProductId);
                        s_out_report_len = out_len;
                        break;
                    }
                }
            }
        }
    }

    if (s_hid_handle == INVALID_HANDLE_VALUE || s_hid_handle == nullptr) return;

    uint8_t report[96] = {};
    DWORD   write_len  = 0;
    bool    is_ds4     = (s_hid_pid == 0x05C4u || s_hid_pid == 0x09CCu || s_hid_pid == 0x0BA0u);

    if (is_ds4) {
        if (s_out_report_len >= 78u) {
            // DualShock 4 Bluetooth (78-byte report 0x11)
            report[0] = 0x11u;
            report[1] = 0xC0u;
            report[2] = 0x20u;
            report[3] = 0x01u; // Enable rumble only
            report[6] = right_motor;
            report[7] = left_motor;
            uint32_t crc = ComputeSonyBtCrc32(0xA2u, report, 74u);
            std::memcpy(report + 74u, &crc, sizeof(crc));
            write_len = 78u;
        } else {
            // DualShock 4 USB (32-byte report 0x05)
            report[0] = 0x05u;
            report[1] = 0x01u; // Enable rumble only
            report[4] = right_motor;
            report[5] = left_motor;
            write_len = (s_out_report_len >= 32u && s_out_report_len <= 64u) ? s_out_report_len : 32u;
        }
    } else {
        if (s_out_report_len >= 78u) {
            // DualSense / DualSense Edge Bluetooth (78-byte report 0x31)
            report[0]  = 0x31u;
            report[1]  = 0x02u;
            report[2]  = 0x03u; // valid_flag0: Compatible vibration (0x01) | Haptics select (0x02)
            report[3]  = 0x00u; // valid_flag1
            report[4]  = right_motor;
            report[5]  = left_motor;
            report[40] = 0x02u; // valid_flag2: Compatible vibration2 (required by modern firmware)
            uint32_t crc = ComputeSonyBtCrc32(0xA2u, report, 74u);
            std::memcpy(report + 74u, &crc, sizeof(crc));
            write_len = 78u;
        } else {
            // DualSense / DualSense Edge USB (48-byte report 0x02)
            report[0]  = 0x02u;
            report[1]  = 0x03u; // valid_flag0: Compatible vibration (0x01) | Haptics select (0x02)
            report[2]  = 0x00u; // valid_flag1
            report[3]  = right_motor;
            report[4]  = left_motor;
            report[39] = 0x02u; // valid_flag2: Compatible vibration2 (required by modern firmware)
            write_len = (s_out_report_len >= 48u && s_out_report_len <= 64u) ? s_out_report_len : 48u;
        }
    }

    HANDLE hDev = s_hid_handle;
    if (hDev == INVALID_HANDLE_VALUE || hDev == nullptr) return;

    CancelIo(hDev);
    std::memset(&s_hid_ovl, 0, sizeof(s_hid_ovl));
    s_hid_ovl.hEvent = s_hid_event;
    if (s_hid_event) ResetEvent(s_hid_event);

    DWORD bytes_written = 0;
    BOOL ok = WriteFile(hDev, report, write_len, &bytes_written, &s_hid_ovl);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            CloseHandle(hDev);
            s_hid_handle = INVALID_HANDLE_VALUE;
            return;
        }
    }

    s_last_left    = left_motor;
    s_last_right   = right_motor;
    s_last_send_ms = now_ms;
}

static void UpdateSonyVibrationFromGameSEH(uintptr_t exe_base, uint8_t raw_large, uint8_t raw_small) {
    if (!exe_base) return;
    __try {
        static uint8_t  s_prev_vib_opt    = 0xFFu;
        static uint64_t s_pulse_until_ms  = 0;

        uint8_t vib_enabled = 1u;
        const uint8_t* sys_ptr = *reinterpret_cast<const uint8_t* const*>(exe_base + AczRva(0x32C8F0u));
        if (sys_ptr != nullptr) {
            vib_enabled = (sys_ptr[0x95] & 1u) ? 1u : 0u;
            if (s_prev_vib_opt != 0xFFu && s_prev_vib_opt == 0u && vib_enabled == 1u) {
                // User just switched Vibration from OFF to ON in Options -> give immediate 220ms tactile feedback!
                s_pulse_until_ms = GetTickCount64() + 220ULL;
            }
            s_prev_vib_opt = vib_enabled;
        }

        uint8_t left_motor  = 0u;
        uint8_t right_motor = 0u;
        if (vib_enabled) {
            // Soften DualSense haptic rumble to match smooth, pleasant Xbox XInput emulation:
            // Low-frequency heavy motor scaled to 60%, high-frequency light motor scaled to 80 (instead of 200)
            left_motor  = static_cast<uint8_t>((static_cast<uint32_t>(raw_large) * 60u) / 100u);
            uint32_t sm = (raw_small == 1u) ? 80u : ((static_cast<uint32_t>(raw_small) * 60u) / 100u);
            right_motor = static_cast<uint8_t>(sm);
            uint64_t now_ms = GetTickCount64();
            if (now_ms < s_pulse_until_ms && left_motor == 0u && right_motor == 0u) {
                left_motor  = 100u;
                right_motor = 70u;
            }
        }
        SendSonyHidVibrationReport(left_motor, right_motor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static constexpr uintptr_t RVA_PollDirectInputDevice = 0x0021A480;
static const uint8_t PROLOGUE_PollDirectInputDevice[19] = {
    0x40, 0x55,                                     // push rbp
    0x53,                                           // push rbx
    0x56,                                           // push rsi
    0x57,                                           // push rdi
    0x41, 0x54,                                     // push r12
    0x41, 0x55,                                     // push r13
    0x41, 0x57,                                     // push r15
    0x48, 0x8D, 0xAC, 0x24, 0xA0, 0xFE, 0xFF, 0xFF  // lea rbp, [rsp-160h]
};

using FnPollDirectInputDevice = char (__fastcall*)(
    uint8_t*       out_state,
    const uint8_t* dev_info,
    void**         pp_dinput_dev,
    void*          ff_effects_v1,
    void*          ff_effects_v2
);
static FnPollDirectInputDevice g_OrigPollDirectInputDevice = nullptr;

using FnPollAllInputDevices = void (__fastcall*)(uint8_t* state_array);
static FnPollAllInputDevices g_OrigPollAllInputDevices = nullptr;
static constexpr uintptr_t RVA_PollAllInputDevices = 0x0021D4C0;
static const uint8_t PROLOGUE_PollAllInputDevices[20] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x20, 0xFA, 0xFF, 0xFF
};

using FnInitPlayerSlot = bool (__fastcall*)(void* slot, uint64_t dev_uid);
static constexpr uintptr_t RVA_InitPlayerSlot = 0x0021CAE0;

static void FixDefaultDirectInputPovHatTypesSEH(uintptr_t exe_base) {
    if (!exe_base) return;
    __try {
        uintptr_t def_map = exe_base + AczRva(0x33B1C0u);
        for (size_t i = 0; i < 24; ++i) {
            uintptr_t vec_addr = def_map + 0x20u + i * 0x18u;
            uint8_t* vec_begin = *reinterpret_cast<uint8_t**>(vec_addr + 0x00u);
            uint8_t* vec_end   = *reinterpret_cast<uint8_t**>(vec_addr + 0x08u);
            if (vec_begin != nullptr && vec_end > vec_begin && (vec_end - vec_begin) <= 0x1000) {
                for (uint8_t* p = vec_begin; p + 20 <= vec_end; p += 20) {
                    int32_t id   = *reinterpret_cast<const int32_t*>(p + 0x00);
                    int32_t type = *reinterpret_cast<const int32_t*>(p + 0x04);
                    if (id >= 33 && id <= 48 && type == 0) {
                        *reinterpret_cast<int32_t*>(p + 0x04) = 1;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void ApplyDirectInputDualSenseAndPovEnhancementsSEH(
    uint8_t*       out_state,
    const uint8_t* dev_info,
    void**         pp_dinput_dev
) {
    if (!out_state || !dev_info || !pp_dinput_dev) return;
    __try {
        void* pDev = *pp_dinput_dev;
        if (!pDev) return;
        void** vtbl = *reinterpret_cast<void***>(pDev);
        if (!vtbl || !vtbl[9]) return;

        using FnGetDeviceState = long (__fastcall*)(void* dev, uint32_t cbData, void* lpvData);
        RawDiJoyState2_Acz js{};
        js.rgdwPOV[0] = 0xFFFFFFFFu;
        js.rgdwPOV[1] = 0xFFFFFFFFu;
        js.rgdwPOV[2] = 0xFFFFFFFFu;
        js.rgdwPOV[3] = 0xFFFFFFFFu;
        long hr = reinterpret_cast<FnGetDeviceState>(vtbl[9])(pDev, 0x110u, &js);
        if (hr < 0) return;

        uint16_t btns = *reinterpret_cast<const uint16_t*>(out_state + 0x28);
        uint16_t pov_btns = 0u;
        uint32_t pov  = js.rgdwPOV[0];
        if ((pov & 0xFFFFu) != 0xFFFFu && pov <= 35999u) {
            if (pov >= 31500u || pov <= 4500u)  pov_btns |= 0x0010u; // D-Pad Up
            if (pov >= 4500u  && pov <= 13500u) pov_btns |= 0x0020u; // D-Pad Right
            if (pov >= 13500u && pov <= 22500u) pov_btns |= 0x0040u; // D-Pad Down
            if (pov >= 22500u && pov <= 31500u) pov_btns |= 0x0080u; // D-Pad Left
        }
        btns |= pov_btns;

        uint32_t guid_d1  = *reinterpret_cast<const uint32_t*>(dev_info + 0x04u);
        uint32_t guid_alt = *reinterpret_cast<const uint32_t*>(dev_info + 0x00u);
        bool is_sony = ((guid_d1 & 0xFFFFu) == 0x054Cu) || ((guid_alt & 0xFFFFu) == 0x054Cu);
        if (is_sony) {
            uint16_t ps_btns = pov_btns;
            if (js.rgbButtons[0]  & 0x80u) ps_btns |= 0x8000u; // B1:  Square   (X - Toggle weapon)
            if (js.rgbButtons[1]  & 0x80u) ps_btns |= 0x4000u; // B2:  Cross    (A - Confirm / Machine gun)
            if (js.rgbButtons[2]  & 0x80u) ps_btns |= 0x2000u; // B3:  Circle   (B - Cancel / Fire missile)
            if (js.rgbButtons[3]  & 0x80u) ps_btns |= 0x1000u; // B4:  Triangle (Y - Switch target)
            if (js.rgbButtons[4]  & 0x80u) ps_btns |= 0x0400u; // B5:  L1       (LB - Yaw left)
            if (js.rgbButtons[5]  & 0x80u) ps_btns |= 0x0800u; // B6:  R1       (RB - Yaw right)
            if (js.rgbButtons[9]  & 0x80u) ps_btns |= 0x0008u; // B10: Options  (Menu - Pause menu)
            if (js.rgbButtons[10] & 0x80u) ps_btns |= 0x0002u; // B11: L3       (LSC - Boresight)
            if (js.rgbButtons[11] & 0x80u) ps_btns |= 0x0004u; // B12: R3       (RSC - Change view)
            // IMPORTANT: On PS5 DualSense / DS4, ONLY Touchpad click (B14 = rgbButtons[13]) is View (Switch radar)!
            if (js.rgbButtons[13] & 0x80u) ps_btns |= 0x0001u; // B14: Touchpad (View - Switch radar)

            float lt = 0.0f;
            float rt = 0.0f;
            if (js.lRx > 1024 && js.lRx <= 65535) {
                lt = static_cast<float>(js.lRx - 1024) / static_cast<float>(65520 - 1024);
                if (lt > 1.0f) lt = 1.0f;
            }
            if ((js.rgbButtons[6] & 0x80u) != 0 && lt < 0.1f) {
                lt = 1.0f;
            }
            if (lt > 0.0f || (js.rgbButtons[6] & 0x80u) != 0) {
                ps_btns |= 0x0100u;
            }

            if (js.lRy > 1024 && js.lRy <= 65535) {
                rt = static_cast<float>(js.lRy - 1024) / static_cast<float>(65520 - 1024);
                if (rt > 1.0f) rt = 1.0f;
            }
            if ((js.rgbButtons[7] & 0x80u) != 0 && rt < 0.1f) {
                rt = 1.0f;
            }
            if (rt > 0.0f || (js.rgbButtons[7] & 0x80u) != 0) {
                ps_btns |= 0x0200u;
            }

// Apply radial deadzone with smooth linear rescale to native DirectInput stick axes
            // so PS mode matches the feel, stability, and deadzone of Xbox XInput without center drift
            auto apply_radial_deadzone = [](float& x, float& y, float deadzone) {
                float mag = std::sqrt(x * x + y * y);
                if (mag <= deadzone) {
                    x = 0.0f;
                    y = 0.0f;
                } else {
                    float factor = (mag - deadzone) / ((1.0f - deadzone) * mag);
                    x *= factor;
                    y *= factor;
                    if (x > 1.0f)  x = 1.0f;
                    if (x < -1.0f) x = -1.0f;
                    if (y > 1.0f)  y = 1.0f;
                    if (y < -1.0f) y = -1.0f;
                }
            };

            float* p_lx = reinterpret_cast<float*>(out_state + 0x08);
            float* p_ly = reinterpret_cast<float*>(out_state + 0x0C);
            float* p_rx = reinterpret_cast<float*>(out_state + 0x10);
            float* p_ry = reinterpret_cast<float*>(out_state + 0x14);
            apply_radial_deadzone(*p_lx, *p_ly, 0.16f);
            apply_radial_deadzone(*p_rx, *p_ry, 0.18f);
            *reinterpret_cast<float*>(out_state + 0x18)    = (js.rgbButtons[4] & 0x80u) ? 1.0f : 0.0f;
            *reinterpret_cast<float*>(out_state + 0x1C)    = (js.rgbButtons[5] & 0x80u) ? 1.0f : 0.0f;
            *reinterpret_cast<float*>(out_state + 0x20)    = lt;
            *reinterpret_cast<float*>(out_state + 0x24)    = rt;
            *reinterpret_cast<uint16_t*>(out_state + 0x28) = ps_btns;
            *reinterpret_cast<uint32_t*>(out_state + 0x2C) = 1u;
        } else {
            *reinterpret_cast<uint16_t*>(out_state + 0x28) = btns;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static char __fastcall Hook_PollDirectInputDevice(
    uint8_t*       out_state,
    const uint8_t* dev_info,
    void**         pp_dinput_dev,
    void*          ff_effects_v1,
    void*          ff_effects_v2
) {
    static std::atomic<bool> s_fixed_def_pov{false};
    if (!s_fixed_def_pov.load(std::memory_order_relaxed)) {
        s_fixed_def_pov.store(true, std::memory_order_relaxed);
        FixDefaultDirectInputPovHatTypesSEH(g_ExeBase);
    }
    if (!g_OrigPollDirectInputDevice || !out_state || !dev_info || !pp_dinput_dev || !ff_effects_v1 || !ff_effects_v2) {
        return 0;
    }
    uint8_t req_large = 0u;
    uint8_t req_small = 0u;
    __try {
        req_large = out_state[0];
        req_small = out_state[1];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    char ok = 0;
    __try {
        ok = g_OrigPollDirectInputDevice(out_state, dev_info, pp_dinput_dev, ff_effects_v1, ff_effects_v2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }
    if (ok) {
        ApplyDirectInputDualSenseAndPovEnhancementsSEH(out_state, dev_info, pp_dinput_dev);
        uint32_t guid_d1 = *reinterpret_cast<const uint32_t*>(dev_info + 0x04u);
        if ((guid_d1 & 0xFFFFu) == 0x054Cu) {
            UpdateSonyVibrationFromGameSEH(g_ExeBase, req_large, req_small);
        }
    }
    return ok;
}

static void __fastcall Hook_PollAllInputDevices_Enh(uint8_t* state_array) {
    if (!g_OrigPollAllInputDevices || !state_array) {
        if (g_OrigPollAllInputDevices) g_OrigPollAllInputDevices(state_array);
        return;
    }

    uint8_t p1_large     = 0u;
    uint8_t p1_small     = 0u;
    uint8_t startup_init = 0u;
    __try {
        p1_large     = state_array[0x00];
        p1_small     = state_array[0x01];
        startup_init = *reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(0x32C930u));

        // When startup_init == 0 (normal frame polling after initial slot allocation):
        // Ensure both Gamepad and Keyboard are initialized across PlayerSlots[0] and PlayerSlots[1]
        // when Controller2 in settings is unassigned (-1).
        // NOTE: In InputDevice (0xA0 bytes at g_InputDevices 0x33B180), the 64-bit dev_uid
        // expected by InitPlayerSlot (+0x21CAE0) is at offset +0x28!
        if (startup_init == 0u) {
            uintptr_t slots_begin = *reinterpret_cast<const uintptr_t*>(g_ExeBase + AczRva(0x33B938u));
            uintptr_t slots_end   = *reinterpret_cast<const uintptr_t*>(g_ExeBase + AczRva(0x33B940u));
            uintptr_t devs_begin  = *reinterpret_cast<const uintptr_t*>(g_ExeBase + AczRva(0x33B180u));
            uintptr_t devs_end    = *reinterpret_cast<const uintptr_t*>(g_ExeBase + AczRva(0x33B188u));
            uint64_t  cfg_ctrl2   = *reinterpret_cast<const uint64_t*>(g_ExeBase + AczRva(0x31E4A0u));

            if (slots_begin != 0 && (slots_end - slots_begin) == 0xA0u &&
                devs_begin != 0 && devs_end > devs_begin && cfg_ctrl2 == ~0ULL) {
                size_t   num_devs    = (devs_end - devs_begin) / 0xA0u;
                int32_t  pad_dev_idx = -1;
                uint64_t pad_uid     = ~0ULL;
                int32_t  kb_dev_idx  = -1;
                uint64_t kb_uid      = ~0ULL;

                for (size_t i = 0; i < num_devs; ++i) {
                    uintptr_t d     = devs_begin + i * 0xA0u;
                    uint32_t  dtype = *reinterpret_cast<const uint32_t*>(d + 0x00u);
                    uint32_t  guid1 = *reinterpret_cast<const uint32_t*>(d + 0x04u);
                    uint64_t  duid  = *reinterpret_cast<const uint64_t*>(d + 0x28u);
                    if (dtype == 3u) {
                        if (kb_dev_idx < 0) {
                            kb_dev_idx = static_cast<int32_t>(i);
                            kb_uid     = duid;
                        }
                    } else {
                        // Prefer Sony controller (VID 0x054C) if multiple gamepads are present
                        if (pad_dev_idx < 0 || (guid1 & 0xFFFFu) == 0x054Cu) {
                            pad_dev_idx = static_cast<int32_t>(i);
                            pad_uid     = duid;
                        }
                    }
                }

                uint8_t* slot0 = reinterpret_cast<uint8_t*>(slots_begin + 0x00u);
                uint8_t* slot1 = reinterpret_cast<uint8_t*>(slots_begin + 0x50u);
                int32_t  idx0  = *reinterpret_cast<const int32_t*>(slot0 + 0x00u);
                int32_t  idx1  = *reinterpret_cast<const int32_t*>(slot1 + 0x00u);
                FnInitPlayerSlot fn_init_slot = reinterpret_cast<FnInitPlayerSlot>(g_ExeBase + AczRva(RVA_InitPlayerSlot));

                if (idx0 < 0 && pad_dev_idx >= 0) {
                    fn_init_slot(slot0, pad_uid);
                    idx0 = *reinterpret_cast<const int32_t*>(slot0 + 0x00u);
                }
                if (idx0 >= 0 && static_cast<size_t>(idx0) < num_devs && idx1 < 0) {
                    uint32_t type0 = *reinterpret_cast<const uint32_t*>(devs_begin + static_cast<size_t>(idx0) * 0xA0u);
                    if (type0 == 3u && pad_dev_idx >= 0) {
                        fn_init_slot(slot1, pad_uid);
                    } else if (type0 != 3u && kb_dev_idx >= 0) {
                        fn_init_slot(slot1, kb_uid);
                    }
                }

                // If slot1 is the Gamepad, copy Player 1's vibration bytes to state_array + 0x30 before polling
                idx1 = *reinterpret_cast<const int32_t*>(slot1 + 0x00u);
                if (idx1 >= 0 && static_cast<size_t>(idx1) < num_devs) {
                    uint32_t type1 = *reinterpret_cast<const uint32_t*>(devs_begin + static_cast<size_t>(idx1) * 0xA0u);
                    if (type1 != 3u) {
                        state_array[0x30] = p1_large;
                        state_array[0x31] = p1_small;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    // Call the engine's native PollAllInputDevices untouched (never modify 0x32C930!)
    g_OrigPollAllInputDevices(state_array);

    if (startup_init != 0u) {
        return;
    }

    __try {
        uintptr_t slots_begin = *reinterpret_cast<const uintptr_t*>(g_ExeBase + AczRva(0x33B938u));
        uintptr_t slots_end   = *reinterpret_cast<const uintptr_t*>(g_ExeBase + AczRva(0x33B940u));
        uintptr_t devs_begin  = *reinterpret_cast<const uintptr_t*>(g_ExeBase + AczRva(0x33B180u));
        uintptr_t devs_end    = *reinterpret_cast<const uintptr_t*>(g_ExeBase + AczRva(0x33B188u));
        uint64_t  cfg_ctrl2   = *reinterpret_cast<const uint64_t*>(g_ExeBase + AczRva(0x31E4A0u));

        if (slots_begin != 0 && (slots_end - slots_begin) == 0xA0u &&
            devs_begin != 0 && devs_end > devs_begin) {
            size_t   num_devs = (devs_end - devs_begin) / 0xA0u;
            uint8_t* slot0    = reinterpret_cast<uint8_t*>(slots_begin + 0x00u);
            uint8_t* slot1    = reinterpret_cast<uint8_t*>(slots_begin + 0x50u);
            int32_t  idx0     = *reinterpret_cast<const int32_t*>(slot0 + 0x00u);
            int32_t  idx1     = *reinterpret_cast<const int32_t*>(slot1 + 0x00u);

            uint32_t type0 = (idx0 >= 0 && static_cast<size_t>(idx0) < num_devs)
                ? *reinterpret_cast<const uint32_t*>(devs_begin + static_cast<size_t>(idx0) * 0xA0u)
                : 0xFFFFFFFFu;
            uint32_t type1 = (idx1 >= 0 && static_cast<size_t>(idx1) < num_devs)
                ? *reinterpret_cast<const uint32_t*>(devs_begin + static_cast<size_t>(idx1) * 0xA0u)
                : 0xFFFFFFFFu;

            uint8_t*  pad_st  = nullptr;
            uint8_t*  kb_st   = nullptr;
            uintptr_t pad_dev = 0;

            if (type0 != 0xFFFFFFFFu && type0 != 3u) {
                pad_st  = state_array + 0x00u;
                pad_dev = devs_begin + static_cast<size_t>(idx0) * 0xA0u;
            } else if (type1 != 0xFFFFFFFFu && type1 != 3u) {
                pad_st  = state_array + 0x30u;
                pad_dev = devs_begin + static_cast<size_t>(idx1) * 0xA0u;
            }
            if (type0 == 3u) {
                kb_st = state_array + 0x00u;
            } else if (type1 == 3u) {
                kb_st = state_array + 0x30u;
            }

            // Fix Escape double-click on Keyboard when opening/closing Pause Menu:
            // When Escape is pressed on Keyboard, let the initial press pass through to trigger
            // the pause action, then suppress repeat bits (0x6008: Start / Circle) on kb_st ONLY
            // for subsequent frames while Escape remains physically held down.
            // NOTE: Never modify engine state variables 0x32C932/0x32C933 or gamepad state pad_st!
            static bool s_esc_was_down    = false;
            static bool s_kb_suppress_esc = false;
            bool esc_now = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;

            if (esc_now) {
                if (!s_esc_was_down) {
                    s_esc_was_down = true;
                    s_kb_suppress_esc = false;
                } else {
                    s_kb_suppress_esc = true;
                }
            } else {
                s_esc_was_down = false;
                s_kb_suppress_esc = false;
            }

            if (kb_st != nullptr && s_kb_suppress_esc) {
                uint16_t kb_btns = *reinterpret_cast<const uint16_t*>(kb_st + 0x28u);
                *reinterpret_cast<uint16_t*>(kb_st + 0x28u) = static_cast<uint16_t>(kb_btns & ~0x6008u);
            }

            if (pad_dev != 0) {
                uint32_t guid_d1 = *reinterpret_cast<const uint32_t*>(pad_dev + 0x04u);
                if ((guid_d1 & 0xFFFFu) == 0x054Cu) {
                    UpdateSonyVibrationFromGameSEH(g_ExeBase, p1_large, p1_small);
                }
            }

            // Handle Single-Player Seamless Merge vs Unlocked 2-Player Separate Controls:
            if (cfg_ctrl2 == ~0ULL && type0 != 0xFFFFFFFFu && type1 != 0xFFFFFFFFu && type0 != type1) {
                uint8_t* st0 = state_array + 0x00u;
                uint8_t* st1 = state_array + 0x30u;

                if (!g_Unlock2PlayerCampaign) {
                    // Single-Player Mode (Unlock2PlayerCampaign = 0):
                    // 1. Merge st1 (Keyboard) into st0 (Player 1) so Gamepad & Keyboard both control Player 1 seamlessly.
                    // 2. Mark st1 as DISCONNECTED (status = 1 at +0x04, pad_mode = 0 at +0x2C) so 0x14018820E & 0x140188D25
                    //    see only 1 controller connected and keep the 2-Player mode (VS MODE) locked with its padlock icon!
                    if (*reinterpret_cast<const uint32_t*>(st1 + 0x04u) == 0u) {
                        *reinterpret_cast<uint16_t*>(st0 + 0x28u) |= *reinterpret_cast<const uint16_t*>(st1 + 0x28u);
                        float*       ax0 = reinterpret_cast<float*>(st0 + 0x08u);
                        const float* ax1 = reinterpret_cast<const float*>(st1 + 0x08u);
                        for (int i = 0; i < 8; ++i) {
                            float v0 = ax0[i] >= 0.0f ? ax0[i] : -ax0[i];
                            float v1 = ax1[i] >= 0.0f ? ax1[i] : -ax1[i];
                            if (v1 > v0) {
                                ax0[i] = ax1[i];
                            }
                        }
                        *reinterpret_cast<uint32_t*>(st0 + 0x04u) = 0u;
                        *reinterpret_cast<uint32_t*>(st0 + 0x2Cu) = 1u;
                        *reinterpret_cast<uint32_t*>(slot0 + 0x04u) = 0u;
                    }
                    *reinterpret_cast<uint32_t*>(st1 + 0x04u) = 1u; // status = 1 (DISCONNECTED)
                    std::memset(st1 + 0x08u, 0, 0x28u);             // zeroes axes (+0x08..+0x27), buttons (+0x28), and pad_mode (+0x2C = 0)
                } else {
                    // Unlocked 2-Player Mode (Unlock2PlayerCampaign = 1):
                    // Keep Gamepad (Player 1 -> st0) and Keyboard (Player 2 -> st1) completely separate!
                    // If slot0 was Keyboard and slot1 was Gamepad, swap st0 and st1 payloads so Player 1 is Gamepad and Player 2 is Keyboard.
                    if (type0 == 3u && type1 != 3u) {
                        uint8_t tmp[0x2Cu];
                        std::memcpy(tmp,        st0 + 0x04u, 0x2Cu);
                        std::memcpy(st0 + 0x04u, st1 + 0x04u, 0x2Cu);
                        std::memcpy(st1 + 0x04u, tmp,        0x2Cu);
                    }
                    *reinterpret_cast<uint32_t*>(st0 + 0x04u) = 0u;
                    *reinterpret_cast<uint32_t*>(st0 + 0x2Cu) = 1u;
                    *reinterpret_cast<uint32_t*>(st1 + 0x04u) = 0u;
                    *reinterpret_cast<uint32_t*>(st1 + 0x2Cu) = 1u;
                    *reinterpret_cast<uint32_t*>(slot0 + 0x04u) = 0u;
                    *reinterpret_cast<uint32_t*>(slot1 + 0x04u) = 0u;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

using FnSwgOptionSetAspect = void (__fastcall*)(void* rcx, void* rdx, void* r8, int32_t* r9);
static FnSwgOptionSetAspect g_OrigSwgOptionSetAspect = nullptr;

static void __fastcall Hook_SwgOptionSetAspect(void* rcx, void* rdx, void* r8, int32_t* r9) {
    if (r9 != nullptr) {
        int32_t aspect = *r9;
        if (aspect >= 0 && aspect <= 2) {
            g_ScreenAspect = aspect;
            SaveScreenAspectToIni(g_ScreenAspect);
            if (g_ExeBase != 0) {
                *reinterpret_cast<int32_t*>(g_ExeBase + AczRva(RVA_ScreenSizeOption)) = g_ScreenAspect;
            }
            LogMsg("[Enhancer] In-game menu changed ScreenAspect to %d. Saved to enhancer.ini (save file remains standard 16:9).", g_ScreenAspect);
        }
    }
    if (g_OrigSwgOptionSetAspect != nullptr) {
        g_OrigSwgOptionSetAspect(rcx, rdx, r8, r9);
    }
}

using FnCSysDataSave = void* (__fastcall*)(uint8_t* rcx, uint8_t* rdx);
static FnCSysDataSave g_OrigCSysDataSave = nullptr;

static void* __fastcall Hook_CSysDataSave(uint8_t* rcx, uint8_t* rdx) {
    // 1. Ensure runtime ScreenSizeOption (rcx + 0x24) is g_ScreenAspect
    if (rcx != nullptr) {
        *reinterpret_cast<int32_t*>(rcx + 0x24) = g_ScreenAspect;
    }
    void* ret = g_OrigCSysDataSave(rcx, rdx);
    // 2. Force the serialized save buffer on disk (rdx + 0x14 corresponds to rcx + 0x24) to 1 (16:9)
    //    so the save file on disk ALWAYS stores vanilla-compatible 16:9!
    if (rdx != nullptr) {
        *reinterpret_cast<int32_t*>(rdx + 0x14) = 1;
    }
    return ret;
}

using FnCSysDataLoad = void* (__fastcall*)(uint8_t* rcx, uint8_t* rdx);
static FnCSysDataLoad g_OrigCSysDataLoad = nullptr;

static void* __fastcall Hook_CSysDataLoad(uint8_t* rcx, uint8_t* rdx) {
    void* ret = g_OrigCSysDataLoad(rcx, rdx);
    // Ignore the aspect ratio stored in the save file and enforce g_ScreenAspect from enhancer.ini!
    if (rcx != nullptr) {
        *reinterpret_cast<int32_t*>(rcx + 0x24) = g_ScreenAspect;
    }
    if (g_ExeBase != 0) {
        *reinterpret_cast<int32_t*>(g_ExeBase + AczRva(RVA_ScreenSizeOption)) = g_ScreenAspect;
    }
    return ret;
}

void InstallWidescreenAndUiHooks() {
    if (!g_ExeBase) return;

    ApplyEngine21x9AspectPatches();

    static constexpr uint8_t PROLOGUE_SwgOptionSetAspect[15] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20
    };
    InstallDetour64(
        "SWG_option_setAspect",
        g_ExeBase + AczRva(0x1F3850),
        PROLOGUE_SwgOptionSetAspect,
        sizeof(PROLOGUE_SwgOptionSetAspect),
        reinterpret_cast<const void*>(&Hook_SwgOptionSetAspect),
        reinterpret_cast<void**>(&g_OrigSwgOptionSetAspect)
    );

    static constexpr uint8_t PROLOGUE_CSysDataSave[14] = {
        0xC5, 0xFC, 0x10, 0x41, 0x10, 0xC5, 0xFC, 0x11, 0x02, 0xC5, 0xFC, 0x10, 0x49, 0x30
    };
    InstallDetour64(
        "CSysData_Save",
        g_ExeBase + AczRva(0x1895B0),
        PROLOGUE_CSysDataSave,
        sizeof(PROLOGUE_CSysDataSave),
        reinterpret_cast<const void*>(&Hook_CSysDataSave),
        reinterpret_cast<void**>(&g_OrigCSysDataSave)
    );

    static constexpr uint8_t PROLOGUE_CSysDataLoad[14] = {
        0xC5, 0xFC, 0x10, 0x02, 0xC5, 0xFC, 0x11, 0x41, 0x10, 0xC5, 0xFC, 0x10, 0x4A, 0x20
    };
    InstallDetour64(
        "CSysData_Load",
        g_ExeBase + AczRva(0x1895E0),
        PROLOGUE_CSysDataLoad,
        sizeof(PROLOGUE_CSysDataLoad),
        reinterpret_cast<const void*>(&Hook_CSysDataLoad),
        reinterpret_cast<void**>(&g_OrigCSysDataLoad)
    );

    bool ok10 = InstallDetour64(
        "CSwgInstance_Init",
        g_ExeBase + AczRva(RVA_CSwgInstanceInit),
        PROLOGUE_CSwgInstanceInit,
        sizeof(PROLOGUE_CSwgInstanceInit),
        reinterpret_cast<const void*>(&Hook_CSwgInstanceInit),
        reinterpret_cast<void**>(&g_OrigCSwgInstanceInit)
    );

    bool ok11 = InstallDetour64(
        "CSwgInstance_Update",
        g_ExeBase + AczRva(RVA_CSwgInstanceUpdate),
        PROLOGUE_CSwgInstanceUpdate,
        sizeof(PROLOGUE_CSwgInstanceUpdate),
        reinterpret_cast<const void*>(&Hook_CSwgInstanceUpdate),
        reinterpret_cast<void**>(&g_OrigCSwgInstanceUpdate)
    );

    bool ok12 = InstallDetour64(
        "CSwadMenu_Draw",
        g_ExeBase + AczRva(RVA_CSwadMenuDraw),
        PROLOGUE_CSwadMenuDraw,
        sizeof(PROLOGUE_CSwadMenuDraw),
        reinterpret_cast<const void*>(&Hook_CSwadMenuDraw),
        reinterpret_cast<void**>(&g_OrigCSwadMenuDraw)
    );

    static constexpr uint8_t PROLOGUE_DrawMovieQuad[20] = {
        0x48, 0x8B, 0xC4, 0x55, 0x53, 0x57, 0x48, 0x8D, 0xA8, 0x58, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC,
        0x90, 0x01, 0x00, 0x00
    };
    bool ok13 = InstallDetour64(
        "DrawMovieQuad",
        g_ExeBase + AczRva(0x181BA0),
        PROLOGUE_DrawMovieQuad,
        sizeof(PROLOGUE_DrawMovieQuad),
        reinterpret_cast<const void*>(&Hook_DrawMovieQuad),
        reinterpret_cast<void**>(&g_OrigDrawMovieQuad)
    );

    bool ok14 = InstallDetour64(
        "MissionTitle_Draw",
        g_ExeBase + AczRva(RVA_MissionTitleDraw),
        PROLOGUE_MissionTitleDraw,
        sizeof(PROLOGUE_MissionTitleDraw),
        reinterpret_cast<const void*>(&Hook_MissionTitleDraw),
        reinterpret_cast<void**>(&g_OrigMissionTitleDraw)
    );

    bool ok15 = InstallDetour64(
        "LoadingScreen_Draw",
        g_ExeBase + AczRva(RVA_LoadingScreenDraw),
        PROLOGUE_LoadingScreenDraw,
        sizeof(PROLOGUE_LoadingScreenDraw),
        reinterpret_cast<const void*>(&Hook_LoadingScreenDraw),
        reinterpret_cast<void**>(&g_OrigLoadingScreenDraw)
    );

    bool ok17 = InstallDetour64(
        "CutsceneSubFont_Draw",
        g_ExeBase + AczRva(RVA_CutsceneSubFontDraw),
        PROLOGUE_CutsceneSubFontDraw,
        sizeof(PROLOGUE_CutsceneSubFontDraw),
        reinterpret_cast<const void*>(&Hook_CutsceneSubFontDraw),
        reinterpret_cast<void**>(&g_OrigCutsceneSubFontDraw)
    );

    bool ok18 = InstallDetour64(
        "CutsceneSubFont_DrawEmblem",
        g_ExeBase + AczRva(RVA_CutsceneSubFontDrawEmblem),
        PROLOGUE_CutsceneSubFontDrawEmblem,
        sizeof(PROLOGUE_CutsceneSubFontDrawEmblem),
        reinterpret_cast<const void*>(&Hook_CutsceneSubFontDrawEmblem),
        reinterpret_cast<void**>(&g_OrigCutsceneSubFontDrawEmblem)
    );

    bool ok19 = InstallDetour64(
        "CutsceneFont_Draw",
        g_ExeBase + AczRva(RVA_CutsceneFontDraw),
        PROLOGUE_CutsceneFontDraw,
        sizeof(PROLOGUE_CutsceneFontDraw),
        reinterpret_cast<const void*>(&Hook_CutsceneFontDraw),
        reinterpret_cast<void**>(&g_OrigCutsceneFontDraw)
    );

    bool ok20 = InstallDetour64(
        "GsBuildProjectionMatrices",
        g_ExeBase + AczRva(RVA_GsBuildProjectionMatrices),
        PROLOGUE_GsBuildProjectionMatrices,
        sizeof(PROLOGUE_GsBuildProjectionMatrices),
        reinterpret_cast<const void*>(&Hook_GsBuildProjectionMatrices),
        reinterpret_cast<void**>(&g_OrigGsBuildProjectionMatrices)
    );

    bool ok21 = false;
    {
        void* addr = reinterpret_cast<void*>(g_ExeBase + AczRva(RVA_GetAspectScaleY));
        DWORD old_prot = 0;
        if (VirtualProtect(addr, 16, PAGE_EXECUTE_READWRITE, &old_prot)) {
            WriteAbsoluteJump64(addr, reinterpret_cast<const void*>(&Hook_GetAspectScaleY));
            VirtualProtect(addr, 16, old_prot, &old_prot);
            FlushInstructionCache(GetCurrentProcess(), addr, 16);
            ok21 = true;
            LogMsg("[Enhancer] Installed direct hook 'GetAspectScaleY' at %p", addr);
        }
    }

    bool ok22 = InstallDetour64(
        "RadioSubFont_Draw",
        g_ExeBase + AczRva(RVA_RadioSubFontDraw),
        PROLOGUE_RadioSubFontDraw,
        sizeof(PROLOGUE_RadioSubFontDraw),
        reinterpret_cast<const void*>(&Hook_RadioSubFontDraw),
        reinterpret_cast<void**>(&g_OrigRadioSubFontDraw)
    );

    bool ok23 = InstallDetour64(
        "CHudManager_Draw",
        g_ExeBase + AczRva(RVA_CHudManagerDraw),
        PROLOGUE_CHudManagerDraw,
        sizeof(PROLOGUE_CHudManagerDraw),
        reinterpret_cast<const void*>(&Hook_CHudManagerDraw),
        reinterpret_cast<void**>(&g_OrigCHudManagerDraw)
    );

    bool ok24 = InstallDetour64(
        "PauseMenu_Draw",
        g_ExeBase + AczRva(RVA_PauseMenuDraw),
        PROLOGUE_PauseMenuDraw,
        sizeof(PROLOGUE_PauseMenuDraw),
        reinterpret_cast<const void*>(&Hook_PauseMenuDraw),
        reinterpret_cast<void**>(&g_OrigPauseMenuDraw)
    );

    bool ok25 = InstallDetour64(
        "HudDrawTextLeft",
        g_ExeBase + AczRva(RVA_HudDrawTextLeft),
        PROLOGUE_HudDrawTextLeft,
        sizeof(PROLOGUE_HudDrawTextLeft),
        reinterpret_cast<const void*>(&Hook_HudDrawTextLeft),
        reinterpret_cast<void**>(&g_OrigHudDrawTextLeft)
    );

    bool ok26 = InstallDetour64(
        "HudDrawTextRight",
        g_ExeBase + AczRva(RVA_HudDrawTextRight),
        PROLOGUE_HudDrawTextRight,
        sizeof(PROLOGUE_HudDrawTextRight),
        reinterpret_cast<const void*>(&Hook_HudDrawTextRight),
        reinterpret_cast<void**>(&g_OrigHudDrawTextRight)
    );

    bool ok27 = InstallDetour64(
        "HudStatusAlerts",
        g_ExeBase + AczRva(RVA_HudStatusAlerts),
        PROLOGUE_HudStatusAlerts,
        sizeof(PROLOGUE_HudStatusAlerts),
        reinterpret_cast<const void*>(&Hook_HudStatusAlerts),
        reinterpret_cast<void**>(&g_OrigHudStatusAlerts)
    );

    bool ok28 = InstallDetour64(
        "HudBottomMessage",
        g_ExeBase + AczRva(RVA_HudBottomMessage),
        PROLOGUE_HudBottomMessage,
        sizeof(PROLOGUE_HudBottomMessage),
        reinterpret_cast<const void*>(&Hook_HudBottomMessage),
        reinterpret_cast<void**>(&g_OrigHudBottomMessage)
    );

    bool ok29 = InstallDetour64(
        "HudCountdown",
        g_ExeBase + AczRva(RVA_HudCountdown),
        PROLOGUE_HudCountdown,
        sizeof(PROLOGUE_HudCountdown),
        reinterpret_cast<const void*>(&Hook_HudCountdown),
        reinterpret_cast<void**>(&g_OrigHudCountdown)
    );

    bool ok30 = InstallDetour64(
        "HudDrawTextFloat",
        g_ExeBase + AczRva(RVA_HudDrawTextFloat),
        PROLOGUE_HudDrawTextFloat,
        sizeof(PROLOGUE_HudDrawTextFloat),
        reinterpret_cast<const void*>(&Hook_HudDrawTextFloat),
        reinterpret_cast<void**>(&g_OrigHudDrawTextFloat)
    );

    bool ok31 = InstallDetour64(
        "PauseSubMenu_Draw",
        g_ExeBase + AczRva(RVA_PauseSubMenuDraw),
        PROLOGUE_PauseSubMenuDraw,
        sizeof(PROLOGUE_PauseSubMenuDraw),
        reinterpret_cast<const void*>(&Hook_PauseSubMenuDraw),
        reinterpret_cast<void**>(&g_OrigPauseSubMenuDraw)
    );

    bool ok32 = InstallDetour64(
        "Debriefing_Draw",
        g_ExeBase + AczRva(RVA_DebriefingDraw),
        PROLOGUE_DebriefingDraw,
        sizeof(PROLOGUE_DebriefingDraw),
        reinterpret_cast<const void*>(&Hook_DebriefingDraw),
        reinterpret_cast<void**>(&g_OrigDebriefingDraw)
    );

    FixDefaultDirectInputPovHatTypesSEH(g_ExeBase);
    {
        const uint8_t* di_site = reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_PollDirectInputDevice));
        if (!(di_site[0] == 0xFF && di_site[1] == 0x25)) {
            InstallDetour64(
                "PollDirectInputDevice_PS5",
                g_ExeBase + AczRva(RVA_PollDirectInputDevice),
                PROLOGUE_PollDirectInputDevice,
                sizeof(PROLOGUE_PollDirectInputDevice),
                reinterpret_cast<const void*>(&Hook_PollDirectInputDevice),
                reinterpret_cast<void**>(&g_OrigPollDirectInputDevice)
            );
        }
        const uint8_t* poll_all_site = reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_PollAllInputDevices));
        if (!(poll_all_site[0] == 0xFF && poll_all_site[1] == 0x25)) {
            InstallDetour64(
                "PollAllInputDevices_PS5",
                g_ExeBase + AczRva(RVA_PollAllInputDevices),
                PROLOGUE_PollAllInputDevices,
                sizeof(PROLOGUE_PollAllInputDevices),
                reinterpret_cast<const void*>(&Hook_PollAllInputDevices_Enh),
                reinterpret_cast<void**>(&g_OrigPollAllInputDevices)
            );
        }
    }

    // Standalone Fallback: if acz_hdtex.dll is not installed on disk or in memory,
    // install a lightweight hook on GameFont_Init so select.msa displays "21:9" in Options!
    std::error_code ec;
    bool hdtex_present = (GetModuleHandleA("acz_hdtex.asi") != nullptr) ||
                         (GetModuleHandleA("acz_hdtex.dll") != nullptr) ||
                         fs::exists(g_EnhancerDir / "acz_hdtex.asi", ec) ||
                         fs::exists(g_ModsDir / "acz_hdtex.asi", ec) ||
                         fs::exists(g_ModsDir / "acz_hdtex.dll", ec);
    if (!hdtex_present) {
        InstallDetour64(
            "GameFont_Init_21x9Fallback",
            g_ExeBase + AczRva(RVA_GameFontInit_Fallback),
            PROLOGUE_GameFontInit_Fallback,
            sizeof(PROLOGUE_GameFontInit_Fallback),
            reinterpret_cast<const void*>(&Hook_FallbackGameFontInit),
            reinterpret_cast<void**>(&g_OrigGameFontInitFallback)
        );
    }

    if (ok10 && ok11 && ok12 && ok13 && ok14 && ok15 && ok17 && ok18 && ok19 && ok20 && ok21 && ok22 && ok23 && ok24 && ok25 && ok26 && ok27 && ok28 && ok29 && ok30 && ok31 && ok32) {
        LogMsg("[Enhancer] All 22 Widescreen UI, HUD, Menu, Subtitle, Debriefing & 21:9 3D FOV hooks installed!");
    } else {
        LogMsg("[Enhancer] WARNING: One or more Widescreen UI/HUD hooks failed to install.");
    }
}


static void ApplyEnhancerPatchesAndHooks() {
    if (g_ExeBase == 0) return;

    // 1. Expand main engine heap (64MB -> 128MB), per-frame 3D GIF DMA stream buffer (2MB -> 8MB total / 4MB per frame),
    //    and D3D12 upload ring buffers (48MB / 16MB) to prevent memory/DMA exhaustion:
    {
        const uint32_t orig_heap_total = 0x03FFEFF0u, new_heap_total = 0x07FFEFF0u;
        const uint32_t orig_heap_free  = 0x03FFEFC0u, new_heap_free  = 0x07FFEFC0u;
        const uint32_t orig_dma_total  = 0x00200000u, new_dma_total  = 0x00800000u;
        const uint32_t orig_dma_frame  = 0x00100000u, new_dma_frame  = 0x00400000u;
        PatchMemoryBytes(g_ExeBase + 0x1FF35Cu, reinterpret_cast<const uint8_t*>(&new_heap_total), 4, reinterpret_cast<const uint8_t*>(&orig_heap_total));
        PatchMemoryBytes(g_ExeBase + 0x1FF38Fu, reinterpret_cast<const uint8_t*>(&new_heap_free),  4, reinterpret_cast<const uint8_t*>(&orig_heap_free));
        PatchMemoryBytes(g_ExeBase + 0x1FF42Au, reinterpret_cast<const uint8_t*>(&new_dma_total),  4, reinterpret_cast<const uint8_t*>(&orig_dma_total));
        PatchMemoryBytes(g_ExeBase + 0x1FF456u, reinterpret_cast<const uint8_t*>(&new_dma_frame),  4, reinterpret_cast<const uint8_t*>(&orig_dma_frame));

        const uint32_t orig_up12     = 12582912u, new_up12     = 50331648u;
        const uint32_t orig_up4      = 4194304u,  new_up4      = 16777216u;
        const uint32_t orig_mesh_res = 967184u,   new_mesh_res = 3867152u;
        const uint32_t orig_cpt_res  = 82288u,    new_cpt_res  = 328048u;
        PatchMemoryBytes(g_ExeBase + 1670482u, reinterpret_cast<const uint8_t*>(&new_up12),     4, reinterpret_cast<const uint8_t*>(&orig_up12));
        PatchMemoryBytes(g_ExeBase + 1670492u, reinterpret_cast<const uint8_t*>(&new_up12),     4, reinterpret_cast<const uint8_t*>(&orig_up12));
        PatchMemoryBytes(g_ExeBase + 1670509u, reinterpret_cast<const uint8_t*>(&new_up4),      4, reinterpret_cast<const uint8_t*>(&orig_up4));
        PatchMemoryBytes(g_ExeBase + 1670526u, reinterpret_cast<const uint8_t*>(&new_up4),      4, reinterpret_cast<const uint8_t*>(&orig_up4));
        PatchMemoryBytes(g_ExeBase + 1673401u, reinterpret_cast<const uint8_t*>(&new_mesh_res), 4, reinterpret_cast<const uint8_t*>(&orig_mesh_res));
        PatchMemoryBytes(g_ExeBase + 1676432u, reinterpret_cast<const uint8_t*>(&new_cpt_res),  4, reinterpret_cast<const uint8_t*>(&orig_cpt_res));
    }

    // 2. 60 FPS Frame-Pacing Unlocker (RVA 0x20BCAF in BeginFrame +0x20B9C0) & D3D12 Placed-Resource Texture Pool Expansion (+0x2A9DE0):
    //    Prevents any single >16.67ms frame from setting g_FramesBehind ([0x14030F270]) >= 1 and trapping
    //    the game in a permanent 2-ticks-per-VBlank 30.0 FPS catch-up loop.
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(g_ExeBase + AczRva(0x20BCAF));
        __try {
            if (p[0] == 0x42 && p[1] == 0x8D && p[2] == 0x44 && p[3] == 0x09 && p[4] == 0xFF) {
                DWORD old_prot = 0;
                if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old_prot)) {
                    p[0] = 0x31; // xor eax, eax
                    p[1] = 0xC0;
                    p[2] = 0x90; // nop
                    p[3] = 0x90; // nop
                    p[4] = 0x90; // nop
                    VirtualProtect(p, 5, old_prot, &old_prot);
                    FlushInstructionCache(GetCurrentProcess(), p, 5);
                }
                if (p[5] == 0x89 && p[6] == 0x05) {
                    int32_t disp = *reinterpret_cast<const int32_t*>(p + 7);
                    int32_t* pFramesBehind = reinterpret_cast<int32_t*>(p + 11 + disp);
                    *pFramesBehind = 0;
                }
                LogMsg("[Enhancer] Applied 60 FPS frame-pacing unlocker at RVA 0x20BCAF (disabled 30 FPS catch-up trap).");
            }
            // Expand D3D12 placed-resource texture pools (+0x2A9DE0)
            uint32_t* pool_tbl = reinterpret_cast<uint32_t*>(g_ExeBase + AczRva(0x2A9DE0));
            if (pool_tbl[0] == 0x04000000u && pool_tbl[1] == 1u &&
                pool_tbl[2] == 0x02040000u && pool_tbl[3] == 8u &&
                pool_tbl[4] == 0x00420000u && pool_tbl[5] == 8u) {
                DWORD old_prot = 0;
                if (VirtualProtect(pool_tbl, 64, PAGE_READWRITE, &old_prot)) {
                    pool_tbl[1]  = 8u;         // Pool[0] (64 MB):   1 -> 8    (512 MB)
                    pool_tbl[3]  = 32u;        // Pool[1] (32 MB):   8 -> 32   (1032 MB)
                    pool_tbl[4]  = 0x00640000u;// Pool[2] slot size: 4.125 MB (0x00420000) -> 6.25 MB (0x00640000) for 16:9 (4.75 MB) & 21:9 (6.125 MB) BC7!
                    pool_tbl[5]  = 128u;       // Pool[2] (6.25 MB): 8 -> 128  (800 MB)
                    pool_tbl[7]  = 128u;       // Pool[3] (2.7 MB):  64 -> 128 (344 MB)
                    pool_tbl[9]  = 256u;       // Pool[4] (1.1 MB):  64 -> 256 (272 MB)
                    pool_tbl[11] = 512u;       // Pool[5] (320 KB):  128 -> 512 (160 MB)
                    VirtualProtect(pool_tbl, 64, old_prot, &old_prot);
                    LogMsg("[Enhancer] Expanded D3D12 texture pool capacities (64MB: 8, 32MB: 32, 6.25MB: 128, 2.7MB: 128, 1.1MB: 256, 320KB: 512).");
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    // 3. Install Widescreen 16:9 / 21:9 Menus, HUD, Radio/Cutscene Subtitles, Debriefing, Video, 3D FOV & Input hooks
    InstallWidescreenAndUiHooks();
}

// ----------------------------------------------------------------------------
// Optional Cross-DLL Diagnostic Summary Export (Used by memdump.dll if loaded)
// ----------------------------------------------------------------------------
extern "C" __declspec(dllexport) void __fastcall AczEnhancer_WriteStateSummary(const char* report_path_a) {
    if (!report_path_a || !report_path_a[0]) return;
    FILE* rf = nullptr;
    fopen_s(&rf, report_path_a, "ab");
    if (!rf) return;

    const int aspect_opt = GetGameAspectOption();
    const char* aspect_str = (aspect_opt == 2) ? "21:9 (Ultrawide)" : ((aspect_opt == 1) ? "16:9 (Widescreen)" : "4:3 (Standard)");

    fprintf(rf, "--------------------------------------------------------------------------------\n");
    fprintf(rf, "ACZERO ENHANCER (enhancer.dll %s) ACTIVE STATE:\n", ENHANCER_VERSION_STR);
    fprintf(rf, "  EnableMenuOverrides:     %s (MenuAspectMode: %d)\n",
            g_EnableMenuOverrides ? "ENABLED" : "DISABLED", g_MenuAspectMode);
    fprintf(rf, "  Active Screen Aspect:    %s (option=%d)\n", aspect_str, aspect_opt);
    fprintf(rf, "  Unlock2PlayerCampaign:   %s\n",
            g_Unlock2PlayerCampaign ? "ENABLED (2P Split: Pad=P1, KB=P2)" : "DISABLED (Single-Player Merged Pad+KB=P1)");
    fclose(rf);
}

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------
static void InitializeEnhancerPlugin(const AczAddonApi* api) {
    bool expected = false;
    if (!g_PluginInitialized.compare_exchange_strong(expected, true)) {
        return;
    }

    if (api != nullptr && api->struct_size >= sizeof(AczAddonApi) && api->log_fn != nullptr) {
        g_ModLoaderLogFn = api->log_fn;
    }

    char exe_path_buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_path_buf, MAX_PATH);
    g_GameDir         = fs::path(exe_path_buf).parent_path();
    g_ModsDir         = g_GameDir / "mods";
    g_TexturesDir     = g_ModsDir / "textures";
    g_EnhancerDir     = g_ModsDir / "enhancer";
    g_EnhancerIniPath = g_EnhancerDir / "enhancer.ini";

    std::error_code ec;
    fs::create_directories(g_EnhancerDir, ec);

    LoadEnhancerConfigIni();

    if (g_EnableLogging && g_ModLoaderLogFn == nullptr) {
        fs::path modloader_log_path = g_EnhancerDir / "enhancer.log";
        g_FallbackLogFile = _fsopen(modloader_log_path.string().c_str(), "a", _SH_DENYNO);
    }

    g_ExeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    g_AczVersion = DetectAczExeVersion(g_ExeBase);
    LogMsg("[Enhancer] ================================================================");
    LogMsg("[Enhancer]  ACZero Enhancer — Widescreen, HUD, Input & 60 FPS Engine %s", ENHANCER_VERSION_STR);
    LogMsg("[Enhancer] ================================================================");
    LogMsg("[Enhancer] Game executable base: %p (Detected version: %s)",
           reinterpret_cast<void*>(g_ExeBase), GetAczExeVersionString(g_AczVersion));
    LogMsg("[Enhancer] Config file:          %s", g_EnhancerIniPath.string().c_str());
    LogMsg("[Enhancer] Config: EnableLogging=%d, EnableMenuOverrides=%d, MenuAspectMode=%d, Unlock2PlayerCampaign=%d, ScreenAspect=%d",
           g_EnableLogging ? 1 : 0,
           g_EnableMenuOverrides ? 1 : 0,
           g_MenuAspectMode,
           g_Unlock2PlayerCampaign ? 1 : 0,
           g_ScreenAspect);

    if (g_ExeBase != 0) {
        *reinterpret_cast<int32_t*>(g_ExeBase + AczRva(RVA_ScreenSizeOption)) = g_ScreenAspect;
    }

    ApplyEnhancerPatchesAndHooks();
}

// ----------------------------------------------------------------------------
// ASI & Add-on Entry Points (Called by version.dll ASI Loader)
// ----------------------------------------------------------------------------
extern "C" __declspec(dllexport) void __fastcall AczAddonInit(const AczAddonApi* api) {
    InitializeEnhancerPlugin(api);
}

extern "C" __declspec(dllexport) void __cdecl InitializeASI() {
    InitializeEnhancerPlugin(nullptr);
}

// ----------------------------------------------------------------------------
// DLL Entry Point
// ----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_SelfModule = hModule;
        DisableThreadLibraryCalls(hModule);
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        LogMsg("[Enhancer] Shutting down.");
        if (g_FallbackLogFile != nullptr) {
            fclose(g_FallbackLogFile);
            g_FallbackLogFile = nullptr;
        }
    }
    return TRUE;
}
