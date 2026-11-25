#pragma once
// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — Live Display & Aspect Engine
// File: acz_display_engine.h
// ============================================================================
// Provides:
//   1. Startup setting.ini whitelist bypass (accepts any WxH >= 640x480,
//      including 1920x822, 16:10, 21:9, 32:9, 4:3, 5:4) on v1.0.2.1 & v2.0.0.1.
//   2. Disables the stock < 1.6778f viewport zoom/crop hack in
//      CreateWindowSizeDependentResources so all aspect ratios map 1:1.
//   3. Clean Win32 Windowed & multi-monitor native Borderless controller
//      (GWLP_WNDPROC subclass fixing WM_GETMINMAXINFO + Alt+Enter live toggle).
//   4. Zero-reload live SwapChain + 26 offscreen GS RenderTarget rebuild
//      between frames (WaitForGpu -> SwapChain::ResizeBuffers -> ClearRtMap
//      -> RT #0 + CreateAllGsRTs at native WxH, capped <= 3840px for 16-bit
//      XYZ2 safety). 100% compatible with acz_hdtex and acz_radial_fog.
//   5. Continuous arbitrary aspect ratio source (AUTO / 4:3 / 16:10 / 16:9 /
//      21:9 / 32:9) + 10-second revert state + settings/setting.ini sync.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace acz_display {

struct ResolutionEntry {
    uint32_t width  = 1920;
    uint32_t height = 1080;
    std::string label;
};

enum AspectOverrideMode : int {
    kAspectAuto  = 0,
    kAspect4x3   = 1,
    kAspect16x10 = 2,
    kAspect16x9  = 3,
    kAspect21x9  = 4,
    kAspect32x9  = 5,
    kAspectCount = 6
};

using FnRvaMapper          = uintptr_t (*)(uintptr_t rva_1021);
using FnLog                = void (*)(const char* fmt, ...);
using FnInstallDetour      = bool (*)(const char* name, uintptr_t target_addr, const uint8_t* expected_prologue, size_t prologue_len, const void* hook_fn, void** out_trampoline);
using FnVoidCallback       = void (*)();

struct HostContext {
    uintptr_t       exe_base              = 0;
    std::filesystem::path game_dir;
    FnRvaMapper     acz_rva               = nullptr;
    FnLog           log_msg               = nullptr;
    FnInstallDetour install_hook          = nullptr;
    FnVoidCallback  on_invalidate_ui_rtv  = nullptr;
    FnVoidCallback  on_aspect_changed     = nullptr;
    FnVoidCallback  on_save_enhancer_ini  = nullptr;
};

// Native acz.exe function signatures
using FnInitD3D12Subsystem = void (__fastcall*)(uint32_t virtual_w, uint32_t virtual_h);
using FnBeginFrame         = void (__fastcall*)();
using FnWaitForGpu         = void (__fastcall*)(void* device_resources);
using FnClearRtMap         = void (__fastcall*)(void* rt_hash_map);
using FnCreateOffscreenRT  = int32_t (__fastcall*)(int32_t width, int32_t height, uintptr_t unused_r8, uint8_t has_depth, uint8_t flag5, int32_t use_msaa);
using FnCreateAllGsRTs     = void (__fastcall*)(void* gs_emu_obj);

// Constants
static constexpr uint32_t kMinW = 640u;
static constexpr uint32_t kMinH = 480u;
static constexpr uint32_t kMaxOutW = 7680u;
static constexpr uint32_t kMaxOutH = 4320u;
static constexpr uint32_t kMaxGsInternalW = 3840u; // Safe below 16-bit XYZ2 overflow limit (3967)
static constexpr uint32_t kMaxGsInternalH = 3838u; // Safe below 16-bit XYZ2 overflow limit (3839)

// Global module state
inline HostContext g_Host{};
inline bool        g_Installed = false;

// Dynamically resolved engine addresses (version-independent for v1.0.2.1 & v2.0.0.1)
inline uintptr_t   g_AddrDeviceResourcesPtr = 0; // uint8_t** (0x3589D8)
inline uintptr_t   g_AddrD3D12CtxPtr        = 0; // uint8_t** (0x33DA48)
inline uintptr_t   g_AddrRenderWidth        = 0; // uint32_t* (0x30F274)
inline uintptr_t   g_AddrRenderHeight       = 0; // uint32_t* (0x30F278)
inline uintptr_t   g_AddrClientW            = 0; // uint32_t* (0x356748)
inline uintptr_t   g_AddrClientH            = 0; // uint32_t* (0x35674C)
inline uintptr_t   g_AddrGsInternalW        = 0; // uint32_t* (0x3566C8)
inline uintptr_t   g_AddrGsInternalH        = 0; // uint32_t* (0x3566CC)
inline uintptr_t   g_AddrIs4x3Window        = 0; // uint8_t*  (0x32C937)
inline uintptr_t   g_AddrIsBorderlessActive = 0; // uint8_t*  (0x32C936)
inline uintptr_t   g_AddrHwndGlobal         = 0; // HWND*     (0x3589E8)
inline uintptr_t   g_AddrGsMainPtr          = 0; // uint8_t** (0x31CF20)
inline uintptr_t   g_AddrGsEmuObj           = 0; // void*     (0x31CF38)
inline uintptr_t   g_AddrRtHashMap          = 0; // void*     (0x3566E8)
inline uintptr_t   g_AddrFeedbackRtCache    = 0; // uint64_t* (0x31D750)

inline FnInitD3D12Subsystem g_OrigInitD3D12Subsystem = nullptr;
inline FnBeginFrame         g_OrigBeginFrame         = nullptr;
inline FnWaitForGpu         g_FnWaitForGpu           = nullptr;
inline FnClearRtMap         g_FnClearRtMap           = nullptr;
inline FnCreateOffscreenRT  g_FnCreateOffscreenRT    = nullptr;
inline FnCreateAllGsRTs     g_FnCreateAllGsRTs       = nullptr;

// Win32 subclassing state
inline HWND    g_SubclassedHwnd = nullptr;
inline WNDPROC g_OrigWndProc    = nullptr;
inline bool    g_SuppressWndProcResize = false;

// Active & configured display state
inline bool     g_ActiveBorderless   = false;
inline uint32_t g_ActiveOutW         = 1920u;
inline uint32_t g_ActiveOutH         = 1080u;
inline uint32_t g_ActiveGsW          = 1920u;
inline uint32_t g_ActiveGsH          = 1080u;
inline uint32_t g_WindowedW          = 1920u;
inline uint32_t g_WindowedH          = 1080u;
inline uint32_t g_MonitorNativeW     = 1920u;
inline uint32_t g_MonitorNativeH     = 1080u;
inline int      g_AspectOverride     = kAspectAuto;
inline bool     g_SettingIniLoaded   = false;

// Live pending apply / RT rebuild state (consumed on render thread at frame boundary)
inline std::atomic<bool> g_PendingApply{false};
inline bool              g_PendingBorderless    = false;
inline uint32_t          g_PendingWindowW       = 1920u;
inline uint32_t          g_PendingWindowH       = 1080u;
inline bool              g_PendingWriteIniNow   = false;

