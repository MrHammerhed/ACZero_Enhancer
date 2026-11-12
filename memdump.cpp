// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — ACZero Enhancer Diagnostic Memory Dumper
// File: memdump.cpp (v0.16.5 — Native ASI Plugin Support, Shared mods\enhancer\enhancer.log, On-Demand D3D12 Readback & Crash Handler)
// Build: Visual Studio 2026 x64 (Release) -> memdump.asi
// Usage: Place memdump.asi into "<GameDir>\mods\enhancer\memdump.asi" (loaded by version.dll)
//        Configuration is stored in "<GameDir>\mods\enhancer\memdump.ini"
//        Dumps and screenshots are written to "<GameDir>\mods\memory_dumps\"
//        Operates 100% independently of enhancer.dll and acz_hdtex.dll (automatically
//        bridges with them at runtime if they are loaded in the process).
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <objbase.h>
#include <wincodec.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <share.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
// Configuration & Constants
// ----------------------------------------------------------------------------
static constexpr const char* MEMDUMP_VERSION_STR = "v0.16.5";

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
    if (rva_1021 < 0x00324600u) return rva_1021 + 0x3000u;
    return rva_1021 + 0x3020u;
}

// Validated IAT RVAs in acz.exe for clean pre-loader-lock exit dumps
static constexpr uintptr_t RVA_IAT_ExitProcess            = 0x0027B2E8;
static constexpr uintptr_t RVA_IAT_PostQuitMessage        = 0x0027B6F0;

// Engine global RVAs for diagnostic memory inspection & D3D12 SwapChain context
static constexpr uintptr_t RVA_GlobalD3D12CmdList         = 0x0033ACE8;
static constexpr uintptr_t RVA_GlobalD3D12Context         = 0x0033DA48;
static constexpr uintptr_t RVA_GlobalUploadRingOffset     = 0x003567A8;

// Validated RVA for EndFrameAndPresent (+0x20C280, 15-byte prologue)
static constexpr uintptr_t RVA_EndFrameAndPresent         = 0x0020C280;

static const uint8_t PROLOGUE_EndFrameAndPresent[15] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, // mov [rsp+08h], rbx
    0x48, 0x89, 0x74, 0x24, 0x10, // mov [rsp+10h], rsi
    0x48, 0x89, 0x7C, 0x24, 0x18  // mov [rsp+18h], rdi
};

// Inline IID_ID3D12Resource ({696442BE-A72E-4059-BC79-5B5C98040FAD}) to avoid dxguid.lib dependency
static const GUID kIID_ID3D12Resource = {
    0x696442be, 0xa72e, 0x4059, { 0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad }
};

// ----------------------------------------------------------------------------
// DbgHelp MiniDumpWriteDump Dynamic Declarations (Zero extra .lib dependencies)
// ----------------------------------------------------------------------------
struct MiniDumpExceptionInfo {
    DWORD               ThreadId;
    PEXCEPTION_POINTERS ExceptionPointers;
    BOOL                ClientPointers;
};

using FnMiniDumpWriteDump = BOOL (WINAPI*)(
    HANDLE                            hProcess,
    DWORD                             ProcessId,
    HANDLE                            hFile,
    uint32_t                          DumpType,
    const MiniDumpExceptionInfo*      ExceptionParam,
    const void*                       UserStreamParam,
    const void*                       CallbackParam
);

static constexpr uint32_t MDUMP_WithDataSegs                   = 0x00000001u;
static constexpr uint32_t MDUMP_WithFullMemory                 = 0x00000002u;
static constexpr uint32_t MDUMP_WithHandleData                 = 0x00000004u;
static constexpr uint32_t MDUMP_WithUnloadedModules            = 0x00000020u;
static constexpr uint32_t MDUMP_WithIndirectlyReferencedMemory = 0x00000040u;
static constexpr uint32_t MDUMP_WithProcessThreadData          = 0x00000100u;
static constexpr uint32_t MDUMP_WithFullMemoryInfo             = 0x00000800u;
static constexpr uint32_t MDUMP_WithThreadInfo                 = 0x00001000u;

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

// Optional Cross-DLL Bridges (queried dynamically via GetModuleHandleA / GetProcAddress)
using FnAczHdTexDumpLiveState = void (__fastcall*)(
    const wchar_t* dump_folder_w,
    const char*    report_path_a,
    int            dump_live_resources
);
using FnAczEnhancerWriteSummary = void (__fastcall*)(const char* report_path_a);

using FnExitProcess        = void (WINAPI*)(UINT uExitCode);
using FnPostQuitMessage    = void (WINAPI*)(int nExitCode);
using FnEndFrameAndPresent = void (__fastcall*)();

// ----------------------------------------------------------------------------
// Global State
// ----------------------------------------------------------------------------
static FnModLoaderLog               g_ModLoaderLogFn                = nullptr;
static FILE*                        g_FallbackLogFile               = nullptr;
static std::atomic<bool>            g_PluginInitialized{false};
static std::atomic<bool>            g_ShuttingDown{false};
static std::atomic<bool>            g_ExitDumpDone{false};
static std::atomic<uint32_t>        g_DumpCounter{0};
static HANDLE                       g_HotkeyThread                  = nullptr;
static HMODULE                      g_SelfModule                    = nullptr;
static uintptr_t                    g_ExeBase                       = 0;

static FnExitProcess                g_OrigExitProcess               = nullptr;
static FnPostQuitMessage            g_OrigPostQuitMessage           = nullptr;
static FnEndFrameAndPresent         g_OrigEndFrameAndPresent        = nullptr;
static FnMiniDumpWriteDump          g_MiniDumpWriteDump             = nullptr;
static LPTOP_LEVEL_EXCEPTION_FILTER g_PrevExceptionFilter           = nullptr;
static PVOID                        g_VectoredContinueHandle        = nullptr;
static std::atomic<bool>            g_CrashDumpDone{false};

static fs::path                     g_GameDir;
static fs::path                     g_ModsDir;
static fs::path                     g_MemDumpDir;
static fs::path                     g_MemDumpIniPath;
static fs::path                     g_FallbackEnhancerIniPath;
static fs::path                     g_MemoryDumpOutDir;

// Configuration options loaded from mods\memdump\memdump.ini
static bool                         g_EnableLogging                 = true;
static bool                         g_EnableMemoryDump              = true;
static bool                         g_EnableF2MemoryDump            = true;
static bool                         g_DumpMemoryOnExit              = false;
static bool                         g_WriteMinidumpFile             = true;
static bool                         g_FullMemoryMinidump            = false;
static bool                         g_DumpLiveResourcesOnMemoryDump = true;
static bool                         g_SaveScreenshotOnDump          = true;
static bool                         g_AutoDeleteOldDumps            = true;
static int                          g_MaxDumpAgeHours               = 24;