inline bool              g_NeedRtRebuild        = false;
inline uint32_t          g_TargetGsW            = 1920u;
inline uint32_t          g_TargetGsH            = 1080u;

// Revert snapshot (for 10-second confirmation dialog)
inline bool     g_HasRevertSnapshot  = false;
inline bool     g_RevertBorderless   = false;
inline uint32_t g_RevertWindowW      = 1920u;
inline uint32_t g_RevertWindowH      = 1080u;

// Enumerated resolutions for UI selector
inline std::vector<ResolutionEntry> g_Resolutions;

// ----------------------------------------------------------------------------
// Internal Helpers
// ----------------------------------------------------------------------------
inline uintptr_t DecodeRipDisp32(uintptr_t instr_addr, size_t instr_len, size_t disp_offset) {
    int32_t disp = *reinterpret_cast<const int32_t*>(instr_addr + disp_offset);
    return static_cast<uintptr_t>(static_cast<intptr_t>(instr_addr + instr_len) + static_cast<intptr_t>(disp));
}

inline bool PatchRawBytes(uintptr_t addr, const uint8_t* bytes, size_t len) {
    if (!addr || !bytes || len == 0) return false;
    DWORD old_prot = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), len, PAGE_EXECUTE_READWRITE, &old_prot)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(addr), bytes, len);
    VirtualProtect(reinterpret_cast<void*>(addr), len, old_prot, &old_prot);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), len);
    return true;
}

inline void ComputeSafeInternalSize(uint32_t outW, uint32_t outH, uint32_t& outGsW, uint32_t& outGsH) {
    outW = std::clamp(outW, kMinW, kMaxOutW);
    outH = std::clamp(outH, kMinH, kMaxOutH);
    if (outW <= kMaxGsInternalW && outH <= kMaxGsInternalH) {
        outGsW = (outW & ~1u);
        outGsH = (outH & ~1u);
    } else {
        const double sx = static_cast<double>(kMaxGsInternalW) / static_cast<double>(outW);
        const double sy = static_cast<double>(kMaxGsInternalH) / static_cast<double>(outH);
        const double s  = (std::min)(sx, sy);
        uint32_t gw = static_cast<uint32_t>(std::lround(static_cast<double>(outW) * s)) & ~1u;
        uint32_t gh = static_cast<uint32_t>(std::lround(static_cast<double>(outH) * s)) & ~1u;
        outGsW = std::clamp(gw, kMinW, kMaxGsInternalW);
        outGsH = std::clamp(gh, kMinH, kMaxGsInternalH);
    }
    if (outGsW < kMinW) outGsW = kMinW;
    if (outGsH < kMinH) outGsH = kMinH;
}