// ----------------------------------------------------------------------------
// Double-Buffered D3D12 Readback State for Zero-Stall Pre-Present Capture
// ----------------------------------------------------------------------------
static constexpr uint32_t           kReadbackSlotCount              = 2u;
static std::mutex                   g_ReadbackMutex;
static ID3D12Resource*              g_ReadbackBuffers[kReadbackSlotCount]   = { nullptr, nullptr };
static uint8_t*                     g_ReadbackMappedPtr[kReadbackSlotCount] = { nullptr, nullptr };
static uint32_t                     g_ReadbackWidth                 = 0;
static uint32_t                     g_ReadbackHeight                = 0;
static uint32_t                     g_ReadbackRowPitch              = 0;
static uint64_t                     g_ReadbackTotalBytes            = 0;
static DXGI_FORMAT                  g_ReadbackFormat                = DXGI_FORMAT_UNKNOWN;
static std::atomic<uint32_t>        g_LastCapturedSlot{0};
static std::atomic<uint64_t>        g_TotalCapturedFrames{0};

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
// POD-Only Safe Memory Helpers
// ----------------------------------------------------------------------------
static bool SafeReadU64(uintptr_t addr, uint64_t* out_val) {
    if (!addr || !out_val) return false;
    __try {
        *out_val = *reinterpret_cast<const volatile uint64_t*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU32(uintptr_t addr, uint32_t* out_val) {
    if (!addr || !out_val) return false;
    __try {
        *out_val = *reinterpret_cast<const volatile uint32_t*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// ----------------------------------------------------------------------------
// Minimal Self-Contained RGBA PNG Encoder (Uncompressed Stored-Block Zlib)
// 100% crash-safe fallback that requires no COM/WIC locks during exceptions.
// ----------------------------------------------------------------------------
static uint32_t CalcCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void AppendBe32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

static void AppendPngChunk(std::vector<uint8_t>& png, const char type[4], const uint8_t* data, size_t len) {
    AppendBe32(png, static_cast<uint32_t>(len));
    size_t type_pos = png.size();
    png.push_back(static_cast<uint8_t>(type[0]));
    png.push_back(static_cast<uint8_t>(type[1]));
    png.push_back(static_cast<uint8_t>(type[2]));
    png.push_back(static_cast<uint8_t>(type[3]));
    if (len > 0 && data != nullptr) {
        png.insert(png.end(), data, data + len);
    }
    uint32_t crc = CalcCrc32(png.data() + type_pos, 4 + len);
    AppendBe32(png, crc);
}

static void EncodeRgbaToPngBytesFallback(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& out_png) {
    out_png.clear();
    if (!rgba || width == 0 || height == 0) return;

    const size_t row_bytes = static_cast<size_t>(width) * 4u;
    std::vector<uint8_t> raw;
    raw.reserve((row_bytes + 1u) * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0); // filter type 0 (None)
        const uint8_t* row_ptr = rgba + static_cast<size_t>(y) * row_bytes;
        raw.insert(raw.end(), row_ptr, row_ptr + row_bytes);
    }

    std::vector<uint8_t> idat;
    idat.reserve(raw.size() + (raw.size() / 65535u + 2u) * 5u + 6u);
    idat.push_back(0x78); // Zlib CMF
    idat.push_back(0x01); // Zlib FLG

    size_t pos = 0;
    while (pos < raw.size()) {
        size_t chunk = raw.size() - pos;
        if (chunk > 65535u) chunk = 65535u;
        bool is_last = (pos + chunk == raw.size());
        idat.push_back(is_last ? 0x01 : 0x00);
        uint16_t len16  = static_cast<uint16_t>(chunk);
        uint16_t nlen16 = static_cast<uint16_t>(~len16);
        idat.push_back(static_cast<uint8_t>(len16 & 0xFF));
        idat.push_back(static_cast<uint8_t>((len16 >> 8) & 0xFF));
        idat.push_back(static_cast<uint8_t>(nlen16 & 0xFF));
        idat.push_back(static_cast<uint8_t>((nlen16 >> 8) & 0xFF));
        idat.insert(idat.end(), raw.begin() + pos, raw.begin() + pos + chunk);
        pos += chunk;
    }

    // Adler-32 checksum
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    for (size_t i = 0; i < raw.size(); ) {
        size_t batch = raw.size() - i;
        if (batch > 5552u) batch = 5552u;
        for (size_t j = 0; j < batch; ++j) {
            s1 += raw[i + j];
            s2 += s1;
        }
        s1 %= 65521u;
        s2 %= 65521u;
        i += batch;
    }
    uint32_t adler = (s2 << 16) | s1;
    AppendBe32(idat, adler);

    static const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    out_png.insert(out_png.end(), kPngSig, kPngSig + 8);

    uint8_t ihdr[13] = {};
    ihdr[0]  = static_cast<uint8_t>((width >> 24) & 0xFF);
    ihdr[1]  = static_cast<uint8_t>((width >> 16) & 0xFF);
    ihdr[2]  = static_cast<uint8_t>((width >> 8) & 0xFF);
    ihdr[3]  = static_cast<uint8_t>(width & 0xFF);
    ihdr[4]  = static_cast<uint8_t>((height >> 24) & 0xFF);
    ihdr[5]  = static_cast<uint8_t>((height >> 16) & 0xFF);
    ihdr[6]  = static_cast<uint8_t>((height >> 8) & 0xFF);
    ihdr[7]  = static_cast<uint8_t>(height & 0xFF);
    ihdr[8]  = 8; // bit depth = 8
    ihdr[9]  = 6; // color type = 6 (RGBA)
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace

    AppendPngChunk(out_png, "IHDR", ihdr, sizeof(ihdr));
    AppendPngChunk(out_png, "IDAT", idat.data(), idat.size());
    AppendPngChunk(out_png, "IEND", nullptr, 0);
}

// ----------------------------------------------------------------------------
// WIC Compressed PNG Writer (Used for normal F2 & Exit dumps)
// ----------------------------------------------------------------------------
static bool WriteRgbaToPngFileViaWic(const fs::path& out_path, const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!rgba || width == 0 || height == 0) return false;

    HRESULT hr_co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool need_uninit = (hr_co == S_OK || hr_co == S_FALSE);

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr) || !factory) {
        if (need_uninit) CoUninitialize();
        return false;
    }

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream) {
        factory->Release();
        if (need_uninit) CoUninitialize();
        return false;
    }

    hr = stream->InitializeFromFilename(out_path.wstring().c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        stream->Release();
        factory->Release();
        if (need_uninit) CoUninitialize();
        return false;
    }

    IWICBitmapEncoder* encoder = nullptr;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr) || !encoder) {
        stream->Release();
        factory->Release();
        if (need_uninit) CoUninitialize();
        return false;
    }

    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frame, &props);
    }
    if (SUCCEEDED(hr) && frame) {
        hr = frame->Initialize(props);
    }
    if (SUCCEEDED(hr) && frame) {
        hr = frame->SetSize(width, height);
    }
    WICPixelFormatGUID pix_fmt = GUID_WICPixelFormat32bppRGBA;
    if (SUCCEEDED(hr) && frame) {
        hr = frame->SetPixelFormat(&pix_fmt);
    }

    bool wrote_ok = false;
    if (SUCCEEDED(hr) && frame) {
        const UINT stride = width * 4u;
        const UINT total_bytes = stride * height;
        // If WIC negotiated 32bppBGRA instead of 32bppRGBA, convert on the fly
        if (IsEqualGUID(pix_fmt, GUID_WICPixelFormat32bppBGRA)) {
            std::vector<uint8_t> bgra(total_bytes);
            for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                bgra[i * 4 + 0] = rgba[i * 4 + 2];
                bgra[i * 4 + 1] = rgba[i * 4 + 1];
                bgra[i * 4 + 2] = rgba[i * 4 + 0];
                bgra[i * 4 + 3] = 255u;
            }
            hr = frame->WritePixels(height, stride, total_bytes, bgra.data());
        } else {
            hr = frame->WritePixels(height, stride, total_bytes, const_cast<BYTE*>(rgba));
        }
        if (SUCCEEDED(hr)) {
            hr = frame->Commit();
        }
        if (SUCCEEDED(hr)) {
            hr = encoder->Commit();
            wrote_ok = SUCCEEDED(hr);
        }
    }

    if (props) props->Release();
    if (frame) frame->Release();
    encoder->Release();
    stream->Release();
    factory->Release();
    if (need_uninit) CoUninitialize();
    return wrote_ok;
}

// ----------------------------------------------------------------------------
// D3D12 Pre-Present Backbuffer Readback Capture (Hook_EndFrameAndPresent)
// ----------------------------------------------------------------------------
static void ReleaseReadbackResourcesLocked() {
    for (uint32_t i = 0; i < kReadbackSlotCount; ++i) {
        if (g_ReadbackBuffers[i] != nullptr) {
            if (g_ReadbackMappedPtr[i] != nullptr) {
                D3D12_RANGE empty_range{ 0, 0 };
                g_ReadbackBuffers[i]->Unmap(0, &empty_range);
                g_ReadbackMappedPtr[i] = nullptr;
            }
            g_ReadbackBuffers[i]->Release();
            g_ReadbackBuffers[i] = nullptr;
        }
    }
    g_ReadbackWidth      = 0;
    g_ReadbackHeight     = 0;
    g_ReadbackRowPitch   = 0;
    g_ReadbackTotalBytes = 0;
    g_ReadbackFormat     = DXGI_FORMAT_UNKNOWN;
}

static bool EnsureReadbackBuffersLocked(
    ID3D12Device* device,
    uint32_t width,
    uint32_t height,
    uint32_t row_pitch,
    uint64_t total_bytes,
    DXGI_FORMAT format
) {
    if (!device || width == 0 || height == 0 || row_pitch == 0 || total_bytes == 0) {
        return false;
    }

    if (g_ReadbackBuffers[0] != nullptr &&
        g_ReadbackBuffers[1] != nullptr &&
        g_ReadbackMappedPtr[0] != nullptr &&
        g_ReadbackMappedPtr[1] != nullptr &&
        g_ReadbackWidth == width &&
        g_ReadbackHeight == height &&
        g_ReadbackRowPitch == row_pitch &&
        g_ReadbackTotalBytes == total_bytes &&
        g_ReadbackFormat == format) {
        return true;
    }

    ReleaseReadbackResourcesLocked();

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type                 = D3D12_HEAP_TYPE_READBACK;
    heap_props.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props.CreationNodeMask     = 1;
    heap_props.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC buf_desc{};
    buf_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf_desc.Alignment          = 0;
    buf_desc.Width              = total_bytes;
    buf_desc.Height             = 1;
    buf_desc.DepthOrArraySize   = 1;
    buf_desc.MipLevels          = 1;
    buf_desc.Format             = DXGI_FORMAT_UNKNOWN;
    buf_desc.SampleDesc.Count   = 1;
    buf_desc.SampleDesc.Quality = 0;
    buf_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buf_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    for (uint32_t i = 0; i < kReadbackSlotCount; ++i) {
        HRESULT hr = device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &buf_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            kIID_ID3D12Resource,
            reinterpret_cast<void**>(&g_ReadbackBuffers[i])
        );
        if (FAILED(hr) || g_ReadbackBuffers[i] == nullptr) {
            ReleaseReadbackResourcesLocked();
            return false;
        }

        void* mapped = nullptr;
        hr = g_ReadbackBuffers[i]->Map(0, nullptr, &mapped);
        if (FAILED(hr) || mapped == nullptr) {
            ReleaseReadbackResourcesLocked();
            return false;
        }
        g_ReadbackMappedPtr[i] = static_cast<uint8_t*>(mapped);
    }

    g_ReadbackWidth      = width;
    g_ReadbackHeight     = height;
    g_ReadbackRowPitch   = row_pitch;
    g_ReadbackTotalBytes = total_bytes;
    g_ReadbackFormat     = format;

    LogMsg("[MemDump] [FrameCapture] Initialized double-buffered D3D12 pre-Present readback (%ux%u, pitch=%u, fmt=%d, %.2f MB)",
           width, height, row_pitch, static_cast<int>(format),
           static_cast<double>(total_bytes) / (1024.0 * 1024.0));
    return true;
}