inline HWND GetGameHwnd() {
    if (g_SubclassedHwnd && IsWindow(g_SubclassedHwnd)) return g_SubclassedHwnd;
    if (g_AddrHwndGlobal) {
        __try {
            HWND h = *reinterpret_cast<HWND*>(g_AddrHwndGlobal);
            if (h && IsWindow(h)) return h;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_AddrD3D12CtxPtr) {
        __try {
            uint8_t* ctx = *reinterpret_cast<uint8_t**>(g_AddrD3D12CtxPtr);
            if (ctx) {
                HWND h = *reinterpret_cast<HWND*>(ctx + 0x108);
                if (h && IsWindow(h)) return h;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return nullptr;
}

inline uint8_t* GetD3D12DeviceResources() {
    if (g_AddrD3D12CtxPtr) {
        __try {
            uint8_t* p = *reinterpret_cast<uint8_t**>(g_AddrD3D12CtxPtr);
            if (p) return p;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_AddrDeviceResourcesPtr) {
        __try {
            uint8_t* p = *reinterpret_cast<uint8_t**>(g_AddrDeviceResourcesPtr);
            if (p) return p;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return nullptr;
}

inline bool QueryMonitorInfoForHwnd(HWND hwnd, MONITORINFOEXW& outMi) {
    std::memset(&outMi, 0, sizeof(outMi));
    outMi.cbSize = sizeof(outMi);
    HMONITOR hMon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
                         : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    if (!hMon || !GetMonitorInfoW(hMon, &outMi)) {
        outMi.rcMonitor = RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        outMi.rcWork    = outMi.rcMonitor;
        return false;
    }
    return true;
}

inline void RefreshMonitorNativeSize(HWND hwnd = nullptr) {
    MONITORINFOEXW mi{};
    QueryMonitorInfoForHwnd(hwnd ? hwnd : GetGameHwnd(), mi);
    uint32_t mw = static_cast<uint32_t>((std::max)(640L, mi.rcMonitor.right - mi.rcMonitor.left));
    uint32_t mh = static_cast<uint32_t>((std::max)(480L, mi.rcMonitor.bottom - mi.rcMonitor.top));
    g_MonitorNativeW = mw;
    g_MonitorNativeH = mh;
}

// ----------------------------------------------------------------------------
// settings/setting.ini Reader & Comment-Preserving Writer
// ----------------------------------------------------------------------------
inline std::filesystem::path GetSettingIniPath() {
    return g_Host.game_dir / "settings" / "setting.ini";
}

inline void LoadFromSettingIni() {
    g_SettingIniLoaded = false;
    std::filesystem::path p = GetSettingIniPath();
    std::ifstream in(p);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Resolution=", 0) == 0) {
            unsigned int w = 0, h = 0;
            if (std::sscanf(line.c_str() + 11, "%ux%u", &w, &h) == 2 ||
                std::sscanf(line.c_str() + 11, "%uX%u", &w, &h) == 2) {
                if (w >= kMinW && h >= kMinH && w <= kMaxOutW && h <= kMaxOutH) {
                    g_WindowedW = w;
                    g_WindowedH = h;
                    g_SettingIniLoaded = true;
                }
            }
        } else if (line.rfind("ScreenMode=", 0) == 0) {
            std::string val = line.substr(11);
            size_t cut = val.find_first_of(" \t;");
            if (cut != std::string::npos) val.resize(cut);
            if (_stricmp(val.c_str(), "Borderless") == 0) {
                g_ActiveBorderless = true;
            } else if (_stricmp(val.c_str(), "Windowed") == 0) {
                g_ActiveBorderless = false;
            }
        }
    }
}

inline void SaveToSettingIni(uint32_t windowedW, uint32_t windowedH, bool borderless) {
    std::filesystem::path p = GetSettingIniPath();
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    std::vector<std::string> lines;
    {
        std::ifstream in(p);
        if (in.is_open()) {
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }
    }

    char resBuf[64];
    std::snprintf(resBuf, sizeof(resBuf), "%ux%u", windowedW, windowedH);
    const char* modeStr = borderless ? "Borderless" : "Windowed";

    bool foundRes  = false;
    bool foundMode = false;

    auto replaceKeyValue = [](std::string& line, const char* prefix, const char* newVal) {
        const size_t prefLen = std::strlen(prefix);
        size_t tailPos = line.find_first_of(" \t;", prefLen);
        std::string tail = (tailPos != std::string::npos) ? line.substr(tailPos) : std::string();
        line = std::string(prefix) + newVal + tail;
    };

    for (auto& line : lines) {
        if (line.rfind("Resolution=", 0) == 0) {
            replaceKeyValue(line, "Resolution=", resBuf);
            foundRes = true;
        } else if (line.rfind("ScreenMode=", 0) == 0) {
            replaceKeyValue(line, "ScreenMode=", modeStr);
            foundMode = true;
        }
    }
    if (!foundRes) {
        lines.insert(lines.begin(), std::string("Resolution=") + resBuf);
    }
    if (!foundMode) {
        lines.push_back(std::string("ScreenMode=") + modeStr);
    }

    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
        for (size_t i = 0; i < lines.size(); ++i) {
            out << lines[i] << "\r\n";
        }
        out.flush();
        if (g_Host.log_msg) {
            g_Host.log_msg("[Display] Saved settings/setting.ini (Resolution=%s, ScreenMode=%s)", resBuf, modeStr);
        }
    }
}

// ----------------------------------------------------------------------------
// Resolution List Enumeration (Monitor Modes + Active Custom Resolution)
// ----------------------------------------------------------------------------
inline void RefreshResolutionList(HWND hwnd = nullptr) {
    MONITORINFOEXW mi{};
    const bool hasMon = QueryMonitorInfoForHwnd(hwnd ? hwnd : GetGameHwnd(), mi);
    g_MonitorNativeW = static_cast<uint32_t>((std::max)(640L, mi.rcMonitor.right - mi.rcMonitor.left));
    g_MonitorNativeH = static_cast<uint32_t>((std::max)(480L, mi.rcMonitor.bottom - mi.rcMonitor.top));

    static constexpr uint32_t kListMinW = 1280u;
    static constexpr uint32_t kListMinH = 720u;

    std::vector<std::pair<uint32_t, uint32_t>> raw;
    auto addUnique = [&](uint32_t w, uint32_t h) {
        if (w < kListMinW || h < kListMinH || w > kMaxOutW || h > kMaxOutH) return;
        for (const auto& p : raw) {
            if (p.first == w && p.second == h) return;
        }
        raw.emplace_back(w, h);
    };

    // 1. Enumerate actual monitor display modes (>= 1280x720)
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    const wchar_t* devName = (hasMon && mi.szDevice[0] != L'\0') ? mi.szDevice : nullptr;
    for (DWORD idx = 0; EnumDisplaySettingsW(devName, idx, &dm); ++idx) {
        if (dm.dmBitsPerPel == 32 || dm.dmBitsPerPel == 0) {
            addUnique(dm.dmPelsWidth, dm.dmPelsHeight);
        }
    }

    // 2. Always include monitor native, current active, and saved windowed resolution (>= 1280x720)
    addUnique(g_MonitorNativeW, g_MonitorNativeH);
    addUnique(g_WindowedW, g_WindowedH);
    addUnique(g_ActiveOutW, g_ActiveOutH);

    // 3. Add standard 16:9 / 16:10, 4:3, and 21:9 windowed presets that fit within the monitor
    static constexpr uint32_t kPresets[][2] = {
        // 16:9 / 16:10
        { 1280u,  720u },
        { 1280u,  800u },
        { 1600u,  900u },
        { 1920u, 1080u },
        { 1920u, 1200u },
        { 2560u, 1440u },
        { 3840u, 2160u },
        // 4:3
        { 1280u,  960u },
        { 1440u, 1080u },
        { 1920u, 1440u },
        { 2880u, 2160u },
        // 21:9
        { 1680u,  720u },
        { 1920u,  822u },
        { 2560u, 1080u },
        { 3440u, 1440u },
        { 3840u, 1600u },
        { 5120u, 2160u },
    };
    for (const auto& pr : kPresets) {
        if (pr[0] <= g_MonitorNativeW && pr[1] <= g_MonitorNativeH) {
            addUnique(pr[0], pr[1]);
        }
    }

    // 4. Dynamically generate matching 4:3 and 21:9 counterparts for every enumerated resolution (including custom monitor sizes)
    const std::vector<std::pair<uint32_t, uint32_t>> baseModes = raw;
    for (const auto& m : baseModes) {
        const uint32_t mw = m.first;
        const uint32_t mh = m.second;
        // 4:3 at same height
        const uint32_t w43 = static_cast<uint32_t>(std::lround(static_cast<double>(mh) * (4.0 / 3.0))) & ~1u;
        if (w43 >= kListMinW && w43 <= g_MonitorNativeW && mh <= g_MonitorNativeH) {
            addUnique(w43, mh);
        }
        // 21:9 at same height (if it fits monitor width)
        const uint32_t w219 = static_cast<uint32_t>(std::lround(static_cast<double>(mh) * (21.0 / 9.0))) & ~1u;
        if (w219 >= kListMinW && w219 <= g_MonitorNativeW && mh <= g_MonitorNativeH) {
            addUnique(w219, mh);
        }
        // 21:9 letterboxed at same width (e.g. 1826x782 on 1826x1026 monitor, 1920x822 on 1920x1080)
        const uint32_t h219 = static_cast<uint32_t>(std::lround(static_cast<double>(mw) * (9.0 / 21.0))) & ~1u;
        if (mw >= kListMinW && h219 >= kListMinH && mw <= g_MonitorNativeW && h219 <= g_MonitorNativeH) {
            addUnique(mw, h219);
        }
    }

    // Sort descending by area, then width
    std::sort(raw.begin(), raw.end(), [](const auto& a, const auto& b) {
        const uint64_t areaA = static_cast<uint64_t>(a.first) * a.second;
        const uint64_t areaB = static_cast<uint64_t>(b.first) * b.second;
        if (areaA != areaB) return areaA > areaB;
        return a.first > b.first;
    });

    auto FormatAspectTag = [](uint32_t w, uint32_t h) -> const char* {
        const double r = static_cast<double>(w) / static_cast<double>(h);
        if (std::fabs(r - (4.0  / 3.0))  < 0.04) return " (4:3)";
        if (std::fabs(r - (5.0  / 4.0))  < 0.04) return " (5:4)";
        if (std::fabs(r - (16.0 / 10.0)) < 0.04) return " (16:10)";
        if (std::fabs(r - (16.0 / 9.0))  < 0.04) return " (16:9)";
        if (r >= 2.25 && r <= 2.45)              return " (21:9)";
        if (r >= 3.45 && r <= 3.65)              return " (32:9)";
        return "";
    };

    g_Resolutions.clear();
    g_Resolutions.reserve(raw.size());
    for (const auto& p : raw) {
        ResolutionEntry e{};
        e.width  = p.first;
        e.height = p.second;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%ux%u%s", e.width, e.height, FormatAspectTag(e.width, e.height));
        e.label = buf;
        g_Resolutions.push_back(std::move(e));
    }
}

inline int FindResolutionIndex(uint32_t w, uint32_t h) {
    if (g_Resolutions.empty()) RefreshResolutionList();
    for (int i = 0; i < static_cast<int>(g_Resolutions.size()); ++i) {
        if (g_Resolutions[i].width == w && g_Resolutions[i].height == h) return i;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Continuous Aspect Ratio Queries
// ----------------------------------------------------------------------------
inline float GetPhysicalOutputAspect() {
    uint32_t w = g_ActiveOutW;
    uint32_t h = g_ActiveOutH;
    if (g_AddrRenderWidth && g_AddrRenderHeight) {
        __try {
            uint32_t rw = *reinterpret_cast<const uint32_t*>(g_AddrRenderWidth);
            uint32_t rh = *reinterpret_cast<const uint32_t*>(g_AddrRenderHeight);
            if (rw >= kMinW && rh >= kMinH) { w = rw; h = rh; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (w == 0 || h == 0) return 16.0f / 9.0f;
    return static_cast<float>(w) / static_cast<float>(h);
}

inline float GetEffectiveAspectRatio() {
    // Always derive aspect ratio automatically from the active output resolution
    return GetPhysicalOutputAspect();
}

// Maps continuous aspect ratio to engine tier (0 = 4:3, 1 = Widescreen, 2 = Ultrawide)
inline int GetAspectTier012() {
    const float a = GetEffectiveAspectRatio();
    if (a <= 1.38f) return 0;
    if (a <= 1.85f) return 1;
    return 2;
}

inline int GetAspectOverrideMode() { return kAspectAuto; }
inline void SetAspectOverrideMode(int) {
    g_AspectOverride = kAspectAuto;
    if (g_Host.on_aspect_changed) g_Host.on_aspect_changed();
}

// ----------------------------------------------------------------------------
// Win32 Window Geometry & Style Controller
// ----------------------------------------------------------------------------
inline void ApplyWindowModeAndSize(HWND hwnd, bool borderless, uint32_t winW, uint32_t winH, uint32_t& outActualW, uint32_t& outActualH) {
    outActualW = std::clamp(winW, kMinW, kMaxOutW);
    outActualH = std::clamp(winH, kMinH, kMaxOutH);
    if (!hwnd || !IsWindow(hwnd)) return;

    MONITORINFOEXW mi{};
    QueryMonitorInfoForHwnd(hwnd, mi);
    const LONG monX = mi.rcMonitor.left;
    const LONG monY = mi.rcMonitor.top;
    const LONG monW = (std::max)(640L, mi.rcMonitor.right - mi.rcMonitor.left);
    const LONG monH = (std::max)(480L, mi.rcMonitor.bottom - mi.rcMonitor.top);
    g_MonitorNativeW = static_cast<uint32_t>(monW);
    g_MonitorNativeH = static_cast<uint32_t>(monH);

    g_SuppressWndProcResize = true;
    if (borderless) {
        // Borderless ALWAYS uses native monitor resolution (100% pixel-perfect, multi-monitor aware)
        if (IsZoomed(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        }
        const LONG_PTR style = static_cast<LONG_PTR>(WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        SetWindowPos(
            hwnd, HWND_TOP,
            static_cast<int>(monX), static_cast<int>(monY),
            static_cast<int>(monW), static_cast<int>(monH),
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW
        );
        outActualW = static_cast<uint32_t>(monW);
        outActualH = static_cast<uint32_t>(monH);
    } else {
        if (IsZoomed(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        }
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE;
        SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(style));

        RECT rc{ 0, 0, static_cast<LONG>(outActualW), static_cast<LONG>(outActualH) };
        AdjustWindowRectEx(&rc, style, FALSE, 0);
        const int outerW = static_cast<int>(rc.right - rc.left);
        const int outerH = static_cast<int>(rc.bottom - rc.top);

        const int workW = static_cast<int>(mi.rcWork.right - mi.rcWork.left);
        const int workH = static_cast<int>(mi.rcWork.bottom - mi.rcWork.top);
        int posX = static_cast<int>(mi.rcWork.left) + (workW - outerW) / 2;
        int posY = static_cast<int>(mi.rcWork.top)  + (workH - outerH) / 2;
        if (posX < static_cast<int>(mi.rcWork.left)) posX = static_cast<int>(mi.rcWork.left);
        if (posY < static_cast<int>(mi.rcWork.top))  posY = static_cast<int>(mi.rcWork.top);

        SetWindowPos(
            hwnd, HWND_NOTOPMOST,
            posX, posY, outerW, outerH,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW
        );

        RECT clientRc{};
        if (GetClientRect(hwnd, &clientRc)) {
            const LONG cw = clientRc.right - clientRc.left;
            const LONG ch = clientRc.bottom - clientRc.top;
            if (cw >= static_cast<LONG>(kMinW) && ch >= static_cast<LONG>(kMinH)) {
                outActualW = static_cast<uint32_t>(cw);
                outActualH = static_cast<uint32_t>(ch);
            }
        }
    }
    g_SuppressWndProcResize = false;

    if (g_AddrIsBorderlessActive) {
        __try {
            *reinterpret_cast<uint8_t*>(g_AddrIsBorderlessActive) = borderless ? 1u : 0u;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// ----------------------------------------------------------------------------
// Live Apply / Revert Request API (Thread-Safe, Executed Between Frames)
// ----------------------------------------------------------------------------
inline void RequestLiveDisplayChange(bool borderless, uint32_t winW, uint32_t winH, bool recordRevertSnapshot, bool writeIniImmediately = false) {
    if (recordRevertSnapshot) {
        g_HasRevertSnapshot = true;
        g_RevertBorderless  = g_ActiveBorderless;
        g_RevertWindowW     = g_WindowedW;
        g_RevertWindowH     = g_WindowedH;
    }
    g_PendingBorderless  = borderless;
    g_PendingWindowW     = std::clamp(winW, kMinW, kMaxOutW);
    g_PendingWindowH     = std::clamp(winH, kMinH, kMaxOutH);
    g_PendingWriteIniNow = writeIniImmediately;
    g_PendingApply.store(true, std::memory_order_release);
}

inline void ConfirmDisplayChangeAndSave() {
    g_HasRevertSnapshot = false;
    SaveToSettingIni(g_WindowedW, g_WindowedH, g_ActiveBorderless);
    if (g_Host.on_save_enhancer_ini) g_Host.on_save_enhancer_ini();
}

inline void RevertDisplayChange() {
    if (!g_HasRevertSnapshot) return;
    const bool     rb = g_RevertBorderless;
    const uint32_t rw = g_RevertWindowW;
    const uint32_t rh = g_RevertWindowH;
    g_HasRevertSnapshot = false;
    RequestLiveDisplayChange(rb, rw, rh, false, false);
}

// ----------------------------------------------------------------------------
// Subclassed WndProc (Fixes WM_GETMINMAXINFO Lock & Routes Alt+Enter Cleanly)
// ----------------------------------------------------------------------------
inline LRESULT CALLBACK SubclassWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_GETMINMAXINFO && lParam != 0) {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        // Release acz.exe's locked startup resolution constraint so Windowed/Borderless resize freely
        mmi->ptMinTrackSize.x = static_cast<LONG>(kMinW);
        mmi->ptMinTrackSize.y = static_cast<LONG>(kMinH);
        mmi->ptMaxTrackSize.x = 16384;
        mmi->ptMaxTrackSize.y = 16384;
        return 0;
    }
    if (msg == WM_SYSKEYDOWN && wParam == VK_RETURN && ((lParam & (1LL << 29)) != 0)) {
        // Alt+Enter: toggle Windowed <-> Native Monitor Borderless cleanly with full RT rebuild!
        const bool nextBorderless = !g_ActiveBorderless;
        RequestLiveDisplayChange(nextBorderless, g_WindowedW, g_WindowedH, false, true);
        return 0;
    }
    if (msg == WM_SYSCOMMAND) {
        const WPARAM cmd = wParam & 0xFFF0u;
        if (cmd == SC_MAXIMIZE && !g_ActiveBorderless) {
            RequestLiveDisplayChange(true, g_WindowedW, g_WindowedH, false, true);
            return 0;
        }
        if (cmd == SC_RESTORE && g_ActiveBorderless) {
            RequestLiveDisplayChange(false, g_WindowedW, g_WindowedH, false, true);
            return 0;
        }
    }
    if (msg == WM_SIZE && g_SuppressWndProcResize) {
        return 0;
    }
    if (g_OrigWndProc) {
        return CallWindowProcW(g_OrigWndProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

inline void EnsureWindowSubclassed(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    if (g_SubclassedHwnd == hwnd && g_OrigWndProc != nullptr) return;
    WNDPROC prev = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SubclassWndProc))
    );
    if (prev && prev != &SubclassWndProc) {
        g_OrigWndProc    = prev;
        g_SubclassedHwnd = hwnd;
        if (g_Host.log_msg) {
            g_Host.log_msg("[Display] Subclassed game HWND=%p (WM_GETMINMAXINFO unlocked, Alt+Enter live RT toggle active).", hwnd);
        }
    }
}

// ----------------------------------------------------------------------------
// Safe Inter-Frame Execution Points (Called from EndFrameAndPresent & BeginFrame)
// ----------------------------------------------------------------------------
// Called strictly AFTER g_OrigEndFrameAndPresent() returns (command list is CLOSED):
inline void OnAfterPresent() {
    if (!g_Installed) return;
    if (!g_PendingApply.exchange(false, std::memory_order_acq_rel)) return;

    __try {
        uint8_t* d3d_ctx = GetD3D12DeviceResources();
        HWND hwnd = GetGameHwnd();
        if (!d3d_ctx || !hwnd) return;
        EnsureWindowSubclassed(hwnd);

        // 1. Flush GPU completely before resizing SwapChain or freeing old backbuffers
        if (g_FnWaitForGpu) {
            g_FnWaitForGpu(d3d_ctx);
        }

        // 2. Invalidate UiRenderer cached backbuffer RTV pointers before ResizeBuffers
        if (g_Host.on_invalidate_ui_rtv) {
            g_Host.on_invalidate_ui_rtv();
        }

        // 3. Apply Win32 window style & geometry
        const bool targetBorderless = g_PendingBorderless;
        if (!targetBorderless) {
            g_WindowedW = g_PendingWindowW;
            g_WindowedH = g_PendingWindowH;
        }
        uint32_t actualW = 0, actualH = 0;
        ApplyWindowModeAndSize(hwnd, targetBorderless, g_WindowedW, g_WindowedH, actualW, actualH);

        g_ActiveBorderless = targetBorderless;
        g_ActiveOutW       = actualW;
        g_ActiveOutH       = actualH;

        // 4. Update acz.exe resolution & DeviceResources resize trigger
        if (g_AddrRenderWidth)  *reinterpret_cast<uint32_t*>(g_AddrRenderWidth)  = actualW;
        if (g_AddrRenderHeight) *reinterpret_cast<uint32_t*>(g_AddrRenderHeight) = actualH;
        if (g_AddrClientW)      *reinterpret_cast<uint32_t*>(g_AddrClientW)      = actualW;
        if (g_AddrClientH)      *reinterpret_cast<uint32_t*>(g_AddrClientH)      = actualH;
        if (g_AddrIs4x3Window)  *reinterpret_cast<uint8_t*>(g_AddrIs4x3Window)   = 0u;

        // DeviceResources desiredOutputRect (+0xD4..+0xE0) and m_needsResize (+0xE4)
        *reinterpret_cast<int32_t*>(d3d_ctx + 0xD4) = 0;
        *reinterpret_cast<int32_t*>(d3d_ctx + 0xD8) = 0;
        *reinterpret_cast<int32_t*>(d3d_ctx + 0xDC) = static_cast<int32_t>(actualW);
        *reinterpret_cast<int32_t*>(d3d_ctx + 0xE0) = static_cast<int32_t>(actualH);
        *reinterpret_cast<uint8_t*>(d3d_ctx + 0xE4) = 1u;

        // 5. Queue offscreen GS RenderTarget rebuild for right after BeginFrame opens the command list
        ComputeSafeInternalSize(actualW, actualH, g_TargetGsW, g_TargetGsH);
        g_NeedRtRebuild = true;

        RefreshResolutionList(hwnd);
        if (g_Host.on_aspect_changed) g_Host.on_aspect_changed();

        if (g_PendingWriteIniNow) {
            g_PendingWriteIniNow = false;
            SaveToSettingIni(g_WindowedW, g_WindowedH, g_ActiveBorderless);
        }

        if (g_Host.log_msg) {
            g_Host.log_msg("[Display] Applied %s %ux%u (queued GS RT rebuild -> %ux%u, aspect=%.4f)",
                           g_ActiveBorderless ? "Borderless" : "Windowed",
                           actualW, actualH, g_TargetGsW, g_TargetGsH, GetEffectiveAspectRatio());
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_Host.log_msg) g_Host.log_msg("[Display] WARNING: Exception caught during OnAfterPresent resize.");
    }
}

// Called strictly AFTER g_OrigBeginFrame() returns (SwapChain resized, GPU was flushed, command list is OPEN, g_RtStack = [-1]):
inline void OnAfterBeginFrame() {
    if (!g_NeedRtRebuild) return;
    g_NeedRtRebuild = false;

    __try {
        if (!g_AddrGsInternalW || !g_AddrGsInternalH || !g_FnClearRtMap || !g_FnCreateOffscreenRT || !g_FnCreateAllGsRTs || !g_AddrRtHashMap || !g_AddrGsEmuObj) {
            return;
        }
        const uint32_t curGsW = *reinterpret_cast<const uint32_t*>(g_AddrGsInternalW);
        const uint32_t curGsH = *reinterpret_cast<const uint32_t*>(g_AddrGsInternalH);
        if (curGsW == g_TargetGsW && curGsH == g_TargetGsH) {
            return;
        }

        // 1. Clear cached pointers to old RT map entries (so they are re-resolved cleanly)
        if (g_AddrFeedbackRtCache) {
            *reinterpret_cast<uint64_t*>(g_AddrFeedbackRtCache) = 0;
        }
        if (g_AddrGsMainPtr) {
            uint8_t* gsMain = *reinterpret_cast<uint8_t**>(g_AddrGsMainPtr);
            if (gsMain) {
                *reinterpret_cast<uint64_t*>(gsMain + 0xD0) = 0;
            }
        }

        // 2. Release all existing offscreen RT COM resources cleanly (zero VRAM leak)
        g_FnClearRtMap(reinterpret_cast<void*>(g_AddrRtHashMap));

        // 3. Commit new native internal GS dimensions
        *reinterpret_cast<uint32_t*>(g_AddrGsInternalW) = g_TargetGsW;
        *reinterpret_cast<uint32_t*>(g_AddrGsInternalH) = g_TargetGsH;
        g_ActiveGsW = g_TargetGsW;
        g_ActiveGsH = g_TargetGsH;

        // 4. Recreate RT #0 (1920x1080 FMV/Briefing decode target — always gets slot 0)
        g_FnCreateOffscreenRT(1920, 1080, 0, 0, 0, 0);

        // 5. Recreate all 26 GS RenderTargets at new g_TargetGsW x g_TargetGsH (and clear them on open command list)
        g_FnCreateAllGsRTs(reinterpret_cast<void*>(g_AddrGsEmuObj));

        if (g_Host.on_aspect_changed) g_Host.on_aspect_changed();

        if (g_Host.log_msg) {
            g_Host.log_msg("[Display] Rebuilt all offscreen GS RenderTargets -> %ux%u (was %ux%u).",
                           g_TargetGsW, g_TargetGsH, curGsW, curGsH);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_Host.log_msg) g_Host.log_msg("[Display] WARNING: Exception caught during live GS RT rebuild.");
    }
}

// ----------------------------------------------------------------------------
// Engine Hooks (InitD3D12Subsystem & BeginFrame)
// ----------------------------------------------------------------------------
inline void __fastcall Hook_InitD3D12Subsystem(uint32_t virtual_w, uint32_t virtual_h) {
    uint32_t targetOutW = 1920u;
    uint32_t targetOutH = 1080u;
    bool     isBorderless = false;

    __try {
        uint8_t* d3d_ctx = GetD3D12DeviceResources();
        HWND hwnd = GetGameHwnd();
        if (hwnd) {
            EnsureWindowSubclassed(hwnd);
        }
        if (g_AddrIsBorderlessActive) {
            isBorderless = (*reinterpret_cast<const uint8_t*>(g_AddrIsBorderlessActive) != 0);
        } else {
            isBorderless = g_ActiveBorderless;
        }
        if (g_AddrRenderWidth && g_AddrRenderHeight) {
            uint32_t rw = *reinterpret_cast<const uint32_t*>(g_AddrRenderWidth);
            uint32_t rh = *reinterpret_cast<const uint32_t*>(g_AddrRenderHeight);
            if (rw >= kMinW && rh >= kMinH && rw <= kMaxOutW && rh <= kMaxOutH) {
                targetOutW = rw;
                targetOutH = rh;
                if (!g_SettingIniLoaded) {
                    g_WindowedW = rw;
                    g_WindowedH = rh;
                }
            }
        }

        if (hwnd) {
            RefreshMonitorNativeSize(hwnd);
            if (isBorderless) {
                // Enforce true native monitor Borderless right from startup
                ApplyWindowModeAndSize(hwnd, true, g_WindowedW, g_WindowedH, targetOutW, targetOutH);
            } else {
                // Re-center / verify exact client rect for custom windowed resolutions (e.g. 1920x822)
                RECT cr{};
                if (GetClientRect(hwnd, &cr)) {
                    const uint32_t cw = static_cast<uint32_t>((std::max)(0L, cr.right - cr.left));
                    const uint32_t ch = static_cast<uint32_t>((std::max)(0L, cr.bottom - cr.top));
                    if (cw != targetOutW || ch != targetOutH) {
                        ApplyWindowModeAndSize(hwnd, false, targetOutW, targetOutH, targetOutW, targetOutH);
                    }
                }
            }
        }

        if (g_AddrRenderWidth)  *reinterpret_cast<uint32_t*>(g_AddrRenderWidth)  = targetOutW;
        if (g_AddrRenderHeight) *reinterpret_cast<uint32_t*>(g_AddrRenderHeight) = targetOutH;
        if (g_AddrClientW)      *reinterpret_cast<uint32_t*>(g_AddrClientW)      = targetOutW;
        if (g_AddrClientH)      *reinterpret_cast<uint32_t*>(g_AddrClientH)      = targetOutH;
        if (g_AddrIs4x3Window)  *reinterpret_cast<uint8_t*>(g_AddrIs4x3Window)   = 0u;

        if (d3d_ctx) {
            *reinterpret_cast<int32_t*>(d3d_ctx + 0xD4) = 0;
            *reinterpret_cast<int32_t*>(d3d_ctx + 0xD8) = 0;
            *reinterpret_cast<int32_t*>(d3d_ctx + 0xDC) = static_cast<int32_t>(targetOutW);
            *reinterpret_cast<int32_t*>(d3d_ctx + 0xE0) = static_cast<int32_t>(targetOutH);
            *reinterpret_cast<uint8_t*>(d3d_ctx + 0xE4) = 1u;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    if (g_OrigInitD3D12Subsystem) {
        g_OrigInitD3D12Subsystem(virtual_w, virtual_h);
    }

    // Override stock hardcoded (H * 16 / 9) internal GS RT width with true native aspect dimensions!
    __try {
        uint32_t gsW = 1920u, gsH = 1080u;
        ComputeSafeInternalSize(targetOutW, targetOutH, gsW, gsH);
        if (g_AddrGsInternalW) *reinterpret_cast<uint32_t*>(g_AddrGsInternalW) = gsW;
        if (g_AddrGsInternalH) *reinterpret_cast<uint32_t*>(g_AddrGsInternalH) = gsH;
        if (g_AddrIs4x3Window) *reinterpret_cast<uint8_t*>(g_AddrIs4x3Window)  = 0u;

        g_ActiveBorderless = isBorderless;
        g_ActiveOutW       = targetOutW;
        g_ActiveOutH       = targetOutH;
        g_ActiveGsW        = gsW;
        g_ActiveGsH        = gsH;
        RefreshResolutionList(GetGameHwnd());

        if (g_Host.on_aspect_changed) g_Host.on_aspect_changed();
        if (g_Host.log_msg) {
            g_Host.log_msg("[Display] Startup D3D12 initialized: Mode=%s, Output=%ux%u, InternalGS=%ux%u, EffectiveAspect=%.4f",
                           g_ActiveBorderless ? "Borderless" : "Windowed",
                           g_ActiveOutW, g_ActiveOutH, g_ActiveGsW, g_ActiveGsH, GetEffectiveAspectRatio());
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

inline void __fastcall Hook_BeginFrame() {
    if (g_OrigBeginFrame) {
        g_OrigBeginFrame();
    }
    OnAfterBeginFrame();
}

// ----------------------------------------------------------------------------
// Startup Patches (LoadSettingIni Whitelist Bypass & < 1.6778f Crop Removal)
// ----------------------------------------------------------------------------
inline void PatchLoadSettingIniWhitelistAOB(uintptr_t exe_base) {
    // Signature of LoadSettingIni height whitelist:
    //   cmp r9d, 2160; je +0x3E; cmp r9d, 1800; je +0x35; cmp r9d, 1440; je +0x2C
    static const uint8_t kSig[27] = {
        0x41, 0x81, 0xF9, 0x70, 0x08, 0x00, 0x00, 0x74, 0x3E,
        0x41, 0x81, 0xF9, 0x08, 0x07, 0x00, 0x00, 0x74, 0x35,
        0x41, 0x81, 0xF9, 0xA0, 0x05, 0x00, 0x00, 0x74, 0x2C
    };
    const uintptr_t scanStart = exe_base + 0x21D000u;
    const uintptr_t scanEnd   = exe_base + 0x236000u;

    __try {
        for (uintptr_t addr = scanStart; addr + sizeof(kSig) + 0xC4u < scanEnd; ++addr) {
            if (std::memcmp(reinterpret_cast<const void*>(addr), kSig, sizeof(kSig)) != 0) continue;

            // 1. Bypass height whitelist: jmp +0x45 directly to mov r8d, [rsp+24h]
            static const uint8_t kJmpHeight[9] = { 0xEB, 0x45, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
            bool okH = PatchRawBytes(addr, kJmpHeight, sizeof(kJmpHeight));

            // 2. Bypass strict 16:9 / 4:3 width check at addr + 0x4C: jmp +0x34 directly to non-zero check
            bool okW = false;
            const uint8_t* wSite = reinterpret_cast<const uint8_t*>(addr + 0x4Cu);
            if (wSite[0] == 0x41 && wSite[1] == 0x8B && wSite[2] == 0xC9 &&
                wSite[3] == 0xB8 && wSite[4] == 0x39 && wSite[5] == 0x8E) {
                static const uint8_t kJmpWidth[2] = { 0xEB, 0x34 };
                okW = PatchRawBytes(addr + 0x4Cu, kJmpWidth, sizeof(kJmpWidth));
            }

            // 3. Force g_Is4x3Window = 0 at addr + 0xBA (0F 94 05 disp32 -> C6 05 disp32 00)
            bool ok43 = false;
            const uint8_t* f43Site = reinterpret_cast<const uint8_t*>(addr + 0xBAu);
            if (f43Site[0] == 0x0F && f43Site[1] == 0x94 && f43Site[2] == 0x05) {
                uint8_t movZero[7] = { 0xC6, 0x05, f43Site[3], f43Site[4], f43Site[5], f43Site[6], 0x00 };
                ok43 = PatchRawBytes(addr + 0xBAu, movZero, sizeof(movZero));
            }

            if (g_Host.log_msg) {
                g_Host.log_msg("[Display] Patched LoadSettingIni resolution whitelist at RVA +0x%llX (height=%d, width=%d, no4x3Flag=%d).",
                               static_cast<unsigned long long>(addr - exe_base), okH ? 1 : 0, okW ? 1 : 0, ok43 ? 1 : 0);
            }
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    if (g_Host.log_msg) {
        g_Host.log_msg("[Display] WARNING: LoadSettingIni whitelist signature not found (startup fallback active).");
    }
}

inline void PatchViewportCropHack(uintptr_t fnCreateWindowSizeDep) {
    if (!fnCreateWindowSizeDep) return;
    __try {
        const uint8_t* base = reinterpret_cast<const uint8_t*>(fnCreateWindowSizeDep);
        for (size_t off = 0x200u; off < 0x340u; ++off) {
            // Find: 41 0F 92 C0 (setb r8b) preceded by C5 F8 2F 15 (vcomiss xmm2, [1.6778f])
            if (base[off] == 0x41 && base[off + 1] == 0x0F && base[off + 2] == 0x92 && base[off + 3] == 0xC0) {
                bool hasVcomiss = false;
                for (size_t back = 8; back <= 28 && back <= off; ++back) {
                    const size_t p = off - back;
                    if (base[p] == 0xC5 && base[p + 1] == 0xF8 && base[p + 2] == 0x2F && base[p + 3] == 0x15) {
                        hasVcomiss = true;
                        break;
                    }
                }
                if (hasVcomiss) {
                    static const uint8_t kXorR8dNop[4] = { 0x45, 0x31, 0xC0, 0x90 }; // xor r8d, r8d; nop
                    if (PatchRawBytes(fnCreateWindowSizeDep + off, kXorR8dNop, sizeof(kXorR8dNop))) {
                        if (g_Host.log_msg) {
                            g_Host.log_msg("[Display] Disabled <1.6778f viewport crop hack at CreateWindowSizeDep+0x%zX (1:1 aspect mapping enabled).", off);
                        }
                    }
                    return;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ----------------------------------------------------------------------------
// Installation Entry Point (Called from enhancer.cpp ApplyEnhancerPatchesAndHooks)
// ----------------------------------------------------------------------------
inline bool Install(const HostContext& ctx) {
    if (g_Installed || !ctx.exe_base || !ctx.acz_rva || !ctx.install_hook) return false;
    g_Host = ctx;

    // 1. Load saved Resolution & ScreenMode from settings/setting.ini
    LoadFromSettingIni();
    RefreshMonitorNativeSize(nullptr);
    RefreshResolutionList(nullptr);

    // 2. Patch LoadSettingIni whitelist BEFORE WinMain reads settings/setting.ini
    PatchLoadSettingIniWhitelistAOB(ctx.exe_base);

    // 3. Dynamically resolve all D3D12/GS functions & globals from < 0x21E068 functions (v1.0.2.1 & v2.0.0.1 safe!)
    const uintptr_t addrInitD3D12   = ctx.exe_base + ctx.acz_rva(0x0020B0C0u);
    const uintptr_t addrBeginFrame  = ctx.exe_base + ctx.acz_rva(0x0020B9C0u);
    const uintptr_t addrGsFrameChk  = ctx.exe_base + ctx.acz_rva(0x0018FA80u);
    const uintptr_t addrSetAspect   = ctx.exe_base + ctx.acz_rva(0x00189C20u);
    const uintptr_t addrFeedbackUse = ctx.exe_base + ctx.acz_rva(0x00198774u);
    const uintptr_t addrClearRtMap  = ctx.exe_base + ctx.acz_rva(0x00210DF0u);

    uintptr_t fnCreateWinSizeDep = 0;

    __try {
        // From InitD3D12Subsystem (+0x20B0C0)
        const uint8_t* pInit = reinterpret_cast<const uint8_t*>(addrInitD3D12);
        if (pInit[0x1D] == 0x4C && pInit[0x1E] == 0x8B && pInit[0x1F] == 0x05)
            g_AddrDeviceResourcesPtr = DecodeRipDisp32(addrInitD3D12 + 0x1Du, 7, 3);
        if (pInit[0x3A] == 0x8B && pInit[0x3B] == 0x0D) {
            g_AddrRenderHeight = DecodeRipDisp32(addrInitD3D12 + 0x3Au, 6, 2);
            g_AddrRenderWidth  = g_AddrRenderHeight - 4u;
        }
        if (pInit[0x46] == 0x89 && pInit[0x47] == 0x0D)
            g_AddrGsInternalH = DecodeRipDisp32(addrInitD3D12 + 0x46u, 6, 2);
        if (pInit[0x68] == 0x89 && pInit[0x69] == 0x15)
            g_AddrGsInternalW = DecodeRipDisp32(addrInitD3D12 + 0x68u, 6, 2);

        // From BeginFrame (+0x20B9C0)
        const uint8_t* pBF = reinterpret_cast<const uint8_t*>(addrBeginFrame);
        if (pBF[0x16] == 0x48 && pBF[0x17] == 0x8B && pBF[0x18] == 0x05)
            g_AddrD3D12CtxPtr = DecodeRipDisp32(addrBeginFrame + 0x16u, 7, 3);
        if (pBF[0x2D] == 0xC5 && pBF[0x2E] == 0xFA && pBF[0x2F] == 0x2A && pBF[0x30] == 0x05)
            g_AddrClientW = DecodeRipDisp32(addrBeginFrame + 0x2Du, 8, 4);
        if (pBF[0x3D] == 0xC5 && pBF[0x3E] == 0xFA && pBF[0x3F] == 0x2A && pBF[0x40] == 0x05)
            g_AddrClientH = DecodeRipDisp32(addrBeginFrame + 0x3Du, 8, 4);
        if (pBF[0x89] == 0xE8)
            fnCreateWinSizeDep = DecodeRipDisp32(addrBeginFrame + 0x89u, 5, 1);

        // From CreateWindowSizeDependentResources (dynamically resolved!)
        if (fnCreateWinSizeDep) {
            const uint8_t* pCW = reinterpret_cast<const uint8_t*>(fnCreateWinSizeDep);
            if (pCW[0x43] == 0xE8) {
                g_FnWaitForGpu = reinterpret_cast<FnWaitForGpu>(DecodeRipDisp32(fnCreateWinSizeDep + 0x43u, 5, 1));
            }
            PatchViewportCropHack(fnCreateWinSizeDep);
        }

        // From GsFrameCheck (+0x18FA80)
        const uint8_t* pGF = reinterpret_cast<const uint8_t*>(addrGsFrameChk);
        if (pGF[0x9F] == 0x48 && pGF[0xA0] == 0x8B && pGF[0xA1] == 0x0D)
            g_AddrGsMainPtr = DecodeRipDisp32(addrGsFrameChk + 0x9Fu, 7, 3);
        if (pGF[0x14B] == 0x48 && pGF[0x14C] == 0x8D && pGF[0x14D] == 0x0D)
            g_AddrGsEmuObj = DecodeRipDisp32(addrGsFrameChk + 0x14Bu, 7, 3);
        if (pGF[0x152] == 0xE8) {
            const uintptr_t addrCreateAllGs = DecodeRipDisp32(addrGsFrameChk + 0x152u, 5, 1);
            g_FnCreateAllGsRTs = reinterpret_cast<FnCreateAllGsRTs>(addrCreateAllGs);
            const uint8_t* pCA = reinterpret_cast<const uint8_t*>(addrCreateAllGs);
            if (pCA[0xEB] == 0xE8)
                g_FnCreateOffscreenRT = reinterpret_cast<FnCreateOffscreenRT>(DecodeRipDisp32(addrCreateAllGs + 0xEBu, 5, 1));
            if (pCA[0x1F8] == 0x48 && pCA[0x1F9] == 0x8D && pCA[0x1FA] == 0x0D)
                g_AddrRtHashMap = DecodeRipDisp32(addrCreateAllGs + 0x1F8u, 7, 3);
        }

        // From SetScreenAspectAndResize (+0x189C20)
        const uint8_t* pSA = reinterpret_cast<const uint8_t*>(addrSetAspect);
        if (pSA[0x67] == 0x0F && pSA[0x68] == 0x94 && pSA[0x69] == 0x05) {
            g_AddrIs4x3Window = DecodeRipDisp32(addrSetAspect + 0x67u, 7, 3);
            // Also force g_Is4x3Window = 0 inside SetScreenAspectAndResize
            uint8_t movZero[7] = { 0xC6, 0x05, pSA[0x6A], pSA[0x6B], pSA[0x6C], pSA[0x6D], 0x00 };
            PatchRawBytes(addrSetAspect + 0x67u, movZero, sizeof(movZero));
        }
        if (pSA[0x8E] == 0x38 && pSA[0x8F] == 0x0D)
            g_AddrIsBorderlessActive = DecodeRipDisp32(addrSetAspect + 0x8Eu, 6, 2);
        if (pSA[0xF1] == 0x48 && pSA[0xF2] == 0x8B && pSA[0xF3] == 0x0D)
            g_AddrHwndGlobal = DecodeRipDisp32(addrSetAspect + 0xF1u, 7, 3);

        // Feedback RT pointer cache (+0x198774 -> 0x31D750)
        const uint8_t* pFB = reinterpret_cast<const uint8_t*>(addrFeedbackUse);
        if (pFB[0] == 0x48 && pFB[1] == 0x8D && pFB[2] == 0x3D)
            g_AddrFeedbackRtCache = DecodeRipDisp32(addrFeedbackUse, 7, 3);

        // ClearRtMap (+0x210DF0)
        const uint8_t* pCR = reinterpret_cast<const uint8_t*>(addrClearRtMap);
        if (pCR[0] == 0x40 && pCR[1] == 0x57 && pCR[2] == 0x48 && pCR[3] == 0x83 && pCR[4] == 0xEC && pCR[5] == 0x20)
            g_FnClearRtMap = reinterpret_cast<FnClearRtMap>(addrClearRtMap);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (ctx.log_msg) ctx.log_msg("[Display] WARNING: Exception while resolving engine display symbols.");
    }

    // 4. Install detours on InitD3D12Subsystem (15B) and BeginFrame (16B)
    static const uint8_t kPrologueInitD3D12[15] = {
        0x40, 0x55,                         // push rbp
        0x56,                               // push rsi
        0x48, 0x8D, 0x6C, 0x24, 0xE8,       // lea rbp, [rsp-18h]
        0x48, 0x81, 0xEC, 0x18, 0x01, 0x00, 0x00 // sub rsp, 118h
    };
    static const uint8_t kPrologueBeginFrame[16] = {
        0x41, 0x54,                         // push r12
        0x41, 0x56,                         // push r14
        0x41, 0x57,                         // push r15
        0x48, 0x81, 0xEC, 0x10, 0x02, 0x00, 0x00, // sub rsp, 210h
        0x41, 0xB6, 0x01                    // mov r14b, 1
    };

    const bool okInit = ctx.install_hook(
        "InitD3D12Subsystem_Display",
        addrInitD3D12,
        kPrologueInitD3D12,
        sizeof(kPrologueInitD3D12),
        reinterpret_cast<const void*>(&Hook_InitD3D12Subsystem),
        reinterpret_cast<void**>(&g_OrigInitD3D12Subsystem)
    );
    const bool okBegin = ctx.install_hook(
        "BeginFrame_Display",
        addrBeginFrame,
        kPrologueBeginFrame,
        sizeof(kPrologueBeginFrame),
        reinterpret_cast<const void*>(&Hook_BeginFrame),
        reinterpret_cast<void**>(&g_OrigBeginFrame)
    );

    g_Installed = (okInit && okBegin);
    if (ctx.log_msg) {
        ctx.log_msg("[Display] Display Engine installed (InitHook=%d, BeginFrameHook=%d, WaitForGpu=%d, ClearRt=%d, CreateRT=%d, CreateAllGs=%d).",
                    okInit ? 1 : 0, okBegin ? 1 : 0,
                    g_FnWaitForGpu ? 1 : 0, g_FnClearRtMap ? 1 : 0,
                    g_FnCreateOffscreenRT ? 1 : 0, g_FnCreateAllGsRTs ? 1 : 0);
    }
    return g_Installed;
}

} // namespace acz_display