static bool TryRecordBackbufferReadbackSeh(
    uint8_t* d3d_ctx,
    uint32_t* out_slot
) {
    if (!d3d_ctx || !out_slot) return false;
    __try {
        uint32_t backbuf_idx             = (*reinterpret_cast<const uint32_t*>(d3d_ctx + 0x00)) & 1u;
        ID3D12Device* device             = *reinterpret_cast<ID3D12Device* const*>(d3d_ctx + 0x08);
        ID3D12GraphicsCommandList* clist = *reinterpret_cast<ID3D12GraphicsCommandList* const*>(d3d_ctx + 0x10);
        ID3D12Resource* backbuffer       = *reinterpret_cast<ID3D12Resource* const*>(d3d_ctx + static_cast<size_t>(backbuf_idx) * 8u + 0x48);

        if (!device || !clist || !backbuffer) {
            return false;
        }

        D3D12_RESOURCE_DESC desc = backbuffer->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            desc.Width == 0 || desc.Width > 16384 ||
            desc.Height == 0 || desc.Height > 16384) {
            return false;
        }

        // Only 32-bpp RGBA8 / BGRA8 swapchain formats are used by acz.exe (28, 29, 87, 91)
        const uint32_t w = static_cast<uint32_t>(desc.Width);
        const uint32_t h = desc.Height;
        const uint32_t row_pitch = (w * 4u + 255u) & ~255u; // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256
        const uint64_t total_bytes = static_cast<uint64_t>(row_pitch) * static_cast<uint64_t>(h);

        ID3D12Resource* dst_readback = g_ReadbackBuffers[backbuf_idx];
        if (dst_readback == nullptr ||
            g_ReadbackWidth != w ||
            g_ReadbackHeight != h ||
            g_ReadbackRowPitch != row_pitch ||
            g_ReadbackFormat != desc.Format) {
            return false;
        }

        // 1. Transition backbuffer: RENDER_TARGET (4) -> COPY_SOURCE (0x800)
        D3D12_RESOURCE_BARRIER barrier_to_copy{};
        barrier_to_copy.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier_to_copy.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier_to_copy.Transition.pResource   = backbuffer;
        barrier_to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier_to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier_to_copy.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        clist->ResourceBarrier(1, &barrier_to_copy);

        // 2. Copy rendered backbuffer into the persistently mapped READBACK resource
        D3D12_TEXTURE_COPY_LOCATION dst_loc{};
        dst_loc.pResource                          = dst_readback;
        dst_loc.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst_loc.PlacedFootprint.Offset             = 0;
        dst_loc.PlacedFootprint.Footprint.Format   = desc.Format;
        dst_loc.PlacedFootprint.Footprint.Width    = w;
        dst_loc.PlacedFootprint.Footprint.Height   = h;
        dst_loc.PlacedFootprint.Footprint.Depth    = 1;
        dst_loc.PlacedFootprint.Footprint.RowPitch = row_pitch;

        D3D12_TEXTURE_COPY_LOCATION src_loc{};
        src_loc.pResource        = backbuffer;
        src_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_loc.SubresourceIndex = 0;

        clist->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

        // 3. Transition backbuffer back: COPY_SOURCE (0x800) -> RENDER_TARGET (4)
        //    so EndFrameAndPresent (+0x20C280) can transition it from RENDER_TARGET to PRESENT as usual.
        D3D12_RESOURCE_BARRIER barrier_to_rt{};
        barrier_to_rt.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier_to_rt.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier_to_rt.Transition.pResource   = backbuffer;
        barrier_to_rt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier_to_rt.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier_to_rt.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        clist->ResourceBarrier(1, &barrier_to_rt);

        *out_slot = backbuf_idx;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeInspectBackbufferDesc(
    uint8_t* d3d_ctx,
    ID3D12Device** out_device,
    uint32_t* out_w,
    uint32_t* out_h,
    uint32_t* out_pitch,
    uint64_t* out_total,
    DXGI_FORMAT* out_fmt
) {
    if (!d3d_ctx) return false;
    __try {
        uint32_t backbuf_idx       = (*reinterpret_cast<const uint32_t*>(d3d_ctx + 0x00)) & 1u;
        ID3D12Device* device       = *reinterpret_cast<ID3D12Device* const*>(d3d_ctx + 0x08);
        ID3D12Resource* backbuffer = *reinterpret_cast<ID3D12Resource* const*>(d3d_ctx + static_cast<size_t>(backbuf_idx) * 8u + 0x48);
        if (!device || !backbuffer) return false;

        D3D12_RESOURCE_DESC desc = backbuffer->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            desc.Width == 0 || desc.Width > 16384 ||
            desc.Height == 0 || desc.Height > 16384) {
            return false;
        }

        uint32_t w     = static_cast<uint32_t>(desc.Width);
        uint32_t h     = desc.Height;
        uint32_t pitch = (w * 4u + 255u) & ~255u;
        uint64_t total = static_cast<uint64_t>(pitch) * static_cast<uint64_t>(h);

        *out_device = device;
        *out_w      = w;
        *out_h      = h;
        *out_pitch  = pitch;
        *out_total  = total;
        *out_fmt    = desc.Format;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static LONG WINAPI CrashDumpExceptionFilter(PEXCEPTION_POINTERS exc_ptrs);

static std::atomic<bool> g_RequestImmediateCapture{false};

// Waits for the GPU command queue fence signaled inside EndFrameAndPresent (+0x20C411)
// for the specified backbuffer slot so the D3D12_HEAP_TYPE_READBACK buffer has valid pixels.
static void WaitForSlotFenceCompletionSeh(uint8_t* d3d_ctx, uint32_t slot) {
    if (!d3d_ctx || slot >= kReadbackSlotCount) return;
    __try {
        ID3D12Fence* fence = *reinterpret_cast<ID3D12Fence* const*>(d3d_ctx + 0x68);
        uint64_t target_val = *reinterpret_cast<const uint64_t*>(d3d_ctx + static_cast<size_t>(slot) * 8u + 0x70);
        if (!fence || target_val == 0) return;
        for (int i = 0; i < 100; ++i) {
            if (fence->GetCompletedValue() >= target_val) {
                break;
            }
            Sleep(1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void __fastcall Hook_EndFrameAndPresent() {
    static bool s_reasserted_filter = false;
    static uint32_t s_present_frame_counter = 0;
    if (!s_reasserted_filter && g_EnableMemoryDump) {
        s_reasserted_filter = true;
        LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(&CrashDumpExceptionFilter);
        if (prev != &CrashDumpExceptionFilter && prev != nullptr) {
            g_PrevExceptionFilter = prev;
        }
    }
    uint32_t captured_slot = 0xFFFFFFFFu;
    uint8_t* captured_d3d_ctx = nullptr;
    ++s_present_frame_counter;
    bool immediate_req = g_RequestImmediateCapture.exchange(false, std::memory_order_acq_rel);
    // Use an odd interval (59 frames) so background refresh alternates between backbuffer slots 0 and 1
    bool want_capture = immediate_req || (s_present_frame_counter <= 4u) || ((s_present_frame_counter % 59u) == 0u);

    if (want_capture && g_EnableMemoryDump && g_SaveScreenshotOnDump &&
        !g_ShuttingDown.load(std::memory_order_relaxed) && g_ExeBase != 0) {
        uint64_t ctx_u64 = 0;
        if (SafeReadU64(g_ExeBase + AczRva(RVA_GlobalD3D12Context), &ctx_u64) && ctx_u64 != 0) {
            uint8_t* d3d_ctx = reinterpret_cast<uint8_t*>(ctx_u64);
            ID3D12Device* device = nullptr;
            uint32_t w = 0, h = 0, pitch = 0;
            uint64_t total = 0;
            DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;

            if (SafeInspectBackbufferDesc(d3d_ctx, &device, &w, &h, &pitch, &total, &fmt)) {
                std::unique_lock<std::mutex> lock(g_ReadbackMutex, std::try_to_lock);
                if (lock.owns_lock()) {
                    if (EnsureReadbackBuffersLocked(device, w, h, pitch, total, fmt)) {
                        if (TryRecordBackbufferReadbackSeh(d3d_ctx, &captured_slot)) {
                            captured_d3d_ctx = d3d_ctx;
                        } else {
                            captured_slot = 0xFFFFFFFFu;
                        }
                    }
                }
            }
        }
    }

    g_OrigEndFrameAndPresent();

    if (captured_slot != 0xFFFFFFFFu) {
        if (immediate_req && captured_d3d_ctx != nullptr) {
            WaitForSlotFenceCompletionSeh(captured_d3d_ctx, captured_slot);
        }
        g_LastCapturedSlot.store(captured_slot, std::memory_order_release);
        g_TotalCapturedFrames.fetch_add(1, std::memory_order_release);
    }
}

static bool SafeCopyReadbackToRgba(
    const uint8_t* mapped_src,
    uint32_t width,
    uint32_t height,
    uint32_t row_pitch,
    DXGI_FORMAT format,
    uint8_t* dst_rgba
) {
    if (!mapped_src || !dst_rgba || width == 0 || height == 0 || row_pitch < width * 4u) {
        return false;
    }
    __try {
        // acz.exe uses DXGI_FORMAT_R10G10B10A2_UNORM (format == 24) for its D3D12 SwapChain backbuffer!
        // Unpack 10-bit R (bits 0..9), 10-bit G (bits 10..19), and 10-bit B (bits 20..29) to 8-bit RGBA.
        const bool is_r10g10b10a2 = (format == DXGI_FORMAT_R10G10B10A2_UNORM ||
                                     format == DXGI_FORMAT_R10G10B10A2_UINT);
        const bool is_bgra        = (format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                                     format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                                     format == DXGI_FORMAT_B8G8R8X8_UNORM ||
                                     format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);

        uint32_t non_zero_rgb = 0;
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* src_row = mapped_src + static_cast<size_t>(y) * row_pitch;
            uint8_t*       dst_row = dst_rgba   + static_cast<size_t>(y) * width * 4u;
            if (is_r10g10b10a2) {
                for (uint32_t x = 0; x < width; ++x) {
                    uint32_t px  = *reinterpret_cast<const uint32_t*>(src_row + static_cast<size_t>(x) * 4u);
                    non_zero_rgb |= (px & 0x3FFFFFFFu);
                    uint32_t r10 = px & 0x3FFu;
                    uint32_t g10 = (px >> 10) & 0x3FFu;
                    uint32_t b10 = (px >> 20) & 0x3FFu;
                    dst_row[x * 4 + 0] = static_cast<uint8_t>((r10 * 255u + 511u) / 1023u); // R
                    dst_row[x * 4 + 1] = static_cast<uint8_t>((g10 * 255u + 511u) / 1023u); // G
                    dst_row[x * 4 + 2] = static_cast<uint8_t>((b10 * 255u + 511u) / 1023u); // B
                    dst_row[x * 4 + 3] = 255u;                                               // Opaque A
                }
            } else if (is_bgra) {
                for (uint32_t x = 0; x < width; ++x) {
                    uint8_t b = src_row[x * 4 + 0];
                    uint8_t g = src_row[x * 4 + 1];
                    uint8_t r = src_row[x * 4 + 2];
                    non_zero_rgb |= (r | g | b);
                    dst_row[x * 4 + 0] = r;    // R
                    dst_row[x * 4 + 1] = g;    // G
                    dst_row[x * 4 + 2] = b;    // B
                    dst_row[x * 4 + 3] = 255u; // Opaque A
                }
            } else {
                for (uint32_t x = 0; x < width; ++x) {
                    uint8_t r = src_row[x * 4 + 0];
                    uint8_t g = src_row[x * 4 + 1];
                    uint8_t b = src_row[x * 4 + 2];
                    non_zero_rgb |= (r | g | b);
                    dst_row[x * 4 + 0] = r;    // R
                    dst_row[x * 4 + 1] = g;    // G
                    dst_row[x * 4 + 2] = b;    // B
                    dst_row[x * 4 + 3] = 255u; // Opaque A
                }
            }
        }
        return (non_zero_rgb != 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SaveCapturedFrameScreenshotToFolder(
    const fs::path& dump_folder,
    bool is_crash,
    uint32_t* out_w,
    uint32_t* out_h,
    uint64_t* out_frame_num
) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (out_frame_num) *out_frame_num = 0;

    if (!g_SaveScreenshotOnDump) return false;

    uint64_t total_frames = g_TotalCapturedFrames.load(std::memory_order_acquire);
    if (total_frames == 0) return false;

    uint32_t slot           = g_LastCapturedSlot.load(std::memory_order_acquire) & 1u;
    const uint8_t* src_ptr  = g_ReadbackMappedPtr[slot];
    uint32_t width          = g_ReadbackWidth;
    uint32_t height         = g_ReadbackHeight;
    uint32_t row_pitch      = g_ReadbackRowPitch;
    DXGI_FORMAT format      = g_ReadbackFormat;

    if (!src_ptr || width == 0 || height == 0 || row_pitch < width * 4u) {
        return false;
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4u);
    if (!SafeCopyReadbackToRgba(src_ptr, width, height, row_pitch, format, rgba.data())) {
        // Try the alternate readback buffer slot if the primary slot was mid-transfer
        uint32_t alt_slot = slot ^ 1u;
        const uint8_t* alt_ptr = g_ReadbackMappedPtr[alt_slot];
        if (!alt_ptr || !SafeCopyReadbackToRgba(alt_ptr, width, height, row_pitch, format, rgba.data())) {
            return false;
        }
    }

    fs::path png_path = dump_folder / "screenshot.png";
    bool saved = false;

    // On normal F2 / Exit dumps, use WIC for compact compressed PNG output.
    // On Crash dumps (or if WIC fails), use our zero-dependency stored-block PNG writer!
    if (!is_crash) {
        saved = WriteRgbaToPngFileViaWic(png_path, rgba.data(), width, height);
    }
    if (!saved) {
        std::vector<uint8_t> png_bytes;
        EncodeRgbaToPngBytesFallback(rgba.data(), width, height, png_bytes);
        if (!png_bytes.empty()) {
            FILE* pf = nullptr;
            fopen_s(&pf, png_path.string().c_str(), "wb");
            if (pf) {
                size_t written = fwrite(png_bytes.data(), 1, png_bytes.size(), pf);
                fclose(pf);
                saved = (written == png_bytes.size());
            }
        }
    }

    if (saved) {
        if (out_w) *out_w = width;
        if (out_h) *out_h = height;
        if (out_frame_num) *out_frame_num = total_frames;
        LogMsg("[MemDump] [MemoryDump] Saved pre-Present framebuffer snapshot (%ux%u, frame #%llu): %s",
               width, height, static_cast<unsigned long long>(total_frames), png_path.string().c_str());
    }
    return saved;
}


// ----------------------------------------------------------------------------
// Minimal x64 Detour & IAT Hook Installers
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
    if (std::memcmp(target_bytes, expected_prologue, prologue_len) != 0) {
        LogMsg("[MemDump] WARNING: Prologue mismatch at %s (%p)!", name, reinterpret_cast<void*>(target_addr));
        return false;
    }

    uint8_t* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, kTrampAllocSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)
    );
    if (!tramp) return false;

    std::memset(tramp, 0xCC, kTrampAllocSize);
    std::memcpy(tramp, target_bytes, prologue_len);
    WriteAbsoluteJump64(tramp + prologue_len, reinterpret_cast<const void*>(target_addr + prologue_len));
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
    LogMsg("[MemDump] Installed hook '%s' at %p (trampoline at %p)",
           name, reinterpret_cast<void*>(target_addr), tramp);
    return true;
}
#pragma warning(pop)

static bool InstallIatHook64(uintptr_t iat_entry_addr, const void* hook_fn, void** out_orig_fn) {
    if (!iat_entry_addr || !hook_fn || !out_orig_fn) return false;
    DWORD old_protect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(iat_entry_addr), sizeof(void*), PAGE_READWRITE, &old_protect)) {
        return false;
    }
    *out_orig_fn = *reinterpret_cast<void**>(iat_entry_addr);
    *reinterpret_cast<const void**>(iat_entry_addr) = hook_fn;
    VirtualProtect(reinterpret_cast<void*>(iat_entry_addr), sizeof(void*), old_protect, &old_protect);
    return true;
}

// ----------------------------------------------------------------------------
// Load mods\memdump\memdump.ini Configuration
// ----------------------------------------------------------------------------
static int ReadMemDumpIniInt(const char* key, int default_val) {
    const std::string ini_str = g_MemDumpIniPath.string();
    int val = GetPrivateProfileIntA("MemoryDump", key, -99999, ini_str.c_str());
    if (val != -99999) return val;
    val = GetPrivateProfileIntA("Settings", key, -99999, ini_str.c_str());
    if (val != -99999) return val;
    if (fs::exists(g_FallbackEnhancerIniPath)) {
        const std::string enh_str = g_FallbackEnhancerIniPath.string();
        val = GetPrivateProfileIntA("MemoryDump", key, -99999, enh_str.c_str());
        if (val != -99999) return val;
    }
    return default_val;
}

static void LoadMemDumpConfigIni() {
    std::error_code ec;
    fs::create_directories(g_MemDumpDir, ec);

    if (!fs::exists(g_MemDumpIniPath, ec)) {
        FILE* f = nullptr;
        fopen_s(&f, g_MemDumpIniPath.string().c_str(), "w");
        if (f) {
            fprintf(f,
                "; ============================================================================\n"
                "; Ace Combat Zero: The Belkan War — Diagnostic Memory Dumper Configuration (%s)\n"
                "; Path: <GameDir>\\mods\\enhancer\\memdump.ini\n; Used by: mods\\enhancer\\memdump.asi\n"
                "; ============================================================================\n\n"
                "[MemoryDump]\n"
                "; EnableLogging: Write [MemDump] events directly into mods\\enhancer\\enhancer.log\n"
                "EnableLogging=1\n\n"
                "; EnableMemoryDump: Master switch for diagnostic memory dumps in mods\\memory_dumps\\\n"
                "EnableMemoryDump=1\n\n"
                "; EnableF2MemoryDump: Press F2 at any moment in-game to create an instant memory dump\n"
                "EnableF2MemoryDump=1\n\n"
                "; DumpMemoryOnExit: Automatically create a memory & state dump when the game closes\n"
                "DumpMemoryOnExit=0\n\n"
                "; SaveScreenshotOnDump: Capture the rendered D3D12 backbuffer right before Present\n"
                "SaveScreenshotOnDump=1\n\n"
                "; AutoDeleteOldDumps: Automatically delete old dump_* folders in mods\\memory_dumps\\ on startup\n"
                "AutoDeleteOldDumps=1\n\n"
                "; MaxDumpAgeHours: Maximum age of dump folders in hours before automatic cleanup (default: 24)\n"
                "MaxDumpAgeHours=24\n\n"
                "; WriteMinidumpFile: Write a standard Windows .dmp file (acz_process.dmp) inside each dump folder\n"
                "WriteMinidumpFile=1\n\n"
                "; FullMemoryMinidump: Set to 1 to dump the entire process RAM (large file) or 0 for compact debug .dmp\n"
                "FullMemoryMinidump=0\n\n"
                "; DumpLiveResourcesOnMemoryDump: Export live textures/fonts & 4MB GS VRAM when a dump is triggered\n"
                "DumpLiveResourcesOnMemoryDump=1\n",
                MEMDUMP_VERSION_STR
            );
            fclose(f);
        }
    }

    g_EnableLogging                 = ReadMemDumpIniInt("EnableLogging",                 1) != 0;
    g_EnableMemoryDump              = ReadMemDumpIniInt("EnableMemoryDump",              1) != 0;
    g_EnableF2MemoryDump            = ReadMemDumpIniInt("EnableF2MemoryDump",            1) != 0;
    g_DumpMemoryOnExit              = ReadMemDumpIniInt("DumpMemoryOnExit",              0) != 0;
    g_SaveScreenshotOnDump          = ReadMemDumpIniInt("SaveScreenshotOnDump",          1) != 0;
    g_AutoDeleteOldDumps            = ReadMemDumpIniInt("AutoDeleteOldDumps",            1) != 0;
    g_MaxDumpAgeHours               = ReadMemDumpIniInt("MaxDumpAgeHours",              24);
    if (g_MaxDumpAgeHours < 1) g_MaxDumpAgeHours = 1;
    g_WriteMinidumpFile             = ReadMemDumpIniInt("WriteMinidumpFile",             1) != 0;
    g_FullMemoryMinidump            = ReadMemDumpIniInt("FullMemoryMinidump",            0) != 0;
    g_DumpLiveResourcesOnMemoryDump = ReadMemDumpIniInt("DumpLiveResourcesOnMemoryDump", 1) != 0;
}

static void CleanupOldMemoryDumpsOnStartup() {
    if (!g_AutoDeleteOldDumps || g_MaxDumpAgeHours <= 0) return;

    std::error_code ec;
    if (!fs::exists(g_MemoryDumpOutDir, ec) || !fs::is_directory(g_MemoryDumpOutDir, ec)) {
        return;
    }

    FILETIME ft_now{};
    GetSystemTimeAsFileTime(&ft_now);
    ULARGE_INTEGER ul_now{};
    ul_now.LowPart  = ft_now.dwLowDateTime;
    ul_now.HighPart = ft_now.dwHighDateTime;

    const uint64_t max_age_ticks = static_cast<uint64_t>(g_MaxDumpAgeHours) * 3600ULL * 10000000ULL;

    std::wstring search_pat = (g_MemoryDumpOutDir / L"dump_*").wstring();
    WIN32_FIND_DATAW wfd{};
    HANDLE hFind = FindFirstFileW(search_pat.c_str(), &wfd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<fs::path> expired_dirs;
    do {
        if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
        if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;

        ULARGE_INTEGER ul_dir{};
        ul_dir.LowPart  = wfd.ftLastWriteTime.dwLowDateTime;
        ul_dir.HighPart = wfd.ftLastWriteTime.dwHighDateTime;

        if (ul_now.QuadPart > ul_dir.QuadPart &&
            (ul_now.QuadPart - ul_dir.QuadPart) >= max_age_ticks) {
            expired_dirs.push_back(g_MemoryDumpOutDir / wfd.cFileName);
        }
    } while (FindNextFileW(hFind, &wfd));
    FindClose(hFind);

    uint32_t deleted_count = 0;
    for (const fs::path& dir_p : expired_dirs) {
        std::error_code rem_ec;
        uintmax_t removed = fs::remove_all(dir_p, rem_ec);
        if (!rem_ec && removed > 0) {
            ++deleted_count;
            LogMsg("[MemDump] [Cleanup] Removed expired memory dump (> %d hours old): %s",
                   g_MaxDumpAgeHours, dir_p.filename().string().c_str());
        }
    }

    if (deleted_count > 0) {
        LogMsg("[MemDump] [Cleanup] Deleted %u expired memory dump folder(s) older than %d hours.",
               deleted_count, g_MaxDumpAgeHours);
    }
}

// ============================================================================
// Diagnostic Memory & Live-State Dumper (F2 Hotkey, Normal Exit & Crash Handler)
// ============================================================================
static void WriteMinidumpToPath(const fs::path& dmp_path, PEXCEPTION_POINTERS exc_ptrs) {
    if (!g_WriteMinidumpFile || g_MiniDumpWriteDump == nullptr) return;

    HANDLE hFile = CreateFileW(
        dmp_path.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        LogMsg("[MemDump] ERROR: Could not create minidump file: %s", dmp_path.string().c_str());
        return;
    }

    uint32_t dump_type = MDUMP_WithDataSegs |
                         MDUMP_WithHandleData |
                         MDUMP_WithUnloadedModules |
                         MDUMP_WithProcessThreadData |
                         MDUMP_WithThreadInfo |
                         MDUMP_WithIndirectlyReferencedMemory;
    if (g_FullMemoryMinidump) {
        dump_type |= MDUMP_WithFullMemory | MDUMP_WithFullMemoryInfo;
    }

    MiniDumpExceptionInfo mei{};
    const MiniDumpExceptionInfo* mei_ptr = nullptr;
    if (exc_ptrs != nullptr) {
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = exc_ptrs;
        mei.ClientPointers    = FALSE;
        mei_ptr               = &mei;
    }

    BOOL ok = g_MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        hFile,
        dump_type,
        mei_ptr,
        nullptr,
        nullptr
    );
    if (!ok) {
        SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(hFile);
        uint32_t fallback_type = MDUMP_WithDataSegs | MDUMP_WithProcessThreadData | MDUMP_WithThreadInfo;
        ok = g_MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            fallback_type,
            mei_ptr,
            nullptr,
            nullptr
        );
    }
    CloseHandle(hFile);

    if (ok) {
        LogMsg("[MemDump] Wrote Windows Minidump (%s): %s",
               g_FullMemoryMinidump ? "Full RAM" : "Compact Debug",
               dmp_path.string().c_str());
    } else {
        LogMsg("[MemDump] WARNING: MiniDumpWriteDump returned FALSE (err=%lu)", GetLastError());
    }
}

static void PerformFullMemoryAndStateDump(
    const char* reason_tag,
    PEXCEPTION_POINTERS exc_ptrs,
    bool allow_minidump_call
) {
    if (!g_EnableMemoryDump) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    uint32_t seq = g_DumpCounter.fetch_add(1, std::memory_order_relaxed) + 1u;

    char folder_name[128];
    snprintf(folder_name, sizeof(folder_name), "dump_%04u%02u%02u_%02u%02u%02u_%02u_%s",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond,
             seq, reason_tag ? reason_tag : "Manual");

    std::error_code ec;
    fs::path dump_folder = g_MemoryDumpOutDir / folder_name;
    fs::create_directories(dump_folder, ec);

    LogMsg("[MemDump] ================================================================");
    LogMsg("[MemDump] Starting memory & state dump [%s] -> %s",
           reason_tag ? reason_tag : "Manual", dump_folder.string().c_str());

    // 1. Save the pre-Present D3D12 framebuffer snapshot (screenshot.png) first!
    uint32_t shot_w = 0, shot_h = 0;
    uint64_t shot_frame = 0;
    bool is_crash = (exc_ptrs != nullptr);
    bool shot_saved = SaveCapturedFrameScreenshotToFolder(dump_folder, is_crash, &shot_w, &shot_h, &shot_frame);

    // 2. Write Windows Minidump (acz_process.dmp) if enabled
    if (allow_minidump_call && g_WriteMinidumpFile) {
        fs::path dmp_path = dump_folder / "acz_process.dmp";
        WriteMinidumpToPath(dmp_path, exc_ptrs);
    }

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));

    uint64_t d3d12_cmd_list = 0;
    uint32_t upload_ring_off = 0;
    if (g_ExeBase != 0) {
        SafeReadU64(g_ExeBase + AczRva(RVA_GlobalD3D12CmdList), &d3d12_cmd_list);
        SafeReadU32(g_ExeBase + AczRva(RVA_GlobalUploadRingOffset), &upload_ring_off);
    }

    fs::path report_path = dump_folder / "memory_report.txt";
    FILE* rf = nullptr;
    fopen_s(&rf, report_path.string().c_str(), "wb");
    if (rf) {
        fprintf(rf, "================================================================================\n");
        fprintf(rf, " ACE COMBAT ZERO — DIAGNOSTIC MEMORY DUMPER (%s) STATE REPORT\n", MEMDUMP_VERSION_STR);
        fprintf(rf, "================================================================================\n");
        fprintf(rf, "Timestamp:                 %04u-%02u-%02u %02u:%02u:%02u.%03u\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        fprintf(rf, "Trigger Reason:            %s\n", reason_tag ? reason_tag : "Manual");
        fprintf(rf, "Process ID / Thread ID:    %lu / %lu\n", GetCurrentProcessId(), GetCurrentThreadId());
        fprintf(rf, "acz.exe Base Address:      %p (%s)\n", reinterpret_cast<void*>(g_ExeBase), GetAczExeVersionString(g_AczVersion));
        fprintf(rf, "memdump.dll Base:          %p\n", reinterpret_cast<void*>(g_SelfModule));
        fprintf(rf, "Working Set RAM:           %.2f MB (Peak: %.2f MB)\n",
                static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0),
                static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0));
        fprintf(rf, "Private Commit RAM:        %.2f MB\n",
                static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0));
        fprintf(rf, "D3D12 CommandList Ptr:     0x%016llX\n", static_cast<unsigned long long>(d3d12_cmd_list));
        fprintf(rf, "D3D12 Upload Ring Offset:  0x%08X (%u bytes)\n", upload_ring_off, upload_ring_off);
        if (shot_saved) {
            fprintf(rf, "Pre-Present Screenshot:    screenshot.png (%ux%u, Frame #%llu)\n",
                    shot_w, shot_h, static_cast<unsigned long long>(shot_frame));
        } else {
            fprintf(rf, "Pre-Present Screenshot:    %s\n",
                    g_SaveScreenshotOnDump ? "NOT AVAILABLE (no frames presented yet)" : "DISABLED (SaveScreenshotOnDump=0)");
        }

        if (exc_ptrs != nullptr && exc_ptrs->ExceptionRecord != nullptr) {
            fprintf(rf, "--------------------------------------------------------------------------------\n");
            fprintf(rf, "EXCEPTION RECORD:\n");
            fprintf(rf, "  ExceptionCode:           0x%08lX\n", exc_ptrs->ExceptionRecord->ExceptionCode);
            fprintf(rf, "  ExceptionAddress:        %p\n", exc_ptrs->ExceptionRecord->ExceptionAddress);
            if (g_ExeBase != 0 &&
                reinterpret_cast<uintptr_t>(exc_ptrs->ExceptionRecord->ExceptionAddress) >= g_ExeBase) {
                fprintf(rf, "  acz.exe RVA:             +0x%llX\n",
                        static_cast<unsigned long long>(
                            reinterpret_cast<uintptr_t>(exc_ptrs->ExceptionRecord->ExceptionAddress) - g_ExeBase));
            }
        }
        fclose(rf);
    }

    // Optional Runtime Bridge 1: Query enhancer.asi / enhancer.dll if loaded in the process
    HMODULE hEnhancer = GetModuleHandleA("enhancer.asi");
    if (!hEnhancer) hEnhancer = GetModuleHandleA("enhancer.dll");
    bool enhancer_bridged = false;
    if (hEnhancer != nullptr) {
        auto fnEnhSummary = reinterpret_cast<FnAczEnhancerWriteSummary>(
            GetProcAddress(hEnhancer, "AczEnhancer_WriteStateSummary")
        );
        if (fnEnhSummary != nullptr) {
            fnEnhSummary(report_path.string().c_str());
            enhancer_bridged = true;
        }
    }

    // Optional Runtime Bridge 2: Query acz_hdtex.asi / acz_hdtex.dll if loaded in the process
    HMODULE hHdTex = GetModuleHandleA("acz_hdtex.asi");
    if (!hHdTex) hHdTex = GetModuleHandleA("acz_hdtex.dll");
    bool hdtex_bridged = false;
    if (hHdTex != nullptr) {
        auto fnDumpBridge = reinterpret_cast<FnAczHdTexDumpLiveState>(
            GetProcAddress(hHdTex, "AczHdTex_DumpLiveState")
        );
        if (fnDumpBridge == nullptr) {
            fnDumpBridge = reinterpret_cast<FnAczHdTexDumpLiveState>(
                GetProcAddress(hHdTex, "AczHdTex_DumpLiveStateForQoL")
            );
        }
        if (fnDumpBridge != nullptr) {
            fnDumpBridge(
                dump_folder.wstring().c_str(),
                report_path.string().c_str(),
                g_DumpLiveResourcesOnMemoryDump ? 1 : 0
            );
            hdtex_bridged = true;
        }
    }

    LogMsg("[MemDump] Completed dump [%s]: report=%s, Screenshot=%s, EnhancerBridge=%s, HDTexBridge=%s",
           reason_tag ? reason_tag : "Manual",
           report_path.string().c_str(),
           shot_saved ? "YES" : "NO",
           enhancer_bridged ? "ACTIVE" : "STANDALONE",
           hdtex_bridged ? "ACTIVE" : "STANDALONE");
    LogMsg("[MemDump] ================================================================");
}

static bool IsFatalExceptionCode(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_INT_OVERFLOW:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case 0xC0000374u: // STATUS_HEAP_CORRUPTION
        case 0xC0000409u: // STATUS_STACK_BUFFER_OVERRUN
            return true;
        default:
            return false;
    }
}

static LONG WINAPI CrashDumpExceptionFilter(PEXCEPTION_POINTERS exc_ptrs) {
    if (g_EnableMemoryDump) {
        char tag[64];
        DWORD code = (exc_ptrs && exc_ptrs->ExceptionRecord) ? exc_ptrs->ExceptionRecord->ExceptionCode : 0;
        snprintf(tag, sizeof(tag), "CRASH_0x%08lX", code);
        PerformFullMemoryAndStateDump(tag, exc_ptrs, true);
    }
    if (g_PrevExceptionFilter != nullptr && exc_ptrs != nullptr) {
        return g_PrevExceptionFilter(exc_ptrs);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void WINAPI Hook_PostQuitMessage(int nExitCode) {
    if (g_EnableMemoryDump && g_DumpMemoryOnExit) {
        bool expected = false;
        if (g_ExitDumpDone.compare_exchange_strong(expected, true)) {
            PerformFullMemoryAndStateDump("Exit_PostQuit", nullptr, true);
        }
    }
    if (g_OrigPostQuitMessage != nullptr) {
        g_OrigPostQuitMessage(nExitCode);
    }
}

static void WINAPI Hook_ExitProcess(UINT uExitCode) {
    g_ShuttingDown.store(true, std::memory_order_release);
    if (g_EnableMemoryDump && g_DumpMemoryOnExit) {
        bool expected = false;
        if (g_ExitDumpDone.compare_exchange_strong(expected, true)) {
            PerformFullMemoryAndStateDump("Exit_Process", nullptr, true);
        }
    }
    if (g_OrigExitProcess != nullptr) {
        g_OrigExitProcess(uExitCode);
    }
}

static DWORD WINAPI HotkeyMonitorThread(LPVOID lpParam) {
    (void)lpParam;
    const DWORD self_pid = GetCurrentProcessId();
    bool was_f2_down = false;

    while (!g_ShuttingDown.load(std::memory_order_acquire)) {
        Sleep(50);
        if (!g_EnableMemoryDump || !g_EnableF2MemoryDump) {
            continue;
        }

        SHORT key_state = GetAsyncKeyState(VK_F2);
        bool is_f2_down = (key_state & 0x8000) != 0;

        if (is_f2_down && !was_f2_down) {
            HWND fg_wnd = GetForegroundWindow();
            DWORD fg_pid = 0;
            if (fg_wnd != nullptr) {
                GetWindowThreadProcessId(fg_wnd, &fg_pid);
            }
            if (fg_pid == self_pid) {
                if (g_SaveScreenshotOnDump) {
                    uint64_t prev_frames = g_TotalCapturedFrames.load(std::memory_order_acquire);
                    g_RequestImmediateCapture.store(true, std::memory_order_release);
                    for (int wait_i = 0; wait_i < 12; ++wait_i) {
                        if (g_TotalCapturedFrames.load(std::memory_order_acquire) != prev_frames) {
                            break;
                        }
                        Sleep(5);
                    }
                }
                PerformFullMemoryAndStateDump("F2_Hotkey", nullptr, true);
                Beep(880, 90);
            }
        }
        was_f2_down = is_f2_down;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------
static void InitializeMemDumpPlugin(const AczAddonApi* api) {
    bool expected = false;
    if (!g_PluginInitialized.compare_exchange_strong(expected, true)) {
        return;
    }

    if (api != nullptr && api->struct_size >= sizeof(AczAddonApi) && api->log_fn != nullptr) {
        g_ModLoaderLogFn = api->log_fn;
    }

    char exe_path_buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_path_buf, MAX_PATH);
    g_GameDir                 = fs::path(exe_path_buf).parent_path();
    g_ModsDir                 = g_GameDir / "mods";
    g_MemDumpDir              = g_ModsDir / "enhancer";
    g_MemDumpIniPath          = g_MemDumpDir / "memdump.ini";
    g_FallbackEnhancerIniPath = g_MemDumpDir / "enhancer.ini";
    g_MemoryDumpOutDir        = g_ModsDir / "memory_dumps";

    std::error_code ec;
    fs::create_directories(g_MemDumpDir, ec);

    LoadMemDumpConfigIni();

    if (g_EnableLogging && g_ModLoaderLogFn == nullptr) {
        fs::path modloader_log_path = g_MemDumpDir / "enhancer.log";
        g_FallbackLogFile = _fsopen(modloader_log_path.string().c_str(), "a", _SH_DENYNO);
    }

    g_ExeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    g_AczVersion = DetectAczExeVersion(g_ExeBase);
    LogMsg("[MemDump] ================================================================");
    LogMsg("[MemDump]  ACZero Enhancer — Diagnostic Memory & State Dumper %s", MEMDUMP_VERSION_STR);
    LogMsg("[MemDump] ================================================================");
    LogMsg("[MemDump] Game executable base: %p (Detected version: %s)",
           reinterpret_cast<void*>(g_ExeBase), GetAczExeVersionString(g_AczVersion));
    LogMsg("[MemDump] Config file:          %s", g_MemDumpIniPath.string().c_str());
    LogMsg("[MemDump] Memory dumps folder:  %s", g_MemoryDumpOutDir.string().c_str());
    LogMsg("[MemDump] Config: EnableLogging=%d, EnableMemoryDump=%d, EnableF2MemoryDump=%d, DumpMemoryOnExit=%d, SaveScreenshotOnDump=%d, AutoDeleteOldDumps=%d, MaxDumpAgeHours=%d, WriteMinidumpFile=%d, FullMemoryMinidump=%d, DumpLiveResourcesOnMemoryDump=%d",
           g_EnableLogging ? 1 : 0,
           g_EnableMemoryDump ? 1 : 0,
           g_EnableF2MemoryDump ? 1 : 0,
           g_DumpMemoryOnExit ? 1 : 0,
           g_SaveScreenshotOnDump ? 1 : 0,
           g_AutoDeleteOldDumps ? 1 : 0,
           g_MaxDumpAgeHours,
           g_WriteMinidumpFile ? 1 : 0,
           g_FullMemoryMinidump ? 1 : 0,
           g_DumpLiveResourcesOnMemoryDump ? 1 : 0);

    CleanupOldMemoryDumpsOnStartup();

    if (g_EnableMemoryDump && g_WriteMinidumpFile) {
        HMODULE hDbgHelp = LoadLibraryA("dbghelp.dll");
        if (hDbgHelp != nullptr) {
            g_MiniDumpWriteDump = reinterpret_cast<FnMiniDumpWriteDump>(
                GetProcAddress(hDbgHelp, "MiniDumpWriteDump")
            );
        }
    }

    if (g_EnableMemoryDump) {
        g_PrevExceptionFilter = SetUnhandledExceptionFilter(&CrashDumpExceptionFilter);

        if (g_SaveScreenshotOnDump && g_ExeBase != 0) {
            InstallDetour64(
                "EndFrameAndPresent",
                g_ExeBase + AczRva(RVA_EndFrameAndPresent),
                PROLOGUE_EndFrameAndPresent,
                sizeof(PROLOGUE_EndFrameAndPresent),
                reinterpret_cast<const void*>(&Hook_EndFrameAndPresent),
                reinterpret_cast<void**>(&g_OrigEndFrameAndPresent)
            );
        }

        if (g_DumpMemoryOnExit && g_ExeBase != 0) {
            InstallIatHook64(
                g_ExeBase + AczRva(RVA_IAT_PostQuitMessage),
                reinterpret_cast<const void*>(&Hook_PostQuitMessage),
                reinterpret_cast<void**>(&g_OrigPostQuitMessage)
            );
            InstallIatHook64(
                g_ExeBase + AczRva(RVA_IAT_ExitProcess),
                reinterpret_cast<const void*>(&Hook_ExitProcess),
                reinterpret_cast<void**>(&g_OrigExitProcess)
            );
        }

        if (g_EnableF2MemoryDump) {
            g_HotkeyThread = CreateThread(nullptr, 0, &HotkeyMonitorThread, nullptr, 0, nullptr);
            LogMsg("[MemDump] F2 on-demand memory dump + pre-Present screenshot listener active.");
        }
    }
}

// ----------------------------------------------------------------------------
// ASI & Add-on Entry Points (Called by version.dll ASI Loader)
// ----------------------------------------------------------------------------
extern "C" __declspec(dllexport) void __fastcall AczAddonInit(const AczAddonApi* api) {
    InitializeMemDumpPlugin(api);
}

extern "C" __declspec(dllexport) void __cdecl InitializeASI() {
    InitializeMemDumpPlugin(nullptr);
}

// ----------------------------------------------------------------------------
// DLL Entry Point
// ----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_SelfModule = hModule;
        DisableThreadLibraryCalls(hModule);
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        g_ShuttingDown.store(true, std::memory_order_release);

        if (g_EnableMemoryDump && g_DumpMemoryOnExit) {
            bool expected = false;
            if (g_ExitDumpDone.compare_exchange_strong(expected, true)) {
                bool safe_for_minidump = (lpReserved == nullptr);
                PerformFullMemoryAndStateDump("Exit_DllDetach", nullptr, safe_for_minidump);
            }
        }

        LogMsg("[MemDump] Shutting down.");
        if (g_FallbackLogFile != nullptr) {
            fclose(g_FallbackLogFile);
            g_FallbackLogFile = nullptr;
        }
    }
    return TRUE;
}
