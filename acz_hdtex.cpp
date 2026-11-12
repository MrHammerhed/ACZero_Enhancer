// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — Universal HD Texture Replacer
// File: acz_hdtex.cpp (v0.23.4 — Live Controller/Keyboard Auto-Detect, Controls Screen Diagram Swapper, 2P Co-Op Config & DDS Cache Engine)
// Build: Visual Studio 2026 x64 (Release) -> acz_hdtex.dll
// Usage: Place acz_hdtex.dll into "<GameDir>\mods\acz_hdtex.dll"
//        Configuration is stored in "<GameDir>\mods\enhancer\acz_hdtex.ini"
//        Place replacement PNGs (or MASTER_<LANG>_font_32x32.png + .txt) into "<GameDir>\mods\textures\"
//        Note: Widescreen HUD/menus & controller input (enhancer.dll) and memory dumping (memdump.dll) are separate independent modules.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <intrin.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <thread>
#include <cmath>
#include <filesystem>
#include <share.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
// Configuration & Constants
// ----------------------------------------------------------------------------
static constexpr const char* PLUGIN_VERSION_STR = "v0.23.4";
static constexpr uint16_t    CUSTOM_EXTRATEX_TAG = 0x7FFF;
static constexpr uint32_t    MAX_CUSTOM_TEXTURES = 65535;
static constexpr uint64_t    FNV1A_64_OFFSET     = 0xCBF29CE484222325ULL;
static constexpr uint64_t    FNV1A_64_PRIME      = 0x00000100000001B3ULL;
static constexpr size_t      PS2_GS_VRAM_SIZE    = 4u * 1024u * 1024u; // 4 MB (16384 blocks * 256 bytes)

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
    return rva_1021 + 0x3020u; // BSS globals
}

// Validated RVAs in acz.exe (ImageBase = 0x140000000)
// Hook 1: InitGimDescriptor            (+0x2038C0, 14-byte prologue)
// Hook 2: BuildGimGifPacket            (+0x203B30, 15-byte prologue)
// Hook 3: BuildGimPixelOnlyGifPacket   (+0x203D90, 15-byte prologue) [FHM HUD/Font sub-textures]
// Hook 4: BuildGimArrayGifPackets      (+0x14CA50, 16-byte prologue) [FHM multi-GIM arrays]
// Hook 5: GsUploadVramBlock            (+0x191A00, 14-byte prologue) [Inline FHM & Font tile transfers]
// Hook 6: LookupOrCreateGsTexture      (+0x191040, 17-byte prologue)
// Hook 7: GameFont_Init                (+0x1FFBD0, 16-byte prologue) [Dynamic tile fonts: GameFont/Pmenu/CUnicodeFont]
// Hook 8: GameFont_UploadStringGlyphs  (+0x1FFCD0, 17-byte prologue) [Dynamic tile font per-string glyph uploads]
// Hook 9: LoadTextureFromArcPath       (+0x214230, 15-byte prologue) [Intercepts extratex/*.png loads before .arc mods]
static constexpr uintptr_t RVA_InitGimDescriptor           = 0x002038C0;
static constexpr uintptr_t RVA_BuildGimGifPacket           = 0x00203B30;
static constexpr uintptr_t RVA_BuildGimPixelOnlyGifPacket  = 0x00203D90;
static constexpr uintptr_t RVA_BuildGimArrayGifPackets     = 0x0014CA50;
static constexpr uintptr_t RVA_GsUploadVramBlock           = 0x00191A00;
static constexpr uintptr_t RVA_LookupOrCreateGsTexture     = 0x00191040;
static constexpr uintptr_t RVA_GameFontInit                = 0x001FFBD0;
static constexpr uintptr_t RVA_GameFontUploadStringGlyphs  = 0x001FFCD0;
static constexpr uintptr_t RVA_LoadTextureFromArcPath      = 0x00214230;

// Screen aspect option & render dimensions (read-only for _16x9 / _21x9 texture variant selection)
static constexpr uintptr_t RVA_ScreenSizeOption            = 0x00310E8C; // CSysData+0x2C: 0 = 4:3, 1 = 16:9, 2 = 21:9 (+0x313E8C in v2.0.0.1)
static constexpr uintptr_t RVA_RenderWidth                 = 0x0030F274; // Active render width (+0x312274 in v2.0.0.1)
static constexpr uintptr_t RVA_RenderHeight                = 0x0030F278; // Active render height (+0x312278 in v2.0.0.1)

// Native WIC PNG decoder + D3D12 GsTexObj creator inside acz.exe (+0x214420)
static constexpr uintptr_t RVA_CreateTextureFromPngMemory  = 0x00214420;
static constexpr uintptr_t RVA_AllocatePooledTexture       = 0x0020DAA0;
static constexpr uintptr_t RVA_UploadTextureToGpu          = 0x0020DCF0;
static constexpr uintptr_t RVA_DestroyPooledTexture        = 0x0020DFA0;
static constexpr uintptr_t RVA_EngineMalloc                = 0x0022DA9C;

// Expected prologues for safety verification before patching
static const uint8_t PROLOGUE_InitGim[14] = {
    0x40, 0x53,                         // push rbx
    0x48, 0x89, 0x51, 0x08,             // mov [rcx+8], rdx
    0x4C, 0x8D, 0x5A, 0x10,             // lea r11, [rdx+10h]
    0x4C, 0x89, 0x59, 0x10              // mov [rcx+10h], r11
};

static const uint8_t PROLOGUE_BuildGif[15] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,       // mov [rsp+10h], rbx
    0x57,                               // push rdi
    0x48, 0x83, 0xEC, 0x20,             // sub rsp, 20h
    0x48, 0x83, 0x79, 0x10, 0x00        // cmp qword ptr [rcx+10h], 0
};

static const uint8_t PROLOGUE_BuildGifPix[15] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,       // mov [rsp+10h], rbx
    0x57,                               // push rdi
    0x48, 0x83, 0xEC, 0x20,             // sub rsp, 20h
    0x48, 0x83, 0x79, 0x10, 0x00        // cmp qword ptr [rcx+10h], 0
};

static const uint8_t PROLOGUE_BuildGifArr[16] = {
    0x48, 0x89, 0x6C, 0x24, 0x18,       // mov [rsp+18h], rbp
    0x48, 0x89, 0x7C, 0x24, 0x20,       // mov [rsp+20h], rdi
    0x41, 0x56,                         // push r14
    0x48, 0x83, 0xEC, 0x20              // sub rsp, 20h
};

static const uint8_t PROLOGUE_GsUploadVram[14] = {
    0x44, 0x89, 0x4C, 0x24, 0x20,       // mov [rsp+20h], r9d
    0x44, 0x89, 0x44, 0x24, 0x18,       // mov [rsp+18h], r8d
    0x89, 0x54, 0x24, 0x10              // mov [rsp+10h], edx
};

static const uint8_t PROLOGUE_LookupTex[17] = {
    0x40, 0x55,                         // push rbp
    0x57,                               // push rdi
    0x41, 0x56,                         // push r14
    0x48, 0x8D, 0x6C, 0x24, 0xB9,       // lea rbp, [rsp-47h]
    0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00 // sub rsp, 0A0h
};

static const uint8_t PROLOGUE_GameFontInit[16] = {
    0x48, 0x83, 0xEC, 0x08,             // sub rsp, 8
    0x48, 0x8B, 0x02,                   // mov rax, [rdx]
    0x4C, 0x8B, 0xCA,                   // mov r9, rdx
    0x4C, 0x8B, 0xC1,                   // mov r8, rcx
    0x48, 0x85, 0xC0                    // test rax, rax
};

static const uint8_t PROLOGUE_GameFontUpload[17] = {
    0x4C, 0x89, 0x4C, 0x24, 0x20,       // mov [rsp+20h], r9
    0x53,                               // push rbx
    0x56,                               // push rsi
    0x41, 0x54,                         // push r12
    0x48, 0x83, 0xEC, 0x30,             // sub rsp, 30h
    0xF6, 0x41, 0x52, 0x01              // test byte ptr [rcx+52h], 1
};

static const uint8_t PROLOGUE_LoadTextureFromArc[15] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,       // mov [rsp+10h], rbx
    0x48, 0x89, 0x74, 0x24, 0x18,       // mov [rsp+18h], rsi
    0x57,                               // push rdi
    0x48, 0x83, 0xEC, 0x70              // sub rsp, 70h
};

static const uint8_t PROLOGUE_UploadTextureToGpu[18] = {
    0x48, 0x89, 0x7C, 0x24, 0x20,                   // mov [rsp+20h], rdi
    0x55,                                           // push rbp
    0x48, 0x8D, 0x6C, 0x24, 0xB0,                   // lea rbp, [rsp-50h]
    0x48, 0x81, 0xEC, 0x50, 0x01, 0x00, 0x00        // sub rsp, 150h
};


// ----------------------------------------------------------------------------
// Engine Function Typedefs
// ----------------------------------------------------------------------------
using FnInitGimDescriptor          = void      (__fastcall*)(uint8_t* desc, uint8_t* gim_data);
using FnBuildGimGifPacket          = uint8_t*  (__fastcall*)(uint8_t* desc, uint8_t* gif_buf);
using FnBuildGimPixelOnlyGifPacket = uint8_t*  (__fastcall*)(uint8_t* desc, uint8_t* gif_buf);
using FnBuildGimArrayGifPackets    = uint8_t*  (__fastcall*)(uint8_t* array_ctx, uint8_t* gif_buf);
using FnGsUploadVramBlock          = void      (__fastcall*)(
    void* gs_vram_ctx,
    uint32_t dbp,
    uint32_t rrw,
    uint32_t rrh,
    uint32_t qwc,
    uint8_t* src_data,
    uint32_t dpsm,
    uint32_t extratex_id
);

using FnLookupOrCreateGsTexture    = uint8_t*  (__fastcall*)(
    void* gs_ctx,
    uint64_t tex0_reg,
    uint8_t flag_r8
);

using FnGameFontInit               = bool      (__fastcall*)(
    uint8_t* font_obj,
    uint8_t** data_ptr_in_out
);

using FnGameFontUploadStringGlyphs = bool      (__fastcall*)(
    uint8_t* font_obj,
    int string_idx,
    uint32_t* vram_tbp_cursor,
    uint8_t** gif_buf_in_out
);

using FnLoadTextureFromArcPath     = uint8_t*  (__fastcall*)(
    const char* const* arc_path_ptr
);

using FnCreateTextureFromPngMemory = uint8_t*  (__fastcall*)(
    const void* png_bytes,
    size_t png_len
);
using FnAllocatePooledTexture = uint8_t* (__fastcall*)(
    uint32_t width,
    uint32_t height,
    uint32_t mip_levels,
    uint32_t dxgi_format,
    void* existing_slot
);
using FnUploadTextureToGpu = void (__fastcall*)(
    uint8_t* hd_tex
);
using FnDestroyPooledTexture = void (__fastcall*)(
    uint8_t* hd_tex
);
using FnEngineMalloc = void* (__fastcall*)(
    size_t size
);

static FnInitGimDescriptor          g_OrigInitGimDescriptor          = nullptr;
static FnBuildGimGifPacket          g_OrigBuildGimGifPacket          = nullptr;
static FnBuildGimPixelOnlyGifPacket g_OrigBuildGimPixelOnlyGifPacket = nullptr;
static FnBuildGimArrayGifPackets    g_OrigBuildGimArrayGifPackets    = nullptr;
static FnGsUploadVramBlock          g_OrigGsUploadVramBlock          = nullptr;
static FnLookupOrCreateGsTexture    g_OrigLookupOrCreateGsTexture    = nullptr;
static FnGameFontInit               g_OrigGameFontInit               = nullptr;
static FnGameFontUploadStringGlyphs g_OrigGameFontUploadStringGlyphs = nullptr;
static FnLoadTextureFromArcPath     g_OrigLoadTextureFromArcPath     = nullptr;
static FnCreateTextureFromPngMemory g_CreateTextureFromPngMemory     = nullptr;
static FnAllocatePooledTexture      g_AllocatePooledTexture          = nullptr;
static FnUploadTextureToGpu         g_UploadTextureToGpu             = nullptr;
static FnUploadTextureToGpu         g_OrigUploadTextureToGpu         = nullptr;
static SRWLOCK                      g_UploadTextureGpuLock           = SRWLOCK_INIT;
static FnDestroyPooledTexture       g_DestroyPooledTexture           = nullptr;
static FnEngineMalloc               g_EngineMalloc                   = nullptr;

// ----------------------------------------------------------------------------
// Plugin State & Configuration
// ----------------------------------------------------------------------------
struct CustomSlotInfo {
    uint64_t             hash        = 0;
    std::string          png_path;
    std::string          png_path_tier[3];        // [0] = 4:3 default, [1] = _16x9, [2] = _21x9
    std::vector<uint8_t> mem_png;                 // Used for sliced dynamic font / 16x16 GIM tile PNGs (loaded lazily on render thread)
    uint8_t*             loaded_tex  = nullptr;
    uint8_t*             loaded_tex_tier[3]{};    // Loaded D3D12 GsTexObj* per aspect tier
    bool                 load_failed = false;
    bool                 load_failed_tier[3]{};
};

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

struct GimQuickInfo {
    bool     valid        = false;
    uint32_t total_size   = 0;
    uint32_t clut_offset  = 0;
    uint16_t width        = 0;
    uint16_t height       = 0;
    uint8_t  psm          = 0;
    uint64_t quick_fp     = 0; // O(1) 64-byte sample fingerprint (header + mid-pixels + CLUT, invariant to VRAM alloc)
    uint64_t content_tag  = 0; // Ultra-fast 2-qword tag for lock-free descriptor check
};

struct GimFastCacheEntry {
    uint64_t quick_fp          = 0;
    uint64_t content_tag       = 0;
    uint32_t total_size        = 0;
    uint32_t clut_offset       = 0;
    uint16_t width             = 0;
    uint16_t height            = 0;
    uint8_t  orig_psm          = 0;
    uint64_t primary_hash      = 0;
    uint32_t custom_id         = 0;
    bool     has_orig_headers  = false;
    uint8_t  orig_hdr[32]{};      // Original 32-byte GIM header before runtime tbp0/tbw/psm mutation
    uint8_t  orig_clut_hdr[16]{}; // Original 16-byte CLUT header before runtime cbp/csm mutation
};

struct GimInspectionResult {
    bool     valid          = false;
    uint32_t total_size     = 0;
    uint32_t clut_offset    = 0;
    uint16_t width          = 0;
    uint16_t height         = 0;
    uint8_t  psm            = 0;
    uint64_t hash_exact     = 0; // Byte 0x12 zeroed (matches ACZ_GIM_PNG_Converter.py & ACZ_FHM_Converter.py)
    uint64_t hash_psm_norm  = 0; // Byte 0x12 zeroed + PSM 0x1B restored to 0x13
    uint64_t hash_fhm_norm  = 0; // Byte 0x10..0x12 & CLUT CBP/CSM zeroed + PSM 0x1B->0x13
    uint64_t hash_raw       = 0; // Unmodified in-memory bytes
};

struct Tim2InspectionResult {
    bool     valid              = false;
    uint32_t pic_offset         = 16;   // 16 (align=0) or 128 (align=1)
    uint16_t header_size        = 0;    // 48 (0x30) or 80 (0x50 with eXt + GIDX)
    uint16_t clut_colors        = 0;    // 16 or 256 (0 for direct RGB)
    uint32_t image_size         = 0;
    uint32_t clut_size          = 0;
    uint32_t total_pic_size     = 0;
    uint32_t total_size         = 0;    // pic_offset + total_pic_size
    uint16_t width              = 0;
    uint16_t height             = 0;
    uint8_t  clut_type          = 0;
    uint8_t  image_type         = 0;    // 4 = IDTEX4 (4-bit), 5 = IDTEX8 (8-bit), 1..3 = RGB16/24/32
    uint8_t  psm                = 0;    // 0x14 (PSMT4) or 0x13 (PSMT8)
    uint8_t  canon_hdr_32[32]{};        // Reconstructed 32-byte TIM2 disk header (+0x00..+0x1F)
    uint64_t content_tag        = 0;    // Fast 64-bit tag for lock-free cache check
    uint64_t hash_tim2_norm     = 0;    // Full canonical TIM2 file hash (matches disk .tm2 / .swa TIM2)
    uint64_t hash_tim2_pix_clut = 0;    // FNV-1a over (pixels + CLUT)
    uint64_t hash_tim2_pixels   = 0;    // FNV-1a over pixel bytes only
};

struct Tim2FastCacheEntry {
    uint64_t content_tag    = 0;
    uint32_t total_size     = 0;
    uint16_t width          = 0;
    uint16_t height         = 0;
    uint8_t  image_type     = 0;
    uint8_t  psm            = 0;
    uint64_t primary_hash   = 0;
    uint32_t custom_id      = 0;
};

struct FontInspectionResult {
    bool     valid          = false;
    uint32_t total_size     = 0;
    uint32_t num_strings    = 0;
    uint16_t num_glyphs     = 0;
    uint16_t glyph_dim      = 0;
    uint32_t rdi_off        = 0;
    uint32_t r10_off        = 0;
    uint32_t end_idx_off    = 0;
    uint32_t pal_off        = 0;
    uint32_t bmp_off        = 0;
    uint8_t* glyph_meta_ptr = nullptr;
    uint8_t* clut_ptr       = nullptr;
    uint8_t* tiles_ptr      = nullptr;
    uint64_t hash_norm      = 0; // Runtime fields (runtime_tbp +4..7, flags +10) zeroed
    uint64_t hash_clean     = 0; // Runtime fields + uninitialized 0xCD padding zeroed
    uint64_t hash_visual    = 0; // Visual atlas hash (num_glyphs, glyph_dim, clut, tiles)
};

struct CachedFontReplacement {
    std::vector<uint32_t> glyph_custom_ids;
};

struct OrigGlyphRecord {
    uint16_t char_code        = 0;
    uint16_t adv_x            = 0;
    uint16_t height           = 0;
    bool     is_non_empty     = false;
    uint64_t exact_tile_hash  = 0; // FNV-1a of (glyph_dim, char_code, adv_x, height, orig_clut[64], orig_tile[tile_bytes])
    uint64_t bitmap_tile_hash = 0; // FNV-1a of (glyph_dim, char_code, adv_x, height, orig_tile[tile_bytes])
    uint32_t char_dim_key     = 0; // (uint32_t(glyph_dim) << 16) | uint32_t(char_code)
};

struct OrigFontSnapshot {
    uint64_t                     primary_hash = 0;
    uint64_t                     family_hash  = 0;
    uint16_t                     num_glyphs   = 0;
    uint16_t                     glyph_dim    = 0;
    uint32_t                     num_strings  = 0;
    uint32_t                     total_size   = 0;
    uint8_t                      orig_clut[64]{};
    std::vector<uint8_t>         orig_tiles;
    std::vector<OrigGlyphRecord> glyphs;
};

struct FontHeapInstance {
    uint64_t primary_hash = 0;
    uint64_t family_hash  = 0;
    uint32_t total_size   = 0;
    uint16_t num_glyphs   = 0;
    uint16_t glyph_dim    = 0;
    uint32_t num_strings  = 0;
};

struct FamilyGlyphEntry {
    uint16_t              char_code        = 0;
    uint16_t              adv_x            = 0;
    uint16_t              height           = 0;
    uint16_t              glyph_dim        = 0;
    bool                  is_non_empty     = false;
    uint64_t              exact_tile_hash  = 0;
    uint64_t              bitmap_tile_hash = 0;
    uint64_t              family_hash      = 0;
    uint8_t               clut_ps2[64]{};
    std::vector<uint8_t>  tile_4b;
    std::vector<uint8_t>  rgba_tile;
    std::vector<uint64_t> alt_hashes;
};

struct FontFamilyInfo {
    uint64_t                      family_hash = 0;
    uint16_t                      glyph_dim   = 0;
    uint8_t                       clut_ps2[64]{};
    std::vector<uint64_t>         member_hashes;
    std::vector<std::string>      sample_strings;
    std::vector<FamilyGlyphEntry> glyphs; // Kept sorted by (char_code, exact_tile_hash, bitmap_tile_hash)
};

struct MasterAtlasFamilySection {
    uint64_t    family_hash   = 0;
    uint32_t    start_idx     = 0;
    uint32_t    end_idx       = 0;
    uint32_t    new_glyphs    = 0;
    size_t      member_blocks = 0;
    std::string sample;
};

// Runtime Master Font Atlas per tile dimension (e.g. 32x32, 16x16)
struct MasterFontAtlasInfo {
    uint16_t                              glyph_dim   = 0;
    std::vector<uint64_t>                 member_hashes;
    std::vector<std::string>              sample_strings;
    std::vector<MasterAtlasFamilySection> sections;
    std::vector<FamilyGlyphEntry>         glyphs;
};

// Runtime Master 16x16 GIM Atlas
struct MasterGim16Tile {
    uint64_t             hash_psm_norm = 0;
    uint64_t             hash_exact    = 0;
    uint64_t             hash_fhm_norm = 0;
    uint8_t              psm           = 0;
    std::vector<uint8_t> rgba_16x16; // 16 * 16 * 4 = 1024 bytes
};

struct SharedGlyphReplacement {
    uint32_t custom_id        = 0;
    uint64_t source_font_hash = 0;
    uint64_t family_hash      = 0;
    uint16_t glyph_dim        = 0;
    uint16_t char_code        = 0;
    uint16_t std_h            = 0;
    bool     is_master_atlas  = false;
};

struct ActivePixelBinding {
    uint32_t custom_id  = 0;
    uint16_t expected_w = 0;
    uint16_t expected_h = 0;
};

// Lock-free open-addressed hash table slot for Hook_GsUploadVramBlock
// Eliminates mutex locking and std::unordered_map lookups on the render/upload thread!
struct LockFreePixelSlot {
    std::atomic<uintptr_t> addr{0};
    std::atomic<uint64_t>  packed_meta{0}; // bits 0..31: custom_id (0 = invalidated), 32..47: expected_w, 48..63: expected_h
    std::atomic<uint64_t>  content_tag{0}; // 128-byte FNV-1a content fingerprint to prevent heap-reuse collisions
};

// Lock-free direct-mapped GIM descriptor validation slot
struct LockFreeGimFastSlot {
    std::atomic<uintptr_t> gim_addr{0};
    std::atomic<uint64_t>  content_tag{0};
    std::atomic<uint32_t>  custom_id{0};
};

static std::mutex                                                  g_Mutex;
static FnModLoaderLog                                              g_ModLoaderLogFn                   = nullptr;
static FILE*                                                       g_FallbackLogFile                  = nullptr;
static std::atomic<bool>                                           g_PluginInitialized{false};
static HMODULE                                                     g_SelfModule                       = nullptr;
static uintptr_t                                                   g_ExeBase                          = 0;
static std::atomic<void*>                                          g_LastGsVramCtx{nullptr};
static std::atomic<void*>                                          g_LastGsRenderCtx{nullptr};

static fs::path                                                    g_GameDir;
static fs::path                                                    g_ModsDir;
static fs::path                                                    g_TexturesDir;
static fs::path                                                    g_DdsCacheDir;
static fs::path                                                    g_DumpDir;
static fs::path                                                    g_FontDumpDir;

static bool                                                        g_EnableLogging                    = true;
static bool                                                        g_DumpAllGims                      = false;
static bool                                                        g_DumpReplacedTextures             = false;
static bool                                                        g_DumpRawGimFiles                  = false;
static bool                                                        g_HotReload                        = false;
static bool                                                        g_AutoShareFontGlyphs              = true;
static bool                                                        g_ShareCharCodeToSingleLineSubs    = false;
static bool                                                        g_ForceGlobalCharCodeSharing       = false;
static bool                                                        g_DumpSingleLineSubtitles          = false;
static bool                                                        g_DumpMasterFontAtlas              = true;
static bool                                                        g_EnableMenuOverrides              = true;
static int                                                         g_MenuAspectMode                   = 0; // 0 = Auto (in-game 16:9 check), 1 = Force 16:9, 2 = Force 4:3, 3 = Force 21:9
static int                                                         g_GamepadGlyphsMode                = 0;
static bool                                                        g_Unlock2PlayerCampaign            = false; // 0 = Clean Menu (Hide cut 2P NEW GAME), 1 = Show 2P NEW GAME // 0 = AUTO, 1 = PS, 2 = XBOX, 3 = KB
static bool                                                        g_EnableDdsCache                   = true;
static int                                                         g_DdsCacheCompression              = 1; // 1 = Smart BC7 4:1 for >=256x256 + BGRA8 for small icons, 0 = Lossless BGRA8 DDS
static bool                                                        g_DumpMergedFamilyAtlases          = false;
static bool                                                        g_DumpIndividualFontAtlases        = false;

// ============================================================================
// Lock-Free Fast-Path Structures for Render Thread & GS Upload Thread (v0.13)
// ============================================================================
// 1. 524,288-bit (64 KB) Lock-Free Atomic Bloom Filter + 65,536-slot Lock-Free Open-Addressed Pixel Table
static constexpr size_t                                            BLOOM_WORD_COUNT          = 8192;  // 8192 * 64 = 524,288 bits (64 KB)
static constexpr size_t                                            BLOOM_WORD_MASK           = BLOOM_WORD_COUNT - 1;
static constexpr size_t                                            LOCKFREE_PIXEL_TABLE_SIZE = 65536; // Power of 2
static constexpr size_t                                            LOCKFREE_PIXEL_TABLE_MASK = LOCKFREE_PIXEL_TABLE_SIZE - 1;

static std::atomic<uintptr_t>                                      g_MinReplacedPixelAddr{~uintptr_t(0)};
static std::atomic<uintptr_t>                                      g_MaxReplacedPixelAddr{0};
static std::atomic<uint64_t>                                       g_ReplacedPixelBloomTable[BLOOM_WORD_COUNT]{};
static LockFreePixelSlot                                           g_LockFreePixelTable[LOCKFREE_PIXEL_TABLE_SIZE]{};

// 2. Direct-mapped lock-free table of already-initialized GIM buffers + content tag
static constexpr size_t                                            FAST_GIM_CACHE_SIZE = 4096;
static LockFreeGimFastSlot                                         g_FastGimTable[FAST_GIM_CACHE_SIZE]{};

// 3. Direct-mapped lock-free table of (font_addr ^ epoch) for Hook_GameFontUploadStringGlyphs
static constexpr size_t                                            FAST_FONT_CACHE_SIZE = 1024;
static std::atomic<uintptr_t>                                      g_FastFontEpochCache[FAST_FONT_CACHE_SIZE]{};

// 4. Lock-free array of loaded D3D12 GsTexObj* per slot & aspect tier (0=4:3, 1=16:9, 2=21:9)
static std::atomic<uint8_t*>                                       g_SlotLoadedTex[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint8_t*>                                       g_SlotLoadedTexTier[3][MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<bool>                                           g_SlotLoadFailed[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<bool>                                           g_SlotLoadFailedTier[3][MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint8_t>                                        g_SlotAvailMask[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint16_t>                                       g_SlotExpectedW[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint16_t>                                       g_SlotExpectedH[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint16_t>                                       g_SlotAltExpectedW[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint16_t>                                       g_SlotAltExpectedH[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<bool>                                           g_SlotAllowHalfDim[MAX_CUSTOM_TEXTURES + 1]{};

static inline void RecordSlotExpectedDims(uint16_t slot_idx, uint16_t w, uint16_t h) {
    if (slot_idx == 0 || slot_idx > MAX_CUSTOM_TEXTURES || w == 0 || h == 0) return;
    uint16_t exp_w = g_SlotExpectedW[slot_idx].load(std::memory_order_relaxed);
    uint16_t exp_h = g_SlotExpectedH[slot_idx].load(std::memory_order_relaxed);
    if (exp_w == 0 || exp_h == 0) {
        g_SlotExpectedW[slot_idx].store(w, std::memory_order_release);
        g_SlotExpectedH[slot_idx].store(h, std::memory_order_release);
    } else if (exp_w != w || exp_h != h) {
        g_SlotAltExpectedW[slot_idx].store(w, std::memory_order_release);
        g_SlotAltExpectedH[slot_idx].store(h, std::memory_order_release);
    }
}
// 5. Built-in PC port extratex slots:
//    Category 1 (hudfont): [0] = extratex/hudfont/000.png (0x00000001), [1] = extratex/hudfont/001.png (0x00010001)
//    Category 2 (texture): [0..16] = extratex/texture/000.png .. 016.png ((idx << 16) | 2, gamepad glyphs 001..008)
static std::atomic<uint16_t>                                       g_BuiltinExtratexSlot[2]{};
static std::atomic<uint16_t>                                       g_BuiltinPadExtratexSlot[18]{};
static std::atomic<uint16_t>                                       g_BuiltinTextureExtratexSlot[32]{};
static std::atomic<uint16_t>                                       g_BuiltinManual000Slot{0};
static std::atomic<bool>                                           g_SlotIsPadSelector[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<void*>                                          g_PadSlotOrigHdr[3][MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint32_t>                                       g_PadSlotOrigHandle[3][MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint8_t*>                                       g_ManualTargetTexObj{nullptr};
static std::atomic<void*>                                          g_ManualOrigSlotHdr{nullptr};
static std::atomic<uint32_t>                                       g_ManualOrigHandle{0xFFFFFFFFu};
static std::atomic<void*>                                          g_ManualXboxSlotHdr{nullptr};
static std::atomic<uint32_t>                                       g_ManualXboxHandle{0xFFFFFFFFu};
static std::atomic<void*>                                          g_ManualKbSlotHdr{nullptr};
static std::atomic<uint32_t>                                       g_ManualKbHandle{0xFFFFFFFFu};
static std::atomic<void*>                                          g_ManualPsSlotHdr{nullptr};
static std::atomic<uint32_t>                                       g_ManualPsHandle{0xFFFFFFFFu};
static std::atomic<uint8_t*>                                       g_BtnTargetTexObj{nullptr};
static std::atomic<void*>                                          g_BtnOrigSlotHdr{nullptr};
static std::atomic<uint32_t>                                       g_BtnOrigHandle{0xFFFFFFFFu};
static std::atomic<int>                                            g_LastSyncedPadVariant{-1};

// 6. Lock-free TIM2 cache + Content Hash Table + Per-TBP dynamic sprite tracking (v0.16)
static LockFreeGimFastSlot                                         g_FastTim2Table[FAST_GIM_CACHE_SIZE]{};
struct LockFreeContentHashSlot {
    std::atomic<uint64_t> hash64{0};
    std::atomic<uint16_t> slot_idx{0};
};
static constexpr size_t                                            LOCKFREE_HASH_TABLE_SIZE = 65536;
static constexpr size_t                                            LOCKFREE_HASH_TABLE_MASK = LOCKFREE_HASH_TABLE_SIZE - 1;
static LockFreeContentHashSlot                                     g_LockFreeContentHashTable[LOCKFREE_HASH_TABLE_SIZE]{};
static std::atomic<uint32_t>                                       g_LastTbpCustomId[16384]{};
static std::atomic<uint16_t>                                       g_LastTbpOrigW[16384]{};
static std::atomic<uint16_t>                                       g_LastTbpOrigH[16384]{};
static std::atomic<uint32_t>                                       g_LastUploadedTim2CustomId{0};
static std::atomic<bool>                                           g_LastUploadWasSkipped{false};
static uint64_t                                                    g_SlotLastWriteTime[3][MAX_CUSTOM_TEXTURES + 1]{};
static uint64_t                                                    g_SlotLastHotCheckTick[3][MAX_CUSTOM_TEXTURES + 1]{};

static inline uint32_t NextPow2U32(uint32_t v) {
    if (v <= 1u) return 1u;
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1u;
}

// Slot index (1 .. 65535) -> CustomSlotInfo
static std::vector<CustomSlotInfo>                                 g_Slots;
// 64-bit FNV-1a hash -> Slot index
static std::unordered_map<uint64_t, uint16_t>                      g_HashToSlot;
static std::unordered_map<uint64_t, uint16_t>                      g_FamilyHashToSlot;
static std::unordered_map<uint64_t, uint16_t>                      g_MasterHashToSlot;
// O(1) fast cache per in-memory GIM / TIM2 pointer
static std::unordered_map<uintptr_t, GimFastCacheEntry>            g_GimFastCache;
static std::unordered_map<uintptr_t, Tim2FastCacheEntry>           g_Tim2FastCache;
// Track already-logged GIM & Font hashes so we don't spam the log every frame
static std::unordered_set<uint64_t>                                g_LoggedHashes;
static std::unordered_set<uint64_t>                                g_LoggedMatchedGimHashes;
static std::unordered_set<uint64_t>                                g_LoggedFontHashes;
static std::unordered_set<uint64_t>                                g_LoggedBoundFontHashes;
// Deduplicated original font snapshots by primary_hash (saves ~90% RAM across menu transitions)
static std::unordered_map<uint64_t, OrigFontSnapshot>              g_FontSnapshotsByHash;
// Active font instances by heap address (automatically pruned when overwritten)
static std::unordered_map<uintptr_t, FontHeapInstance>             g_FontHeapInstances;
// Active pixel address bindings (mirrors g_LockFreePixelTable under g_Mutex for range invalidation & reporting)
static std::unordered_map<uintptr_t, ActivePixelBinding>           g_ActivePixelBindings;
// Cache per-glyph custom_ids for a loaded dynamic font replacement
static std::unordered_map<uint64_t, CachedFontReplacement>         g_FontReplacements;
// Global shared HD glyph tables populated from replaced Font Atlases & Master Atlases
static std::unordered_map<uint64_t, SharedGlyphReplacement>        g_SharedGlyphByExactHash;
static std::unordered_map<uint64_t, SharedGlyphReplacement>        g_SharedGlyphByFamilyBitmapKey;
static std::unordered_map<uint64_t, SharedGlyphReplacement>        g_SharedGlyphByBitmapHash;
static std::unordered_map<uint32_t, SharedGlyphReplacement>        g_SharedGlyphByCharKey;
static std::atomic<uint32_t>                                       g_SharedGlyphEpoch{1};
// Track already-processed font_obj start_ptrs -> epoch when last processed
static std::unordered_map<uintptr_t, uint32_t>                     g_ProcessedFontEpoch;
struct SavedFontOrigHeights {
    std::vector<uint16_t> char_codes;
    std::vector<uint16_t> adv_xs;
    std::vector<uint16_t> orig_heights;
    std::vector<uint16_t> expanded_heights;
};
static std::unordered_map<uintptr_t, SavedFontOrigHeights>         g_FontOrigHeights;
// Font Family clustering: family_hash -> FontFamilyInfo + chronological registration order
static std::unordered_map<uint64_t, FontFamilyInfo>                g_FontFamilies;
static std::vector<uint64_t>                                       g_FontFamilyOrder;
// Runtime Master Font Atlases by tile dimension (32 -> MASTER_FONT_32x32, 16 -> MASTER_FONT_16x16)
static std::unordered_map<uint16_t, MasterFontAtlasInfo>           g_MasterFontAtlasesByDim;
// Runtime Master 16x16 GIM Atlas
static std::vector<MasterGim16Tile>                                g_MasterGim16Tiles;
static std::unordered_set<uint64_t>                                g_MasterGim16SeenHashes;

static inline uint64_t MakeFamilyBitmapKey(uint64_t family_hash, uint64_t bitmap_tile_hash) {
    return (family_hash * FNV1A_64_PRIME) ^ bitmap_tile_hash;
}

// ----------------------------------------------------------------------------
// Lock-Free Pixel Table & 512-Kbit Bloom Filter Operations
// ----------------------------------------------------------------------------
static inline size_t HashPixelAddr(uintptr_t addr) {
    uint64_t x = static_cast<uint64_t>(addr >> 4);
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDULL;
    x ^= x >> 33;
    return static_cast<size_t>(x);
}

static inline void LockFreeClearContentHashTable() {
    for (size_t i = 0; i < LOCKFREE_HASH_TABLE_SIZE; ++i) {
        g_LockFreeContentHashTable[i].hash64.store(0, std::memory_order_relaxed);
        g_LockFreeContentHashTable[i].slot_idx.store(0, std::memory_order_relaxed);
    }
    for (size_t i = 0; i < 16384; ++i) {
        g_LastTbpCustomId[i].store(0, std::memory_order_relaxed);
        g_LastTbpOrigW[i].store(0, std::memory_order_relaxed);
        g_LastTbpOrigH[i].store(0, std::memory_order_relaxed);
    }
    g_LastUploadedTim2CustomId.store(0, std::memory_order_relaxed);
}

static inline void LockFreeInsertContentHash(uint64_t hash64, uint16_t slot_idx) {
    if (hash64 == 0 || slot_idx == 0) return;
    size_t idx = static_cast<size_t>(hash64) & LOCKFREE_HASH_TABLE_MASK;
    for (size_t probe = 0; probe < 256; ++probe) {
        size_t cur_idx = (idx + probe) & LOCKFREE_HASH_TABLE_MASK;
        uint64_t cur_h = g_LockFreeContentHashTable[cur_idx].hash64.load(std::memory_order_relaxed);
        if (cur_h == hash64 || cur_h == 0) {
            g_LockFreeContentHashTable[cur_idx].hash64.store(hash64, std::memory_order_relaxed);
            g_LockFreeContentHashTable[cur_idx].slot_idx.store(slot_idx, std::memory_order_release);
            return;
        }
    }
}

static inline uint16_t LockFreeLookupContentHash(uint64_t hash64) {
    if (hash64 == 0) return 0;
    size_t idx = static_cast<size_t>(hash64) & LOCKFREE_HASH_TABLE_MASK;
    for (size_t probe = 0; probe < 256; ++probe) {
        size_t cur_idx = (idx + probe) & LOCKFREE_HASH_TABLE_MASK;
        uint64_t cur_h = g_LockFreeContentHashTable[cur_idx].hash64.load(std::memory_order_acquire);
        if (cur_h == 0) return 0;
        if (cur_h == hash64) {
            return g_LockFreeContentHashTable[cur_idx].slot_idx.load(std::memory_order_acquire);
        }
    }
    return 0;
}

static inline void BloomInsertPixelAddr(uintptr_t addr) {
    size_t h = HashPixelAddr(addr);
    size_t word_idx = (h >> 12) & BLOOM_WORD_MASK;
    uint64_t mask   = (1ULL << (h & 63u)) | (1ULL << ((h >> 6) & 63u));
    g_ReplacedPixelBloomTable[word_idx].fetch_or(mask, std::memory_order_release);
}

static inline bool BloomTestPixelAddr(uintptr_t addr) {
    size_t h = HashPixelAddr(addr);
    size_t word_idx = (h >> 12) & BLOOM_WORD_MASK;
    uint64_t mask   = (1ULL << (h & 63u)) | (1ULL << ((h >> 6) & 63u));
    uint64_t word   = g_ReplacedPixelBloomTable[word_idx].load(std::memory_order_acquire);
    return (word & mask) == mask;
}

static inline uint64_t SafeComputePixelBlockTag(uintptr_t addr) {
    if (addr <= 0x10000u) return 0;
    __try {
        const uint64_t* q = reinterpret_cast<const uint64_t*>(addr);
        uint64_t h = FNV1A_64_OFFSET;
        for (size_t i = 0; i < 8; ++i) {
            h ^= q[i];
            h *= FNV1A_64_PRIME;
        }
        return (h == 0) ? 1ULL : h;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static inline void LockFreeUpsertPixelBinding(uintptr_t addr, uint32_t custom_id, uint16_t expected_w, uint16_t expected_h) {
    if (addr == 0) return;
    uint64_t packed = static_cast<uint64_t>(custom_id) |
                      (static_cast<uint64_t>(expected_w) << 32) |
                      (static_cast<uint64_t>(expected_h) << 48);
    uint64_t tag = (custom_id != 0) ? SafeComputePixelBlockTag(addr) : 0ULL;

    size_t idx = HashPixelAddr(addr) & LOCKFREE_PIXEL_TABLE_MASK;
    for (size_t step = 0; step < 128; ++step) {
        uintptr_t cur = g_LockFreePixelTable[idx].addr.load(std::memory_order_relaxed);
        if (cur == addr) {
            g_LockFreePixelTable[idx].content_tag.store(tag, std::memory_order_relaxed);
            g_LockFreePixelTable[idx].packed_meta.store(packed, std::memory_order_release);
            return;
        }
        if (cur == 0) {
            uintptr_t expected = 0;
            if (g_LockFreePixelTable[idx].addr.compare_exchange_strong(expected, addr, std::memory_order_acq_rel)) {
                g_LockFreePixelTable[idx].content_tag.store(tag, std::memory_order_relaxed);
                g_LockFreePixelTable[idx].packed_meta.store(packed, std::memory_order_release);
                return;
            }
            if (expected == addr) {
                g_LockFreePixelTable[idx].content_tag.store(tag, std::memory_order_relaxed);
                g_LockFreePixelTable[idx].packed_meta.store(packed, std::memory_order_release);
                return;
            }
        }
        idx = (idx + 1) & LOCKFREE_PIXEL_TABLE_MASK;
    }
    size_t base_idx = HashPixelAddr(addr) & LOCKFREE_PIXEL_TABLE_MASK;
    g_LockFreePixelTable[base_idx].addr.store(addr, std::memory_order_release);
    g_LockFreePixelTable[base_idx].content_tag.store(tag, std::memory_order_relaxed);
    g_LockFreePixelTable[base_idx].packed_meta.store(packed, std::memory_order_release);
}

static inline uint32_t LockFreeLookupPixelBinding(uintptr_t addr, uint32_t rrw, uint32_t rrh) {
    size_t idx = HashPixelAddr(addr) & LOCKFREE_PIXEL_TABLE_MASK;
    for (size_t step = 0; step < 128; ++step) {
        uintptr_t cur = g_LockFreePixelTable[idx].addr.load(std::memory_order_acquire);
        if (cur == 0) {
            return 0;
        }
        if (cur == addr) {
            uint64_t packed = g_LockFreePixelTable[idx].packed_meta.load(std::memory_order_acquire);
            uint32_t cid = static_cast<uint32_t>(packed & 0xFFFFFFFFu);
            if (cid == 0) return 0;
            uint32_t exp_w = static_cast<uint32_t>((packed >> 32) & 0xFFFFu);
            uint32_t exp_h = static_cast<uint32_t>((packed >> 48) & 0xFFFFu);
            if (exp_w != 0 && exp_h != 0 && rrw != 0 && rrh != 0) {
                bool dim_ok = (rrw == exp_w && rrh == exp_h) ||
                              (rrw * 2u == exp_w && rrh * 2u == exp_h) ||
                              (rrw * 4u == exp_w && rrh * 2u == exp_h) ||
                              (rrw * 2u == exp_w && rrh * 4u == exp_h);
                if (!dim_ok) {
                    return 0;
                }
            }
            uint64_t expected_tag = g_LockFreePixelTable[idx].content_tag.load(std::memory_order_acquire);
            if (expected_tag != 0 && SafeComputePixelBlockTag(addr) != expected_tag) {
                return 0;
            }
            return cid;
        }
        idx = (idx + 1) & LOCKFREE_PIXEL_TABLE_MASK;
    }
    return 0;
}

static inline void RegisterReplacedPixelAddress(uintptr_t addr, uint32_t custom_id, uint16_t expected_w, uint16_t expected_h) {
    ActivePixelBinding bind{};
    bind.custom_id  = custom_id;
    bind.expected_w = expected_w;
    bind.expected_h = expected_h;
    g_ActivePixelBindings[addr] = bind;

    LockFreeUpsertPixelBinding(addr, custom_id, expected_w, expected_h);

    uintptr_t cur_min = g_MinReplacedPixelAddr.load(std::memory_order_relaxed);
    while (addr < cur_min && !g_MinReplacedPixelAddr.compare_exchange_weak(cur_min, addr, std::memory_order_relaxed)) {}

    uintptr_t cur_max = g_MaxReplacedPixelAddr.load(std::memory_order_relaxed);
    while (addr > cur_max && !g_MaxReplacedPixelAddr.compare_exchange_weak(cur_max, addr, std::memory_order_relaxed)) {}

    BloomInsertPixelAddr(addr);
}

// Automatically purge stale pixel/glyph tile bindings when a new GIM or Font is loaded at [range_start, range_end)
// Caller must hold g_Mutex.
static void InvalidatePixelBindingsInRange(uintptr_t range_start, uint32_t byte_size, uintptr_t keep_exact_base) {
    if (byte_size == 0) return;
    uintptr_t range_end = range_start + static_cast<uintptr_t>(byte_size);

    for (auto it = g_ActivePixelBindings.begin(); it != g_ActivePixelBindings.end(); ) {
        uintptr_t p = it->first;
        if (p >= range_start && p < range_end) {
            LockFreeUpsertPixelBinding(p, 0, 0, 0);
            it = g_ActivePixelBindings.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = g_FontHeapInstances.begin(); it != g_FontHeapInstances.end(); ) {
        uintptr_t f_start = it->first;
        uintptr_t f_end   = f_start + static_cast<uintptr_t>(it->second.total_size);
        if (f_start != keep_exact_base && f_start < range_end && f_end > range_start) {
            g_ProcessedFontEpoch.erase(f_start);
            it = g_FontHeapInstances.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = g_GimFastCache.begin(); it != g_GimFastCache.end(); ) {
        uintptr_t g_start = it->first;
        uintptr_t g_end   = g_start + static_cast<uintptr_t>(it->second.total_size);
        if (g_start != keep_exact_base && g_start < range_end && g_end > range_start) {
            it = g_GimFastCache.erase(it);
        } else {
            ++it;
        }
    }
}

static size_t CountBloomPopulatedBits() {
    size_t set_bits = 0;
    for (size_t i = 0; i < BLOOM_WORD_COUNT; ++i) {
        uint64_t w = g_ReplacedPixelBloomTable[i].load(std::memory_order_relaxed);
        set_bits += static_cast<size_t>(__popcnt64(w));
    }
    return set_bits;
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
// POD-Only Safe Memory Helpers (Isolated from C++ destructors to avoid C2712)
// ----------------------------------------------------------------------------
static bool SafeReadU32(uintptr_t addr, uint32_t* out_val) {
    if (!addr || !out_val) return false;
    __try {
        *out_val = *reinterpret_cast<const volatile uint32_t*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeCopyMemoryBlock(const void* src, void* dst, size_t len) {
    if (!src || !dst || len == 0) return false;
    __try {
        const volatile uint8_t* s = static_cast<const volatile uint8_t*>(src);
        uint8_t probe_first = s[0];
        uint8_t probe_last  = s[len - 1];
        (void)probe_first;
        (void)probe_last;
        std::memcpy(dst, const_cast<const uint8_t*>(s), len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static const uint8_t* SafeReadGsVramPtr(const void* gs_vram_ctx) {
    if (!gs_vram_ctx) return nullptr;
    __try {
        const uint8_t* vram_base = *reinterpret_cast<const uint8_t* const*>(gs_vram_ctx);
        if (!vram_base) return nullptr;
        volatile uint8_t p0 = vram_base[0];
        volatile uint8_t p1 = vram_base[PS2_GS_VRAM_SIZE - 1];
        (void)p0;
        (void)p1;
        return vram_base;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Fast 2-qword content tag for lock-free GIM descriptor verification (~2ns)
static bool SafeReadGimContentTag(const uint8_t* gim_data, uint64_t* out_tag) {
    if (!gim_data || !out_tag) return false;
    __try {
        if (gim_data[0] != 'G' || gim_data[1] != 'I' || gim_data[2] != 'M' || gim_data[3] != 0) {
            return false;
        }
        uint8_t psm = gim_data[0x13];
        if (psm != 0x13 && psm != 0x1B && psm != 0x14 && psm != 0x24) {
            return false;
        }
        uint64_t dims_q = *reinterpret_cast<const uint64_t*>(gim_data + 0x18);
        uint64_t pix0_q = *reinterpret_cast<const uint64_t*>(gim_data + 0x20);
        uint64_t pix1_q = *reinterpret_cast<const uint64_t*>(gim_data + 0x28);
        *out_tag = (dims_q * FNV1A_64_PRIME) ^ pix0_q ^ (pix1_q << 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeRestoreFontOrigHeights(
    uint8_t* start_ptr,
    const uint16_t* char_codes,
    const uint16_t* adv_xs,
    const uint16_t* orig_heights,
    size_t count
) {
    if (!start_ptr || !char_codes || !adv_xs || !orig_heights || count == 0) return false;
    __try {
        uint16_t num_gl_cur = *reinterpret_cast<const uint16_t*>(start_ptr + 0x04);
        if (num_gl_cur != count) return false;
        for (size_t g = 0; g < count; ++g) {
            uint16_t cur_adv = *reinterpret_cast<const uint16_t*>(start_ptr + 16u + g * 16u + 0u);
            uint16_t cur_cp  = *reinterpret_cast<const uint16_t*>(start_ptr + 16u + g * 16u + 8u);
            if (cur_cp != char_codes[g] || cur_adv != adv_xs[g]) {
                return false;
            }
        }
        for (size_t g = 0; g < count; ++g) {
            *reinterpret_cast<uint16_t*>(start_ptr + 16u + g * 16u + 2u) = orig_heights[g];
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeApplyFontExpandedHeights(
    uint8_t* start_ptr,
    const uint16_t* expanded_heights,
    size_t count
) {
    if (!start_ptr || !expanded_heights || count == 0) return;
    __try {
        uint16_t num_gl_cur = *reinterpret_cast<const uint16_t*>(start_ptr + 0x04);
        if (num_gl_cur != count) return;
        for (size_t g = 0; g < count; ++g) {
            uint16_t* h_ptr = reinterpret_cast<uint16_t*>(start_ptr + 16u + g * 16u + 2u);
            if (*h_ptr < expanded_heights[g]) {
                *h_ptr = expanded_heights[g];
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ----------------------------------------------------------------------------
// CPU-Only WIC PNG Decoder (Decodes PNG on disk -> 32-bit RGBA in RAM)
// ----------------------------------------------------------------------------
static bool DecodePngFileViaWic(
    const std::string& png_path,
    std::vector<uint8_t>& out_rgba,
    uint32_t* out_w,
    uint32_t* out_h
) {
    if (!out_w || !out_h) return false;
    *out_w = 0;
    *out_h = 0;
    out_rgba.clear();

    const HRESULT hr_co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr_co) && hr_co != RPC_E_CHANGED_MODE) {
        // WIC creation below will fail gracefully if COM is unavailable
    }

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr) || !factory) {
        return false;
    }

    std::wstring wpath = fs::path(png_path).wstring();
    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(
        wpath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder
    );
    if (FAILED(hr) || !decoder) {
        factory->Release();
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        factory->Release();
        return false;
    }

    UINT w = 0, h = 0;
    hr = frame->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0 || w > 16384 || h > 16384) {
        frame->Release();
        decoder->Release();
        factory->Release();
        return false;
    }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        factory->Release();
        return false;
    }

    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return false;
    }

    const size_t stride     = static_cast<size_t>(w) * 4u;
    const size_t total_size = stride * static_cast<size_t>(h);
    out_rgba.resize(total_size);

    hr = converter->CopyPixels(
        nullptr,
        static_cast<UINT>(stride),
        static_cast<UINT>(total_size),
        out_rgba.data()
    );
    converter->Release();

    if (FAILED(hr)) {
        frame->Release();
        decoder->Release();
        factory->Release();
        out_rgba.clear();
        return false;
    }

    // Automatic ICC Color Profile -> sRGB Conversion (fixes desaturated colors when PNGs are saved
    // with Adobe RGB (1998) or Display P3 color profiles in Photoshop, while preserving exact 8-bit Alpha):
    UINT icc_count = 0;
    if (SUCCEEDED(frame->GetColorContexts(0, nullptr, &icc_count)) && icc_count > 0) {
        IWICColorContext* src_ctx = nullptr;
        if (SUCCEEDED(factory->CreateColorContext(&src_ctx)) && src_ctx) {
            UINT fetched = 0;
            if (SUCCEEDED(frame->GetColorContexts(1, &src_ctx, &fetched)) && fetched > 0) {
                bool converted_via_wic = false;
                IWICColorContext* dst_ctx = nullptr;
                if (SUCCEEDED(factory->CreateColorContext(&dst_ctx)) && dst_ctx) {
                    if (SUCCEEDED(dst_ctx->InitializeFromExifColorSpace(1))) { // 1 = Standard sRGB
                        IWICColorTransform* color_xform = nullptr;
                        if (SUCCEEDED(factory->CreateColorTransformer(&color_xform)) && color_xform) {
                            if (SUCCEEDED(color_xform->Initialize(frame, src_ctx, dst_ctx, GUID_WICPixelFormat32bppBGRA))) {
                                std::vector<uint8_t> bgra_srgb(total_size);
                                if (SUCCEEDED(color_xform->CopyPixels(
                                        nullptr, static_cast<UINT>(stride),
                                        static_cast<UINT>(total_size), bgra_srgb.data()))) {
                                    const size_t px_count = static_cast<size_t>(w) * static_cast<size_t>(h);
                                    for (size_t i = 0; i < px_count; ++i) {
                                        out_rgba[i * 4u + 0] = bgra_srgb[i * 4u + 2]; // R (sRGB)
                                        out_rgba[i * 4u + 1] = bgra_srgb[i * 4u + 1]; // G (sRGB)
                                        out_rgba[i * 4u + 2] = bgra_srgb[i * 4u + 0]; // B (sRGB)
                                    }
                                    converted_via_wic = true;
                                }
                            } else {
                                const size_t stride24 = (static_cast<size_t>(w) * 3u + 3u) & ~size_t(3u);
                                const size_t size24   = stride24 * static_cast<size_t>(h);
                                if (SUCCEEDED(color_xform->Initialize(frame, src_ctx, dst_ctx, GUID_WICPixelFormat24bppBGR))) {
                                    std::vector<uint8_t> bgr_srgb(size24);
                                    if (SUCCEEDED(color_xform->CopyPixels(
                                            nullptr, static_cast<UINT>(stride24),
                                            static_cast<UINT>(size24), bgr_srgb.data()))) {
                                        for (UINT y = 0; y < h; ++y) {
                                            const uint8_t* src_row = bgr_srgb.data() + static_cast<size_t>(y) * stride24;
                                            uint8_t* dst_row = out_rgba.data() + static_cast<size_t>(y) * stride;
                                            for (UINT x = 0; x < w; ++x) {
                                                dst_row[x * 4u + 0] = src_row[x * 3u + 2]; // R (sRGB)
                                                dst_row[x * 4u + 1] = src_row[x * 3u + 1]; // G (sRGB)
                                                dst_row[x * 4u + 2] = src_row[x * 3u + 0]; // B (sRGB)
                                            }
                                        }
                                        converted_via_wic = true;
                                    }
                                }
                            }
                            color_xform->Release();
                        }
                    }
                    dst_ctx->Release();
                }

                // Pure-math fallback for Adobe RGB (1998) if WIC ColorTransform is unavailable (e.g. Wine/Proton):
                if (!converted_via_wic) {
                    UINT prof_len = 0;
                    if (SUCCEEDED(src_ctx->GetProfileBytes(0, nullptr, &prof_len)) && prof_len > 0 && prof_len < 65536) {
                        std::vector<uint8_t> prof_bytes(prof_len);
                        if (SUCCEEDED(src_ctx->GetProfileBytes(prof_len, prof_bytes.data(), &prof_len))) {
                            std::string prof_str(reinterpret_cast<const char*>(prof_bytes.data()), prof_len);
                            if (prof_str.find("Adobe RGB") != std::string::npos) {
                                float lin_lut[256];
                                for (int v = 0; v < 256; ++v) {
                                    lin_lut[v] = std::pow(static_cast<float>(v) * (1.0f / 255.0f), 2.19921875f);
                                }
                                auto lin_to_srgb_u8 = [](float c) -> uint8_t {
                                    c = std::clamp(c, 0.0f, 1.0f);
                                    float s = (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
                                    return static_cast<uint8_t>(std::clamp(static_cast<int>(s * 255.0f + 0.5f), 0, 255));
                                };
                                const size_t px_count = static_cast<size_t>(w) * static_cast<size_t>(h);
                                for (size_t i = 0; i < px_count; ++i) {
                                    float lr = lin_lut[out_rgba[i * 4u + 0]];
                                    float lg = lin_lut[out_rgba[i * 4u + 1]];
                                    float lb = lin_lut[out_rgba[i * 4u + 2]];
                                    float sr =  1.3982832f * lr - 0.3982831f * lg;
                                    float sg =                    1.0000000f * lg;
                                    float sb =                  - 0.0429383f * lg + 1.0429383f * lb;
                                    out_rgba[i * 4u + 0] = lin_to_srgb_u8(sr);
                                    out_rgba[i * 4u + 1] = lin_to_srgb_u8(sg);
                                    out_rgba[i * 4u + 2] = lin_to_srgb_u8(sb);
                                }
                            }
                        }
                    }
                }
            }
            src_ctx->Release();
        }
    }

    frame->Release();
    decoder->Release();
    factory->Release();

    *out_w = static_cast<uint32_t>(w);
    *out_h = static_cast<uint32_t>(h);
    return true;
}

// ----------------------------------------------------------------------------
// Minimal Self-Contained RGBA PNG Encoder (Uncompressed Stored-Block Zlib)
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

static void EncodeRgbaToPngBytes(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& out_png) {
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

// ============================================================================
// Autonomous DDS Texture Cache & Direct D3D12 Loader (BC7 Mode 6 & BGRA8)
// Automatically compiles .png files in mods\textures\ into mods\textures\.dds_cache\
// on first launch (or whenever modified in Photoshop) and uploads directly to D3D12
// without runtime PNG decoding, saving 4x VRAM on large textures via BC7 Mode 6.
// ============================================================================
#pragma pack(push, 1)
struct DdsPixelFormat {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};

struct DdsFileHeaderDx10 {
    uint32_t       dwMagic;              // 0x20534444 ('DDS ')
    uint32_t       dwSize;               // 124
    uint32_t       dwFlags;              // 0x000A1007
    uint32_t       dwHeight;
    uint32_t       dwWidth;
    uint32_t       dwPitchOrLinearSize;
    uint32_t       dwDepth;              // 1
    uint32_t       dwMipMapCount;        // 1
    uint32_t       dwReserved1[11];      // [0..1]=src_mtime, [2]=src_fsize, [3]='ACZ2', [4]=orig_w, [5]=orig_h, [6]=comp_mode
    DdsPixelFormat ddspf;                // dwFourCC = 'DX10' (0x30315844)
    uint32_t       dwCaps;               // 0x1000
    uint32_t       dwCaps2;
    uint32_t       dwCaps3;
    uint32_t       dwCaps4;
    uint32_t       dwReserved2;
    // DX10 extension (20 bytes):
    uint32_t       dxgiFormat;           // 98 = DXGI_FORMAT_BC7_UNORM, 87 = DXGI_FORMAT_B8G8R8A8_UNORM
    uint32_t       resourceDimension;    // 3 = D3D12_RESOURCE_DIMENSION_TEXTURE2D
    uint32_t       miscFlag;             // 0
    uint32_t       arraySize;            // 1
    uint32_t       miscFlags2;           // 0
};
#pragma pack(pop)

static constexpr uint32_t DDS_MAGIC_WORD       = 0x20534444u; // "DDS "
static constexpr uint32_t DDS_FOURCC_DX10      = 0x30315844u; // "DX10"
static constexpr uint32_t DDS_CACHE_TAG_MAGIC  = 0x41435A33u; // "ACZ3" (v0.23.2 ICC->sRGB color-managed)
static constexpr uint32_t DXGI_FMT_BGRA8_UNORM = 87u;         // DXGI_FORMAT_B8G8R8A8_UNORM
static constexpr uint32_t DXGI_FMT_BC7_UNORM   = 98u;         // DXGI_FORMAT_BC7_UNORM

// High-precision BC7 Mode 6 block encoder (full 7-bit RGB + 7-bit A + 1-bit P0/P1 = 8-bit RGBA endpoints,
// 16-level 4-bit interpolation across all 16 pixels, with transparent-pixel RGB bleed normalization).
static void EncodeBlockBc7Mode6(const uint8_t px_in[16][4], uint8_t out_block[16]) {
    uint8_t px[16][4];
    uint32_t opaque_r = 0, opaque_g = 0, opaque_b = 0, opaque_cnt = 0;
    for (int i = 0; i < 16; ++i) {
        px[i][0] = px_in[i][0];
        px[i][1] = px_in[i][1];
        px[i][2] = px_in[i][2];
        px[i][3] = px_in[i][3];
        if (px[i][3] > 0) {
            opaque_r += px[i][0];
            opaque_g += px[i][1];
            opaque_b += px[i][2];
            ++opaque_cnt;
        }
    }
    // If the block has both transparent (A=0) and visible (A>0) pixels, fill A=0 pixels with the mean
    // visible RGB of the block so straight-alpha (0,0,0,0) background pixels never pull anti-aliased edges dark!
    if (opaque_cnt > 0 && opaque_cnt < 16) {
        uint8_t avg_r = static_cast<uint8_t>((opaque_r + (opaque_cnt >> 1)) / opaque_cnt);
        uint8_t avg_g = static_cast<uint8_t>((opaque_g + (opaque_cnt >> 1)) / opaque_cnt);
        uint8_t avg_b = static_cast<uint8_t>((opaque_b + (opaque_cnt >> 1)) / opaque_cnt);
        for (int i = 0; i < 16; ++i) {
            if (px[i][3] == 0) {
                px[i][0] = avg_r;
                px[i][1] = avg_g;
                px[i][2] = avg_b;
            }
        }
    }

    uint8_t min_c[4] = { 255, 255, 255, 255 };
    uint8_t max_c[4] = { 0, 0, 0, 0 };
    int32_t sum_c[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 4; ++c) {
            uint8_t v = px[i][c];
            if (v < min_c[c]) min_c[c] = v;
            if (v > max_c[c]) max_c[c] = v;
            sum_c[c] += v;
        }
    }

    // Fast path for constant-color 4x4 block:
    if (min_c[0] == max_c[0] && min_c[1] == max_c[1] && min_c[2] == max_c[2] && min_c[3] == max_c[3]) {
        uint8_t p0 = static_cast<uint8_t>(((min_c[0] & 1) + (min_c[1] & 1) + (min_c[2] & 1) + (min_c[3] & 1)) >= 2 ? 1 : 0);
        uint8_t p1 = p0;
        uint64_t c0[4], c1[4];
        for (int c = 0; c < 4; ++c) {
            c0[c] = static_cast<uint64_t>(min_c[c] >> 1);
            c1[c] = c0[c];
        }
        uint64_t lo = 0x40ULL |
                      (c0[0] << 7)  | (c1[0] << 14) |
                      (c0[1] << 21) | (c1[1] << 28) |
                      (c0[2] << 35) | (c1[2] << 42) |
                      (c0[3] << 49) | (c1[3] << 56) |
                      (static_cast<uint64_t>(p0) << 63);
        uint64_t hi = static_cast<uint64_t>(p1); // all 16 indices = 0
        *reinterpret_cast<uint64_t*>(out_block + 0) = lo;
        *reinterpret_cast<uint64_t*>(out_block + 8) = hi;
        return;
    }

    float mean[4] = {
        sum_c[0] * (1.0f / 16.0f),
        sum_c[1] * (1.0f / 16.0f),
        sum_c[2] * (1.0f / 16.0f),
        sum_c[3] * (1.0f / 16.0f)
    };

    float cov[4][4]{};
    for (int i = 0; i < 16; ++i) {
        float d0 = static_cast<float>(px[i][0]) - mean[0];
        float d1 = static_cast<float>(px[i][1]) - mean[1];
        float d2 = static_cast<float>(px[i][2]) - mean[2];
        float d3 = static_cast<float>(px[i][3]) - mean[3];
        cov[0][0] += d0 * d0; cov[0][1] += d0 * d1; cov[0][2] += d0 * d2; cov[0][3] += d0 * d3;
        cov[1][1] += d1 * d1; cov[1][2] += d1 * d2; cov[1][3] += d1 * d3;
        cov[2][2] += d2 * d2; cov[2][3] += d2 * d3;
        cov[3][3] += d3 * d3;
    }
    cov[1][0] = cov[0][1];
    cov[2][0] = cov[0][2]; cov[2][1] = cov[1][2];
    cov[3][0] = cov[0][3]; cov[3][1] = cov[1][3]; cov[3][2] = cov[2][3];

    float axis[4] = {
        static_cast<float>(max_c[0] - min_c[0]),
        static_cast<float>(max_c[1] - min_c[1]),
        static_cast<float>(max_c[2] - min_c[2]),
        static_cast<float>(max_c[3] - min_c[3])
    };
    for (int iter = 0; iter < 4; ++iter) {
        float nxt[4] = {
            cov[0][0] * axis[0] + cov[0][1] * axis[1] + cov[0][2] * axis[2] + cov[0][3] * axis[3],
            cov[1][0] * axis[0] + cov[1][1] * axis[1] + cov[1][2] * axis[2] + cov[1][3] * axis[3],
            cov[2][0] * axis[0] + cov[2][1] * axis[1] + cov[2][2] * axis[2] + cov[2][3] * axis[3],
            cov[3][0] * axis[0] + cov[3][1] * axis[1] + cov[3][2] * axis[2] + cov[3][3] * axis[3]
        };
        float norm2 = nxt[0] * nxt[0] + nxt[1] * nxt[1] + nxt[2] * nxt[2] + nxt[3] * nxt[3];
        if (norm2 > 1e-6f) {
            float inv = 1.0f / std::sqrt(norm2);
            axis[0] = nxt[0] * inv;
            axis[1] = nxt[1] * inv;
            axis[2] = nxt[2] * inv;
            axis[3] = nxt[3] * inv;
        }
    }

    float t_min = 1e30f;
    float t_max = -1e30f;
    for (int i = 0; i < 16; ++i) {
        float t = (static_cast<float>(px[i][0]) - mean[0]) * axis[0] +
                  (static_cast<float>(px[i][1]) - mean[1]) * axis[1] +
                  (static_cast<float>(px[i][2]) - mean[2]) * axis[2] +
                  (static_cast<float>(px[i][3]) - mean[3]) * axis[3];
        if (t < t_min) t_min = t;
        if (t > t_max) t_max = t;
    }

    int32_t e0_i[4], e1_i[4];
    if (t_max - t_min < 1e-4f) {
        for (int c = 0; c < 4; ++c) {
            e0_i[c] = min_c[c];
            e1_i[c] = max_c[c];
        }
    } else {
        for (int c = 0; c < 4; ++c) {
            float v0 = mean[c] + axis[c] * t_min;
            float v1 = mean[c] + axis[c] * t_max;
            e0_i[c] = std::clamp(static_cast<int32_t>(v0 + 0.5f), 0, 255);
            e1_i[c] = std::clamp(static_cast<int32_t>(v1 + 0.5f), 0, 255);
        }
    }

    uint32_t p0 = (((e0_i[0] & 1) + (e0_i[1] & 1) + (e0_i[2] & 1) + (e0_i[3] & 1)) >= 2) ? 1u : 0u;
    uint32_t p1 = (((e1_i[0] & 1) + (e1_i[1] & 1) + (e1_i[2] & 1) + (e1_i[3] & 1)) >= 2) ? 1u : 0u;

    uint64_t c0[4], c1[4];
    int32_t  v0[4], v1[4], dv[4];
    int32_t  dlen2 = 0;
    for (int c = 0; c < 4; ++c) {
        int32_t q0 = std::clamp((e0_i[c] - static_cast<int32_t>(p0) + 1) >> 1, 0, 127);
        int32_t q1 = std::clamp((e1_i[c] - static_cast<int32_t>(p1) + 1) >> 1, 0, 127);
        c0[c] = static_cast<uint64_t>(q0);
        c1[c] = static_cast<uint64_t>(q1);
        v0[c] = (q0 << 1) | static_cast<int32_t>(p0);
        v1[c] = (q1 << 1) | static_cast<int32_t>(p1);
        dv[c] = v1[c] - v0[c];
        dlen2 += dv[c] * dv[c];
    }

    uint8_t indices[16]{};
    if (dlen2 > 0) {
        for (int i = 0; i < 16; ++i) {
            int32_t dot = (static_cast<int32_t>(px[i][0]) - v0[0]) * dv[0] +
                          (static_cast<int32_t>(px[i][1]) - v0[1]) * dv[1] +
                          (static_cast<int32_t>(px[i][2]) - v0[2]) * dv[2] +
                          (static_cast<int32_t>(px[i][3]) - v0[3]) * dv[3];
            int32_t idx = (dot * 15 + (dlen2 >> 1)) / dlen2;
            indices[i] = static_cast<uint8_t>(std::clamp(idx, 0, 15));
        }
    }

    // BC7 Mode 6 anchor index 0 constraint: MSB of index[0] must be 0 (< 8)
    if (indices[0] >= 8u) {
        for (int c = 0; c < 4; ++c) {
            std::swap(c0[c], c1[c]);
        }
        std::swap(p0, p1);
        for (int i = 0; i < 16; ++i) {
            indices[i] = static_cast<uint8_t>(15u - indices[i]);
        }
    }

    uint64_t lo = 0x40ULL |
                  (c0[0] << 7)  | (c1[0] << 14) |
                  (c0[1] << 21) | (c1[1] << 28) |
                  (c0[2] << 35) | (c1[2] << 42) |
                  (c0[3] << 49) | (c1[3] << 56) |
                  (static_cast<uint64_t>(p0) << 63);
    uint64_t hi = static_cast<uint64_t>(p1) |
                  (static_cast<uint64_t>(indices[0] & 0x07u) << 1);
    for (int k = 1; k < 16; ++k) {
        hi |= (static_cast<uint64_t>(indices[k] & 0x0Fu) << (4 * k));
    }

    *reinterpret_cast<uint64_t*>(out_block + 0) = lo;
    *reinterpret_cast<uint64_t*>(out_block + 8) = hi;
}

static fs::path GetDdsCachePathForPng(const std::string& png_path) {
    std::string norm = png_path;
    for (char& c : norm) {
        if (c == '/') c = '\\';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    uint64_t h = FNV1A_64_OFFSET;
    for (unsigned char ch : norm) {
        h ^= static_cast<uint64_t>(ch);
        h *= FNV1A_64_PRIME;
    }
    h *= FNV1A_64_PRIME;

    fs::path p(png_path);
    std::string stem = p.stem().string();
    if (stem.size() > 80) {
        stem.resize(80);
    }
    char name_buf[160];
    snprintf(name_buf, sizeof(name_buf), "%s_%016llX.dds", stem.c_str(), static_cast<unsigned long long>(h));
    return g_DdsCacheDir / name_buf;
}

static bool IsDdsCacheFileValid(
    const fs::path& dds_path,
    uint64_t src_mtime,
    uint32_t src_fsize,
    DdsFileHeaderDx10* out_hdr = nullptr
) {
    FILE* df = _fsopen(dds_path.string().c_str(), "rb", _SH_DENYNO);
    if (!df) return false;
    DdsFileHeaderDx10 hdr{};
    size_t r = fread(&hdr, 1, sizeof(hdr), df);
    fclose(df);
    if (r != sizeof(hdr)) return false;

    uint64_t cached_mtime = static_cast<uint64_t>(hdr.dwReserved1[0]) |
                            (static_cast<uint64_t>(hdr.dwReserved1[1]) << 32);
    if (hdr.dwMagic != DDS_MAGIC_WORD ||
        hdr.dwSize != 124u ||
        hdr.ddspf.dwFourCC != DDS_FOURCC_DX10 ||
        hdr.dwReserved1[3] != DDS_CACHE_TAG_MAGIC ||
        cached_mtime != src_mtime ||
        hdr.dwReserved1[2] != src_fsize ||
        hdr.dwReserved1[4] != 0u ||
        hdr.dwReserved1[5] != 0u ||
        hdr.dwReserved1[6] != static_cast<uint32_t>(g_DdsCacheCompression)) {
        return false;
    }
    if (out_hdr) {
        *out_hdr = hdr;
    }
    return true;
}

// Compiles a single PNG file into an optimized .dds cache file (BC7 Mode 6 for large textures, BGRA8 for small icons)
static bool BuildOrUpdateDdsCacheForPng(
    const std::string& png_path,
    bool is_hud_000_or_001,
    fs::path* out_dds_path,
    bool* out_was_rebuilt = nullptr
) {
    if (out_was_rebuilt) *out_was_rebuilt = false;
    if (!g_EnableDdsCache || png_path.empty()) return false;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(png_path.c_str(), GetFileExInfoStandard, &fad)) {
        return false;
    }
    uint64_t src_mtime = (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                         static_cast<uint64_t>(fad.ftLastWriteTime.dwLowDateTime);
    uint32_t src_fsize = fad.nFileSizeLow;

    fs::path dds_path = GetDdsCachePathForPng(png_path);
    if (out_dds_path) *out_dds_path = dds_path;

    if (IsDdsCacheFileValid(dds_path, src_mtime, src_fsize)) {
        return true;
    }

    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (!DecodePngFileViaWic(png_path, rgba, &w, &h) || w == 0 || h == 0) {
        return false;
    }

    // 1. Apply RGB=(105,105,105) transparent pixel normalization for built-in HUD font atlases (000.png / 001.png)
    if (is_hud_000_or_001) {
        const size_t total_px = static_cast<size_t>(w) * h;
        for (size_t i = 0; i < total_px; ++i) {
            if (rgba[i * 4u + 3] == 0) {
                rgba[i * 4u + 0] = 105u;
                rgba[i * 4u + 1] = 105u;
                rgba[i * 4u + 2] = 105u;
            }
        }
    }

    // 2. Choose BC7 Mode 6 (4:1 compression) for textures >= 256x256 with multiple-of-4 dimensions, or lossless BGRA8
    bool use_bc7 = (g_DdsCacheCompression != 0) &&
                   !is_hud_000_or_001 &&
                   w >= 256u && h >= 256u &&
                   (w & 3u) == 0u && (h & 3u) == 0u;

    DdsFileHeaderDx10 hdr{};
    hdr.dwMagic             = DDS_MAGIC_WORD;
    hdr.dwSize              = 124u;
    hdr.dwFlags             = 0x00001007u | (use_bc7 ? 0x00080000u : 0x00000008u);
    hdr.dwHeight            = h;
    hdr.dwWidth             = w;
    hdr.dwPitchOrLinearSize = use_bc7 ? ((w / 4u) * (h / 4u) * 16u) : (w * 4u);
    hdr.dwDepth             = 1u;
    hdr.dwMipMapCount       = 1u;
    hdr.dwReserved1[0]      = static_cast<uint32_t>(src_mtime & 0xFFFFFFFFULL);
    hdr.dwReserved1[1]      = static_cast<uint32_t>(src_mtime >> 32);
    hdr.dwReserved1[2]      = src_fsize;
    hdr.dwReserved1[3]      = DDS_CACHE_TAG_MAGIC;
    hdr.dwReserved1[4]      = 0u;
    hdr.dwReserved1[5]      = 0u;
    hdr.dwReserved1[6]      = static_cast<uint32_t>(g_DdsCacheCompression);
    hdr.ddspf.dwSize        = 32u;
    hdr.ddspf.dwFlags       = 0x00000004u; // DDPF_FOURCC
    hdr.ddspf.dwFourCC      = DDS_FOURCC_DX10;
    hdr.dwCaps              = 0x00001000u; // DDSCAPS_TEXTURE
    hdr.dxgiFormat          = use_bc7 ? DXGI_FMT_BC7_UNORM : DXGI_FMT_BGRA8_UNORM;
    hdr.resourceDimension   = 3u;          // D3D12_RESOURCE_DIMENSION_TEXTURE2D
    hdr.miscFlag            = 0u;
    hdr.arraySize           = 1u;
    hdr.miscFlags2          = 0u;

    std::vector<uint8_t> payload;
    if (use_bc7) {
        const uint32_t bx_cnt = w / 4u;
        const uint32_t by_cnt = h / 4u;
        payload.resize(static_cast<size_t>(bx_cnt) * by_cnt * 16u);
        for (uint32_t by = 0; by < by_cnt; ++by) {
            uint8_t* dst_row = payload.data() + static_cast<size_t>(by) * bx_cnt * 16u;
            for (uint32_t bx = 0; bx < bx_cnt; ++bx) {
                uint8_t blk_px[16][4];
                for (uint32_t py = 0; py < 4u; ++py) {
                    const uint8_t* src_p = rgba.data() + (static_cast<size_t>(by * 4u + py) * w + bx * 4u) * 4u;
                    for (uint32_t px_i = 0; px_i < 4u; ++px_i) {
                        uint32_t k = py * 4u + px_i;
                        blk_px[k][0] = src_p[px_i * 4u + 0];
                        blk_px[k][1] = src_p[px_i * 4u + 1];
                        blk_px[k][2] = src_p[px_i * 4u + 2];
                        blk_px[k][3] = src_p[px_i * 4u + 3];
                    }
                }
                EncodeBlockBc7Mode6(blk_px, dst_row + static_cast<size_t>(bx) * 16u);
            }
        }
    } else {
        const size_t total_px = static_cast<size_t>(w) * h;
        payload.resize(total_px * 4u);
        for (size_t i = 0; i < total_px; ++i) {
            payload[i * 4u + 0] = rgba[i * 4u + 2]; // B
            payload[i * 4u + 1] = rgba[i * 4u + 1]; // G
            payload[i * 4u + 2] = rgba[i * 4u + 0]; // R
            payload[i * 4u + 3] = rgba[i * 4u + 3]; // A
        }
    }

    std::error_code ec;
    fs::create_directories(g_DdsCacheDir, ec);

    char tmp_suffix[64];
    snprintf(tmp_suffix, sizeof(tmp_suffix), ".tmp_%lu", static_cast<unsigned long>(GetCurrentThreadId()));
    std::string tmp_path = dds_path.string() + tmp_suffix;

    FILE* wf = _fsopen(tmp_path.c_str(), "wb", _SH_DENYNO);
    if (!wf) return false;
    bool write_ok = (fwrite(&hdr, 1, sizeof(hdr), wf) == sizeof(hdr)) &&
                    (fwrite(payload.data(), 1, payload.size(), wf) == payload.size());
    fclose(wf);

    if (!write_ok) {
        DeleteFileA(tmp_path.c_str());
        return false;
    }
    if (!MoveFileExA(tmp_path.c_str(), dds_path.string().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp_path.c_str());
        return false;
    }
    if (out_was_rebuilt) *out_was_rebuilt = true;
    return true;
}

// Loads a cached .dds texture directly into acz.exe's D3D12 placed-resource pool & upload queue
static uint8_t* LoadTextureFromDdsCacheFile(
    const fs::path& dds_path,
    uint64_t expected_src_mtime,
    uint32_t expected_src_fsize
) {
    if (!g_AllocatePooledTexture || !g_UploadTextureToGpu || !g_EngineMalloc) {
        return nullptr;
    }

    FILE* df = _fsopen(dds_path.string().c_str(), "rb", _SH_DENYNO);
    if (!df) return nullptr;

    DdsFileHeaderDx10 hdr{};
    if (fread(&hdr, 1, sizeof(hdr), df) != sizeof(hdr)) {
        fclose(df);
        return nullptr;
    }

    uint64_t cached_mtime = static_cast<uint64_t>(hdr.dwReserved1[0]) |
                            (static_cast<uint64_t>(hdr.dwReserved1[1]) << 32);
    if (hdr.dwMagic != DDS_MAGIC_WORD ||
        hdr.dwSize != 124u ||
        hdr.ddspf.dwFourCC != DDS_FOURCC_DX10 ||
        hdr.dwReserved1[3] != DDS_CACHE_TAG_MAGIC ||
        cached_mtime != expected_src_mtime ||
        hdr.dwReserved1[2] != expected_src_fsize ||
        hdr.dwReserved1[4] != 0u ||
        hdr.dwReserved1[5] != 0u ||
        hdr.dwWidth == 0 || hdr.dwHeight == 0) {
        fclose(df);
        return nullptr;
    }

    const uint32_t w   = hdr.dwWidth;
    const uint32_t h   = hdr.dwHeight;
    const uint32_t fmt = hdr.dxgiFormat;
    if (fmt != DXGI_FMT_BC7_UNORM && fmt != DXGI_FMT_BGRA8_UNORM) {
        fclose(df);
        return nullptr;
    }

    const uint32_t num_rows      = (fmt == DXGI_FMT_BC7_UNORM) ? (h / 4u) : h;
    const uint32_t src_row_pitch = (fmt == DXGI_FMT_BC7_UNORM) ? ((w / 4u) * 16u) : (w * 4u);
    if (num_rows == 0 || src_row_pitch == 0) {
        fclose(df);
        return nullptr;
    }

    uint8_t* hd_tex = g_AllocatePooledTexture(w, h, 1u, fmt, nullptr);
    if (!hd_tex) {
        fclose(df);
        return nullptr;
    }

    uint8_t* slot_hdr = *reinterpret_cast<uint8_t**>(hd_tex + 0x00);
    if (!slot_hdr) {
        fclose(df);
        return nullptr;
    }

    uint32_t d3d_total_bytes = *reinterpret_cast<uint32_t*>(slot_hdr + 0x08);
    uint32_t d3d_row_pitch   = *reinterpret_cast<uint32_t*>(hd_tex + 0x1C);
    if (d3d_total_bytes == 0 || d3d_row_pitch < src_row_pitch) {
        fclose(df);
        if (g_DestroyPooledTexture) g_DestroyPooledTexture(hd_tex);
        return nullptr;
    }

    uint8_t* cpu_buf = *reinterpret_cast<uint8_t**>(hd_tex + 0x08);
    if (!cpu_buf) {
        cpu_buf = static_cast<uint8_t*>(g_EngineMalloc(d3d_total_bytes));
        *reinterpret_cast<uint8_t**>(hd_tex + 0x08) = cpu_buf;
    }
    if (!cpu_buf) {
        fclose(df);
        if (g_DestroyPooledTexture) g_DestroyPooledTexture(hd_tex);
        return nullptr;
    }

    bool read_ok = false;
    if (src_row_pitch == d3d_row_pitch) {
        size_t total_src = static_cast<size_t>(src_row_pitch) * num_rows;
        if (total_src <= d3d_total_bytes) {
            read_ok = (fread(cpu_buf, 1, total_src, df) == total_src);
        }
    } else {
        read_ok = true;
        for (uint32_t r = 0; r < num_rows; ++r) {
            size_t dst_off = static_cast<size_t>(r) * d3d_row_pitch;
            if (dst_off + src_row_pitch > d3d_total_bytes ||
                fread(cpu_buf + dst_off, 1, src_row_pitch, df) != src_row_pitch) {
                read_ok = false;
                break;
            }
        }
    }
    fclose(df);

    if (!read_ok) {
        if (g_DestroyPooledTexture) g_DestroyPooledTexture(hd_tex);
        return nullptr;
    }

    g_UploadTextureToGpu(hd_tex);
    return hd_tex;
}

static inline uint8_t Ps2AlphaToPc(uint8_t a) {
    uint32_t v = (static_cast<uint32_t>(a) * 255u + 64u) / 128u;
    return static_cast<uint8_t>(v > 255u ? 255u : v);
}

// Decode a GIM buffer in memory to RGBA pixels for PNG preview dumping
static bool DecodeGimToRgba(const uint8_t* gim_data, const GimInspectionResult& res, std::vector<uint8_t>& out_rgba) {
    if (!gim_data || !res.valid) return false;
    const uint32_t w = res.width;
    const uint32_t h = res.height;
    const uint8_t* pal_src = gim_data + res.clut_offset + 0x10;

    uint8_t pal[256][4] = {};
    uint32_t num_colors = (res.psm == 0x13 || res.psm == 0x1B) ? 256u : 16u;
    for (uint32_t c = 0; c < num_colors; ++c) {
        pal[c][0] = pal_src[c * 4 + 0];
        pal[c][1] = pal_src[c * 4 + 1];
        pal[c][2] = pal_src[c * 4 + 2];
        pal[c][3] = Ps2AlphaToPc(pal_src[c * 4 + 3]);
    }

    // CSM1 palette unswizzle for 8-bit PSMT8
    if (num_colors == 256u) {
        for (uint32_t base = 0; base < 256u; base += 32u) {
            for (uint32_t k = 0; k < 8u; ++k) {
                for (int ch = 0; ch < 4; ++ch) {
                    uint8_t tmp = pal[base + 8u + k][ch];
                    pal[base + 8u + k][ch] = pal[base + 16u + k][ch];
                    pal[base + 16u + k][ch] = tmp;
                }
            }
        }
    }

    out_rgba.resize(w * h * 4u);
    const uint8_t* pix = gim_data + 0x20;
    if (num_colors == 256u) {
        for (uint32_t i = 0; i < w * h; ++i) {
            uint8_t idx = pix[i];
            out_rgba[i * 4 + 0] = pal[idx][0];
            out_rgba[i * 4 + 1] = pal[idx][1];
            out_rgba[i * 4 + 2] = pal[idx][2];
            out_rgba[i * 4 + 3] = pal[idx][3];
        }
    } else {
        for (uint32_t i = 0; i < w * h; ++i) {
            uint8_t b = pix[i >> 1];
            uint8_t idx = (i & 1u) ? ((b >> 4) & 0x0Fu) : (b & 0x0Fu);
            out_rgba[i * 4 + 0] = pal[idx][0];
            out_rgba[i * 4 + 1] = pal[idx][1];
            out_rgba[i * 4 + 2] = pal[idx][2];
            out_rgba[i * 4 + 3] = pal[idx][3];
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
// O(1) Constant-Time GIM Fingerprint (~20ns regardless of 512x512 texture size!)
// Invariant to runtime tbp0/tbw/psm/cbp/csm header mutations.
// ----------------------------------------------------------------------------
static bool SafeQuickInspectGim(const uint8_t* gim_data, GimQuickInfo* out) {
    if (!gim_data || !out) return false;
    __try {
        if (gim_data[0] != 'G' || gim_data[1] != 'I' || gim_data[2] != 'M' || gim_data[3] != 0) {
            return false;
        }

        uint16_t w = *reinterpret_cast<const uint16_t*>(gim_data + 0x1C);
        uint16_t h = *reinterpret_cast<const uint16_t*>(gim_data + 0x1E);
        if (w == 0 || h == 0 || w > 4096 || h > 4096) {
            return false;
        }

        uint8_t psm = gim_data[0x13];
        uint32_t pixel_bytes = 0;
        uint32_t clut_bytes  = 0;

        if (psm == 0x13 || psm == 0x1B) {
            pixel_bytes = static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
            clut_bytes  = 0x400;
        } else if (psm == 0x14 || psm == 0x24) {
            pixel_bytes = (static_cast<uint32_t>(w) * static_cast<uint32_t>(h)) / 2;
            clut_bytes  = 0x40;
        } else {
            return false;
        }

        uint32_t aligned_pix = (pixel_bytes + 15u) & ~15u;
        uint32_t clut_off    = 0x20 + aligned_pix;
        uint32_t total_size  = clut_off + 0x10 + clut_bytes;

        uint16_t cw = *reinterpret_cast<const uint16_t*>(gim_data + clut_off + 0x0C);
        uint16_t ch = *reinterpret_cast<const uint16_t*>(gim_data + clut_off + 0x0E);
        if (static_cast<uint32_t>(cw) * static_cast<uint32_t>(ch) * 4u != clut_bytes) {
            return false;
        }

        volatile uint8_t probe = gim_data[total_size - 1];
        (void)probe;

        uint64_t fp = FNV1A_64_OFFSET;

        uint64_t h0 = *reinterpret_cast<const uint64_t*>(gim_data + 0x00);
        uint64_t h1 = *reinterpret_cast<const uint64_t*>(gim_data + 0x08);
        uint64_t h2 = *reinterpret_cast<const uint64_t*>(gim_data + 0x10) & 0xFFFFFFFF00000000ULL; // mask out 0x10..0x13
        uint64_t h3 = *reinterpret_cast<const uint64_t*>(gim_data + 0x18);
        fp = (fp ^ h0) * FNV1A_64_PRIME;
        fp = (fp ^ h1) * FNV1A_64_PRIME;
        fp = (fp ^ h2) * FNV1A_64_PRIME;
        fp = (fp ^ h3) * FNV1A_64_PRIME;

        if (pixel_bytes >= 32) {
            fp = (fp ^ *reinterpret_cast<const uint64_t*>(gim_data + 0x20)) * FNV1A_64_PRIME;
            fp = (fp ^ *reinterpret_cast<const uint64_t*>(gim_data + 0x20 + (pixel_bytes / 3u))) * FNV1A_64_PRIME;
            fp = (fp ^ *reinterpret_cast<const uint64_t*>(gim_data + 0x20 + ((pixel_bytes * 2u) / 3u))) * FNV1A_64_PRIME;
            fp = (fp ^ *reinterpret_cast<const uint64_t*>(gim_data + 0x20 + pixel_bytes - 8u)) * FNV1A_64_PRIME;
        }
        uint32_t pal_off = clut_off + 0x10;
        fp = (fp ^ *reinterpret_cast<const uint64_t*>(gim_data + pal_off)) * FNV1A_64_PRIME;
        fp = (fp ^ *reinterpret_cast<const uint64_t*>(gim_data + pal_off + (clut_bytes / 2u))) * FNV1A_64_PRIME;
        fp = (fp ^ *reinterpret_cast<const uint64_t*>(gim_data + pal_off + clut_bytes - 8u)) * FNV1A_64_PRIME;

        uint64_t pix0_q = *reinterpret_cast<const uint64_t*>(gim_data + 0x20);
        uint64_t pix1_q = *reinterpret_cast<const uint64_t*>(gim_data + 0x28);

        out->valid       = true;
        out->total_size  = total_size;
        out->clut_offset = clut_off;
        out->width       = w;
        out->height      = h;
        out->psm         = psm;
        out->quick_fp    = fp;
        out->content_tag = (h3 * FNV1A_64_PRIME) ^ pix0_q ^ (pix1_q << 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ----------------------------------------------------------------------------
// Safe Memory Inspection & Multi-Variant Hash Calculation for GIM headers
// ----------------------------------------------------------------------------
static bool SafeInspectAndHashGim(const uint8_t* gim_data, GimInspectionResult* out) {
    if (!gim_data || !out) return false;
    __try {
        if (gim_data[0] != 'G' || gim_data[1] != 'I' || gim_data[2] != 'M' || gim_data[3] != 0) {
            return false;
        }

        uint16_t w = *reinterpret_cast<const uint16_t*>(gim_data + 0x1C);
        uint16_t h = *reinterpret_cast<const uint16_t*>(gim_data + 0x1E);
        if (w == 0 || h == 0 || w > 4096 || h > 4096) {
            return false;
        }

        uint8_t psm = gim_data[0x13];
        uint32_t pixel_bytes = 0;
        uint32_t clut_bytes  = 0;

        if (psm == 0x13 || psm == 0x1B) {
            pixel_bytes = static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
            clut_bytes  = 0x400;
        } else if (psm == 0x14 || psm == 0x24) {
            pixel_bytes = (static_cast<uint32_t>(w) * static_cast<uint32_t>(h)) / 2;
            clut_bytes  = 0x40;
        } else {
            return false;
        }

        uint32_t aligned_pix = (pixel_bytes + 15u) & ~15u;
        uint32_t clut_off    = 0x20 + aligned_pix;
        uint32_t total_size  = clut_off + 0x10 + clut_bytes;

        uint16_t cw = *reinterpret_cast<const uint16_t*>(gim_data + clut_off + 0x0C);
        uint16_t ch = *reinterpret_cast<const uint16_t*>(gim_data + clut_off + 0x0E);
        uint32_t c_colors = static_cast<uint32_t>(cw) * static_cast<uint32_t>(ch);
        if (c_colors * 4u != clut_bytes) {
            return false;
        }

        volatile uint8_t probe = gim_data[total_size - 1];
        (void)probe;

        uint64_t h_exact    = FNV1A_64_OFFSET;
        uint64_t h_psm_norm = FNV1A_64_OFFSET;
        uint64_t h_fhm_norm = FNV1A_64_OFFSET;
        uint64_t h_raw      = FNV1A_64_OFFSET;

        for (uint32_t i = 0; i < total_size; ++i) {
            uint8_t b_raw   = gim_data[i];
            uint8_t b_exact = (i == 0x12) ? 0 : b_raw;
            uint8_t b_psm   = b_exact;
            if (i == 0x13 && b_raw == 0x1B) {
                b_psm = 0x13;
            }
            uint8_t b_fhm   = b_psm;
            if (i == 0x10 || i == 0x11 || i == clut_off || i == clut_off + 1 || i == clut_off + 2) {
                b_fhm = 0;
            }

            h_raw      ^= static_cast<uint64_t>(b_raw);
            h_raw      *= FNV1A_64_PRIME;

            h_exact    ^= static_cast<uint64_t>(b_exact);
            h_exact    *= FNV1A_64_PRIME;

            h_psm_norm ^= static_cast<uint64_t>(b_psm);
            h_psm_norm *= FNV1A_64_PRIME;

            h_fhm_norm ^= static_cast<uint64_t>(b_fhm);
            h_fhm_norm *= FNV1A_64_PRIME;
        }

        out->valid         = true;
        out->total_size    = total_size;
        out->clut_offset   = clut_off;
        out->width         = w;
        out->height        = h;
        out->psm           = psm;
        out->hash_exact    = h_exact;
        out->hash_psm_norm = h_psm_norm;
        out->hash_fhm_norm = h_fhm_norm;
        out->hash_raw      = h_raw;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ----------------------------------------------------------------------------
// Safe Memory Inspection & Hash Calculation for Dynamic Tile Fonts (GameFont / Pmenu)
// Matches exact parser at 0x1401FFBD0 in acz.exe and ACZ_Resource_Scanner.py v7
// Supports both 32x32 fonts (unk06 == 5) and 16x16 fonts (unk06 == 4)
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// In-Memory select.msa String & Glyph Fallback Patcher (for 21:9 Menu Option)
// Ensures Pool 5 ('21:9'), Pool 7 (' 21:9' & '>21:9' + '2' glyph at g=36), and
// Pool 8 ('Best setting for ultrawide 21:9 monitors.') are populated even when
// select.msa is loaded directly from d.arc instead of mods/textures/menus/.
// ----------------------------------------------------------------------------
static void PatchSelectMsaPoolInMemory(uint8_t* start_ptr) {
    if (!start_ptr) return;
    __try {
        if (start_ptr[0] == 'G' && start_ptr[1] == 'I' && start_ptr[2] == 'M' && start_ptr[3] == 0) {
            return;
        }
        uint32_t num_strs   = *reinterpret_cast<const uint32_t*>(start_ptr + 0x00);
        uint16_t num_glyphs = *reinterpret_cast<const uint16_t*>(start_ptr + 0x04);
        uint16_t glyph_dim  = *reinterpret_cast<const uint16_t*>(start_ptr + 0x08);

        // Pool 5 of select.msa: num_strs=140, num_glyphs=48, glyph_dim=32 -> H11111_002 (str[22] = "21:9")
        if (num_strs == 140u && num_glyphs == 48u && glyph_dim == 32u) {
            uint32_t rdi_off  = 16u * (48u + 1u);
            uint32_t r10_off  = ((rdi_off + 2u * 140u) & ~3u) + 4u;
            uint32_t idx_base = r10_off + 4u * 140u;
            int32_t  soff22   = *reinterpret_cast<const int32_t*>(start_ptr + r10_off + 4u * 22u);
            if (soff22 == 158) {
                *reinterpret_cast<uint16_t*>(start_ptr + rdi_off + 2u * 22u) = 4u;
                uint16_t* gidx = reinterpret_cast<uint16_t*>(start_ptr + idx_base + 2u * soff22);
                gidx[0] = 10u; // '2'
                gidx[1] = 9u;  // '1'
                gidx[2] = 18u; // ':'
                gidx[3] = 17u; // '9'
            }
        }
        // Pool 7 of select.msa: num_strs=123, num_glyphs=38, glyph_dim=16 -> g=36 ('2'), str[31]=" 21:9", str[50]=">21:9"
        else if (num_strs == 123u && num_glyphs == 38u && glyph_dim == 16u) {
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
                *gm36_cc = 50u; // 'Z' (90) -> '2' (50)
                std::memcpy(start_ptr + bmp_off + 36u * 128u, start_ptr + bmp_off + 5u * 128u, 40u);
            }
            int32_t soff31 = *reinterpret_cast<const int32_t*>(start_ptr + r10_off + 4u * 31u);
            if (soff31 == 266) {
                *reinterpret_cast<uint16_t*>(start_ptr + rdi_off + 2u * 31u) = 5u;
                uint16_t* gidx = reinterpret_cast<uint16_t*>(start_ptr + idx_base + 2u * soff31);
                gidx[0] = 0u;  // ' '
                gidx[1] = 36u; // '2'
                gidx[2] = 4u;  // '1'
                gidx[3] = 10u; // ':'
                gidx[4] = 9u;  // '9'
            }
            int32_t soff50 = *reinterpret_cast<const int32_t*>(start_ptr + r10_off + 4u * 50u);
            if (soff50 == 442) {
                *reinterpret_cast<uint16_t*>(start_ptr + rdi_off + 2u * 50u) = 5u;
                uint16_t* gidx = reinterpret_cast<uint16_t*>(start_ptr + idx_base + 2u * soff50);
                gidx[0] = 11u; // '>'
                gidx[1] = 36u; // '2'
                gidx[2] = 4u;  // '1'
                gidx[3] = 10u; // ':'
                gidx[4] = 9u;  // '9'
            }
        }
        // Pool 8 of select.msa: num_strs=874, num_glyphs=92, glyph_dim=32 -> IM000_002 (str[830])
        else if (num_strs == 874u && num_glyphs == 92u && glyph_dim == 32u) {
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

static bool SafeInspectAndHashGameFont(uint8_t* font_obj, uint8_t* start_ptr, FontInspectionResult* out) {
    (void)font_obj;
    if (!start_ptr || !out) return false;
    PatchSelectMsaPoolInMemory(start_ptr);
    __try {
        if (start_ptr[0] == 'G' && start_ptr[1] == 'I' && start_ptr[2] == 'M' && start_ptr[3] == 0) {
            return false;
        }

        uint32_t num_strs   = *reinterpret_cast<const uint32_t*>(start_ptr + 0x00);
        uint16_t num_glyphs = *reinterpret_cast<const uint16_t*>(start_ptr + 0x04);
        uint16_t unk06      = *reinterpret_cast<const uint16_t*>(start_ptr + 0x06);
        uint16_t glyph_dim  = *reinterpret_cast<const uint16_t*>(start_ptr + 0x08);
        uint16_t max_adv    = *reinterpret_cast<const uint16_t*>(start_ptr + 0x0E);

        if (num_strs == 0 || num_strs > 4096 || num_glyphs == 0 || num_glyphs > 4096) {
            return false;
        }
        if (glyph_dim != 12 && glyph_dim != 16 && glyph_dim != 20 &&
            glyph_dim != 24 && glyph_dim != 32 && glyph_dim != 48 && glyph_dim != 64) {
            return false;
        }
        // unk06 is log2(glyph_dim): 4 for 16x16 fonts, 5 for 32x32 fonts, 6 for 64x64 fonts
        if (((1u << unk06) != glyph_dim && (unk06 < 4u || unk06 > 6u)) || max_adv == 0 || max_adv > 128u) {
            return false;
        }

        uint32_t rdi_off   = 16u * (static_cast<uint32_t>(num_glyphs) + 1u);
        uint32_t r10_off   = ((rdi_off + 2u * num_strs) & ~3u) + 4u;
        int32_t  first_off = *reinterpret_cast<const int32_t*>(start_ptr + r10_off);
        int16_t  last_len  = *reinterpret_cast<const int16_t*>(start_ptr + rdi_off + 2u * (num_strs - 1u));
        int32_t  last_off  = *reinterpret_cast<const int32_t*>(start_ptr + r10_off + 4u * (num_strs - 1u));
        if (first_off != 0 || last_len < 0 || last_len > 2048 || last_off < 0 || last_off > 262144) {
            return false;
        }

        uint32_t end_idx_off = r10_off + 2u * (static_cast<uint32_t>(last_off) + 2u * num_strs + static_cast<uint32_t>(last_len));
        uint32_t pal_off     = (end_idx_off & ~15u) + 16u;
        uint32_t bmp_off     = pal_off + 0x40u;
        uint32_t bmp_size    = (static_cast<uint32_t>(num_glyphs) * glyph_dim * glyph_dim) / 2u;
        uint32_t total_size  = bmp_off + bmp_size;
        if (total_size > 8u * 1024u * 1024u) {
            return false;
        }

        volatile uint8_t probe = start_ptr[total_size - 1u];
        (void)probe;

        uint32_t glyph_meta_end = 16u + 16u * static_cast<uint32_t>(num_glyphs);
        uint32_t str_len_end    = rdi_off + 2u * num_strs;

        uint64_t h_norm  = FNV1A_64_OFFSET;
        uint64_t h_clean = FNV1A_64_OFFSET;

        for (uint32_t i = 0; i < total_size; ++i) {
            uint8_t b_norm  = start_ptr[i];
            uint8_t b_clean = b_norm;

            if (i >= 16u && i < glyph_meta_end) {
                uint32_t rem = (i - 16u) & 15u;
                // runtime_tbp (+4..+7) and flags (+10) are mutated in RAM by 0x1401FFF2B
                if ((rem >= 4u && rem <= 7u) || rem == 10u) {
                    b_norm  = 0;
                    b_clean = 0;
                } else if (rem >= 11u) {
                    // +11..+15 are uninitialized 0xCD padding in the PS2 tool
                    b_clean = 0;
                }
            } else if ((i >= str_len_end && i < r10_off) || (i >= end_idx_off && i < pal_off)) {
                b_clean = 0;
            }

            h_norm  ^= static_cast<uint64_t>(b_norm);
            h_norm  *= FNV1A_64_PRIME;

            h_clean ^= static_cast<uint64_t>(b_clean);
            h_clean *= FNV1A_64_PRIME;
        }

        // Visual-only hash over (num_glyphs, glyph_dim, clut[64], glyph_tiles[bmp_size])
        uint64_t h_vis = FNV1A_64_OFFSET;
        uint8_t hdr4[4] = {
            static_cast<uint8_t>(num_glyphs & 0xFF),
            static_cast<uint8_t>((num_glyphs >> 8) & 0xFF),
            static_cast<uint8_t>(glyph_dim & 0xFF),
            static_cast<uint8_t>((glyph_dim >> 8) & 0xFF)
        };
        for (int k = 0; k < 4; ++k) {
            h_vis ^= hdr4[k];
            h_vis *= FNV1A_64_PRIME;
        }
        for (uint32_t i = pal_off; i < total_size; ++i) {
            h_vis ^= start_ptr[i];
            h_vis *= FNV1A_64_PRIME;
        }

        out->valid          = true;
        out->total_size     = total_size;
        out->num_strings    = num_strs;
        out->num_glyphs     = num_glyphs;
        out->glyph_dim      = glyph_dim;
        out->rdi_off        = rdi_off;
        out->r10_off        = r10_off;
        out->end_idx_off    = end_idx_off;
        out->pal_off        = pal_off;
        out->bmp_off        = bmp_off;
        out->glyph_meta_ptr = start_ptr + 0x10;
        out->clut_ptr       = start_ptr + pal_off;
        out->tiles_ptr      = start_ptr + bmp_off;
        out->hash_norm      = h_norm;
        out->hash_clean     = h_clean;
        out->hash_visual    = h_vis;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ----------------------------------------------------------------------------
// Decode Dynamic Font Atlas to RGBA + Info Text
// ----------------------------------------------------------------------------
static void AppendUtf8Codepoint(std::string& s, uint16_t cp) {
    if (cp == 0) return;
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static std::string DecodeFontStringUtf8(const uint8_t* start_ptr, const FontInspectionResult& fres, uint32_t str_idx) {
    if (str_idx >= fres.num_strings) return "";
    int16_t slen = *reinterpret_cast<const int16_t*>(start_ptr + fres.rdi_off + 2u * str_idx);
    int32_t soff = *reinterpret_cast<const int32_t*>(start_ptr + fres.r10_off + 4u * str_idx);
    if (slen <= 0 || soff < 0) return "";

    uint32_t rax_off = fres.r10_off + 4u * fres.num_strings;
    std::string out;
    for (int16_t k = 0; k < slen; ++k) {
        uint32_t idx_off = rax_off + 2u * (static_cast<uint32_t>(soff) + static_cast<uint32_t>(k));
        if (idx_off + 2u > fres.end_idx_off) break;
        int16_t g_idx = *reinterpret_cast<const int16_t*>(start_ptr + idx_off);
        if (g_idx < 0) {
            out += " / ";
        } else if (static_cast<uint16_t>(g_idx) < fres.num_glyphs) {
            uint16_t cp = *reinterpret_cast<const uint16_t*>(start_ptr + 16u + static_cast<uint32_t>(g_idx) * 16u + 8u);
            AppendUtf8Codepoint(out, cp);
        }
    }
    return out;
}

static void DumpDynamicFontAtlasToFolder(
    const fs::path& target_dir,
    const uint8_t* start_ptr,
    const FontInspectionResult& fres,
    const OrigFontSnapshot& snap,
    uint64_t primary_hash,
    bool force_raw_bin
) {
    std::error_code ec;
    fs::create_directories(target_dir, ec);

    char base_name[112];
    snprintf(base_name, sizeof(base_name), "%016llX_font_%ugl_%ux%u",
             static_cast<unsigned long long>(primary_hash),
             fres.num_glyphs, fres.glyph_dim, fres.glyph_dim);

    fs::path png_path = target_dir / (std::string(base_name) + ".png");
    fs::path txt_path = target_dir / (std::string(base_name) + ".txt");
    fs::path bin_path = target_dir / (std::string(base_name) + ".bin");

    if (!fs::exists(png_path, ec)) {
        uint8_t pal_pc[16][4] = {};
        for (int c = 0; c < 16; ++c) {
            pal_pc[c][0] = snap.orig_clut[c * 4 + 0];
            pal_pc[c][1] = snap.orig_clut[c * 4 + 1];
            pal_pc[c][2] = snap.orig_clut[c * 4 + 2];
            pal_pc[c][3] = Ps2AlphaToPc(snap.orig_clut[c * 4 + 3]);
        }

        const uint32_t cols    = 8u;
        const uint32_t rows    = (fres.num_glyphs + cols - 1u) / cols;
        const uint32_t dim     = fres.glyph_dim;
        const uint32_t atlas_w = cols * dim;
        const uint32_t atlas_h = rows * dim;

        std::vector<uint8_t> atlas_rgba(atlas_w * atlas_h * 4u, 0);
        const uint32_t tile_bytes = (dim * dim) / 2u;

        for (uint32_t g = 0; g < fres.num_glyphs; ++g) {
            const uint8_t* tile_src = snap.orig_tiles.data() + g * tile_bytes;
            uint32_t gx0 = (g % cols) * dim;
            uint32_t gy0 = (g / cols) * dim;
            for (uint32_t py = 0; py < dim; ++py) {
                for (uint32_t px = 0; px < dim; ++px) {
                    uint32_t p_idx = py * dim + px;
                    uint8_t  byte_v = tile_src[p_idx >> 1];
                    uint8_t  c_idx  = (p_idx & 1u) ? ((byte_v >> 4) & 0x0Fu) : (byte_v & 0x0Fu);
                    size_t   dst    = (static_cast<size_t>(gy0 + py) * atlas_w + (gx0 + px)) * 4u;
                    atlas_rgba[dst + 0] = pal_pc[c_idx][0];
                    atlas_rgba[dst + 1] = pal_pc[c_idx][1];
                    atlas_rgba[dst + 2] = pal_pc[c_idx][2];
                    atlas_rgba[dst + 3] = pal_pc[c_idx][3];
                }
            }
        }

        std::vector<uint8_t> png_bytes;
        EncodeRgbaToPngBytes(atlas_rgba.data(), atlas_w, atlas_h, png_bytes);
        if (!png_bytes.empty()) {
            FILE* pf = nullptr;
            fopen_s(&pf, png_path.string().c_str(), "wb");
            if (pf) {
                fwrite(png_bytes.data(), 1, png_bytes.size(), pf);
                fclose(pf);
            }
        }
    }

    if (!fs::exists(txt_path, ec)) {
        FILE* tf = nullptr;
        fopen_s(&tf, txt_path.string().c_str(), "wb");
        if (tf) {
            fprintf(tf, "====================================================================\n");
            fprintf(tf, " Dynamic Font Atlas Dump (GameFont / CUnicodeFont / Pmenu)\n");
            fprintf(tf, "====================================================================\n");
            fprintf(tf, "Primary Hash (norm):   %016llX\n", static_cast<unsigned long long>(primary_hash));
            fprintf(tf, "Font Family Hash:      %016llX\n", static_cast<unsigned long long>(snap.family_hash));
            fprintf(tf, "Clean Hash (no pad):   %016llX\n", static_cast<unsigned long long>(fres.hash_clean));
            fprintf(tf, "Visual Atlas Hash:     %016llX\n", static_cast<unsigned long long>(fres.hash_visual));
            fprintf(tf, "In-Memory Address:     %p\n", reinterpret_cast<const void*>(start_ptr));
            fprintf(tf, "Grid Columns:          8\n");
            fprintf(tf, "Glyphs:                %u (%ux%u, 8 columns)\n", fres.num_glyphs, fres.glyph_dim, fres.glyph_dim);
            fprintf(tf, "Strings:               %u\n", fres.num_strings);
            fprintf(tf, "To replace in-game:    Copy edited PNG to mods\\textures\\%016llX.png\n",
                    static_cast<unsigned long long>(primary_hash));
            fprintf(tf, "--------------------------------------------------------------------\n");
            fprintf(tf, "DECODED STRINGS:\n");
            for (uint32_t s = 0; s < fres.num_strings; ++s) {
                std::string text = DecodeFontStringUtf8(start_ptr, fres, s);
                fprintf(tf, "  [%03u] %s\n", s, text.c_str());
            }
            fprintf(tf, "--------------------------------------------------------------------\n");
            fprintf(tf, "GLYPH METRICS (Grid 8 columns):\n");
            for (uint32_t g = 0; g < fres.num_glyphs; ++g) {
                const uint8_t* gm = start_ptr + 16u + g * 16u;
                uint16_t adv_x = *reinterpret_cast<const uint16_t*>(gm + 0);
                uint16_t h     = *reinterpret_cast<const uint16_t*>(gm + 2);
                uint16_t cp    = *reinterpret_cast<const uint16_t*>(gm + 8);
                uint64_t thash = (g < snap.glyphs.size()) ? snap.glyphs[g].bitmap_tile_hash : 0ULL;
                uint64_t ehash = (g < snap.glyphs.size()) ? snap.glyphs[g].exact_tile_hash  : 0ULL;
                std::string ch_utf8;
                AppendUtf8Codepoint(ch_utf8, cp);
                fprintf(tf, "  Glyph #%03u (col=%u, row=%u): U+%04X '%s' | adv_x=%u, height=%u | TileHash=%016llX | ExactHash=%016llX\n",
                        g, g % 8u, g / 8u, cp, ch_utf8.c_str(), adv_x, h,
                        static_cast<unsigned long long>(thash),
                        static_cast<unsigned long long>(ehash));
            }
            fclose(tf);
        }
    }

    if ((g_DumpRawGimFiles || force_raw_bin) && !fs::exists(bin_path, ec)) {
        FILE* bf = nullptr;
        fopen_s(&bf, bin_path.string().c_str(), "wb");
        if (bf) {
            std::vector<uint8_t> clean_bin(start_ptr, start_ptr + fres.total_size);
            for (uint32_t g = 0; g < fres.num_glyphs; ++g) {
                uint32_t off = 16u + g * 16u;
                *reinterpret_cast<uint32_t*>(&clean_bin[off + 4u]) = 0;
                clean_bin[off + 10u] = 0;
            }
            fwrite(clean_bin.data(), 1, clean_bin.size(), bf);
            fclose(bf);
        }
    }
}

static void DumpDynamicFontAtlasAndInfo(const uint8_t* start_ptr, const FontInspectionResult& fres, const OrigFontSnapshot& snap, uint64_t primary_hash) {
    DumpDynamicFontAtlasToFolder(g_FontDumpDir, start_ptr, fres, snap, primary_hash, false);
}

static void DumpGimBufferToFolder(
    const fs::path& target_dir,
    const uint8_t* gim_data,
    const GimInspectionResult& res,
    uint64_t primary_hash,
    uint8_t filename_psm,
    bool force_raw_gim,
    const uint8_t* orig_hdr_32 = nullptr,
    const uint8_t* orig_clut_hdr_16 = nullptr
) {
    std::error_code ec;
    fs::create_directories(target_dir, ec);
    char base_name[96];
    snprintf(base_name, sizeof(base_name), "%016llX_%ux%u_psm%02X",
             static_cast<unsigned long long>(primary_hash), res.width, res.height, filename_psm);

    if (g_DumpRawGimFiles || force_raw_gim) {
        fs::path dump_gim_path = target_dir / (std::string(base_name) + ".gim");
        if (!fs::exists(dump_gim_path, ec)) {
            FILE* df = nullptr;
            fopen_s(&df, dump_gim_path.string().c_str(), "wb");
            if (df) {
                std::vector<uint8_t> clean_copy(gim_data, gim_data + res.total_size);
                if (orig_hdr_32 != nullptr && clean_copy.size() >= 32) {
                    std::memcpy(clean_copy.data(), orig_hdr_32, 32);
                }
                if (orig_clut_hdr_16 != nullptr && clean_copy.size() >= res.clut_offset + 16) {
                    std::memcpy(clean_copy.data() + res.clut_offset, orig_clut_hdr_16, 16);
                }
                if (clean_copy.size() > 0x13) {
                    clean_copy[0x12] = 0;
                    if (clean_copy[0x13] == 0x1B) clean_copy[0x13] = 0x13;
                }
                fwrite(clean_copy.data(), 1, clean_copy.size(), df);
                fclose(df);
            }
        }
    }

    fs::path dump_png_path = target_dir / (std::string(base_name) + ".png");
    if (!fs::exists(dump_png_path, ec)) {
        std::vector<uint8_t> rgba;
        if (DecodeGimToRgba(gim_data, res, rgba)) {
            std::vector<uint8_t> png_bytes;
            EncodeRgbaToPngBytes(rgba.data(), res.width, res.height, png_bytes);
            if (!png_bytes.empty()) {
                FILE* pf = nullptr;
                fopen_s(&pf, dump_png_path.string().c_str(), "wb");
                if (pf) {
                    fwrite(png_bytes.data(), 1, png_bytes.size(), pf);
                    fclose(pf);
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Safe Memory Inspection, Decoding & Dumping for PS2 TIM2 (.tm2 / .swa SWAD)
// Handles both intact disk TIM2 headers and in-memory SWAD TIM2 buffers where
// bytes +0x00..+0x1F were overwritten with runtime scene-graph pointers!
// ----------------------------------------------------------------------------
static uint64_t SafeHashByteRange(const uint8_t* ptr, size_t len) {
    if (!ptr || len == 0) return 0;
    __try {
        uint64_t h = FNV1A_64_OFFSET;
        for (size_t i = 0; i < len; ++i) {
            h ^= static_cast<uint64_t>(ptr[i]);
            h *= FNV1A_64_PRIME;
        }
        return h;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static bool SafeReadTim2ContentTag(const uint8_t* src_pixels, uint64_t* out_tag) {
    if (!src_pixels || !out_tag) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(src_pixels);
    if (addr <= 0x10060u) return false;
    __try {
        const uint8_t* hdr_20 = nullptr;
        if (*reinterpret_cast<const uint32_t*>(src_pixels - 0x20) == 0x00745865u && // "eXt\0"
            *reinterpret_cast<const uint32_t*>(src_pixels - 0x10) == 0x58444947u) { // "GIDX"
            hdr_20 = src_pixels - 0x40;
        } else if (*reinterpret_cast<const uint32_t*>(src_pixels - 0x40) == 0x324D4954u && // "TIM2"
                   src_pixels[-0x40 + 4] == 4 && src_pixels[-0x40 + 5] == 0) {
            hdr_20 = src_pixels - 0x20;
        } else {
            return false;
        }
        if (hdr_20[0] != 0 || hdr_20[1] != 1) return false;
        uint8_t img_type = hdr_20[3];
        if (img_type < 1 || img_type > 5) return false;
        uint64_t meta_q = *reinterpret_cast<const uint64_t*>(hdr_20);
        uint64_t pix0_q = *reinterpret_cast<const uint64_t*>(src_pixels + 0x00);
        uint64_t pix1_q = *reinterpret_cast<const uint64_t*>(src_pixels + 0x08);
        uint64_t pix2_q = *reinterpret_cast<const uint64_t*>(src_pixels + 0x10);
        *out_tag = (meta_q * FNV1A_64_PRIME) ^ pix0_q ^ (pix1_q << 1) ^ (pix2_q << 2);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeInspectAndHashTim2(const uint8_t* src_pixels, Tim2InspectionResult* out) {
    if (!src_pixels || !out) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(src_pixels);
    if (addr <= 0x10060u) return false;
    __try {
        const uint8_t* tim2_base = nullptr;
        const uint8_t* hdr_20    = nullptr;
        uint16_t header_size     = 0;
        uint32_t pic_offset      = 16u;

        // 1. Check for SWAD / extended TIM2 with "eXt\0" at src_pixels - 0x20 and "GIDX" at src_pixels - 0x10
        if (*reinterpret_cast<const uint32_t*>(src_pixels - 0x20) == 0x00745865u &&
            *reinterpret_cast<const uint32_t*>(src_pixels - 0x10) == 0x58444947u) {
            tim2_base   = src_pixels - 0x60;
            hdr_20      = src_pixels - 0x40;
            header_size = 0x50u;
            pic_offset  = 16u;
        }
        // 2. Check for standard 48-byte picture header TIM2 ("TIM2" at src_pixels - 0x40)
        else if (*reinterpret_cast<const uint32_t*>(src_pixels - 0x40) == 0x324D4954u &&
                 src_pixels[-0x40 + 4] == 4 && src_pixels[-0x40 + 5] == 0) {
            tim2_base   = src_pixels - 0x40;
            hdr_20      = src_pixels - 0x20;
            header_size = 0x30u;
            pic_offset  = 16u;
        } else {
            return false;
        }

        uint8_t  pict_format = hdr_20[0];
        uint8_t  mipmap_lv   = hdr_20[1];
        uint8_t  clut_type   = hdr_20[2];
        uint8_t  image_type  = hdr_20[3];
        uint16_t w           = *reinterpret_cast<const uint16_t*>(hdr_20 + 4);
        uint16_t h           = *reinterpret_cast<const uint16_t*>(hdr_20 + 6);

        if (pict_format != 0 || mipmap_lv != 1 || w == 0 || h == 0 || w > 4096 || h > 4096) {
            return false;
        }

        uint32_t image_size  = 0;
        uint16_t clut_colors = 0;
        uint32_t clut_size   = 0;
        uint8_t  psm         = 0;

        if (image_type == 4) { // 4-bit indexed (IDTEX4 / PSMT4)
            image_size  = (static_cast<uint32_t>(w) * static_cast<uint32_t>(h)) / 2u;
            clut_colors = 16u;
            clut_size   = ((clut_type & 0x07u) == 1u) ? 32u : 64u;
            psm         = 0x14u;
        } else if (image_type == 5) { // 8-bit indexed (IDTEX8 / PSMT8)
            image_size  = static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
            clut_colors = 256u;
            clut_size   = ((clut_type & 0x07u) == 1u) ? 512u : 1024u;
            psm         = 0x13u;
        } else if (image_type == 3) { // 32-bit RGBA (RGB32)
            image_size  = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4u;
            psm         = 0x00u;
        } else if (image_type == 2) { // 24-bit RGB (RGB24)
            image_size  = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 3u;
            psm         = 0x01u;
        } else if (image_type == 1) { // 16-bit RGBA (RGB16)
            image_size  = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 2u;
            psm         = 0x02u;
        } else {
            return false;
        }

        if (image_size == 0 || image_size > 16u * 1024u * 1024u) return false;
        uint32_t total_pic_size = static_cast<uint32_t>(header_size) + image_size + clut_size;
        uint32_t total_size     = pic_offset + total_pic_size;

        // Probe last byte of TIM2 payload
        volatile uint8_t probe = src_pixels[image_size + clut_size - 1u];
        (void)probe;

        // Reconstruct the exact 32-byte canonical TIM2 header (+0x00..+0x1F)
        uint8_t canon_hdr[32]{};
        if (*reinterpret_cast<const uint32_t*>(tim2_base) == 0x324D4954u) {
            std::memcpy(canon_hdr, tim2_base, 32);
        } else {
            canon_hdr[0] = 'T'; canon_hdr[1] = 'I'; canon_hdr[2] = 'M'; canon_hdr[3] = '2';
            canon_hdr[4] = 4;   canon_hdr[5] = 0;   canon_hdr[6] = 1;   canon_hdr[7] = 0;
            *reinterpret_cast<uint32_t*>(canon_hdr + 0x10) = total_pic_size;
            *reinterpret_cast<uint32_t*>(canon_hdr + 0x14) = clut_size;
            *reinterpret_cast<uint32_t*>(canon_hdr + 0x18) = image_size;
            *reinterpret_cast<uint16_t*>(canon_hdr + 0x1C) = header_size;
            *reinterpret_cast<uint16_t*>(canon_hdr + 0x1E) = clut_colors;
        }

        uint64_t h_norm = FNV1A_64_OFFSET;
        for (size_t i = 0; i < 32u; ++i) {
            h_norm ^= static_cast<uint64_t>(canon_hdr[i]);
            h_norm *= FNV1A_64_PRIME;
        }
        size_t rem_len = static_cast<size_t>(total_size - 32u);
        for (size_t i = 0; i < rem_len; ++i) {
            h_norm ^= static_cast<uint64_t>(hdr_20[i]);
            h_norm *= FNV1A_64_PRIME;
        }

        uint64_t h_pix      = FNV1A_64_OFFSET;
        uint64_t h_pix_clut = FNV1A_64_OFFSET;
        for (uint32_t i = 0; i < image_size; ++i) {
            uint8_t b = src_pixels[i];
            h_pix      ^= static_cast<uint64_t>(b);
            h_pix      *= FNV1A_64_PRIME;
            h_pix_clut ^= static_cast<uint64_t>(b);
            h_pix_clut *= FNV1A_64_PRIME;
        }
        for (uint32_t i = 0; i < clut_size; ++i) {
            h_pix_clut ^= static_cast<uint64_t>(src_pixels[image_size + i]);
            h_pix_clut *= FNV1A_64_PRIME;
        }

        uint64_t meta_q = *reinterpret_cast<const uint64_t*>(hdr_20);
        uint64_t pix0_q = (image_size >= 8)  ? *reinterpret_cast<const uint64_t*>(src_pixels + 0x00) : 0ULL;
        uint64_t pix1_q = (image_size >= 16) ? *reinterpret_cast<const uint64_t*>(src_pixels + 0x08) : 0ULL;
        uint64_t pix2_q = (image_size >= 24) ? *reinterpret_cast<const uint64_t*>(src_pixels + 0x10) : 0ULL;

        out->valid              = true;
        out->pic_offset         = pic_offset;
        out->header_size        = header_size;
        out->clut_colors        = clut_colors;
        out->image_size         = image_size;
        out->clut_size          = clut_size;
        out->total_pic_size     = total_pic_size;
        out->total_size         = total_size;
        out->width              = w;
        out->height             = h;
        out->clut_type          = clut_type;
        out->image_type         = image_type;
        out->psm                = psm;
        std::memcpy(out->canon_hdr_32, canon_hdr, 32);
        out->content_tag        = (meta_q * FNV1A_64_PRIME) ^ pix0_q ^ (pix1_q << 1) ^ (pix2_q << 2);
        out->hash_tim2_norm     = h_norm;
        out->hash_tim2_pix_clut = h_pix_clut;
        out->hash_tim2_pixels   = h_pix;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool DecodeTim2ToRgba(const uint8_t* src_pixels, const Tim2InspectionResult& res, std::vector<uint8_t>& out_rgba) {
    if (!src_pixels || !res.valid) return false;
    const uint32_t w = res.width;
    const uint32_t h = res.height;
    out_rgba.assign(static_cast<size_t>(w) * h * 4u, 0);

    if (res.image_type == 4 || res.image_type == 5) {
        const uint8_t* pal_src = src_pixels + res.image_size;
        uint32_t num_colors = (res.image_type == 5) ? 256u : 16u;
        uint8_t pal[256][4]{};
        if ((res.clut_type & 0x07u) == 1u) { // 16-bit BGR5A1 CLUT
            for (uint32_t c = 0; c < num_colors; ++c) {
                uint16_t v = *reinterpret_cast<const uint16_t*>(pal_src + c * 2u);
                pal[c][0] = static_cast<uint8_t>(((v >> 0)  & 0x1Fu) * 255u / 31u);
                pal[c][1] = static_cast<uint8_t>(((v >> 5)  & 0x1Fu) * 255u / 31u);
                pal[c][2] = static_cast<uint8_t>(((v >> 10) & 0x1Fu) * 255u / 31u);
                pal[c][3] = (v & 0x8000u) ? 255u : 0u;
            }
        } else { // 32-bit RGBA CLUT (PS2 alpha 0..128)
            for (uint32_t c = 0; c < num_colors; ++c) {
                pal[c][0] = pal_src[c * 4u + 0];
                pal[c][1] = pal_src[c * 4u + 1];
                pal[c][2] = pal_src[c * 4u + 2];
                pal[c][3] = Ps2AlphaToPc(pal_src[c * 4u + 3]);
            }
        }
        // CSM1 palette unswizzle for 8-bit IDTEX8 when bit 7 (0x80) of clut_type is 0
        if (num_colors == 256u && (res.clut_type & 0x80u) == 0u) {
            for (uint32_t base = 0; base < 256u; base += 32u) {
                for (uint32_t k = 0; k < 8u; ++k) {
                    for (int ch = 0; ch < 4; ++ch) {
                        uint8_t tmp = pal[base + 8u + k][ch];
                        pal[base + 8u + k][ch]  = pal[base + 16u + k][ch];
                        pal[base + 16u + k][ch] = tmp;
                    }
                }
            }
        }
        if (res.image_type == 5) {
            for (uint32_t i = 0; i < w * h; ++i) {
                uint8_t idx = src_pixels[i];
                out_rgba[i * 4u + 0] = pal[idx][0];
                out_rgba[i * 4u + 1] = pal[idx][1];
                out_rgba[i * 4u + 2] = pal[idx][2];
                out_rgba[i * 4u + 3] = pal[idx][3];
            }
        } else {
            for (uint32_t i = 0; i < w * h; ++i) {
                uint8_t b = src_pixels[i >> 1];
                uint8_t idx = (i & 1u) ? ((b >> 4) & 0x0Fu) : (b & 0x0Fu);
                out_rgba[i * 4u + 0] = pal[idx][0];
                out_rgba[i * 4u + 1] = pal[idx][1];
                out_rgba[i * 4u + 2] = pal[idx][2];
                out_rgba[i * 4u + 3] = pal[idx][3];
            }
        }
        return true;
    } else if (res.image_type == 3) { // 32-bit RGBA
        for (uint32_t i = 0; i < w * h; ++i) {
            out_rgba[i * 4u + 0] = src_pixels[i * 4u + 0];
            out_rgba[i * 4u + 1] = src_pixels[i * 4u + 1];
            out_rgba[i * 4u + 2] = src_pixels[i * 4u + 2];
            out_rgba[i * 4u + 3] = Ps2AlphaToPc(src_pixels[i * 4u + 3]);
        }
        return true;
    }
    return false;
}

static void DumpTim2BufferToFolder(
    const fs::path& target_dir,
    const uint8_t* src_pixels,
    const Tim2InspectionResult& res,
    uint64_t primary_hash,
    bool force_raw_tm2
) {
    std::error_code ec;
    fs::create_directories(target_dir, ec);
    char base_name[112];
    snprintf(base_name, sizeof(base_name), "%016llX_tim2_%ux%u_psm%02X",
             static_cast<unsigned long long>(primary_hash), res.width, res.height, res.psm);

    if (g_DumpRawGimFiles || force_raw_tm2) {
        fs::path dump_tm2_path = target_dir / (std::string(base_name) + ".tm2");
        if (!fs::exists(dump_tm2_path, ec)) {
            FILE* df = nullptr;
            fopen_s(&df, dump_tm2_path.string().c_str(), "wb");
            if (df) {
                const uint8_t* hdr_20 = src_pixels - (res.header_size - 0x10u);
                fwrite(res.canon_hdr_32, 1, 32, df);
                fwrite(hdr_20, 1, res.total_size - 32u, df);
                fclose(df);
            }
        }
    }

    fs::path dump_png_path = target_dir / (std::string(base_name) + ".png");
    if (!fs::exists(dump_png_path, ec)) {
        std::vector<uint8_t> rgba;
        if (DecodeTim2ToRgba(src_pixels, res, rgba)) {
            std::vector<uint8_t> png_bytes;
            EncodeRgbaToPngBytes(rgba.data(), res.width, res.height, png_bytes);
            if (!png_bytes.empty()) {
                FILE* pf = nullptr;
                fopen_s(&pf, dump_png_path.string().c_str(), "wb");
                if (pf) {
                    fwrite(png_bytes.data(), 1, png_bytes.size(), pf);
                    fclose(pf);
                }
            }
        }
    }
}

static void DumpMasterGim16AtlasToFolder(const fs::path& target_dir) {
    if (g_MasterGim16Tiles.empty()) return;
    std::error_code ec;
    fs::create_directories(target_dir, ec);

    const uint32_t num_items = static_cast<uint32_t>(g_MasterGim16Tiles.size());
    const uint32_t cols      = (num_items >= 16u) ? 16u : 8u;
    const uint32_t rows      = (num_items + cols - 1u) / cols;
    const uint32_t dim       = 16u;
    const uint32_t atlas_w   = cols * dim;
    const uint32_t atlas_h   = rows * dim;

    std::vector<uint8_t> atlas_rgba(static_cast<size_t>(atlas_w) * atlas_h * 4u, 0);
    for (uint32_t idx = 0; idx < num_items; ++idx) {
        const auto& item = g_MasterGim16Tiles[idx];
        if (item.rgba_16x16.size() < 16u * 16u * 4u) continue;
        uint32_t gx0 = (idx % cols) * dim;
        uint32_t gy0 = (idx / cols) * dim;
        for (uint32_t py = 0; py < dim; ++py) {
            std::memcpy(
                &atlas_rgba[((static_cast<size_t>(gy0 + py) * atlas_w) + gx0) * 4u],
                &item.rgba_16x16[static_cast<size_t>(py) * dim * 4u],
                dim * 4u
            );
        }
    }

    fs::path png_path = target_dir / "MASTER_GIM_16x16.png";
    fs::path txt_path = target_dir / "MASTER_GIM_16x16.txt";

    std::vector<uint8_t> png_bytes;
    EncodeRgbaToPngBytes(atlas_rgba.data(), atlas_w, atlas_h, png_bytes);
    if (!png_bytes.empty()) {
        FILE* pf = nullptr;
        fopen_s(&pf, png_path.string().c_str(), "wb");
        if (pf) {
            fwrite(png_bytes.data(), 1, png_bytes.size(), pf);
            fclose(pf);
        }
    }

    FILE* tf = nullptr;
    fopen_s(&tf, txt_path.string().c_str(), "wb");
    if (tf) {
        fprintf(tf, "====================================================================\n");
        fprintf(tf, " Master 16x16 GIM Atlas (All Seen 16x16 GIM Textures Combined)\n");
        fprintf(tf, "====================================================================\n");
        fprintf(tf, "Master Atlas Name:     MASTER_GIM_16x16\n");
        fprintf(tf, "Glyph Dimension:       16x16\n");
        fprintf(tf, "Grid Columns:          %u\n", cols);
        fprintf(tf, "Collected Glyphs:      %u (16x16, %u columns)\n", num_items, cols);
        fprintf(tf, "To replace all 16x16 GIM textures in-game:\n");
        fprintf(tf, "  Copy edited MASTER_GIM_16x16.png AND MASTER_GIM_16x16.txt into mods\\textures\\\n");
        fprintf(tf, "--------------------------------------------------------------------\n");
        fprintf(tf, "GIM TILE METRICS (Grid %u columns):\n", cols);
        for (uint32_t idx = 0; idx < num_items; ++idx) {
            const auto& item = g_MasterGim16Tiles[idx];
            fprintf(tf, "  GimTile #%03u (col=%u, row=%u): GimHash=%016llX | GimExactHash=%016llX | GimFhmHash=%016llX | PSM=0x%02X\n",
                    idx, idx % cols, idx / cols,
                    static_cast<unsigned long long>(item.hash_psm_norm),
                    static_cast<unsigned long long>(item.hash_exact),
                    static_cast<unsigned long long>(item.hash_fhm_norm),
                    item.psm);
        }
        fclose(tf);
    }
}

// ----------------------------------------------------------------------------
// Parse Hex Hash or MASTER_ Stem Hash from Filename
// ----------------------------------------------------------------------------
static uint64_t ComputeLowerStemHash(const std::string& stem_low) {
    uint64_t h = FNV1A_64_OFFSET;
    for (unsigned char c : stem_low) {
        h ^= static_cast<uint64_t>(c);
        h *= FNV1A_64_PRIME;
    }
    return h;
}

static bool TryParseHashFromStem(const std::string& stem, uint64_t* out_hash) {
    std::string stem_low = stem;
    for (char& c : stem_low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Support MASTER_<LANG>_font_<dim>x<dim>.png and MASTER_<LANG>_gim_16x16.png directly without hex hash in filename!
    if (stem_low.rfind("master_", 0) == 0) {
        *out_hash = ComputeLowerStemHash(stem_low);
        return true;
    }

    auto try_hex16 = [&](const std::string& s) -> bool {
        if (s.size() != 16) return false;
        for (char c : s) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return false;
        }
        *out_hash = std::strtoull(s.c_str(), nullptr, 16);
        return true;
    };

    if (stem.size() >= 2 && (stem.rfind("0x", 0) == 0 || stem.rfind("0X", 0) == 0)) {
        if (try_hex16(stem.substr(2))) return true;
    }

    if (stem.size() < 16) return false;
    if (try_hex16(stem)) return true;

    if (stem.size() > 16) {
        std::string suffix = stem.substr(stem.size() - 16);
        if (try_hex16(suffix)) return true;
        std::string prefix = stem.substr(0, 16);
        if (stem[16] == '_' && try_hex16(prefix)) return true;
        if (stem.size() >= 23 && (stem.rfind("FAMILY_", 0) == 0 || stem.rfind("family_", 0) == 0)) {
            std::string fhash = stem.substr(7, 16);
            if ((stem.size() == 23 || stem[23] == '_') && try_hex16(fhash)) return true;
        }
        // Also scan any underscore-separated 16-hex token in the stem
        size_t pos = 0;
        while (pos < stem.size()) {
            size_t next = stem.find('_', pos);
            std::string tok = (next == std::string::npos) ? stem.substr(pos) : stem.substr(pos, next - pos);
            if (try_hex16(tok)) return true;
            if (next == std::string::npos) break;
            pos = next + 1;
        }
    }
    return false;
}

// In acz.exe (0x1401950C1..0x1401950E6), built-in extratex IDs are encoded as:
//   low 16 bits  = category (0 = "extratex/manual/", 1 = "extratex/hudfont/", 2 = "extratex/texture/")
//   high 16 bits = index within category (0 = "000.png", 1 = "001.png")
// Therefore, "extratex/hudfont/000.png" has engine ID (0 << 16) | 1 = 0x00000001u,
// and "extratex/hudfont/001.png" has engine ID (1 << 16) | 1 = 0x00010001u.
// We also accept legacy alias 0x00010000u for 000.png.
static inline bool IsBuiltinHudFont000Id(uint32_t ext_id) {
    return (ext_id == 0x00000001u || ext_id == 0x00010000u);
}

static inline bool IsBuiltinHudFont001Id(uint32_t ext_id) {
    return (ext_id == 0x00010001u);
}

static constexpr uint32_t EXTRATEX_ID_MANUAL_000 = 0x00000004u;
static inline bool IsBuiltinManual000Id(uint32_t ext_id) {
    return (ext_id == EXTRATEX_ID_MANUAL_000);
}

static inline bool IsBuiltinPadTexId(uint32_t ext_id, uint32_t* out_idx = nullptr) {
    if ((ext_id & 0xFFFFu) == 2u) {
        uint32_t idx = ext_id >> 16;
        if ((idx >= 1u && idx <= 10u) || idx == 15u || idx == 17u) {
            if (out_idx) *out_idx = idx;
            return true;
        }
    }
    return false;
}

static inline bool IsBuiltinTextureExtId(uint32_t ext_id, uint32_t* out_idx = nullptr) {
    if ((ext_id & 0xFFFFu) == 2u) {
        uint32_t idx = ext_id >> 16;
        if (idx >= 11u && idx <= 31u) {
            if (out_idx) *out_idx = idx;
            return true;
        }
    }
    return false;
}

static void GetDefaultBuiltinTextureDims(uint32_t idx, uint16_t* out_w, uint16_t* out_h) {
    switch (idx) {
        case 11: if (out_w) *out_w = 256; if (out_h) *out_h = 16;  break; // Press any button
        case 12: if (out_w) *out_w = 288; if (out_h) *out_h = 32;  break; // (C)2006 NAMCO
        case 13: if (out_w) *out_w = 448; if (out_h) *out_h = 80;  break; // ACZ Title logo
        case 14: if (out_w) *out_w = 64;  if (out_h) *out_h = 16;  break; // [NEW] tag
        case 15: if (out_w) *out_w = 512; if (out_h) *out_h = 64;  break; // Footer bar
        case 16: if (out_w) *out_w = 496; if (out_h) *out_h = 256; break; // Licensing legal text
        default: if (out_w) *out_w = 0;   if (out_h) *out_h = 0;   break;
    }
}

// Legacy PS2 button TIM2 hashes inside .swa menu archives.
// On the PC port, the developers remapped button sprites via chunk00_layout extratex_idx (1..8 -> extratex/texture/001..008.png)
// and reused the same PS2 TIM2 chunks for different buttons across menus (e.g. AA306350C3C82BBE is used for both Y and B in AircraftSelect.swa).
// Therefore, PS2 button TIM2 hashes must NEVER be bound or matched via TIM2 hash replacement.
static inline bool IsLegacyPs2PadTim2Hash(uint64_t h) {
    switch (h) {
        case 0x731D243962401485ULL: case 0x7A254AB048031FBDULL:
        case 0x901EAE0AA4A02596ULL: case 0xD1652FC7B015CCFEULL:
        case 0xAA306350C3C82BBEULL: case 0xA2A24050B802AC46ULL:
        case 0x6280B18A7D9B323DULL: case 0x8F8A64178D90B865ULL:
        case 0x3BD6FA1FD528A806ULL: case 0x7352CE2EDCC8DECEULL:
        case 0x16B431E093007E7BULL: case 0xC621CD3CEB8CCEC3ULL:
        case 0x39B738F226032297ULL: case 0x5EE5FFDDAB256FFFULL:
        case 0x4585D436D7E1336CULL: case 0x8F9A217B8215BF14ULL:
        case 0x82A5FE09CD6B26FEULL: case 0x37DEED8EFDAEBED6ULL:
        case 0xAC4F249FF4C68439ULL: case 0x200F4DB4911F0091ULL:
        case 0xFB2F5AF80137C3EEULL: case 0xFF685F0EE7DC8656ULL:
        case 0xAD27EC27DAFE7676ULL: case 0x1EE1CAAD9F76BC5EULL:
            return true;
        default:
            return false;
    }
}

// These HUD font GIM hashes are handled natively by acz.exe via extratex/hudfont/000.png (0x00000001)
// and extratex/hudfont/001.png (0x00010001) in GsFlushDrawBatch (+0x1950C4..+0x1950DD), which
// keeps the 1x PS2 VRAM texture in [rbp-0x60] for CropGsTextureRegion (+0x1924E0) when drawing
// dithered Pause Menu / Minimap / MISSION START backgrounds. They must NOT be bound in g_HashToSlot.
static inline bool IsNativeHudFontGimHash(uint64_t h) {
    return (h == 0x1C2024A9952DBC00ULL ||
            h == 0x8C7D0EAD0A3B1421ULL ||
            h == 0x30F023F86A144D5DULL ||
            h == 0xD00645D18395B834ULL ||
            h == 0x9F1B6CEC8FF3284DULL);
}

static bool TryMatchBuiltinExtratexName(const std::string& name_in, uint32_t* out_ext_id) {
    std::string s = name_in;
    for (char& c : s) {
        if (c == '\\') c = '/';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (s.size() > 4 && s.substr(s.size() - 4) == ".png") {
        s = s.substr(0, s.size() - 4);
    }
    if (s.find("extratex/manual/000") != std::string::npos || s == "manual_000" || s == "manual/000") {
        *out_ext_id = EXTRATEX_ID_MANUAL_000;
        return true;
    }
    if (s == "000" || s == "hudfont_000" || s == "hudfont/000" || s.find("extratex/hudfont/000") != std::string::npos) {
        *out_ext_id = 0x00000001u;
        return true;
    }
    if (s == "001" || s == "hudfont_001" || s == "hudfont/001" || s.find("extratex/hudfont/001") != std::string::npos) {
        *out_ext_id = 0x00010001u;
        return true;
    }
    std::string num_str;
    size_t tex_pos = s.find("extratex/texture/");
    if (tex_pos != std::string::npos) {
        num_str = s.substr(tex_pos + 17);
    } else {
        size_t t2_pos = s.find("texture/");
        if (t2_pos != std::string::npos) {
            num_str = s.substr(t2_pos + 8);
        } else if (s.rfind("texture_", 0) == 0) {
            num_str = s.substr(8);
        } else if (!s.empty() && s.find_first_not_of("0123456789") == std::string::npos) {
            num_str = s;
        } else if (s == "btn") {
            num_str = "btn";
        }
    }
    if (!num_str.empty()) {
        if (num_str == "btn") {
            *out_ext_id = (17u << 16) | 2u;
            return true;
        }
        if (num_str.size() <= 3) {
            char* endp = nullptr;
            unsigned long idx = std::strtoul(num_str.c_str(), &endp, 10);
            if (endp != num_str.c_str() && *endp == '\0' && idx >= 1ul && idx <= 31ul) {
                *out_ext_id = (static_cast<uint32_t>(idx) << 16) | 2u;
                return true;
            }
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// Parse Companion <png_stem>.json for Multi-Hash & Built-in Extratex Binding
// Supports both ["HASH1", "HASH2"] and {"hashes": ["HASH1", "HASH2"], "extratex": ["000.png"]}
// ----------------------------------------------------------------------------
static bool ParseCompanionJsonFile(
    const fs::path& json_path,
    std::vector<uint64_t>* out_hashes,
    std::vector<uint32_t>* out_builtin_extratex,
    uint16_t* out_expected_w = nullptr,
    uint16_t* out_expected_h = nullptr
) {
    FILE* f = nullptr;
    fopen_s(&f, json_path.string().c_str(), "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 4 * 1024 * 1024) {
        fclose(f);
        return false;
    }

    std::string content(static_cast<size_t>(len), '\0');
    size_t read_bytes = fread(&content[0], 1, content.size(), f);
    fclose(f);
    content.resize(read_bytes);

    auto add_unique_hash = [&](uint64_t h) {
        if (std::find(out_hashes->begin(), out_hashes->end(), h) == out_hashes->end()) {
            out_hashes->push_back(h);
        }
    };
    auto add_unique_ext = [&](uint32_t ext_id) {
        if (std::find(out_builtin_extratex->begin(), out_builtin_extratex->end(), ext_id) == out_builtin_extratex->end()) {
            out_builtin_extratex->push_back(ext_id);
        }
    };

    size_t scan_limit = content.size();
    const char* glyphs16_pos = std::strstr(content.c_str(), "\"glyphs_16x16\"");
    if (glyphs16_pos != nullptr) {
        scan_limit = static_cast<size_t>(glyphs16_pos - content.c_str());
    }

    size_t i = 0;
    while (i < scan_limit) {
        if (content[i] == '"') {
            ++i;
            std::string token;
            while (i < content.size() && content[i] != '"') {
                if (content[i] == '\\' && i + 1 < content.size()) {
                    ++i;
                }
                token.push_back(content[i]);
                ++i;
            }
            if (i < content.size() && content[i] == '"') ++i;

            // Check if this string is followed by : (a JSON object key)
            size_t k = i;
            while (k < content.size() && (content[k] == ' ' || content[k] == '\t' || content[k] == '\r' || content[k] == '\n')) {
                ++k;
            }
            bool is_json_key = (k < content.size() && content[k] == ':');
            if (is_json_key) continue;

            uint32_t builtin_ext = 0;
            if (TryMatchBuiltinExtratexName(token, &builtin_ext)) {
                add_unique_ext(builtin_ext);
            }

            // Strip .png extension if the user pasted a full filename into the JSON array
            std::string clean_tok = token;
            if (clean_tok.size() > 4) {
                std::string ext_part = clean_tok.substr(clean_tok.size() - 4);
                for (char& c : ext_part) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (ext_part == ".png" || ext_part == ".gim") {
                    clean_tok = clean_tok.substr(0, clean_tok.size() - 4);
                }
            }

            uint64_t h = 0;
            if (TryParseHashFromStem(clean_tok, &h)) {
                add_unique_hash(h);
            }
        } else {
            ++i;
        }
    }

    auto parse_json_dim = [&](const char* key) -> uint16_t {
        const char* pos = std::strstr(content.c_str(), key);
        if (!pos) return 0;
        pos += std::strlen(key);
        while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == '"' || *pos == ':')) {
            ++pos;
        }
        unsigned long v = std::strtoul(pos, nullptr, 10);
        return (v > 0 && v <= 4096) ? static_cast<uint16_t>(v) : 0;
    };
    if (out_expected_w) {
        uint16_t w = parse_json_dim("\"width\"");
        if (w > 0) *out_expected_w = w;
    }
    if (out_expected_h) {
        uint16_t h = parse_json_dim("\"height\"");
        if (h > 0) *out_expected_h = h;
    }

    return (!out_hashes->empty() || !out_builtin_extratex->empty());
}

// ----------------------------------------------------------------------------
// Scan mods/textures for Replacement PNGs (GIMs, Fonts, Families, Master Atlases, Companion .json)
// ----------------------------------------------------------------------------
static void PreloadSharedGlyphsFromCompanionTxt(uint16_t slot_idx, uint64_t hash, const fs::path& png_path);

// Detects aspect-ratio suffixes on PNG filename stems:
//   - "_21x9", "-21x9", or "21x9" -> returns tier 2 (21:9) and strips suffix into out_base_stem
//   - "_16x9", "-16x9", or "16x9" -> returns tier 1 (16:9) and strips suffix into out_base_stem
//   - unsuffixed                  -> returns tier 0 (4:3 default)
static int StripAspectSuffixFromStem(const std::string& stem, std::string* out_base_stem) {
    if (out_base_stem) *out_base_stem = stem;
    std::string low = stem;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto check_suffix = [&](const char* suf) -> bool {
        size_t slen = std::strlen(suf);
        if (low.size() > slen && low.compare(low.size() - slen, slen, suf) == 0) {
            if (out_base_stem) *out_base_stem = stem.substr(0, stem.size() - slen);
            return true;
        }
        return false;
    };
    if (check_suffix("_21x9") || check_suffix("-21x9") || check_suffix("21x9")) return 2;
    if (check_suffix("_16x9") || check_suffix("-16x9") || check_suffix("16x9")) return 1;
    return 0;
}

// Parses a JSON selector file (e.g. mods\textures\pads\pad_cross_a.json) containing:
//   "function": "select_gamepad_glyph", "setting": "GamepadGlyphs",
//   "variants": { "PS": "ps_cross.png", "XBOX": "xbox_a.png", "KB": "kb_enter.png" }
static bool IsStockSentinelVariantName(const std::string& fn) {
    return (fn.empty() || _stricmp(fn.c_str(), "STOCK") == 0 ||
            _stricmp(fn.c_str(), "STOCK_000") == 0 || _stricmp(fn.c_str(), "STOCK_001") == 0);
}

static const char* GetGamepadGlyphsConfigModeStr(int mode) {
    switch (mode) {
        case 1:  return "PS";
        case 2:  return "XBOX";
        case 3:  return "KB";
        default: return "AUTO";
    }
}

static bool TryParseSelectorJsonFile(
    const fs::path& json_path,
    fs::path out_variant_pngs[3], // [0] = PS, [1] = XBOX, [2] = KB
    std::vector<std::string>* out_all_variant_filenames,
    std::vector<uint64_t>* out_hashes,
    std::vector<uint32_t>* out_builtin_ext,
    uint16_t* out_exp_w,
    uint16_t* out_exp_h
) {
    for (int i = 0; i < 3; ++i) out_variant_pngs[i].clear();

    FILE* f = nullptr;
    fopen_s(&f, json_path.string().c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 1024 * 1024) {
        fclose(f);
        return false;
    }
    std::string content(static_cast<size_t>(len), '\0');
    size_t r = fread(&content[0], 1, content.size(), f);
    fclose(f);
    content.resize(r);

    static const char kStrVariants[] = "\"variants\"";
    const char* var_pos = std::strstr(content.c_str(), kStrVariants);
    if (!var_pos) return false;

    const char* brace_open = std::strchr(var_pos, '{');
    const char* brace_close = brace_open ? std::strchr(brace_open, '}') : nullptr;
    if (!brace_open || !brace_close) return false;

    std::string var_block(brace_open + 1, brace_close);
    auto extract_variant_val = [&](const char* key) -> std::string {
        const char* kp = std::strstr(var_block.c_str(), key);
        if (!kp) return {};
        kp += std::strlen(key);
        const char* col = std::strchr(kp, ':');
        if (!col) return {};
        const char* q1 = std::strchr(col, '"');
        if (!q1) return {};
        const char* q2 = std::strchr(q1 + 1, '"');
        if (!q2) return {};
        return std::string(q1 + 1, q2);
    };

    static const char kStrPS[]       = "\"PS\"";
    static const char kStrXBOX[]     = "\"XBOX\"";
    static const char kStrKB[]       = "\"KB\"";
    static const char kStrKEYBOARD[] = "\"KEYBOARD\"";
    std::string ps_fn   = extract_variant_val(kStrPS);
    std::string xbox_fn = extract_variant_val(kStrXBOX);
    std::string kb_fn   = extract_variant_val(kStrKB);
    if (kb_fn.empty()) kb_fn = extract_variant_val(kStrKEYBOARD);

    if (ps_fn.empty() && xbox_fn.empty() && kb_fn.empty()) return false;

    if (out_all_variant_filenames) {
        if (!IsStockSentinelVariantName(ps_fn))   out_all_variant_filenames->push_back(ps_fn);
        if (!IsStockSentinelVariantName(xbox_fn)) out_all_variant_filenames->push_back(xbox_fn);
        if (!IsStockSentinelVariantName(kb_fn))   out_all_variant_filenames->push_back(kb_fn);
    }

    if (!ParseCompanionJsonFile(json_path, out_hashes, out_builtin_ext, out_exp_w, out_exp_h)) {
        return false;
    }

    const fs::path parent = json_path.parent_path();
    if (!IsStockSentinelVariantName(ps_fn))   out_variant_pngs[0] = parent / ps_fn;
    if (!IsStockSentinelVariantName(xbox_fn)) out_variant_pngs[1] = parent / xbox_fn;
    if (!IsStockSentinelVariantName(kb_fn))   out_variant_pngs[2] = parent / kb_fn;

    return (!out_variant_pngs[0].empty() || !out_variant_pngs[1].empty() || !out_variant_pngs[2].empty());
}

static void ScanTexturesDirectory() {
    g_Slots.clear();
    g_Slots.resize(1); // Slot 0 is unused so indices start at 1
    g_HashToSlot.clear();
    g_FamilyHashToSlot.clear();
    g_MasterHashToSlot.clear();
    g_BuiltinExtratexSlot[0].store(0, std::memory_order_relaxed);
    g_BuiltinExtratexSlot[1].store(0, std::memory_order_relaxed);
    g_BuiltinManual000Slot.store(0, std::memory_order_relaxed);
    for (size_t i = 0; i < 18; ++i) {
        g_BuiltinPadExtratexSlot[i].store(0, std::memory_order_relaxed);
    }
    for (size_t i = 0; i < 32; ++i) {
        g_BuiltinTextureExtratexSlot[i].store(0, std::memory_order_relaxed);
    }
    LockFreeClearContentHashTable();
    for (size_t i = 0; i <= MAX_CUSTOM_TEXTURES; ++i) {
        g_SlotLoadedTex[i].store(nullptr, std::memory_order_relaxed);
        g_SlotLoadFailed[i].store(false, std::memory_order_relaxed);
        g_SlotAvailMask[i].store(0, std::memory_order_relaxed);
        g_SlotIsPadSelector[i].store(false, std::memory_order_relaxed);
        for (int t = 0; t < 3; ++t) {
            g_SlotLoadedTexTier[t][i].store(nullptr, std::memory_order_relaxed);
            g_SlotLoadFailedTier[t][i].store(false, std::memory_order_relaxed);
            g_PadSlotOrigHdr[t][i].store(nullptr, std::memory_order_relaxed);
            g_PadSlotOrigHandle[t][i].store(0xFFFFFFFFu, std::memory_order_relaxed);
        }
        g_SlotExpectedW[i].store(0, std::memory_order_relaxed);
        g_SlotExpectedH[i].store(0, std::memory_order_relaxed);
        g_SlotAltExpectedW[i].store(0, std::memory_order_relaxed);
        g_SlotAltExpectedH[i].store(0, std::memory_order_relaxed);
        g_SlotAllowHalfDim[i].store(false, std::memory_order_relaxed);
    }

    std::error_code ec;
    if (!fs::exists(g_TexturesDir, ec)) {
        fs::create_directories(g_TexturesDir, ec);
        LogMsg("[ACZ_HDTex] Created textures directory: %s", g_TexturesDir.string().c_str());
        return;
    }

    std::vector<fs::path> discovered_pngs;
    std::vector<fs::path> discovered_jsons;
    auto collect_dir_recursive = [&](auto& self, const fs::path& cur_dir) -> void {
        std::wstring search_pat = (cur_dir / L"*").wstring();
        WIN32_FIND_DATAW wfd{};
        HANDLE hFind = FindFirstFileW(search_pat.c_str(), &wfd);
        if (hFind == INVALID_HANDLE_VALUE) return;

        std::vector<fs::path> subdirs;
        std::vector<fs::path> png_files;
        std::vector<fs::path> json_files;
        do {
            if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
            if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                // Skip converter raw source folder 'spw' and autonomous '.dds_cache' folder
                if (_wcsicmp(wfd.cFileName, L"spw") == 0 || _wcsicmp(wfd.cFileName, L".dds_cache") == 0) continue;
                subdirs.push_back(cur_dir / wfd.cFileName);
            } else {
                fs::path fp = cur_dir / wfd.cFileName;
                std::wstring wext = fp.extension().wstring();
                if (_wcsicmp(wext.c_str(), L".png") == 0) {
                    png_files.push_back(std::move(fp));
                } else if (_wcsicmp(wext.c_str(), L".json") == 0) {
                    json_files.push_back(std::move(fp));
                }
            }
        } while (FindNextFileW(hFind, &wfd));
        FindClose(hFind);

        std::sort(subdirs.begin(), subdirs.end());
        std::sort(png_files.begin(), png_files.end());
        std::sort(json_files.begin(), json_files.end());

        if (!png_files.empty() || !json_files.empty()) {
            LogMsg("[ACZ_HDTex] Scanning directory: %s (%zu PNG files, %zu JSON files)",
                   cur_dir.string().c_str(), png_files.size(), json_files.size());
            for (auto& pf : png_files) {
                discovered_pngs.push_back(std::move(pf));
            }
            for (auto& jf : json_files) {
                discovered_jsons.push_back(std::move(jf));
            }
        }
        for (const auto& sd : subdirs) {
            self(self, sd);
        }
    };
    collect_dir_recursive(collect_dir_recursive, g_TexturesDir);

    size_t count = 0;
    size_t total_bound_hashes = 0;
    std::unordered_set<std::string> selector_managed_pngs;
    std::unordered_set<uint64_t>    pad_selector_hashes;

    // Pass 1: Process standalone selector JSON files (e.g. mods\textures\pads\pad_*.json)
    for (const fs::path& jp : discovered_jsons) {
        fs::path var_pngs[3]; // [0] = PS, [1] = XBOX, [2] = KB
        std::vector<std::string> variant_fns;
        std::vector<uint64_t> target_hashes;
        std::vector<uint32_t> target_builtin_ext;
        uint16_t json_exp_w = 0, json_exp_h = 0;
        if (!TryParseSelectorJsonFile(jp, var_pngs, &variant_fns, &target_hashes, &target_builtin_ext, &json_exp_w, &json_exp_h)) {
            continue;
        }
        for (const std::string& vfn : variant_fns) {
            fs::path vp = (jp.parent_path() / vfn).lexically_normal();
            std::string vp_low = vp.string();
            for (char& c : vp_low) {
                if (c == '/') c = '\\';
                else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            selector_managed_pngs.insert(vp_low);
        }

        bool is_manual_000_selector = false;
        for (uint32_t ext_id : target_builtin_ext) {
            if (IsBuiltinManual000Id(ext_id)) {
                is_manual_000_selector = true;
                break;
            }
        }

        fs::path valid_pngs[3];
        uint8_t avail_mask = 0;
        fs::path first_valid;
        for (int t = 0; t < 3; ++t) {
            if (!var_pngs[t].empty() && fs::exists(var_pngs[t], ec) && fs::is_regular_file(var_pngs[t], ec)) {
                valid_pngs[t] = var_pngs[t];
                avail_mask |= static_cast<uint8_t>(1u << t);
                if (first_valid.empty()) first_valid = var_pngs[t];
            }
        }
        if (first_valid.empty() || (target_hashes.empty() && target_builtin_ext.empty())) {
            continue;
        }
        if (!is_manual_000_selector) {
            for (int t = 0; t < 3; ++t) {
                if (valid_pngs[t].empty()) {
                    valid_pngs[t] = !valid_pngs[1].empty() ? valid_pngs[1] : first_valid;
                    avail_mask |= static_cast<uint8_t>(1u << t);
                }
            }
        }
        if (g_Slots.size() >= MAX_CUSTOM_TEXTURES) break;

        uint64_t primary_hash = !target_hashes.empty() ? target_hashes[0] : static_cast<uint64_t>(target_builtin_ext[0]);
        uint16_t slot_idx = static_cast<uint16_t>(g_Slots.size());
        CustomSlotInfo info;
        info.hash             = primary_hash;
        info.png_path         = first_valid.string();
        info.png_path_tier[0] = valid_pngs[0].string();
        info.png_path_tier[1] = valid_pngs[1].string();
        info.png_path_tier[2] = valid_pngs[2].string();
        g_Slots.push_back(std::move(info));
        g_SlotAvailMask[slot_idx].store(avail_mask, std::memory_order_release);
        g_SlotIsPadSelector[slot_idx].store(true, std::memory_order_release);
        ++count;

        if (json_exp_w > 0 && json_exp_h > 0) {
            g_SlotExpectedW[slot_idx].store(json_exp_w, std::memory_order_release);
            g_SlotExpectedH[slot_idx].store(json_exp_h, std::memory_order_release);
        }
        target_hashes.erase(
            std::remove_if(target_hashes.begin(), target_hashes.end(),
                           [](uint64_t h) { return IsLegacyPs2PadTim2Hash(h); }),
            target_hashes.end());
        for (uint32_t ext_id : target_builtin_ext) {
            uint32_t pad_idx = 0;
            if (IsBuiltinManual000Id(ext_id)) {
                g_BuiltinManual000Slot.store(slot_idx, std::memory_order_release);
                if (json_exp_w == 0) g_SlotExpectedW[slot_idx].store(1920u, std::memory_order_release);
                if (json_exp_h == 0) g_SlotExpectedH[slot_idx].store(1080u, std::memory_order_release);
            } else if (IsBuiltinPadTexId(ext_id, &pad_idx)) {
                g_BuiltinPadExtratexSlot[pad_idx].store(slot_idx, std::memory_order_release);
                if (pad_idx == 15u) {
                    g_BuiltinTextureExtratexSlot[15].store(slot_idx, std::memory_order_release);
                    static const uint64_t kBar015Hashes[4] = {
                        0x04F9AC4DBD57B9D6ULL, 0xB7EC6B6B1D13C8C9ULL,
                        0xA779AB3E20AB7C4FULL, 0x03BB0D1EE77250E6ULL
                    };
                    for (uint64_t bh : kBar015Hashes) {
                        if (std::find(target_hashes.begin(), target_hashes.end(), bh) == target_hashes.end()) {
                            target_hashes.push_back(bh);
                        }
                    }
                }
                uint16_t def_w = (pad_idx == 15u) ? 512u : ((pad_idx == 17u) ? 128u : 32u);
                uint16_t def_h = (pad_idx == 15u) ? 64u  : ((pad_idx == 17u) ? 128u : 32u);
                if (json_exp_w == 0) g_SlotExpectedW[slot_idx].store(def_w, std::memory_order_release);
                if (json_exp_h == 0) g_SlotExpectedH[slot_idx].store(def_h, std::memory_order_release);
            }
        }
        for (uint64_t h : target_hashes) {
            g_HashToSlot[h] = slot_idx;
            LockFreeInsertContentHash(h, slot_idx);
            pad_selector_hashes.insert(h);
            ++total_bound_hashes;
        }
        LogMsg("[ACZ_HDTex] Registered Gamepad/KB Glyph Selector [Slot #%u, Mode=%s] (%zu hashes, %zu extratex) -> PS:%s | XBOX:%s | KB:%s (from %s)",
               slot_idx, GetGamepadGlyphsConfigModeStr(g_GamepadGlyphsMode),
               target_hashes.size(), target_builtin_ext.size(),
               valid_pngs[0].empty() ? "STOCK" : valid_pngs[0].filename().string().c_str(),
               valid_pngs[1].empty() ? "STOCK" : valid_pngs[1].filename().string().c_str(),
               valid_pngs[2].empty() ? "STOCK" : valid_pngs[2].filename().string().c_str(),
               jp.filename().string().c_str());
    }

    // Pass 2: Process PNG files (including _16x9 and _21x9 aspect-ratio variants)
    for (const fs::path& p : discovered_pngs) {
        std::string p_low = p.lexically_normal().string();
        for (char& c : p_low) {
            if (c == '/') c = '\\';
            else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (selector_managed_pngs.find(p_low) != selector_managed_pngs.end()) {
            continue;
        }

        std::string raw_stem = p.stem().string();
        std::string stem;
        int aspect_tier = StripAspectSuffixFromStem(raw_stem, &stem);
        std::string stem_low = stem;
        for (char& c : stem_low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        // Legacy v5 converter compatibility: skip standalone d00645d18395b834.png registration
        // (if present alongside 000.png, 000.png will automatically use d00645d18395b834.png as its source).
        if (stem_low == "d00645d18395b834") {
            fs::path sibling_000 = p.parent_path() / "000.png";
            if (fs::exists(sibling_000, ec)) {
                continue;
            }
        }

        std::vector<uint64_t> target_hashes;
        std::vector<uint32_t> target_builtin_ext;

        // 1. Check if filename stem itself contains a 16-hex hash or MASTER_ prefix
        uint64_t stem_hash = 0;
        if (TryParseHashFromStem(stem, &stem_hash)) {
            if (!IsNativeHudFontGimHash(stem_hash)) {
                target_hashes.push_back(stem_hash);
            } else if (stem_hash == 0xD00645D18395B834ULL || stem_hash == 0x1C2024A9952DBC00ULL || stem_hash == 0x8C7D0EAD0A3B1421ULL) {
                target_builtin_ext.push_back(0x00000001u);
            } else if (stem_hash == 0x9F1B6CEC8FF3284DULL) {
                target_builtin_ext.push_back(0x00010001u);
            }
        }

        // 2. Check if filename stem is a built-in extratex name ("000.png" or "001.png")
        uint32_t stem_ext_id = 0;
        if (TryMatchBuiltinExtratexName(stem, &stem_ext_id)) {
            target_builtin_ext.push_back(stem_ext_id);
        }

        // 3. Check for companion <png_stem>.json or <base_stem>.json in the same folder
        fs::path json_path = p;
        json_path.replace_extension(".json");
        if (!fs::exists(json_path, ec) && aspect_tier != 0) {
            json_path = p.parent_path() / (stem + ".json");
        }
        bool has_companion_json = false;
        uint16_t json_exp_w = 0, json_exp_h = 0;
        if (fs::exists(json_path, ec) && fs::is_regular_file(json_path, ec)) {
            has_companion_json = ParseCompanionJsonFile(json_path, &target_hashes, &target_builtin_ext, &json_exp_w, &json_exp_h);
        }

        // Filter out native HUD font GIM hashes so they never override [rbp-0x60] in Hook_LookupOrCreateGsTexture
        target_hashes.erase(
            std::remove_if(target_hashes.begin(), target_hashes.end(), IsNativeHudFontGimHash),
            target_hashes.end()
        );

        // Ensure menu folder PNGs never override gamepad glyphs managed in mods\textures\pads\!
        if (!target_hashes.empty()) {
            bool is_pad_hash = false;
            for (uint64_t th : target_hashes) {
                if (IsLegacyPs2PadTim2Hash(th) || pad_selector_hashes.find(th) != pad_selector_hashes.end()) {
                    is_pad_hash = true;
                    break;
                }
            }
            if (is_pad_hash) {
                fs::remove(p, ec);
                if (has_companion_json) {
                    fs::remove(json_path, ec);
                }
                LogMsg("[ACZ_HDTex] Ignored & cleaned duplicate menu gamepad glyph '%s' (exclusively managed by mods\\textures\\pads\\)",
                       p.filename().string().c_str());
                continue;
            }
        }

        if (target_hashes.empty() && target_builtin_ext.empty()) {
            LogMsg("[ACZ_HDTex] Skipping PNG without 16-digit hex hash, companion .json, or 000/001.png name: %s",
                   p.filename().string().c_str());
            continue;
        }

        bool is_master_atlas = (stem_low.rfind("master_", 0) == 0);
        bool is_family_atlas = (!is_master_atlas && stem_low.rfind("family_", 0) == 0);
        auto& target_map = is_master_atlas ? g_MasterHashToSlot : (is_family_atlas ? g_FamilyHashToSlot : g_HashToSlot);

        uint64_t primary_hash = !target_hashes.empty() ? target_hashes[0] : static_cast<uint64_t>(target_builtin_ext[0]);

        // If 000.png is being registered and a v5 d00645d18395b834.png exists in the same folder,
        // use d00645d18395b834.png (the true In-Flight & Menu HUD atlas) as the source file!
        fs::path effective_png_path = p;
        if (!target_builtin_ext.empty() && IsBuiltinHudFont000Id(target_builtin_ext[0])) {
            fs::path v5_in_flight = p.parent_path() / "d00645d18395b834.png";
            if (fs::exists(v5_in_flight, ec) && fs::is_regular_file(v5_in_flight, ec)) {
                effective_png_path = v5_in_flight;
            }
        }

        // If any of target_hashes already has a registered slot (e.g. binding _16x9.png or _21x9.png onto an existing 4:3 slot),
        // attach the aspect-ratio variant directly to that slot and bind all companion hashes!
        uint16_t existing_slot_idx = 0;
        if (!is_master_atlas && !is_family_atlas) {
            for (uint64_t h : target_hashes) {
                auto it = target_map.find(h);
                if (it != target_map.end() && it->second > 0 && it->second < g_Slots.size()) {
                    existing_slot_idx = it->second;
                    break;
                }
            }
        }
        if (existing_slot_idx > 0) {
            uint16_t slot_idx = existing_slot_idx;
            CustomSlotInfo& existing_info = g_Slots[slot_idx];
            existing_info.mem_png.clear();
            existing_info.png_path_tier[aspect_tier] = effective_png_path.string();
            if (aspect_tier == 0 || existing_info.png_path.empty()) {
                existing_info.png_path = effective_png_path.string();
            }
            uint8_t prev_mask = g_SlotAvailMask[slot_idx].load(std::memory_order_relaxed);
            g_SlotAvailMask[slot_idx].store(static_cast<uint8_t>(prev_mask | (1u << aspect_tier)), std::memory_order_release);
            if (json_exp_w > 0 && json_exp_h > 0 && g_SlotExpectedW[slot_idx].load(std::memory_order_relaxed) == 0) {
                g_SlotExpectedW[slot_idx].store(json_exp_w, std::memory_order_release);
                g_SlotExpectedH[slot_idx].store(json_exp_h, std::memory_order_release);
            }
            for (uint64_t h : target_hashes) {
                target_map[h] = slot_idx;
                LockFreeInsertContentHash(h, slot_idx);
            }
            ++count;
            LogMsg("[ACZ_HDTex] Attached %s aspect variant to [Slot #%u] Hash=%016llX -> %s",
                   aspect_tier == 2 ? "21:9 (21x9)" : (aspect_tier == 1 ? "16:9 (16x9)" : "4:3"),
                   slot_idx, static_cast<unsigned long long>(primary_hash), p.filename().string().c_str());
            continue;
        }

        if (g_Slots.size() >= MAX_CUSTOM_TEXTURES) {
            LogMsg("[ACZ_HDTex] WARNING: Maximum custom texture slots (%u) reached!", MAX_CUSTOM_TEXTURES);
            break;
        }

        uint16_t slot_idx = static_cast<uint16_t>(g_Slots.size());
        CustomSlotInfo info;
        info.hash                       = primary_hash;
        info.png_path                   = effective_png_path.string();
        info.png_path_tier[aspect_tier] = effective_png_path.string();
        info.loaded_tex                 = nullptr;
        info.load_failed                = false;

        g_Slots.push_back(std::move(info));
        g_SlotAvailMask[slot_idx].store(static_cast<uint8_t>(1u << aspect_tier), std::memory_order_release);
        ++count;

        if (json_exp_w > 0 && json_exp_h > 0) {
            g_SlotExpectedW[slot_idx].store(json_exp_w, std::memory_order_release);
            g_SlotExpectedH[slot_idx].store(json_exp_h, std::memory_order_release);
        }
        for (uint64_t h : target_hashes) {
            target_map[h] = slot_idx;
            if (!is_master_atlas && !is_family_atlas) {
                LockFreeInsertContentHash(h, slot_idx);
            }
            ++total_bound_hashes;
        }
        for (uint32_t ext_id : target_builtin_ext) {
            uint32_t pad_idx = 0;
            uint32_t tex_idx = 0;
            if (IsBuiltinManual000Id(ext_id)) {
                g_BuiltinManual000Slot.store(slot_idx, std::memory_order_release);
                if (json_exp_w == 0) g_SlotExpectedW[slot_idx].store(1920u, std::memory_order_release);
                if (json_exp_h == 0) g_SlotExpectedH[slot_idx].store(1080u, std::memory_order_release);
            } else if (IsBuiltinHudFont000Id(ext_id)) {
                g_BuiltinExtratexSlot[0].store(slot_idx, std::memory_order_release);
                g_SlotExpectedW[slot_idx].store(256u, std::memory_order_release);
                g_SlotExpectedH[slot_idx].store(256u, std::memory_order_release);
            } else if (IsBuiltinHudFont001Id(ext_id)) {
                g_BuiltinExtratexSlot[1].store(slot_idx, std::memory_order_release);
                g_SlotExpectedW[slot_idx].store(256u, std::memory_order_release);
                g_SlotExpectedH[slot_idx].store(256u, std::memory_order_release);
            } else if (IsBuiltinPadTexId(ext_id, &pad_idx)) {
                if (g_BuiltinPadExtratexSlot[pad_idx].load(std::memory_order_relaxed) == 0) {
                    g_BuiltinPadExtratexSlot[pad_idx].store(slot_idx, std::memory_order_release);
                    uint16_t def_dim = (pad_idx == 17u) ? 128u : 32u;
                    if (json_exp_w == 0) g_SlotExpectedW[slot_idx].store(def_dim, std::memory_order_release);
                    if (json_exp_h == 0) g_SlotExpectedH[slot_idx].store(def_dim, std::memory_order_release);
                }
            } else if (IsBuiltinTextureExtId(ext_id, &tex_idx)) {
                if (tex_idx < 32) {
                    if (g_BuiltinTextureExtratexSlot[tex_idx].load(std::memory_order_relaxed) == 0) {
                        g_BuiltinTextureExtratexSlot[tex_idx].store(slot_idx, std::memory_order_release);
                        uint16_t exp_w = 0, exp_h = 0;
                        GetDefaultBuiltinTextureDims(tex_idx, &exp_w, &exp_h);
                        if (json_exp_w == 0 && exp_w > 0) g_SlotExpectedW[slot_idx].store(exp_w, std::memory_order_release);
                        if (json_exp_h == 0 && exp_h > 0) g_SlotExpectedH[slot_idx].store(exp_h, std::memory_order_release);
                        LogMsg("[ACZ_HDTex] Bound built-in extratex/texture/%03u.png to Slot #%u (%ux%u)",
                               tex_idx, slot_idx,
                               g_SlotExpectedW[slot_idx].load(std::memory_order_relaxed),
                               g_SlotExpectedH[slot_idx].load(std::memory_order_relaxed));
                    }
                }
            }
        }

        // Auto-bind known TIM2 menu hashes to corresponding built-in extratex texture slots
        for (uint64_t th : target_hashes) {
            uint32_t auto_tex_idx = 0;
            if (th == 0x3CFCF1D871552129ULL || th == 0xCC972B5039A12E5AULL || th == 0xF7A5F8E6B4A645BEULL) {
                auto_tex_idx = 11; // [ PRESS ANY BUTTON ]
            } else if (th == 0x4290455F2822D245ULL || th == 0x14A2EC170150DA16ULL || th == 0x7C36847EB984ABB1ULL) {
                auto_tex_idx = 12; // (C)2006 NAMCO
            } else if (th == 0x795A02DE9053F52FULL || th == 0x44D477F64514B65CULL || th == 0xFB4142D38FB0E60AULL) {
                auto_tex_idx = 13; // ACZ Title Logo
            } else if (th == 0x5FE26278F9447E6FULL || th == 0xD017F35C51844095ULL) {
                auto_tex_idx = 14; // [NEW] badge
            } else if (th == 0x04F9AC4DBD57B9D6ULL || th == 0xB7EC6B6B1D13C8C9ULL || th == 0xA779AB3E20AB7C4FULL || th == 0x03BB0D1EE77250E6ULL) {
                auto_tex_idx = 15; // Menu / Debriefing top & bottom bar (015.png)
            } else if (th == 0x134990740A53EDA7ULL || th == 0x431F53103CDACF17ULL || th == 0x1B59E715D55F199AULL) {
                auto_tex_idx = 16; // WakeLogo licensing legal text
            }
            if (auto_tex_idx > 0 && g_BuiltinTextureExtratexSlot[auto_tex_idx].load(std::memory_order_relaxed) == 0) {
                g_BuiltinTextureExtratexSlot[auto_tex_idx].store(slot_idx, std::memory_order_release);
                uint16_t exp_w = 0, exp_h = 0;
                GetDefaultBuiltinTextureDims(auto_tex_idx, &exp_w, &exp_h);
                if (json_exp_w == 0 && exp_w > 0) g_SlotExpectedW[slot_idx].store(exp_w, std::memory_order_release);
                if (json_exp_h == 0 && exp_h > 0) g_SlotExpectedH[slot_idx].store(exp_h, std::memory_order_release);
                LogMsg("[ACZ_HDTex] Auto-bound built-in extratex/texture/%03u.png to Slot #%u from content hash %016llX",
                       auto_tex_idx, slot_idx, static_cast<unsigned long long>(th));
            }
        }

        if (has_companion_json && target_hashes.size() > 1) {
            std::string hash_list_str;
            for (size_t hi = 0; hi < target_hashes.size(); ++hi) {
                char buf[24];
                snprintf(buf, sizeof(buf), "%s%016llX", (hi > 0 ? ", " : ""), static_cast<unsigned long long>(target_hashes[hi]));
                hash_list_str += buf;
            }
            LogMsg("[ACZ_HDTex] Registered Multi-Hash JSON replacement [Slot #%u] (%zu hashes: [%s]) -> %s (+ %s)",
                   slot_idx, target_hashes.size(), hash_list_str.c_str(),
                   p.filename().string().c_str(), json_path.filename().string().c_str());
        } else if (!target_builtin_ext.empty()) {
            LogMsg("[ACZ_HDTex] Registered Built-in Extratex replacement [Slot #%u] Extratex=0x%08X (Hashes=%zu) -> %s",
                   slot_idx, target_builtin_ext[0], target_hashes.size(), p.filename().string().c_str());
        } else {
            LogMsg("[ACZ_HDTex] Registered %sreplacement [Slot #%u] Hash=%016llX -> %s",
                   is_master_atlas ? "Master Font/GIM Atlas " : (is_family_atlas ? "Font Family " : ""),
                   slot_idx, static_cast<unsigned long long>(primary_hash), p.filename().string().c_str());
        }

        if ((g_AutoShareFontGlyphs || is_master_atlas) && !target_hashes.empty()) {
            if (is_master_atlas || is_family_atlas || stem_low.find("font") != std::string::npos) {
                PreloadSharedGlyphsFromCompanionTxt(slot_idx, primary_hash, p);
            }
        }
    }

    LogMsg("[ACZ_HDTex] Total registered PNG replacements: %zu files (%zu bound hashes)", count, total_bound_hashes);
}

struct DdsPrebuildJob {
    std::string png_path;
    bool        is_hud_000_001 = false;
};

static void PrebuildDdsCacheOnStartup() {
    if (!g_EnableDdsCache) return;

    std::error_code ec;
    fs::create_directories(g_DdsCacheDir, ec);

    std::vector<DdsPrebuildJob> jobs;
    std::unordered_set<std::string> seen_keys;
    uint16_t hud0 = g_BuiltinExtratexSlot[0].load(std::memory_order_relaxed);
    uint16_t hud1 = g_BuiltinExtratexSlot[1].load(std::memory_order_relaxed);

    // Collect all Master/Family slot indices so we skip them (they are CPU-sliced glyph atlases)
    std::unordered_set<uint16_t> font_atlas_slots;
    for (const auto& kv : g_MasterHashToSlot) font_atlas_slots.insert(kv.second);
    for (const auto& kv : g_FamilyHashToSlot) font_atlas_slots.insert(kv.second);

    for (size_t s = 1; s < g_Slots.size(); ++s) {
        uint16_t slot_idx = static_cast<uint16_t>(s);
        if (font_atlas_slots.find(slot_idx) != font_atlas_slots.end()) continue;

        const CustomSlotInfo& slot = g_Slots[slot_idx];
        if (!slot.mem_png.empty()) continue;
        bool is_hud = (slot_idx == hud0 || slot_idx == hud1);

        for (int t = 0; t < 3; ++t) {
            const std::string& p = slot.png_path_tier[t];
            if (p.empty()) continue;

            if (seen_keys.insert(p).second) {
                jobs.push_back({ p, is_hud });
            }
        }
    }

    if (jobs.empty()) return;

    // Filter down to only jobs whose .dds cache file is missing or outdated, and track expected .dds filenames
    std::vector<DdsPrebuildJob> pending_jobs;
    std::unordered_set<std::string> expected_dds_names;
    expected_dds_names.reserve(jobs.size() * 2u);
    size_t already_cached = 0;
    for (const auto& j : jobs) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExA(j.png_path.c_str(), GetFileExInfoStandard, &fad)) continue;
        uint64_t mtime = (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                         static_cast<uint64_t>(fad.ftLastWriteTime.dwLowDateTime);
        uint32_t fsize = fad.nFileSizeLow;
        fs::path dds_p = GetDdsCachePathForPng(j.png_path);
        std::string fname_lower = dds_p.filename().string();
        for (char& c : fname_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        expected_dds_names.insert(fname_lower);
        if (IsDdsCacheFileValid(dds_p, mtime, fsize)) {
            ++already_cached;
        } else {
            pending_jobs.push_back(j);
        }
    }

    // Automatically clean up any old duplicate/orphaned .dds files in .dds_cache
    size_t pruned_duplicates = 0;
    for (const auto& entry : fs::directory_iterator(g_DdsCacheDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string fn = entry.path().filename().string();
        for (char& c : fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (fn.size() > 4 && fn.substr(fn.size() - 4) == ".dds") {
            if (expected_dds_names.find(fn) == expected_dds_names.end()) {
                std::error_code rm_ec;
                if (fs::remove(entry.path(), rm_ec)) {
                    ++pruned_duplicates;
                }
            }
        }
    }
    if (pruned_duplicates > 0) {
        LogMsg("[ACZ_HDTex] [DDSCache] Cleaned up %zu obsolete/duplicate .dds files from %s.",
               pruned_duplicates, g_DdsCacheDir.string().c_str());
    }

    if (pending_jobs.empty()) {
        LogMsg("[ACZ_HDTex] [DDSCache] All %zu DDS cache files in %s are up to date (0 ms).",
               already_cached, g_DdsCacheDir.string().c_str());
        return;
    }

    uint64_t t0 = GetTickCount64();
    unsigned int hw_threads = std::thread::hardware_concurrency();
    size_t num_threads = std::clamp<size_t>(hw_threads > 0 ? hw_threads : 4u, 2u, 16u);
    if (num_threads > pending_jobs.size()) num_threads = pending_jobs.size();

    LogMsg("[ACZ_HDTex] [DDSCache] Compiling %zu new/modified PNG textures into DDS cache (%zu already cached) using %zu worker threads...",
           pending_jobs.size(), already_cached, num_threads);

    std::atomic<size_t> next_job{0};
    std::atomic<size_t> built_count{0};
    std::vector<std::thread> workers;
    workers.reserve(num_threads);

    for (size_t ti = 0; ti < num_threads; ++ti) {
        workers.emplace_back([&]() {
            HRESULT hr_com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (;;) {
                size_t idx = next_job.fetch_add(1, std::memory_order_relaxed);
                if (idx >= pending_jobs.size()) break;
                const DdsPrebuildJob& job = pending_jobs[idx];
                bool rebuilt = false;
                if (BuildOrUpdateDdsCacheForPng(job.png_path, job.is_hud_000_001, nullptr, &rebuilt)) {
                    if (rebuilt) built_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (SUCCEEDED(hr_com)) {
                CoUninitialize();
            }
        });
    }
    for (auto& th : workers) {
        if (th.joinable()) th.join();
    }

    uint64_t elapsed_ms = GetTickCount64() - t0;
    LogMsg("[ACZ_HDTex] [DDSCache] Completed: %zu newly compiled, %zu already cached in %llu ms (%zu threads).",
           built_count.load(), already_cached, static_cast<unsigned long long>(elapsed_ms), num_threads);
}

// ----------------------------------------------------------------------------
// Read mods\acz_hdtex.ini Configuration (Dedicated Texture Replacer & Dumper settings)
// ----------------------------------------------------------------------------
static void LoadConfigIni() {
    fs::path enh_dir = g_ModsDir / "enhancer";
    std::error_code ec;
    fs::create_directories(enh_dir, ec);
    fs::path ini_path = enh_dir / "acz_hdtex.ini";
    if (!fs::exists(ini_path, ec)) {
        fs::path fallback_ini = g_ModsDir / "acz_hdtex.ini";
        if (fs::exists(fallback_ini, ec)) {
            ini_path = fallback_ini;
        }
    }
    if (!fs::exists(ini_path, ec)) {
        FILE* f = nullptr;
        fopen_s(&f, ini_path.string().c_str(), "w");
        if (f) {
            fprintf(f,
                "[ACZ_HDTex]\n"
                "; Ace Combat Zero — HD Texture & Font Replacer Configuration (%s)\n"
                "; Note: Memory dumper and QoL settings are located in mods\\QoL\\memdump.ini\n"
                "\n"
                "; EnableLogging: Write [ACZ_HDTex] events directly into modloader.log\n"
                "EnableLogging=1\n"
                "\n"
                "; DumpAllGims: Dump loaded textures (into mods\\textures_dump\\) and font atlases\n"
                ";              (into mods\\textures_dump\\fonts\\)\n"
                "DumpAllGims=0\n"
                "\n"
                "; DumpReplacedTextures: When DumpAllGims=1, set to 0 to skip dumping textures and fonts\n"
                ";                       that are already replaced in mods\\textures\\ (set to 1 to dump all)\n"
                "DumpReplacedTextures=0\n"
                "\n"
                "; DumpRawGimFiles: When DumpAllGims=1, set to 0 to dump only .png (and .txt for fonts)\n"
                ";                  without raw .gim / .bin files (set to 1 to also dump .gim / .bin)\n"
                "DumpRawGimFiles=0\n"
                "\n"
                "; HotReload: Re-read PNG files from disk when a texture is recreated\n"
                "HotReload=0\n"
                "\n"
                "; AutoShareFontGlyphs: Automatically share HD glyph textures from Master Atlases, Family\n"
                ";                      Atlases, and replaced dynamic fonts matching by ExactHash / TileHash.\n"
                "AutoShareFontGlyphs=1\n"
                "\n"
                "; DumpMasterFontAtlas: When DumpAllGims=1, combine ALL seen dynamic font glyphs per size\n"
                ";                      into single unified MASTER_FONT_32x32.png + .txt and\n"
                ";                      MASTER_FONT_16x16.png + .txt (plus MASTER_GIM_16x16.png) in\n"
                ";                      mods\\textures_dump\\fonts\\ for maximally simple editing!\n"
                "DumpMasterFontAtlas=1\n"
                "\n"
                "; DumpMergedFamilyAtlases: When DumpAllGims=1, set to 1 if you also want per-typeface\n"
                ";                          FAMILY_<HASH>_font_<dim>x<dim>.png files (0 = off, keeps folder clean)\n"
                "DumpMergedFamilyAtlases=0\n"
                "\n"
                "; DumpIndividualFontAtlases: When DumpAllGims=1, set to 1 if you also want individual\n"
                ";                            <HASH>_font_<N>gl_<dim>x<dim>.png files (0 = off, keeps folder clean)\n"
                "DumpIndividualFontAtlases=0\n"
                "\n"
                "; ShareCharCodeToSingleLineSubtitles: If set to 1, 1-line subtitle blocks (num_strings == 1)\n"
                ";                                     without their own family replacement can fallback to\n"
                ";                                     matching by Unicode char_code from any replaced master\n"
                ";                                     font, while multi-string UI/menu fonts remain isolated.\n"
                "ShareCharCodeToSingleLineSubtitles=0\n"
                "\n"
                "; ForceGlobalCharCodeSharing: If set to 1, forces Unicode char_code sharing across all fonts\n"
                ";                             of the same tile dimension (use 0 to keep distinct typefaces separate)\n"
                "ForceGlobalCharCodeSharing=0\n"
                "\n"
                "; DumpSingleLineSubtitles: When DumpIndividualFontAtlases=1, set to 0 to skip redundant\n"
                ";                          1-line subtitle atlases\n"
                "DumpSingleLineSubtitles=0\n"
                "\n"
                "; GamepadGlyphs: Input prompt & manual diagram glyph mode:\n"
                ";                AUTO = Auto-detect active controller (PlayStation DualSense/DS4, Xbox/XInput, or Keyboard)\n"
                ";                PS   = Force PlayStation 5 DualSense glyphs & manual diagram\n"
                ";                XBOX = Force Xbox Series X|S glyphs & stock Xbox manual diagram\n"
                ";                KB   = Force Keyboard glyphs & stock Keyboard manual diagram\n"
                "GamepadGlyphs=AUTO\n",
                PLUGIN_VERSION_STR
            );
            fclose(f);
        }
    }

    auto ReadHdIniInt = [&](const char* key, int def_val) -> int {
        int v = GetPrivateProfileIntA("ACZ_HDTex", key, -99999, ini_path.string().c_str());
        if (v != -99999) return v;
        v = GetPrivateProfileIntA("Settings", key, -99999, ini_path.string().c_str());
        return (v != -99999) ? v : def_val;
    };
    g_EnableLogging                 = ReadHdIniInt("EnableLogging",                      1) != 0;
    g_DumpAllGims                   = ReadHdIniInt("DumpAllGims", 0) != 0;
    g_DumpReplacedTextures          = ReadHdIniInt("DumpReplacedTextures", 0) != 0;
    g_DumpRawGimFiles               = ReadHdIniInt("DumpRawGimFiles", 0) != 0;
    g_HotReload                     = ReadHdIniInt("HotReload", 0) != 0;
    g_AutoShareFontGlyphs           = ReadHdIniInt("AutoShareFontGlyphs", 1) != 0;
    g_DumpMasterFontAtlas           = ReadHdIniInt("DumpMasterFontAtlas", 1) != 0;
    g_DumpMergedFamilyAtlases       = ReadHdIniInt("DumpMergedFamilyAtlases", 0) != 0;
    g_DumpIndividualFontAtlases     = ReadHdIniInt("DumpIndividualFontAtlases", 0) != 0;
    g_ShareCharCodeToSingleLineSubs = ReadHdIniInt("ShareCharCodeToSingleLineSubtitles", 0) != 0;
    g_ForceGlobalCharCodeSharing    = ReadHdIniInt("ForceGlobalCharCodeSharing", 0) != 0;
    g_DumpSingleLineSubtitles       = ReadHdIniInt("DumpSingleLineSubtitles", 0) != 0;
    g_EnableMenuOverrides           = ReadHdIniInt("EnableMenuOverrides", 1) != 0;
    g_MenuAspectMode                = ReadHdIniInt("MenuAspectMode", 0);
    g_EnableDdsCache                = ReadHdIniInt("EnableDdsCache", 1) != 0;
    g_DdsCacheCompression           = ReadHdIniInt("DdsCacheCompression", 1);

    char pad_buf[64] = {};
    GetPrivateProfileStringA("Settings", "GamepadGlyphs", "", pad_buf, sizeof(pad_buf), ini_path.string().c_str());
    if (pad_buf[0] == '\0') {
        GetPrivateProfileStringA("ACZ_HDTex", "GamepadGlyphs", "", pad_buf, sizeof(pad_buf), ini_path.string().c_str());
    }
    if (pad_buf[0] == '\0') {
        fs::path enh_ini = enh_dir / "enhancer.ini";
        if (fs::exists(enh_ini, ec)) {
            GetPrivateProfileStringA("Enhancer", "GamepadGlyphs", "", pad_buf, sizeof(pad_buf), enh_ini.string().c_str());
            if (pad_buf[0] == '\0') {
                GetPrivateProfileStringA("Settings", "GamepadGlyphs", "", pad_buf, sizeof(pad_buf), enh_ini.string().c_str());
            }
        }
    }
    if (pad_buf[0] == '\0') {
        strcpy_s(pad_buf, "AUTO");
    }
    if (_stricmp(pad_buf, "PS") == 0 || _stricmp(pad_buf, "PS5") == 0 ||
        _stricmp(pad_buf, "PS_MONO") == 0 || _stricmp(pad_buf, "DUALSENSE") == 0 ||
        _stricmp(pad_buf, "DS4") == 0 || _stricmp(pad_buf, "1") == 0) {
        g_GamepadGlyphsMode = 1; // Forced PlayStation
    } else if (_stricmp(pad_buf, "XBOX") == 0 || _stricmp(pad_buf, "XInput") == 0 ||
               _stricmp(pad_buf, "2") == 0) {
        g_GamepadGlyphsMode = 2; // Forced Xbox
    } else if (_stricmp(pad_buf, "KB") == 0 || _stricmp(pad_buf, "KEYBOARD") == 0 ||
               _stricmp(pad_buf, "3") == 0) {
        g_GamepadGlyphsMode = 3; // Forced Keyboard
    } else {
        g_GamepadGlyphsMode = 0; // Default: AUTO (auto-detect PS / XBOX / KB)
    }
    int u2p = ReadHdIniInt("Unlock2PlayerCampaign", -99999);
    if (u2p == -99999) u2p = ReadHdIniInt("Unlock2PlayerMode", -99999);
    if (u2p == -99999) {
        fs::path enh_cfg = enh_dir / "enhancer.ini";
        if (fs::exists(enh_cfg, ec)) {
            const char* secs[] = { "Enhancer", "Settings", "ACZ_HDTex" };
            const char* keys[] = { "Unlock2PlayerCampaign", "Unlock2PlayerMode", "Enable2PlayerCoop" };
            for (const char* s : secs) {
                for (const char* k : keys) {
                    u2p = GetPrivateProfileIntA(s, k, -99999, enh_cfg.string().c_str());
                    if (u2p != -99999) break;
                }
                if (u2p != -99999) break;
            }
        }
    }
    g_Unlock2PlayerCampaign = (u2p > 0);
}

// ----------------------------------------------------------------------------
// Core GIM Inspection, Disk-Hash Caching & Replacement Binding
// ----------------------------------------------------------------------------
static uint32_t InspectGimBufferAndGetCustomId(uint8_t* gim_data, const char* caller_tag, uint64_t* out_content_tag = nullptr) {
    if (!gim_data) return 0;

    GimQuickInfo qinfo{};
    if (!SafeQuickInspectGim(gim_data, &qinfo)) {
        return 0;
    }
    if (out_content_tag) {
        *out_content_tag = qinfo.content_tag;
    }

    uintptr_t gim_addr = reinterpret_cast<uintptr_t>(gim_data);
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        auto fit = g_GimFastCache.find(gim_addr);
        if (fit != g_GimFastCache.end() &&
            fit->second.total_size == qinfo.total_size &&
            fit->second.quick_fp == qinfo.quick_fp) {
            return fit->second.custom_id;
        }
    }

    GimInspectionResult res{};
    if (!SafeInspectAndHashGim(gim_data, &res)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_Mutex);

    InvalidatePixelBindingsInRange(gim_addr, res.total_size, gim_addr);

    uint64_t primary_hash = res.hash_psm_norm;

    // 1. Check if any of the candidate hashes match a registered replacement PNG (or a sliced cell from MASTER_*_gim_16x16)
    uint16_t matched_slot = 0;
    uint64_t matched_hash = primary_hash;

    const uint64_t candidates[5] = {
        primary_hash,
        res.hash_exact,
        res.hash_psm_norm,
        res.hash_fhm_norm,
        res.hash_raw
    };

    for (uint64_t cand : candidates) {
        auto it = g_HashToSlot.find(cand);
        if (it != g_HashToSlot.end()) {
            matched_slot = it->second;
            matched_hash = cand;
            break;
        }
    }

    

    // 2. Log & optionally dump (.png, and MASTER_GIM_16x16.png if 16x16) the first time we see this hash
    if (g_LoggedHashes.insert(primary_hash).second) {
        bool should_dump = g_DumpAllGims && (matched_slot == 0 || g_DumpReplacedTextures);
        if (res.width == 16u && res.height == 16u) {
            if (g_MasterGim16SeenHashes.insert(primary_hash).second) {
                MasterGim16Tile tile16{};
                tile16.hash_psm_norm = res.hash_psm_norm;
                tile16.hash_exact    = res.hash_exact;
                tile16.hash_fhm_norm = res.hash_fhm_norm;
                tile16.psm           = res.psm;
                if (DecodeGimToRgba(gim_data, res, tile16.rgba_16x16)) {
                    g_MasterGim16Tiles.push_back(std::move(tile16));
                    if (should_dump && g_DumpMasterFontAtlas) {
                        DumpMasterGim16AtlasToFolder(g_FontDumpDir);
                    }
                }
            }
        }
        if (should_dump) {
            // If it's a 16x16 GIM and DumpMasterFontAtlas=1, it's already in MASTER_GIM_16x16.png unless DumpIndividualFontAtlases=1
            if (!(res.width == 16u && res.height == 16u && g_DumpMasterFontAtlas && !g_DumpIndividualFontAtlases)) {
                DumpGimBufferToFolder(g_DumpDir, gim_data, res, primary_hash, res.psm, false);
            }
        }
    }

    InvalidatePixelBindingsInRange(gim_addr, res.total_size, gim_addr + 0x20);

    uint32_t final_custom_id = 0;
    if (matched_slot != 0) {
        final_custom_id = (static_cast<uint32_t>(matched_slot) << 16) | CUSTOM_EXTRATEX_TAG;
        RecordSlotExpectedDims(matched_slot, static_cast<uint16_t>(res.width), static_cast<uint16_t>(res.height));
        RegisterReplacedPixelAddress(gim_addr + 0x20, final_custom_id, res.width, res.height);

        if (g_LoggedMatchedGimHashes.insert(matched_hash).second) {
            LogMsg("[ACZ_HDTex] [%s] Matched GIM Hash=%016llX (%ux%u, PSM=0x%02X, %u bytes) -> Slot #%u (extratex_id=0x%08X)",
                   caller_tag, static_cast<unsigned long long>(matched_hash),
                   res.width, res.height, res.psm, res.total_size, matched_slot, final_custom_id);
        }
    } else if (primary_hash == 0x1C2024A9952DBC00ULL ||
               primary_hash == 0x8C7D0EAD0A3B1421ULL ||
               primary_hash == 0x30F023F86A144D5DULL ||
               primary_hash == 0xD00645D18395B834ULL) {
        uint16_t bslot = g_BuiltinExtratexSlot[0].load(std::memory_order_relaxed);
        if (bslot != 0) {
            final_custom_id = 0x00000001u;
            if (g_LoggedMatchedGimHashes.insert(primary_hash).second) {
                LogMsg("[ACZ_HDTex] [%s] Bound Native HUD Font GIM Hash=%016llX (%ux%u, PSM=0x%02X) -> extratex/hudfont/000.png [Slot #%u] (extratex_id=0x%08X)",
                       caller_tag, static_cast<unsigned long long>(primary_hash),
                       res.width, res.height, res.psm, bslot, final_custom_id);
            }
        }
    } else if (primary_hash == 0x9F1B6CEC8FF3284DULL) {
        uint16_t bslot = g_BuiltinExtratexSlot[1].load(std::memory_order_relaxed);
        if (bslot != 0) {
            final_custom_id = 0x00010001u;
            if (g_LoggedMatchedGimHashes.insert(primary_hash).second) {
                LogMsg("[ACZ_HDTex] [%s] Bound Native HUD Font GIM Hash=%016llX (%ux%u, PSM=0x%02X) -> extratex/hudfont/001.png [Slot #%u] (extratex_id=0x%08X)",
                       caller_tag, static_cast<unsigned long long>(primary_hash),
                       res.width, res.height, res.psm, bslot, final_custom_id);
            }
        }
    } else {
        static std::unordered_set<uint64_t> s_LoggedUnmatched;
        if (s_LoggedUnmatched.insert(primary_hash).second) {
            LogMsg("[ACZ_HDTex] [%s] Seen GIM Hash=%016llX (%ux%u, PSM=0x%02X, %u bytes)",
                   caller_tag, static_cast<unsigned long long>(primary_hash),
                   res.width, res.height, res.psm, res.total_size);
        }
    }

    GimFastCacheEntry cache_entry{};
    cache_entry.quick_fp         = qinfo.quick_fp;
    cache_entry.content_tag      = qinfo.content_tag;
    cache_entry.total_size       = qinfo.total_size;
    cache_entry.clut_offset      = qinfo.clut_offset;
    cache_entry.width            = res.width;
    cache_entry.height           = res.height;
    cache_entry.orig_psm         = res.psm;
    cache_entry.primary_hash     = primary_hash;
    cache_entry.custom_id        = final_custom_id;
    cache_entry.has_orig_headers = true;
    std::memcpy(cache_entry.orig_hdr, gim_data, 32);
    std::memcpy(cache_entry.orig_clut_hdr, gim_data + res.clut_offset, 16);

    g_GimFastCache[gim_addr]     = cache_entry;

    return final_custom_id;
}

static uint32_t InspectTim2BufferAndGetCustomId(uint8_t* src_pixels, const char* caller_tag, uint64_t* out_content_tag = nullptr) {
    if (!src_pixels) return 0;

    uintptr_t pix_addr = reinterpret_cast<uintptr_t>(src_pixels);
    uint64_t fast_tag = 0;
    if (!g_HotReload && SafeReadTim2ContentTag(src_pixels, &fast_tag)) {
        if (out_content_tag) {
            *out_content_tag = fast_tag;
        }
        std::lock_guard<std::mutex> lock(g_Mutex);
        auto fit = g_Tim2FastCache.find(pix_addr);
        if (fit != g_Tim2FastCache.end() && fit->second.content_tag == fast_tag) {
            return fit->second.custom_id;
        }
    }

    Tim2InspectionResult res{};
    if (!SafeInspectAndHashTim2(src_pixels, &res)) {
        return 0;
    }
    if (out_content_tag) {
        *out_content_tag = res.content_tag;
    }

    std::lock_guard<std::mutex> lock(g_Mutex);

    if (IsLegacyPs2PadTim2Hash(res.hash_tim2_norm) || IsLegacyPs2PadTim2Hash(res.hash_tim2_pix_clut)) {
        return 0;
    }

    uint64_t primary_hash = res.hash_tim2_norm;
    uint16_t matched_slot = 0;
    uint64_t matched_hash = primary_hash;

    const uint64_t candidates[3] = {
        res.hash_tim2_norm,
        res.hash_tim2_pix_clut,
        res.hash_tim2_pixels
    };

    for (uint64_t cand : candidates) {
        auto it = g_HashToSlot.find(cand);
        if (it != g_HashToSlot.end()) {
            matched_slot = it->second;
            matched_hash = cand;
            break;
        }
    }

    if (g_LoggedHashes.insert(primary_hash).second) {
        bool should_dump = g_DumpAllGims && (matched_slot == 0 || g_DumpReplacedTextures);
        if (should_dump) {
            DumpTim2BufferToFolder(g_DumpDir, src_pixels, res, primary_hash, false);
        }
    }

    uintptr_t tim2_base = (pix_addr > res.header_size) ? (pix_addr - res.header_size) : pix_addr;
    InvalidatePixelBindingsInRange(tim2_base, res.total_size, pix_addr);

    uint32_t final_custom_id = 0;
    if (matched_slot != 0) {
        final_custom_id = (static_cast<uint32_t>(matched_slot) << 16) | CUSTOM_EXTRATEX_TAG;
        RecordSlotExpectedDims(matched_slot, res.width, res.height);
        RegisterReplacedPixelAddress(pix_addr, final_custom_id, res.width, res.height);

        if (g_LoggedMatchedGimHashes.insert(matched_hash).second) {
            LogMsg("[ACZ_HDTex] [%s] Matched TIM2 Hash=%016llX (Norm=%016llX, %ux%u, PSM=0x%02X, %u bytes) -> Slot #%u (extratex_id=0x%08X)",
                   caller_tag,
                   static_cast<unsigned long long>(matched_hash),
                   static_cast<unsigned long long>(primary_hash),
                   res.width, res.height, res.psm, res.total_size, matched_slot, final_custom_id);
        }
    } else {
        static std::unordered_set<uint64_t> s_LoggedUnmatchedTim2;
        if (s_LoggedUnmatchedTim2.insert(primary_hash).second) {
            LogMsg("[ACZ_HDTex] [%s] Seen TIM2 Hash=%016llX (Pix=%016llX, %ux%u, PSM=0x%02X, %u bytes)",
                   caller_tag,
                   static_cast<unsigned long long>(primary_hash),
                   static_cast<unsigned long long>(res.hash_tim2_pixels),
                   res.width, res.height, res.psm, res.total_size);
        }
    }

    Tim2FastCacheEntry tentry{};
    tentry.content_tag  = res.content_tag;
    tentry.total_size   = res.total_size;
    tentry.width        = res.width;
    tentry.height       = res.height;
    tentry.image_type   = res.image_type;
    tentry.psm          = res.psm;
    tentry.primary_hash = primary_hash;
    tentry.custom_id    = final_custom_id;
    g_Tim2FastCache[pix_addr] = tentry;

    return final_custom_id;
}

// ----------------------------------------------------------------------------
// Core Dynamic Font (GameFont / CUnicodeFont / Pmenu) Inspection, Dumping & HD Replacement
// ----------------------------------------------------------------------------
static bool IsTileEmptyOrNearestPlaceholder(
    const uint8_t* tile_rgba,
    uint32_t tile_w,
    uint32_t tile_h,
    uint16_t orig_dim
) {
    (void)orig_dim;
    if (!tile_rgba || tile_w == 0 || tile_h == 0) return true;

    const size_t total_px = static_cast<size_t>(tile_w) * tile_h;
    for (size_t i = 0; i < total_px; ++i) {
        if (tile_rgba[i * 4u + 3] != 0) {
            return false; // Non-empty glyph cell -> always active!
        }
    }
    return true; // Completely empty/transparent space cell
}

static bool BuildPerGlyphHdTexturesForFont(
    uint16_t atlas_slot_idx,
    uint64_t matched_hash,
    const FontInspectionResult& fres,
    uint32_t explicit_grid_cols,
    CachedFontReplacement& out_rep,
    const std::vector<std::pair<uint16_t, uint16_t>>* opt_metrics = nullptr,
    std::vector<uint8_t>* out_uncentered_atlas_rgba = nullptr,
    uint32_t* out_atlas_w = nullptr,
    uint32_t* out_atlas_h = nullptr
) {
    if (atlas_slot_idx == 0 || atlas_slot_idx >= g_Slots.size()) return false;
    const std::string png_path = g_Slots[atlas_slot_idx].png_path;

    std::vector<uint8_t> atlas_rgba;
    uint32_t atlas_w = 0;
    uint32_t atlas_h = 0;
    if (!DecodePngFileViaWic(png_path, atlas_rgba, &atlas_w, &atlas_h)) {
        LogMsg("[ACZ_HDTex] ERROR: WIC DecodePngFileViaWic failed for Dynamic Font atlas: %s", png_path.c_str());
        return false;
    }

    uint32_t cols = (explicit_grid_cols > 0) ? explicit_grid_cols : 8u;
    if (explicit_grid_cols == 0) {
        uint32_t rows8  = (fres.num_glyphs + 7u) / 8u;
        uint32_t rows16 = (fres.num_glyphs + 15u) / 16u;
        if (fres.num_glyphs >= 16u && rows16 > 0 &&
            (atlas_w / 16u) == (atlas_h / rows16) &&
            (atlas_w / 8u) != (atlas_h / rows8)) {
            cols = 16u;
        }
    }
    uint32_t rows   = (fres.num_glyphs + cols - 1u) / cols;
    uint32_t tile_w = (cols > 0) ? (atlas_w / cols) : 0;
    uint32_t tile_h = (rows > 0) ? (atlas_h / rows) : 0;
    if (tile_w == 0 || tile_h == 0) {
        LogMsg("[ACZ_HDTex] ERROR: Invalid tile size (%ux%u) for Dynamic Font atlas (%ux%u, cols=%u, rows=%u)",
               tile_w, tile_h, atlas_w, atlas_h, cols, rows);
        return false;
    }

    out_rep.glyph_custom_ids.assign(fres.num_glyphs, 0);
    std::vector<uint8_t> raw_tile(static_cast<size_t>(tile_w) * tile_h * 4u);
    std::vector<uint8_t> tile_rgba(static_cast<size_t>(tile_w) * tile_h * 4u);
    const size_t atlas_stride = static_cast<size_t>(atlas_w) * 4u;
    const uint32_t dim = (fres.glyph_dim > 0) ? fres.glyph_dim : 32u;
    const uint32_t scale_x = (tile_w >= dim) ? (tile_w / dim) : 1u;
    const uint32_t scale_y = (tile_h >= dim) ? (tile_h / dim) : 1u;

    for (uint32_t g = 0; g < fres.num_glyphs; ++g) {
        uint32_t gx0 = (g % cols) * tile_w;
        uint32_t gy0 = (g / cols) * tile_h;

        for (uint32_t py = 0; py < tile_h; ++py) {
            const uint8_t* src_row = atlas_rgba.data() + static_cast<size_t>(gy0 + py) * atlas_stride + static_cast<size_t>(gx0) * 4u;
            uint8_t*       dst_row = raw_tile.data() + static_cast<size_t>(py) * tile_w * 4u;
            std::memcpy(dst_row, src_row, static_cast<size_t>(tile_w) * 4u);
        }

        if (IsTileEmptyOrNearestPlaceholder(raw_tile.data(), tile_w, tile_h, fres.glyph_dim)) {
            continue;
        }

        // Check if this cell is centered (x_min > 0) and un-center it into [0..adv_x*scale, 0..height*scale]
        // while preserving any restored left halo inside x >= 0 whenever adv_x has spare right margin!
        uint32_t x_min_px = tile_w, x_max_px = 0;
        uint32_t y_min_px = tile_h, y_max_px = 0;
        for (uint32_t py = 0; py < tile_h; ++py) {
            const uint8_t* rptr = raw_tile.data() + static_cast<size_t>(py) * tile_w * 4u;
            for (uint32_t px = 0; px < tile_w; ++px) {
                if (rptr[px * 4u + 3] != 0) {
                    if (px < x_min_px) x_min_px = px;
                    if (px > x_max_px) x_max_px = px;
                    if (py < y_min_px) y_min_px = py;
                    if (py > y_max_px) y_max_px = py;
                }
            }
        }

        uint16_t g_adv = dim, g_h = dim;
        if (opt_metrics != nullptr && g < opt_metrics->size()) {
            if ((*opt_metrics)[g].first > 0)  g_adv = (*opt_metrics)[g].first;
            if ((*opt_metrics)[g].second > 0) g_h   = (*opt_metrics)[g].second;
        }

        // Also find core horizontal bounds (alpha >= 96) so wide outer glows never bias horizontal centering
        uint32_t c_min_px = tile_w, c_max_px = 0;
        for (uint32_t py = 0; py < tile_h; ++py) {
            const uint8_t* rptr = raw_tile.data() + static_cast<size_t>(py) * tile_w * 4u;
            for (uint32_t px = 0; px < tile_w; ++px) {
                if (rptr[px * 4u + 3] >= 96u) {
                    if (px < c_min_px) c_min_px = px;
                    if (px > c_max_px) c_max_px = px;
                }
            }
        }
        if (c_min_px > c_max_px) {
            c_min_px = x_min_px;
            c_max_px = x_max_px;
        }

        uint32_t src_x0 = 0, src_y0 = 0;
        // Note: cells #701..#703 in 32x32 Master Atlas are Subtitle Bracket GIM patches (BB10DED7AB6393C6), keep them at centered coords
        if (x_min_px > 0 && x_min_px < tile_w && opt_metrics != nullptr && g < opt_metrics->size() && !(dim == 32u && g >= 701u)) {
            uint32_t eff_adv  = (g_adv > 0 && g_adv <= dim) ? g_adv : dim;
            uint32_t eff_h    = (g_h > 0 && g_h <= dim) ? g_h : dim;
            uint32_t pad_y_px = ((dim - eff_h) / 2u) * scale_y;
            uint32_t adv_w_px = eff_adv * scale_x;
            uint32_t core_w   = (c_max_px >= c_min_px) ? (c_max_px - c_min_px + 1u) : 0u;
            int32_t  ideal_x0 = (core_w > 0 && core_w <= adv_w_px)
                                ? (static_cast<int32_t>(c_min_px) - static_cast<int32_t>(adv_w_px - core_w) / 2)
                                : static_cast<int32_t>(c_min_px);
            int32_t  max_x0   = (tile_w > adv_w_px) ? static_cast<int32_t>(tile_w - adv_w_px) : 0;
            src_x0 = static_cast<uint32_t>(std::max(0, std::min(max_x0, ideal_x0)));
            // Use the uniform family vertical centering offset pad_y_px so all uppercase, lowercase,
            // and punctuation symbols in the family share the exact same baseline!
            src_y0 = pad_y_px;
        }

        if (src_x0 == 0 && src_y0 == 0) {
            std::memcpy(tile_rgba.data(), raw_tile.data(), tile_rgba.size());
        } else {
            std::memset(tile_rgba.data(), 0, tile_rgba.size());
            uint32_t copy_w = (src_x0 < tile_w) ? (tile_w - src_x0) : 0u;
            uint32_t copy_h = (src_y0 < tile_h) ? (tile_h - src_y0) : 0u;
            for (uint32_t dy = 0; dy < copy_h; ++dy) {
                const uint8_t* srow = raw_tile.data() + static_cast<size_t>(src_y0 + dy) * tile_w * 4u + static_cast<size_t>(src_x0) * 4u;
                uint8_t*       drow = tile_rgba.data() + static_cast<size_t>(dy) * tile_w * 4u;
                std::memcpy(drow, srow, static_cast<size_t>(copy_w) * 4u);
            }
            // Write un-centered cell back into atlas_rgba so 16x16 JSON donor cropping also uses un-centered coordinates
            for (uint32_t py = 0; py < tile_h; ++py) {
                uint8_t*       dst_row = atlas_rgba.data() + static_cast<size_t>(gy0 + py) * atlas_stride + static_cast<size_t>(gx0) * 4u;
                const uint8_t* src_row = tile_rgba.data() + static_cast<size_t>(py) * tile_w * 4u;
                std::memcpy(dst_row, src_row, static_cast<size_t>(tile_w) * 4u);
            }
        }

        if (g_Slots.size() < MAX_CUSTOM_TEXTURES) {
            uint16_t g_slot = static_cast<uint16_t>(g_Slots.size());
            CustomSlotInfo g_info;
            g_info.hash        = matched_hash;
            g_info.png_path    = png_path;
            g_info.loaded_tex  = nullptr; // Created lazily on the D3D12 render thread in LoadReplacementPngForSlot!
            g_info.load_failed = false;
            EncodeRgbaToPngBytes(tile_rgba.data(), tile_w, tile_h, g_info.mem_png);
            g_Slots.push_back(std::move(g_info));
            g_SlotExpectedW[g_slot].store(fres.glyph_dim, std::memory_order_release);
            g_SlotExpectedH[g_slot].store(fres.glyph_dim, std::memory_order_release);

            out_rep.glyph_custom_ids[g] = (static_cast<uint32_t>(g_slot) << 16) | CUSTOM_EXTRATEX_TAG;
        }
    }

    if (out_uncentered_atlas_rgba != nullptr) {
        *out_uncentered_atlas_rgba = std::move(atlas_rgba);
    }
    if (out_atlas_w != nullptr) *out_atlas_w = atlas_w;
    if (out_atlas_h != nullptr) *out_atlas_h = atlas_h;

    LogMsg("[ACZ_HDTex] Sliced HD Atlas in RAM (%ux%u, %u cols -> %u tiles of %ux%u, logical %ux%u) for Hash=%016llX (%s)",
           atlas_w, atlas_h, cols, fres.num_glyphs, tile_w, tile_h, fres.glyph_dim, fres.glyph_dim,
           static_cast<unsigned long long>(matched_hash), png_path.c_str());
    return true;
}

static uint64_t ComputeGlyphTileHash(
    uint16_t glyph_dim,
    uint16_t char_code,
    uint16_t adv_x,
    uint16_t height,
    const uint8_t* clut_64,
    const uint8_t* tile_bytes,
    uint32_t tile_len
) {
    uint64_t h = FNV1A_64_OFFSET;
    uint8_t hdr[8] = {
        static_cast<uint8_t>(glyph_dim & 0xFF),
        static_cast<uint8_t>((glyph_dim >> 8) & 0xFF),
        static_cast<uint8_t>(char_code & 0xFF),
        static_cast<uint8_t>((char_code >> 8) & 0xFF),
        static_cast<uint8_t>(adv_x & 0xFF),
        static_cast<uint8_t>((adv_x >> 8) & 0xFF),
        static_cast<uint8_t>(height & 0xFF),
        static_cast<uint8_t>((height >> 8) & 0xFF)
    };
    for (int i = 0; i < 8; ++i) {
        h ^= hdr[i];
        h *= FNV1A_64_PRIME;
    }
    if (clut_64 != nullptr) {
        for (int i = 0; i < 64; ++i) {
            h ^= clut_64[i];
            h *= FNV1A_64_PRIME;
        }
    }
    for (uint32_t i = 0; i < tile_len; ++i) {
        h ^= tile_bytes[i];
        h *= FNV1A_64_PRIME;
    }
    return h;
}

static void Decode4BitGlyphTileToRgba(
    uint16_t dim,
    const uint8_t* clut_ps2,
    const uint8_t* tile_4b,
    std::vector<uint8_t>& out_rgba
) {
    if (dim == 0 || !clut_ps2 || !tile_4b) return;
    uint8_t pal_pc[16][4] = {};
    for (int c = 0; c < 16; ++c) {
        pal_pc[c][0] = clut_ps2[c * 4 + 0];
        pal_pc[c][1] = clut_ps2[c * 4 + 1];
        pal_pc[c][2] = clut_ps2[c * 4 + 2];
        pal_pc[c][3] = Ps2AlphaToPc(clut_ps2[c * 4 + 3]);
    }
    const size_t total_px = static_cast<size_t>(dim) * dim;
    out_rgba.resize(total_px * 4u);
    for (size_t p_idx = 0; p_idx < total_px; ++p_idx) {
        uint8_t byte_v = tile_4b[p_idx >> 1];
        uint8_t c_idx  = (p_idx & 1u) ? ((byte_v >> 4) & 0x0Fu) : (byte_v & 0x0Fu);
        out_rgba[p_idx * 4u + 0] = pal_pc[c_idx][0];
        out_rgba[p_idx * 4u + 1] = pal_pc[c_idx][1];
        out_rgba[p_idx * 4u + 2] = pal_pc[c_idx][2];
        out_rgba[p_idx * 4u + 3] = pal_pc[c_idx][3];
    }
}

// Cleans disconnected right-edge neighbor bleed, restores cut-off left-edge halos (at x < 0),
// and centers a 1x (dim x dim) RGBA glyph tile at pad_x = (dim - adv_x)/2, pad_y = (dim - height)/2.
static void CleanRestoreAndCenter1xGlyphRgba(
    std::vector<uint8_t>& cell_rgba,
    uint16_t dim,
    uint16_t cp,
    uint16_t adv_x,
    uint16_t height,
    uint16_t std_h = 0
) {
    if (dim == 0 || cell_rgba.size() < static_cast<size_t>(dim) * dim * 4u) return;
    if (cp == 0x20u) {
        std::memset(cell_rgba.data(), 0, cell_rgba.size());
        return;
    }

    int x0_init = dim, x1_init = -1;
    for (int y = 0; y < static_cast<int>(dim); ++y) {
        for (int x = 0; x < static_cast<int>(dim); ++x) {
            if (cell_rgba[(y * dim + x) * 4 + 3] != 0) {
                if (x < x0_init) x0_init = x;
                if (x > x1_init) x1_init = x;
            }
        }
    }
    if (x1_init < 0) return;
    // Idempotency guard: if the glyph is already shifted away from x=0..1, do not run right-bleed cleaning or shift again!
    if (x0_init >= 2 && x1_init <= static_cast<int>(dim) - 3) return;

    // 1a. Clean disconnected right-edge neighbor bleed
    int search_start_x = std::min(static_cast<int>(dim) - 2, std::max(4, static_cast<int>(adv_x) - 1));
    for (int x = search_start_x; x < static_cast<int>(dim); ++x) {
        bool col_empty = true;
        for (int y = 0; y < static_cast<int>(dim); ++y) {
            if (cell_rgba[(y * dim + x) * 4 + 3] != 0) {
                col_empty = false;
                break;
            }
        }
        if (col_empty) {
            for (int y = 0; y < static_cast<int>(dim); ++y) {
                for (int rx = x + 1; rx < static_cast<int>(dim); ++rx) {
                    std::memset(&cell_rgba[(y * dim + rx) * 4], 0, 4);
                }
            }
            break;
        }
    }
    if (static_cast<int>(adv_x) + 6 < static_cast<int>(dim)) {
        for (int y = 0; y < static_cast<int>(dim); ++y) {
            for (int rx = static_cast<int>(adv_x) + 6; rx < static_cast<int>(dim); ++rx) {
                std::memset(&cell_rgba[(y * dim + rx) * 4], 0, 4);
            }
        }
    }

    // 1b. Clean disconnected bottom-edge neighbor bleed (below height)
    int search_start_y = std::min(static_cast<int>(dim) - 2, std::max(4, static_cast<int>(height) - 1));
    for (int y = search_start_y; y < static_cast<int>(dim); ++y) {
        bool row_empty = true;
        for (int x = 0; x < static_cast<int>(dim); ++x) {
            if (cell_rgba[(y * dim + x) * 4 + 3] != 0) {
                row_empty = false;
                break;
            }
        }
        if (row_empty) {
            for (int ry = y + 1; ry < static_cast<int>(dim); ++ry) {
                std::memset(&cell_rgba[(ry * dim) * 4], 0, static_cast<size_t>(dim) * 4u);
            }
            break;
        }
    }
    if (static_cast<int>(height) + 4 < static_cast<int>(dim)) {
        for (int ry = static_cast<int>(height) + 4; ry < static_cast<int>(dim); ++ry) {
            std::memset(&cell_rgba[(ry * dim) * 4], 0, static_cast<size_t>(dim) * 4u);
        }
    }

    // 2. Check for outer halo (0 < alpha <= 90) and restore cut-off left halo at x < 0
    bool has_halo = false;
    bool touches_x0 = false;
    uint32_t r_sum = 0, g_sum = 0, b_sum = 0, fringe_cnt = 0;
    for (int y = 0; y < static_cast<int>(dim); ++y) {
        if (cell_rgba[(y * dim + 0) * 4 + 3] > 8) touches_x0 = true;
        for (int x = 0; x < static_cast<int>(dim); ++x) {
            const uint8_t* p = &cell_rgba[(y * dim + x) * 4];
            if (p[3] > 0 && p[3] <= 90) {
                has_halo = true;
                r_sum += p[0];
                g_sum += p[1];
                b_sum += p[2];
                ++fringe_cnt;
            }
        }
    }

    const int max_halo_w = (dim >= 32u) ? 3 : 2;
    const int ext_w = static_cast<int>(dim) + max_halo_w;
    std::vector<uint8_t> ext(static_cast<size_t>(dim) * ext_w * 4u, 0);
    for (int y = 0; y < static_cast<int>(dim); ++y) {
        std::memcpy(&ext[(y * ext_w + max_halo_w) * 4], &cell_rgba[(y * dim) * 4], static_cast<size_t>(dim) * 4u);
    }

    if (has_halo && touches_x0 && fringe_cnt > 0) {
        uint8_t halo_rgb[3] = {
            static_cast<uint8_t>(r_sum / fringe_cnt),
            static_cast<uint8_t>(g_sum / fringe_cnt),
            static_cast<uint8_t>(b_sum / fringe_cnt)
        };
        int avg_tail[3] = { (dim >= 32u) ? 24 : 24, (dim >= 32u) ? 14 : 10, 6 };
        for (int y = 0; y < static_cast<int>(dim); ++y) {
            int a0 = cell_rgba[(y * dim + 0) * 4 + 3];
            if (a0 <= 8) continue;
            if (a0 > 120) {
                for (int d = 1; d <= max_halo_w; ++d) {
                    int ha = avg_tail[d - 1];
                    if (ha > 0) {
                        uint8_t* dst = &ext[(y * ext_w + (max_halo_w - d)) * 4];
                        dst[0] = halo_rgb[0];
                        dst[1] = halo_rgb[1];
                        dst[2] = halo_rgb[2];
                        dst[3] = static_cast<uint8_t>(ha);
                    }
                }
            } else {
                int cur_a = a0;
                for (int d = 1; d <= max_halo_w; ++d) {
                    cur_a = (cur_a * 55 + 50) / 100;
                    if (cur_a >= 6) {
                        uint8_t* dst = &ext[(y * ext_w + (max_halo_w - d)) * 4];
                        dst[0] = halo_rgb[0];
                        dst[1] = halo_rgb[1];
                        dst[2] = halo_rgb[2];
                        dst[3] = static_cast<uint8_t>(cur_a);
                    }
                }
            }
        }
    }

    // 3. Center inside dim x dim cell
    int orig_x0 = ext_w, orig_x1 = -1, orig_y0 = dim, orig_y1 = -1;
    for (int y = 0; y < static_cast<int>(dim); ++y) {
        for (int x = 0; x < ext_w; ++x) {
            if (ext[(y * ext_w + x) * 4 + 3] != 0) {
                if (x < orig_x0) orig_x0 = x;
                if (x > orig_x1) orig_x1 = x;
                if (y < orig_y0) orig_y0 = y;
                if (y > orig_y1) orig_y1 = y;
            }
        }
    }
    if (orig_x1 < 0) return;
    orig_x0 -= max_halo_w;
    orig_x1 = orig_x1 - max_halo_w + 1;
    orig_y1 += 1;

    uint16_t align_h = (std_h > 0 && std_h <= dim) ? std_h : height;
    int eff_adv = (adv_x > 0 && adv_x <= dim) ? static_cast<int>(adv_x) : static_cast<int>(dim);
    int eff_h   = (align_h > 0 && align_h <= dim) ? static_cast<int>(align_h) : static_cast<int>(dim);
    int pad_x   = (static_cast<int>(dim) - eff_adv) / 2;
    int pad_y   = (static_cast<int>(dim) - eff_h) / 2;

    if (orig_x0 + pad_x < 1) pad_x = 1 - orig_x0;
    if (orig_x1 + pad_x > static_cast<int>(dim) - 1) pad_x = std::max(-orig_x0, static_cast<int>(dim) - 1 - orig_x1);

    std::memset(cell_rgba.data(), 0, cell_rgba.size());
    for (int y = 0; y < static_cast<int>(dim); ++y) {
        int dy = y + pad_y;
        if (dy < 0 || dy >= static_cast<int>(dim)) continue;
        for (int x = -max_halo_w; x < static_cast<int>(dim); ++x) {
            const uint8_t* src = &ext[(y * ext_w + (x + max_halo_w)) * 4];
            if (src[3] == 0) continue;
            int dx = x + pad_x;
            if (dx >= 0 && dx < static_cast<int>(dim)) {
                std::memcpy(&cell_rgba[(dy * dim + dx) * 4], src, 4);
            }
        }
    }
}

static bool AddUniqueAltHashToGlyph(FamilyGlyphEntry& ge, uint64_t h) {
    if (h == 0 || h == ge.bitmap_tile_hash || h == ge.exact_tile_hash) return false;
    if (std::find(ge.alt_hashes.begin(), ge.alt_hashes.end(), h) != ge.alt_hashes.end()) return false;
    ge.alt_hashes.push_back(h);
    return true;
}

static bool AreGlyphEntriesVisuallyIdentical(const FamilyGlyphEntry& a, const FamilyGlyphEntry& b, uint16_t default_dim) {
    if (a.bitmap_tile_hash == b.bitmap_tile_hash) return true;
    uint16_t dim_a = (a.glyph_dim > 0) ? a.glyph_dim : default_dim;
    uint16_t dim_b = (b.glyph_dim > 0) ? b.glyph_dim : default_dim;
    if (dim_a == 0 || dim_a != dim_b) return false;
    int diff_adv = static_cast<int>(a.adv_x) - static_cast<int>(b.adv_x);
    int diff_h   = static_cast<int>(a.height) - static_cast<int>(b.height);
    if (diff_adv < -1 || diff_adv > 1 || diff_h < -1 || diff_h > 1) return false;

    const size_t expected_bytes = static_cast<size_t>(dim_a) * dim_a * 4u;
    if (a.rgba_tile.size() < expected_bytes || b.rgba_tile.size() < expected_bytes) return false;

    for (size_t i = 0; i < expected_bytes; ++i) {
        int d = static_cast<int>(a.rgba_tile[i]) - static_cast<int>(b.rgba_tile[i]);
        if (d < -32 || d > 32) return false;
    }
    return true;
}

static std::string FormatGlyphAltHashesSuffix(const FamilyGlyphEntry& ge) {
    if (ge.alt_hashes.empty()) return "";
    std::vector<uint64_t> sorted_alts;
    sorted_alts.reserve(ge.alt_hashes.size());
    for (uint64_t h : ge.alt_hashes) {
        if (h != 0 && h != ge.bitmap_tile_hash && h != ge.exact_tile_hash) {
            if (std::find(sorted_alts.begin(), sorted_alts.end(), h) == sorted_alts.end()) {
                sorted_alts.push_back(h);
            }
        }
    }
    if (sorted_alts.empty()) return "";
    std::sort(sorted_alts.begin(), sorted_alts.end());
    std::string out = " | AltHashes=";
    for (size_t i = 0; i < sorted_alts.size(); ++i) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%s%016llX", (i > 0 ? "," : ""), static_cast<unsigned long long>(sorted_alts[i]));
        out += buf;
    }
    return out;
}

static void SortFamilyGlyphs(std::vector<FamilyGlyphEntry>& glyphs) {
    std::sort(glyphs.begin(), glyphs.end(), [](const FamilyGlyphEntry& a, const FamilyGlyphEntry& b) {
        if (a.char_code != b.char_code) return a.char_code < b.char_code;
        if (a.exact_tile_hash != b.exact_tile_hash) return a.exact_tile_hash < b.exact_tile_hash;
        return a.bitmap_tile_hash < b.bitmap_tile_hash;
    });
}

// Transitively merge any two FontFamilyInfo entries of the same glyph_dim that share >= 75%
// matching or visually equivalent (CLUT-variant) non-empty glyphs on common character codes.
static void ConsolidateOverlappingFontFamilies(uint16_t glyph_dim) {
    bool merged_any = true;
    while (merged_any) {
        merged_any = false;
        for (size_t i = 0; i < g_FontFamilyOrder.size(); ++i) {
            uint64_t hash_a = g_FontFamilyOrder[i];
            auto it_a = g_FontFamilies.find(hash_a);
            if (it_a == g_FontFamilies.end() || it_a->second.glyph_dim != glyph_dim) continue;
            FontFamilyInfo& fam_a = it_a->second;

            for (size_t j = i + 1; j < g_FontFamilyOrder.size(); ++j) {
                uint64_t hash_b = g_FontFamilyOrder[j];
                auto it_b = g_FontFamilies.find(hash_b);
                if (it_b == g_FontFamilies.end() || it_b->second.glyph_dim != glyph_dim) continue;
                FontFamilyInfo& fam_b = it_b->second;

                uint32_t common_chars = 0;
                uint32_t exact_tiles  = 0;
                for (const auto& ge_b : fam_b.glyphs) {
                    if (!ge_b.is_non_empty) continue;
                    bool char_in_a = false;
                    bool tile_in_a = false;
                    for (const auto& ge_a : fam_a.glyphs) {
                        if (ge_a.char_code == ge_b.char_code) {
                            char_in_a = true;
                            if (AreGlyphEntriesVisuallyIdentical(ge_a, ge_b, glyph_dim)) {
                                tile_in_a = true;
                                break;
                            }
                        }
                    }
                    if (char_in_a) {
                        ++common_chars;
                        if (tile_in_a) ++exact_tiles;
                    }
                }

                if (exact_tiles >= 1 && common_chars > 0 && (exact_tiles * 4u >= common_chars * 3u)) {
                    for (uint64_t mh : fam_b.member_hashes) {
                        if (std::find(fam_a.member_hashes.begin(), fam_a.member_hashes.end(), mh) == fam_a.member_hashes.end()) {
                            fam_a.member_hashes.push_back(mh);
                        }
                        auto sit = g_FontSnapshotsByHash.find(mh);
                        if (sit != g_FontSnapshotsByHash.end()) {
                            sit->second.family_hash = hash_a;
                        }
                    }
                    for (auto& hinst : g_FontHeapInstances) {
                        if (hinst.second.family_hash == hash_b) {
                            hinst.second.family_hash = hash_a;
                        }
                    }
                    for (const auto& ss : fam_b.sample_strings) {
                        if (fam_a.sample_strings.size() < 64 &&
                            std::find(fam_a.sample_strings.begin(), fam_a.sample_strings.end(), ss) == fam_a.sample_strings.end()) {
                            fam_a.sample_strings.push_back(ss);
                        }
                    }
                    for (auto& ge_b : fam_b.glyphs) {
                        bool exists = false;
                        for (auto& ge_a : fam_a.glyphs) {
                            if (ge_a.bitmap_tile_hash == ge_b.bitmap_tile_hash ||
                                (ge_a.char_code == ge_b.char_code && AreGlyphEntriesVisuallyIdentical(ge_a, ge_b, glyph_dim))) {
                                AddUniqueAltHashToGlyph(ge_a, ge_b.bitmap_tile_hash);
                                AddUniqueAltHashToGlyph(ge_a, ge_b.exact_tile_hash);
                                for (uint64_t ah : ge_b.alt_hashes) {
                                    AddUniqueAltHashToGlyph(ge_a, ah);
                                }
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            ge_b.family_hash = hash_a;
                            fam_a.glyphs.push_back(std::move(ge_b));
                        }
                    }
                    SortFamilyGlyphs(fam_a.glyphs);
                    g_FontFamilies.erase(it_b);
                    g_FontFamilyOrder.erase(g_FontFamilyOrder.begin() + static_cast<ptrdiff_t>(j));
                    merged_any = true;
                    break;
                }
            }
            if (merged_any) break;
        }
    }
}

// Rebuilds MasterFontAtlasInfo for `glyph_dim` contiguously family-by-family from g_FontFamilies,
// deduplicating across families by `exact_tile_hash` (so shared uncolored bitmaps with different
// CLUT palettes in Menu Descriptions vs Briefing Subtitles are preserved in their respective sections!).
static void RebuildMasterFontAtlasForDim(uint16_t glyph_dim) {
    MasterFontAtlasInfo& mas = g_MasterFontAtlasesByDim[glyph_dim];
    mas.glyph_dim = glyph_dim;
    mas.glyphs.clear();
    mas.sections.clear();

    std::unordered_set<uint64_t> seen_exact_hashes;

    for (uint64_t fam_hash : g_FontFamilyOrder) {
        auto it = g_FontFamilies.find(fam_hash);
        if (it == g_FontFamilies.end() || it->second.glyph_dim != glyph_dim) continue;
        const FontFamilyInfo& fam = it->second;
        if (fam.glyphs.empty()) continue;

        uint32_t sec_start = static_cast<uint32_t>(mas.glyphs.size());
        uint32_t added_in_sec = 0;
        for (const auto& ge : fam.glyphs) {
            if (seen_exact_hashes.insert(ge.exact_tile_hash).second) {
                mas.glyphs.push_back(ge);
                ++added_in_sec;
            }
        }
        if (added_in_sec > 0) {
            MasterAtlasFamilySection sec{};
            sec.family_hash   = fam.family_hash;
            sec.start_idx     = sec_start;
            sec.end_idx       = static_cast<uint32_t>(mas.glyphs.size() - 1u);
            sec.new_glyphs    = added_in_sec;
            sec.member_blocks = fam.member_hashes.size();
            sec.sample        = fam.sample_strings.empty() ? "" : fam.sample_strings[0];
            mas.sections.push_back(std::move(sec));
        }
    }
}

static void DumpMasterFontAtlasToFolder(const fs::path& target_dir, const MasterFontAtlasInfo& mas) {
    if (mas.glyphs.empty() || mas.glyph_dim == 0) return;
    std::error_code ec;
    fs::create_directories(target_dir, ec);

    const uint32_t num_gl     = static_cast<uint32_t>(mas.glyphs.size());
    const uint32_t cols       = (num_gl >= 16u) ? 16u : 8u;
    const uint32_t rows       = (num_gl + cols - 1u) / cols;
    const uint32_t dim        = mas.glyph_dim;
    const uint32_t atlas_w    = cols * dim;
    const uint32_t atlas_h    = rows * dim;
    const uint32_t tile_bytes = (dim * dim) / 2u;

    char base_name[96];
    snprintf(base_name, sizeof(base_name), "MASTER_FONT_%ux%u", dim, dim);

    fs::path png_path = target_dir / (std::string(base_name) + ".png");
    fs::path txt_path = target_dir / (std::string(base_name) + ".txt");

    std::vector<uint8_t> atlas_rgba(static_cast<size_t>(atlas_w) * atlas_h * 4u, 0);
    std::vector<uint8_t> cell_rgba;
    for (uint32_t g = 0; g < num_gl; ++g) {
        const auto& ge = mas.glyphs[g];
        uint32_t gx0 = (g % cols) * dim;
        uint32_t gy0 = (g / cols) * dim;
        if (ge.rgba_tile.size() >= static_cast<size_t>(dim) * dim * 4u) {
            cell_rgba = ge.rgba_tile;
        } else if (ge.tile_4b.size() >= tile_bytes) {
            Decode4BitGlyphTileToRgba(static_cast<uint16_t>(dim), ge.clut_ps2, ge.tile_4b.data(), cell_rgba);
            CleanRestoreAndCenter1xGlyphRgba(cell_rgba, static_cast<uint16_t>(dim), ge.char_code, ge.adv_x, ge.height);
        } else {
            continue;
        }
        for (uint32_t py = 0; py < dim; ++py) {
            size_t dst = (static_cast<size_t>(gy0 + py) * atlas_w + gx0) * 4u;
            size_t src = static_cast<size_t>(py) * dim * 4u;
            std::memcpy(&atlas_rgba[dst], &cell_rgba[src], static_cast<size_t>(dim) * 4u);
        }
    }

    std::vector<uint8_t> png_bytes;
    EncodeRgbaToPngBytes(atlas_rgba.data(), atlas_w, atlas_h, png_bytes);
    if (!png_bytes.empty()) {
        FILE* pf = nullptr;
        fopen_s(&pf, png_path.string().c_str(), "wb");
        if (pf) {
            fwrite(png_bytes.data(), 1, png_bytes.size(), pf);
            fclose(pf);
        }
    }

    FILE* tf = nullptr;
    fopen_s(&tf, txt_path.string().c_str(), "wb");
    if (tf) {
        fprintf(tf, "====================================================================\n");
        fprintf(tf, " Master Font Atlas (All %ux%u Dynamic Font Glyphs Combined Across %zu Font Blocks)\n",
                dim, dim, mas.member_hashes.size());
        fprintf(tf, "====================================================================\n");
        fprintf(tf, "Master Atlas Name:     %s\n", base_name);
        fprintf(tf, "Glyph Dimension:       %ux%u\n", dim, dim);
        fprintf(tf, "Grid Columns:          %u\n", cols);
        fprintf(tf, "CenteredCells:         1\n");
        fprintf(tf, "Collected Glyphs:      %u (%ux%u, %u columns)\n", num_gl, dim, dim, cols);
        fprintf(tf, "Member Font Blocks:    %zu\n", mas.member_hashes.size());
        fprintf(tf, "To replace all %ux%u fonts in-game:\n", dim, dim);
        fprintf(tf, "  Copy edited %s.png AND %s.txt into mods\\textures\\\n", base_name, base_name);
        fprintf(tf, "--------------------------------------------------------------------\n");
        fprintf(tf, "TYPEFACE SECTIONS IN THIS MASTER ATLAS:\n");
        for (const auto& sec : mas.sections) {
            fprintf(tf, "  * Glyphs #%03u..#%03u (rows %u..%u): Family=%016llX (%u glyphs, %zu blocks) | Sample: \"%s\"\n",
                    sec.start_idx, sec.end_idx,
                    sec.start_idx / cols, sec.end_idx / cols,
                    static_cast<unsigned long long>(sec.family_hash),
                    sec.new_glyphs, sec.member_blocks,
                    sec.sample.c_str());
        }
        fprintf(tf, "--------------------------------------------------------------------\n");
        fprintf(tf, "SAMPLE STRINGS FROM SEEN FONTS:\n");
        for (size_t i = 0; i < mas.sample_strings.size(); ++i) {
            fprintf(tf, "  [%03zu] (Hash=%016llX) %s\n",
                    i, static_cast<unsigned long long>(mas.member_hashes[i]),
                    mas.sample_strings[i].c_str());
        }
        fprintf(tf, "--------------------------------------------------------------------\n");
        fprintf(tf, "GLYPH METRICS (Grid %u columns):\n", cols);
        for (uint32_t g = 0; g < num_gl; ++g) {
            const auto& ge = mas.glyphs[g];
            std::string ch_utf8;
            AppendUtf8Codepoint(ch_utf8, ge.char_code);
            std::string alt_str = FormatGlyphAltHashesSuffix(ge);
            fprintf(tf, "  Glyph #%03u (col=%u, row=%u): U+%04X '%s' | adv_x=%u, height=%u | TileHash=%016llX | ExactHash=%016llX | Family=%016llX%s\n",
                    g, g % cols, g / cols, ge.char_code, ch_utf8.c_str(),
                    ge.adv_x, ge.height,
                    static_cast<unsigned long long>(ge.bitmap_tile_hash),
                    static_cast<unsigned long long>(ge.exact_tile_hash),
                    static_cast<unsigned long long>(ge.family_hash),
                    alt_str.c_str());
        }
        fclose(tf);
    }
}

static void DumpMergedFamilyAtlasToFolder(const fs::path& target_dir, const FontFamilyInfo& fam) {
    if (fam.glyphs.empty() || fam.glyph_dim == 0) return;
    std::error_code ec;
    fs::create_directories(target_dir, ec);

    char base_name[112];
    snprintf(base_name, sizeof(base_name), "FAMILY_%016llX_font_%ux%u",
             static_cast<unsigned long long>(fam.family_hash),
             fam.glyph_dim, fam.glyph_dim);

    fs::path png_path = target_dir / (std::string(base_name) + ".png");
    fs::path txt_path = target_dir / (std::string(base_name) + ".txt");

    uint8_t pal_pc[16][4] = {};
    for (int c = 0; c < 16; ++c) {
        pal_pc[c][0] = fam.clut_ps2[c * 4 + 0];
        pal_pc[c][1] = fam.clut_ps2[c * 4 + 1];
        pal_pc[c][2] = fam.clut_ps2[c * 4 + 2];
        pal_pc[c][3] = Ps2AlphaToPc(fam.clut_ps2[c * 4 + 3]);
    }

    const uint32_t num_gl     = static_cast<uint32_t>(fam.glyphs.size());
    const uint32_t cols       = 8u;
    const uint32_t rows       = (num_gl + cols - 1u) / cols;
    const uint32_t dim        = fam.glyph_dim;
    const uint32_t atlas_w    = cols * dim;
    const uint32_t atlas_h    = rows * dim;
    const uint32_t tile_bytes = (dim * dim) / 2u;

    std::vector<uint8_t> atlas_rgba(static_cast<size_t>(atlas_w) * atlas_h * 4u, 0);
    for (uint32_t g = 0; g < num_gl; ++g) {
        const auto& ge = fam.glyphs[g];
        if (ge.tile_4b.size() < tile_bytes) continue;
        const uint8_t* tile_src = ge.tile_4b.data();
        uint32_t gx0 = (g % cols) * dim;
        uint32_t gy0 = (g / cols) * dim;
        for (uint32_t py = 0; py < dim; ++py) {
            for (uint32_t px = 0; px < dim; ++px) {
                uint32_t p_idx  = py * dim + px;
                uint8_t  byte_v = tile_src[p_idx >> 1];
                uint8_t  c_idx  = (p_idx & 1u) ? ((byte_v >> 4) & 0x0Fu) : (byte_v & 0x0Fu);
                size_t   dst    = (static_cast<size_t>(gy0 + py) * atlas_w + (gx0 + px)) * 4u;
                atlas_rgba[dst + 0] = pal_pc[c_idx][0];
                atlas_rgba[dst + 1] = pal_pc[c_idx][1];
                atlas_rgba[dst + 2] = pal_pc[c_idx][2];
                atlas_rgba[dst + 3] = pal_pc[c_idx][3];
            }
        }
    }

    std::vector<uint8_t> png_bytes;
    EncodeRgbaToPngBytes(atlas_rgba.data(), atlas_w, atlas_h, png_bytes);
    if (!png_bytes.empty()) {
        FILE* pf = nullptr;
        fopen_s(&pf, png_path.string().c_str(), "wb");
        if (pf) {
            fwrite(png_bytes.data(), 1, png_bytes.size(), pf);
            fclose(pf);
        }
    }

    FILE* tf = nullptr;
    fopen_s(&tf, txt_path.string().c_str(), "wb");
    if (tf) {
        fprintf(tf, "====================================================================\n");
        fprintf(tf, " Merged Font Family Atlas (Auto-Combined across %zu Dialogue/Menu Blocks)\n", fam.member_hashes.size());
        fprintf(tf, "====================================================================\n");
        fprintf(tf, "Family Hash:           %016llX\n", static_cast<unsigned long long>(fam.family_hash));
        fprintf(tf, "Grid Columns:          8\n");
        fprintf(tf, "Collected Glyphs:      %u (%ux%u, 8 columns)\n", num_gl, dim, dim);
        fprintf(tf, "Member Font Blocks:    %zu\n", fam.member_hashes.size());
        fprintf(tf, "To replace entire family in-game:\n");
        fprintf(tf, "  Copy edited %s.png (and this .txt file) into mods\\textures\\\n", base_name);
        fprintf(tf, "--------------------------------------------------------------------\n");
        fprintf(tf, "SAMPLE STRINGS FROM THIS FONT FAMILY:\n");
        for (size_t i = 0; i < fam.sample_strings.size(); ++i) {
            fprintf(tf, "  [%03zu] (Hash=%016llX) %s\n",
                    i, static_cast<unsigned long long>(fam.member_hashes[i]),
                    fam.sample_strings[i].c_str());
        }
        fprintf(tf, "--------------------------------------------------------------------\n");
        fprintf(tf, "GLYPH METRICS (Grid 8 columns):\n");
        for (uint32_t g = 0; g < num_gl; ++g) {
            const auto& ge = fam.glyphs[g];
            std::string ch_utf8;
            AppendUtf8Codepoint(ch_utf8, ge.char_code);
            fprintf(tf, "  Glyph #%03u (col=%u, row=%u): U+%04X '%s' | adv_x=%u, height=%u | TileHash=%016llX | ExactHash=%016llX\n",
                    g, g % 8u, g / 8u, ge.char_code, ch_utf8.c_str(),
                    ge.adv_x, ge.height,
                    static_cast<unsigned long long>(ge.bitmap_tile_hash),
                    static_cast<unsigned long long>(ge.exact_tile_hash));
        }
        fclose(tf);
    }
}

static void DumpMergedFamilyAtlas(const FontFamilyInfo& fam) {
    DumpMergedFamilyAtlasToFolder(g_FontDumpDir, fam);
}

// Preload existing MASTER_FONT_<dim>x<dim>.png + .txt from mods\textures_dump\fonts\ when DumpAllGims=1
// so that runtime font dumps accumulate glyphs across multiple play sessions instead of overwriting!
static void PreloadExistingDumpedMasterFontAtlases() {
    if (!g_DumpAllGims || !g_DumpMasterFontAtlas) return;
    std::error_code ec;
    if (!fs::exists(g_FontDumpDir, ec)) return;

    const uint16_t dims[3] = { 64u, 32u, 16u };
    for (uint16_t dim : dims) {
        char base_name[64];
        snprintf(base_name, sizeof(base_name), "MASTER_FONT_%ux%u", dim, dim);
        fs::path png_path = g_FontDumpDir / (std::string(base_name) + ".png");
        fs::path txt_path = g_FontDumpDir / (std::string(base_name) + ".txt");
        if (!fs::exists(png_path, ec) || !fs::exists(txt_path, ec)) continue;

        std::vector<uint8_t> atlas_rgba;
        uint32_t atlas_w = 0, atlas_h = 0;
        if (!DecodePngFileViaWic(png_path.string(), atlas_rgba, &atlas_w, &atlas_h)) continue;
        if (atlas_w < dim || atlas_h < dim) continue;

        FILE* tf = nullptr;
        fopen_s(&tf, txt_path.string().c_str(), "rb");
        if (!tf) continue;

        uint32_t cols = atlas_w / dim;
        if (cols == 0) cols = 16u;
        uint32_t preloaded_count = 0;

        char line[2048];
        while (fgets(line, sizeof(line), tf)) {
            unsigned int gc_val = 0;
            if (sscanf_s(line, "Grid Columns: %u", &gc_val) == 1 && gc_val > 0) {
                cols = gc_val;
                continue;
            }
            const char* u_pos  = std::strstr(line, "U+");
            const char* th_pos = std::strstr(line, "TileHash=");
            const char* eh_pos = std::strstr(line, "ExactHash=");
            const char* fm_pos = std::strstr(line, "Family=");
            if (u_pos && th_pos && eh_pos && fm_pos) {
                unsigned int g_idx = 0, col_v = 0, row_v = 0, cp_v = 0, adv_v = 0, h_v = 0;
                unsigned long long th_v = 0, eh_v = 0, fm_v = 0;
                if (sscanf_s(line, "  Glyph #%u (col=%u, row=%u):", &g_idx, &col_v, &row_v) != 3) continue;
                if (sscanf_s(u_pos + 2, "%04X", &cp_v) != 1) continue;
                const char* adv_pos = std::strstr(line, "adv_x=");
                if (adv_pos) {
                    sscanf_s(adv_pos, "adv_x=%u, height=%u", &adv_v, &h_v);
                }
                if (sscanf_s(th_pos + 9, "%016llX", &th_v) != 1 || th_v == 0) continue;
                if (sscanf_s(eh_pos + 10, "%016llX", &eh_v) != 1 || eh_v == 0) continue;
                if (sscanf_s(fm_pos + 7, "%016llX", &fm_v) != 1 || fm_v == 0) continue;

                uint32_t gx0 = col_v * dim;
                uint32_t gy0 = row_v * dim;
                if (gx0 + dim > atlas_w || gy0 + dim > atlas_h) continue;

                FamilyGlyphEntry ge{};
                ge.char_code        = static_cast<uint16_t>(cp_v);
                ge.adv_x            = static_cast<uint16_t>(adv_v);
                ge.height           = static_cast<uint16_t>(h_v);
                ge.glyph_dim        = dim;
                ge.exact_tile_hash  = static_cast<uint64_t>(eh_v);
                ge.bitmap_tile_hash = static_cast<uint64_t>(th_v);
                ge.family_hash      = static_cast<uint64_t>(fm_v);
                ge.rgba_tile.resize(static_cast<size_t>(dim) * dim * 4u);

                bool non_zero = false;
                for (uint32_t py = 0; py < dim; ++py) {
                    size_t src = (static_cast<size_t>(gy0 + py) * atlas_w + gx0) * 4u;
                    size_t dst = static_cast<size_t>(py) * dim * 4u;
                    std::memcpy(&ge.rgba_tile[dst], &atlas_rgba[src], static_cast<size_t>(dim) * 4u);
                    for (uint32_t px = 0; px < dim * 4u; ++px) {
                        if (ge.rgba_tile[dst + px] != 0) non_zero = true;
                    }
                }
                ge.is_non_empty = (ge.char_code > 0x20u) && non_zero;
                if (ge.is_non_empty) {
                    CleanRestoreAndCenter1xGlyphRgba(ge.rgba_tile, dim, ge.char_code, ge.adv_x, ge.height);
                }

                const char* ah_pos = std::strstr(line, "AltHashes=");
                if (ah_pos) {
                    const char* p_cur = ah_pos + 10;
                    while (*p_cur) {
                        while (*p_cur == ' ' || *p_cur == ',') ++p_cur;
                        if (!*p_cur || *p_cur == '|' || *p_cur == '\r' || *p_cur == '\n') break;
                        char* p_end = nullptr;
                        unsigned long long ah_val = std::strtoull(p_cur, &p_end, 16);
                        if (p_end == p_cur) break;
                        AddUniqueAltHashToGlyph(ge, static_cast<uint64_t>(ah_val));
                        p_cur = p_end;
                    }
                }

                FontFamilyInfo& fam = g_FontFamilies[ge.family_hash];
                if (fam.family_hash == 0) {
                    fam.family_hash = ge.family_hash;
                    fam.glyph_dim   = dim;
                    g_FontFamilyOrder.push_back(ge.family_hash);
                }
                bool exists = false;
                for (const auto& ex : fam.glyphs) {
                    if (ex.bitmap_tile_hash == ge.bitmap_tile_hash || ex.exact_tile_hash == ge.exact_tile_hash) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    fam.glyphs.push_back(std::move(ge));
                    ++preloaded_count;
                }
            }
        }
        fclose(tf);

        if (preloaded_count > 0) {
            for (uint64_t fh : g_FontFamilyOrder) {
                auto fit = g_FontFamilies.find(fh);
                if (fit != g_FontFamilies.end() && fit->second.glyph_dim == dim) {
                    SortFamilyGlyphs(fit->second.glyphs);
                }
            }
            ConsolidateOverlappingFontFamilies(dim);
            RebuildMasterFontAtlasForDim(dim);
            LogMsg("[ACZ_HDTex] Preloaded %u existing dumped %ux%u glyphs from %s (preserving previous session dumps)",
                   preloaded_count, dim, dim, png_path.filename().string().c_str());
        }
    }
}


// Parse companion .txt file (for MASTER_*, FAMILY_*, or individual <HASH>_font_*.png)
static void PreloadSharedGlyphsFromCompanionTxt(uint16_t slot_idx, uint64_t hash, const fs::path& png_path) {
    std::error_code ec;
    std::string stem = png_path.stem().string();
    std::string stem_low = stem;
    for (char& c : stem_low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    bool is_master_txt = (stem_low.rfind("master_", 0) == 0);
    bool is_family_txt = (!is_master_txt && stem_low.rfind("family_", 0) == 0);

    std::vector<fs::path> candidate_txts = {
        png_path.parent_path() / (stem + ".txt"),
        g_FontDumpDir / (stem + ".txt"),
        g_DumpDir / (stem + ".txt")
    };

    if (!is_master_txt) {
        char hex_prefix[20];
        snprintf(hex_prefix, sizeof(hex_prefix), "%016llX", static_cast<unsigned long long>(hash));
        std::string family_prefix = std::string("FAMILY_") + hex_prefix;

        const fs::path search_dirs[2] = { g_FontDumpDir, g_DumpDir };
        for (const auto& sdir : search_dirs) {
            if (fs::exists(sdir, ec)) {
                for (const auto& entry : fs::directory_iterator(sdir, ec)) {
                    if (!entry.is_regular_file(ec)) continue;
                    fs::path p = entry.path();
                    if (p.extension().string() != ".txt") continue;
                    std::string fn = p.stem().string();
                    if (fn.rfind(hex_prefix, 0) == 0 || fn.rfind(family_prefix, 0) == 0) {
                        candidate_txts.push_back(p);
                    }
                }
            }
        }
    }

    fs::path found_txt;
    for (const auto& cand : candidate_txts) {
        if (fs::exists(cand, ec)) {
            std::string cstem = cand.stem().string();
            for (char& c : cstem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            bool cand_is_master = (cstem.rfind("master_", 0) == 0);
            bool cand_is_fam    = (!cand_is_master && cstem.rfind("family_", 0) == 0);
            if (cand_is_master == is_master_txt && cand_is_fam == is_family_txt) {
                found_txt = cand;
                break;
            }
        }
    }
    if (found_txt.empty()) {
        for (const auto& cand : candidate_txts) {
            if (fs::exists(cand, ec)) {
                found_txt = cand;
                break;
            }
        }
    }
    if (found_txt.empty()) {
        if (is_master_txt) {
            LogMsg("[ACZ_HDTex] WARNING: Companion .txt file not found for Master Atlas: %s (expected %s.txt)",
                   png_path.filename().string().c_str(), stem.c_str());
        }
        return;
    }

    FILE* tf = nullptr;
    fopen_s(&tf, found_txt.string().c_str(), "rb");
    if (!tf) return;

    struct ParsedGlyphEntry {
        uint16_t              char_code  = 0;
        uint16_t              glyph_dim  = 0;
        uint16_t              adv_x      = 0;
        uint16_t              height     = 0;
        uint64_t              tile_hash  = 0;
        uint64_t              exact_hash = 0;
        uint64_t              fam_hash   = 0;
        std::vector<uint64_t> alt_hashes;
    };
    struct ParsedGim16Entry {
        uint64_t gim_hash   = 0;
        uint64_t exact_hash = 0;
        uint64_t fhm_hash   = 0;
    };

    uint16_t glyph_dim   = (stem_low.find("64x64") != std::string::npos) ? 64 : ((stem_low.find("16x16") != std::string::npos) ? 16 : 32);
    uint32_t grid_cols   = 0;
    uint64_t parsed_fam  = hash;
    std::vector<ParsedGlyphEntry> glyph_entries;
    std::vector<ParsedGim16Entry> gim16_entries;

    char line[2048];
    while (fgets(line, sizeof(line), tf)) {
        unsigned int gl_cnt = 0, dm_w = 0, dm_h = 0, gc_val = 0;
        unsigned long long fh_val = 0;
        if (sscanf_s(line, "Font Family Hash: %016llX", &fh_val) == 1 && fh_val != 0) {
            parsed_fam = static_cast<uint64_t>(fh_val);
        } else if (sscanf_s(line, "Family Hash: %016llX", &fh_val) == 1 && fh_val != 0) {
            parsed_fam = static_cast<uint64_t>(fh_val);
        } else if (sscanf_s(line, "Grid Columns: %u", &gc_val) == 1 && gc_val > 0) {
            grid_cols = gc_val;
        } else if (sscanf_s(line, "Glyph Dimension: %ux%u", &dm_w, &dm_h) == 2 && dm_w > 0) {
            glyph_dim = static_cast<uint16_t>(dm_w);
        } else if (sscanf_s(line, "Glyphs: %u (%ux%u, %u columns)", &gl_cnt, &dm_w, &dm_h, &gc_val) >= 3 && dm_w > 0) {
            glyph_dim = static_cast<uint16_t>(dm_w);
            if (gc_val > 0 && grid_cols == 0) grid_cols = gc_val;
        } else if (sscanf_s(line, "Collected Glyphs: %u (%ux%u, %u columns)", &gl_cnt, &dm_w, &dm_h, &gc_val) >= 3 && dm_w > 0) {
            glyph_dim = static_cast<uint16_t>(dm_w);
            if (gc_val > 0 && grid_cols == 0) grid_cols = gc_val;
        }

        const char* u_pos  = std::strstr(line, "U+");
        const char* th_pos = std::strstr(line, "TileHash=");
        if (u_pos && th_pos) {
            unsigned int cp = 0;
            unsigned long long th = 0;
            if (sscanf_s(u_pos + 2, "%04X", &cp) == 1 &&
                sscanf_s(th_pos + 9, "%016llX", &th) == 1 && th != 0) {
                ParsedGlyphEntry pe{};
                pe.char_code = static_cast<uint16_t>(cp);
                pe.tile_hash = static_cast<uint64_t>(th);
                pe.fam_hash  = parsed_fam;
                const char* adv_p = std::strstr(line, "adv_x=");
                if (adv_p) {
                    unsigned int ax_v = 0, ht_v = 0;
                    if (sscanf_s(adv_p, "adv_x=%u, height=%u", &ax_v, &ht_v) == 2) {
                        pe.adv_x  = static_cast<uint16_t>(ax_v);
                        pe.height = static_cast<uint16_t>(ht_v);
                    }
                }

                const char* eh_pos = std::strstr(line, "ExactHash=");
                if (eh_pos) {
                    unsigned long long eh = 0;
                    if (sscanf_s(eh_pos + 10, "%016llX", &eh) == 1) {
                        pe.exact_hash = static_cast<uint64_t>(eh);
                    }
                }
                const char* fm_pos = std::strstr(line, "Family=");
                if (fm_pos) {
                    unsigned long long fm = 0;
                    if (sscanf_s(fm_pos + 7, "%016llX", &fm) == 1 && fm != 0) {
                        pe.fam_hash = static_cast<uint64_t>(fm);
                    }
                }

                const char* ah_pos = std::strstr(line, "AltHashes=");
                if (ah_pos) {
                    const char* p_cur = ah_pos + 10;
                    while (*p_cur) {
                        while (*p_cur == ' ' || *p_cur == ',') ++p_cur;
                        if (!*p_cur || *p_cur == '|' || *p_cur == '\r' || *p_cur == '\n') break;
                        char* p_end = nullptr;
                        unsigned long long ah_val = std::strtoull(p_cur, &p_end, 16);
                        if (p_end == p_cur) break;
                        if (ah_val != 0) {
                            pe.alt_hashes.push_back(static_cast<uint64_t>(ah_val));
                        }
                        p_cur = p_end;
                    }
                }
                glyph_entries.push_back(std::move(pe));
            }
            continue;
        }

        const char* gh_pos = std::strstr(line, "GimHash=");
        if (gh_pos) {
            unsigned long long gh = 0;
            if (sscanf_s(gh_pos + 8, "%016llX", &gh) == 1 && gh != 0) {
                ParsedGim16Entry ge{};
                ge.gim_hash = static_cast<uint64_t>(gh);
                const char* ge_pos = std::strstr(line, "GimExactHash=");
                if (ge_pos) {
                    unsigned long long eh = 0;
                    if (sscanf_s(ge_pos + 13, "%016llX", &eh) == 1) ge.exact_hash = static_cast<uint64_t>(eh);
                }
                const char* gf_pos = std::strstr(line, "GimFhmHash=");
                if (gf_pos) {
                    unsigned long long fh = 0;
                    if (sscanf_s(gf_pos + 11, "%016llX", &fh) == 1) ge.fhm_hash = static_cast<uint64_t>(fh);
                }
                gim16_entries.push_back(ge);
            }
        }
    }
    fclose(tf);

    size_t total_cells = !glyph_entries.empty() ? glyph_entries.size() : gim16_entries.size();
    if (total_cells == 0) return;

    if (grid_cols == 0) {
        grid_cols = (is_master_txt && total_cells >= 16) ? 16u : 8u;
    }

    FontInspectionResult dummy_fres{};
    dummy_fres.valid      = true;
    dummy_fres.num_glyphs = static_cast<uint16_t>(total_cells);
    dummy_fres.glyph_dim  = glyph_dim;

    std::unordered_map<uint64_t, uint16_t> fam_std_h_map;
    if (!glyph_entries.empty()) {
        std::unordered_map<uint64_t, std::unordered_map<uint16_t, uint32_t>> alnum_counts;
        std::unordered_map<uint64_t, std::unordered_map<uint16_t, uint32_t>> all_counts;
        for (const auto& pe : glyph_entries) {
            if (pe.height == 0 || pe.char_code <= 0x20u) continue;
            all_counts[pe.fam_hash][pe.height]++;
            if ((pe.char_code >= 0x30u && pe.char_code <= 0x39u) ||
                (pe.char_code >= 0x41u && pe.char_code <= 0x5Au) ||
                (pe.char_code >= 0x61u && pe.char_code <= 0x7Au)) {
                alnum_counts[pe.fam_hash][pe.height]++;
            }
        }
        for (const auto& kv : all_counts) {
            uint64_t fh = kv.first;
            const auto& cmap = (alnum_counts.find(fh) != alnum_counts.end() && !alnum_counts[fh].empty())
                               ? alnum_counts[fh] : kv.second;
            uint16_t best_h = glyph_dim;
            uint32_t best_cnt = 0;
            for (const auto& hc : cmap) {
                if (hc.second > best_cnt || (hc.second == best_cnt && hc.first > best_h)) {
                    best_cnt = hc.second;
                    best_h   = hc.first;
                }
            }
            fam_std_h_map[fh] = best_h;
        }
    }

    std::vector<std::pair<uint16_t, uint16_t>> glyph_metrics;
    if (!glyph_entries.empty()) {
        glyph_metrics.reserve(glyph_entries.size());
        for (const auto& pe : glyph_entries) {
            uint16_t std_h = pe.height;
            auto fit = fam_std_h_map.find(pe.fam_hash);
            if (fit != fam_std_h_map.end() && fit->second > 0) {
                std_h = fit->second;
            }
            glyph_metrics.emplace_back(pe.adv_x, std_h);
        }
    }

    std::vector<uint8_t> atlas32_rgba;
    uint32_t atlas32_w = 0, atlas32_h = 0;
    CachedFontReplacement rep;
    if (!BuildPerGlyphHdTexturesForFont(
            slot_idx,
            hash,
            dummy_fres,
            grid_cols,
            rep,
            !glyph_metrics.empty() ? &glyph_metrics : nullptr,
            (glyph_dim == 32) ? &atlas32_rgba : nullptr,
            (glyph_dim == 32) ? &atlas32_w : nullptr,
            (glyph_dim == 32) ? &atlas32_h : nullptr)) {
        return;
    }

    if (!glyph_entries.empty()) {
        uint32_t registered = 0;
        for (size_t g = 0; g < glyph_entries.size() && g < rep.glyph_custom_ids.size(); ++g) {
            if (rep.glyph_custom_ids[g] == 0) continue;
            const auto& pe = glyph_entries[g];
            SharedGlyphReplacement sh{};
            sh.custom_id        = rep.glyph_custom_ids[g];
            sh.source_font_hash = hash;
            sh.family_hash      = pe.fam_hash;
            sh.glyph_dim        = glyph_dim;
            sh.char_code        = pe.char_code;
            sh.std_h            = (g < glyph_metrics.size()) ? glyph_metrics[g].second : pe.height;
            sh.is_master_atlas  = is_master_txt;

            if (pe.exact_hash != 0) {
                g_SharedGlyphByExactHash[pe.exact_hash] = sh;
            }
            if (pe.fam_hash != 0 && pe.tile_hash != 0) {
                g_SharedGlyphByFamilyBitmapKey[MakeFamilyBitmapKey(pe.fam_hash, pe.tile_hash)] = sh;
            }
            if (!is_master_txt || g_SharedGlyphByBitmapHash.find(pe.tile_hash) == g_SharedGlyphByBitmapHash.end()) {
                g_SharedGlyphByBitmapHash[pe.tile_hash] = sh;
            }
            for (uint64_t alt_h : pe.alt_hashes) {
                if (alt_h == 0) continue;
                if (g_SharedGlyphByExactHash.find(alt_h) == g_SharedGlyphByExactHash.end()) {
                    g_SharedGlyphByExactHash[alt_h] = sh;
                }
                if (g_SharedGlyphByBitmapHash.find(alt_h) == g_SharedGlyphByBitmapHash.end()) {
                    g_SharedGlyphByBitmapHash[alt_h] = sh;
                }
                if (pe.fam_hash != 0) {
                    g_SharedGlyphByFamilyBitmapKey[MakeFamilyBitmapKey(pe.fam_hash, alt_h)] = sh;
                }
            }
            uint32_t char_key = (static_cast<uint32_t>(glyph_dim) << 16) | static_cast<uint32_t>(pe.char_code);
            if (!is_master_txt || g_SharedGlyphByCharKey.find(char_key) == g_SharedGlyphByCharKey.end()) {
                g_SharedGlyphByCharKey[char_key] = std::move(sh);
            }
            ++registered;
        }

        if (!is_family_txt && !is_master_txt) {
            g_FontReplacements[hash] = std::move(rep);
        }

        if (registered > 0) {
            g_SharedGlyphEpoch.fetch_add(1, std::memory_order_release);
            LogMsg("[ACZ_HDTex] Preloaded %u HD %ux%u glyphs from %s (%s)",
                   registered, glyph_dim, glyph_dim,
                   is_master_txt ? "Master Font Atlas" : "companion metadata",
                   found_txt.filename().string().c_str());
        }

        // If this 32x32 Master Atlas contains cells #701..#703 (Subtitle Brackets GIM BB10DED7AB6393C6),
        // composite them into a 4x HD (tw32 x th32 = 128x128) GIM texture slot and bind 0xBB10DED7AB6393C6ULL!
        if (glyph_dim == 32 && !atlas32_rgba.empty() && grid_cols == 16u && total_cells >= 704u) {
            uint32_t rows32_all = (static_cast<uint32_t>(total_cells) + grid_cols - 1u) / grid_cols;
            uint32_t tw32 = atlas32_w / grid_cols;
            uint32_t th32 = (rows32_all > 0) ? (atlas32_h / rows32_all) : tw32;
            if (tw32 >= 32u && th32 == tw32 && g_Slots.size() < MAX_CUSTOM_TEXTURES) {
                const uint32_t sc = tw32 / 32u;
                std::vector<uint8_t> gim_hd_rgba(static_cast<size_t>(tw32) * th32 * 4u, 0);
                struct SubBracketPatch {
                    uint32_t cell_idx;
                    uint32_t src_x_1x, src_y_1x;
                    uint32_t dst_x_1x, dst_y_1x;
                    uint32_t w_1x, h_1x;
                };
                const SubBracketPatch patches[3] = {
                    { 701u, 10u,  3u,  0u,  0u, 11u, 25u }, // '<' -> (0..10, 0..24)
                    { 702u, 11u,  3u, 11u,  0u,  9u, 25u }, // '[' -> (11..19, 0..24)
                    { 703u,  8u, 13u,  0u, 25u, 15u,  7u }  // '-' -> (0..14, 25..31)
                };
                bool any_non_empty = false;
                for (const auto& pt : patches) {
                    if (pt.cell_idx >= total_cells) continue;
                    uint32_t cx0 = (pt.cell_idx % grid_cols) * tw32 + pt.src_x_1x * sc;
                    uint32_t cy0 = (pt.cell_idx / grid_cols) * th32 + pt.src_y_1x * sc;
                    uint32_t dx0 = pt.dst_x_1x * sc;
                    uint32_t dy0 = pt.dst_y_1x * sc;
                    uint32_t pw  = pt.w_1x * sc;
                    uint32_t ph  = pt.h_1x * sc;
                    for (uint32_t py = 0; py < ph; ++py) {
                        if (cy0 + py >= atlas32_h || dy0 + py >= th32) continue;
                        const uint8_t* srow = &atlas32_rgba[(static_cast<size_t>(cy0 + py) * atlas32_w + cx0) * 4u];
                        uint8_t*       drow = &gim_hd_rgba[(static_cast<size_t>(dy0 + py) * tw32 + dx0) * 4u];
                        std::memcpy(drow, srow, static_cast<size_t>(pw) * 4u);
                        for (uint32_t px = 0; px < pw; ++px) {
                            if (srow[px * 4u + 3] != 0) any_non_empty = true;
                        }
                    }
                }
                if (any_non_empty) {
                    constexpr uint64_t SUBTITLE_BRACKETS_GIM_HASH = 0xBB10DED7AB6393C6ULL;
                    constexpr uint64_t SUBTITLE_BRACKETS_FHM_HASH = 0x8734224A0CA3CE64ULL;
                    uint16_t sb_slot = static_cast<uint16_t>(g_Slots.size());
                    CustomSlotInfo sb_info;
                    sb_info.hash        = SUBTITLE_BRACKETS_GIM_HASH;
                    sb_info.png_path    = png_path.string();
                    sb_info.loaded_tex  = nullptr;
                    sb_info.load_failed = false;
                    EncodeRgbaToPngBytes(gim_hd_rgba.data(), tw32, th32, sb_info.mem_png);
                    g_Slots.push_back(std::move(sb_info));
                    g_SlotExpectedW[sb_slot].store(32u, std::memory_order_release);
                    g_SlotExpectedH[sb_slot].store(32u, std::memory_order_release);
                    g_HashToSlot[SUBTITLE_BRACKETS_GIM_HASH] = sb_slot;
                    g_HashToSlot[SUBTITLE_BRACKETS_FHM_HASH] = sb_slot;
                    LockFreeInsertContentHash(SUBTITLE_BRACKETS_GIM_HASH, sb_slot);
                    LockFreeInsertContentHash(SUBTITLE_BRACKETS_FHM_HASH, sb_slot);
                    LogMsg("[ACZ_HDTex] Composited 4x HD Subtitle Brackets GIM (Hash=%016llX, %ux%u) from cells #701..#703 of %s -> Slot #%u",
                           static_cast<unsigned long long>(SUBTITLE_BRACKETS_GIM_HASH), tw32, th32,
                           png_path.filename().string().c_str(), sb_slot);
                }
            }
        }

        // Check if companion .json (e.g. MASTER_EN_font_32x32.json) defines merged "glyphs_16x16" entries
        fs::path comp_json = png_path;
        comp_json.replace_extension(".json");
        if (glyph_dim == 32 && fs::exists(comp_json, ec) && fs::is_regular_file(comp_json, ec)) {
            FILE* jf = nullptr;
            fopen_s(&jf, comp_json.string().c_str(), "rb");
            if (jf) {
                fseek(jf, 0, SEEK_END);
                long jlen = ftell(jf);
                fseek(jf, 0, SEEK_SET);
                if (jlen > 0 && jlen <= 8 * 1024 * 1024) {
                    std::string jcontent(static_cast<size_t>(jlen), '\0');
                    size_t jread = fread(&jcontent[0], 1, jcontent.size(), jf);
                    jcontent.resize(jread);
                    const char* g16_arr = std::strstr(jcontent.c_str(), "\"glyphs_16x16\"");
                    if (g16_arr) {
                        bool decoded_atlas32 = (!atlas32_rgba.empty() && atlas32_w > 0 && atlas32_h > 0);
                        uint32_t rows32 = (static_cast<uint32_t>(total_cells) + grid_cols - 1u) / grid_cols;
                        std::unordered_map<uint32_t, uint32_t> remapped_16_slots;
                        uint32_t parsed_16_defs = 0;
                        uint32_t active_16_hd   = 0;

                        const char* cur = std::strchr(g16_arr, '[');
                        while (cur && *cur) {
                            const char* obj_start = std::strchr(cur, '{');
                            if (!obj_start) break;
                            const char* obj_end = std::strchr(obj_start, '}');
                            if (!obj_end) break;
                            std::string obj_str(obj_start, static_cast<size_t>(obj_end - obj_start + 1));
                            cur = obj_end + 1;

                            auto parse_u32_field = [&](const char* key, uint32_t def_val) -> uint32_t {
                                const char* p = std::strstr(obj_str.c_str(), key);
                                if (!p) return def_val;
                                p += std::strlen(key);
                                while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) ++p;
                                return static_cast<uint32_t>(std::strtoul(p, nullptr, 10));
                            };
                            auto parse_hex64_field = [&](const char* key) -> uint64_t {
                                const char* p = std::strstr(obj_str.c_str(), key);
                                if (!p) return 0;
                                p = std::strchr(p + std::strlen(key), '"');
                                if (!p) return 0;
                                return static_cast<uint64_t>(std::strtoull(p + 1, nullptr, 16));
                            };

                            uint32_t donor_cell = parse_u32_field("\"donor_cell_32\"", 0xFFFFFFFFu);
                            uint32_t src_dim    = parse_u32_field("\"src_dim\"", 24u);
                            uint32_t std_h16    = parse_u32_field("\"std_h16\"", 0u);
                            if (src_dim == 0 || src_dim > 32u) src_dim = 24u;
                            uint64_t th16       = parse_hex64_field("\"tile_hash\"");
                            uint64_t eh16       = parse_hex64_field("\"exact_hash\"");
                            uint64_t fm16       = parse_hex64_field("\"family_hash\"");
                            uint16_t cp16       = 0;
                            const char* cp_pos  = std::strstr(obj_str.c_str(), "U+");
                            if (cp_pos) {
                                cp16 = static_cast<uint16_t>(std::strtoul(cp_pos + 2, nullptr, 16));
                            }
                            if (donor_cell == 0xFFFFFFFFu || th16 == 0) continue;
                            ++parsed_16_defs;

                            if (donor_cell >= rep.glyph_custom_ids.size() || rep.glyph_custom_ids[donor_cell] == 0) {
                                continue;
                            }

                            uint32_t cid16 = 0;
                            if (src_dim >= 30u) {
                                cid16 = rep.glyph_custom_ids[donor_cell];
                                uint16_t d_slot = static_cast<uint16_t>(cid16 >> 16);
                                if (d_slot > 0 && d_slot <= MAX_CUSTOM_TEXTURES) {
                                    g_SlotAllowHalfDim[d_slot].store(true, std::memory_order_release);
                                }
                            } else {
                                uint32_t cache_key = (donor_cell << 8) | (src_dim & 0xFFu);
                                auto cit = remapped_16_slots.find(cache_key);
                                if (cit != remapped_16_slots.end()) {
                                    cid16 = cit->second;
                                } else {
                                    if (!decoded_atlas32) {
                                        decoded_atlas32 = DecodePngFileViaWic(png_path.string(), atlas32_rgba, &atlas32_w, &atlas32_h);
                                    }
                                    if (decoded_atlas32 && grid_cols > 0 && rows32 > 0) {
                                        uint32_t tw32 = atlas32_w / grid_cols;
                                        uint32_t th32 = atlas32_h / rows32;
                                        uint32_t tw16 = (tw32 >= 64u) ? (tw32 / 2u) : 64u;
                                        uint32_t th16 = (th32 >= 64u) ? (th32 / 2u) : 64u;
                                        uint32_t crop_w = (tw32 * src_dim) / 32u;
                                        uint32_t crop_h = (th32 * src_dim) / 32u;
                                        if (crop_w > 0 && crop_h > 0 && g_Slots.size() < MAX_CUSTOM_TEXTURES) {
                                            uint32_t gx0 = (donor_cell % grid_cols) * tw32;
                                            uint32_t gy0 = (donor_cell / grid_cols) * th32;
                                            std::vector<uint8_t> tile16_rgba(static_cast<size_t>(tw16) * th16 * 4u, 0);
                                            for (uint32_t dy = 0; dy < th16; ++dy) {
                                                float sy = (static_cast<float>(dy) + 0.5f) * static_cast<float>(crop_h) / static_cast<float>(th16) - 0.5f;
                                                int iy0 = static_cast<int>(sy);
                                                if (sy < 0.0f) { iy0 = 0; sy = 0.0f; }
                                                int iy1 = (iy0 + 1 < static_cast<int>(crop_h)) ? (iy0 + 1) : iy0;
                                                float fy = sy - static_cast<float>(iy0);
                                                for (uint32_t dx = 0; dx < tw16; ++dx) {
                                                    float sx = (static_cast<float>(dx) + 0.5f) * static_cast<float>(crop_w) / static_cast<float>(tw16) - 0.5f;
                                                    int ix0 = static_cast<int>(sx);
                                                    if (sx < 0.0f) { ix0 = 0; sx = 0.0f; }
                                                    int ix1 = (ix0 + 1 < static_cast<int>(crop_w)) ? (ix0 + 1) : ix0;
                                                    float fx = sx - static_cast<float>(ix0);

                                                    const uint8_t* p00 = &atlas32_rgba[(static_cast<size_t>(gy0 + iy0) * atlas32_w + (gx0 + ix0)) * 4u];
                                                    const uint8_t* p10 = &atlas32_rgba[(static_cast<size_t>(gy0 + iy0) * atlas32_w + (gx0 + ix1)) * 4u];
                                                    const uint8_t* p01 = &atlas32_rgba[(static_cast<size_t>(gy0 + iy1) * atlas32_w + (gx0 + ix0)) * 4u];
                                                    const uint8_t* p11 = &atlas32_rgba[(static_cast<size_t>(gy0 + iy1) * atlas32_w + (gx0 + ix1)) * 4u];

                                                    float w00 = (1.0f - fx) * (1.0f - fy);
                                                    float w10 = fx * (1.0f - fy);
                                                    float w01 = (1.0f - fx) * fy;
                                                    float w11 = fx * fy;

                                                    float a00 = static_cast<float>(p00[3]);
                                                    float a10 = static_cast<float>(p10[3]);
                                                    float a01 = static_cast<float>(p01[3]);
                                                    float a11 = static_cast<float>(p11[3]);
                                                    float a_out = w00 * a00 + w10 * a10 + w01 * a01 + w11 * a11;
                                                    uint8_t* dst = &tile16_rgba[(static_cast<size_t>(dy) * tw16 + dx) * 4u];
                                                    if (a_out >= 1.0f) {
                                                        for (int c = 0; c < 3; ++c) {
                                                            float pm = w00 * p00[c] * a00 + w10 * p10[c] * a10 + w01 * p01[c] * a01 + w11 * p11[c] * a11;
                                                            int cv = static_cast<int>(pm / a_out + 0.5f);
                                                            dst[c] = static_cast<uint8_t>(cv < 0 ? 0 : (cv > 255 ? 255 : cv));
                                                        }
                                                        int av = static_cast<int>(a_out + 0.5f);
                                                        dst[3] = static_cast<uint8_t>(av > 255 ? 255 : av);
                                                    }
                                                }
                                            }
                                            uint16_t g16_slot = static_cast<uint16_t>(g_Slots.size());
                                            CustomSlotInfo g16_info;
                                            g16_info.hash        = hash;
                                            g16_info.png_path    = png_path.string();
                                            g16_info.loaded_tex  = nullptr;
                                            g16_info.load_failed = false;
                                            EncodeRgbaToPngBytes(tile16_rgba.data(), tw16, th16, g16_info.mem_png);
                                            g_Slots.push_back(std::move(g16_info));
                                            g_SlotExpectedW[g16_slot].store(16u, std::memory_order_release);
                                            g_SlotExpectedH[g16_slot].store(16u, std::memory_order_release);
                                            cid16 = (static_cast<uint32_t>(g16_slot) << 16) | CUSTOM_EXTRATEX_TAG;
                                            remapped_16_slots[cache_key] = cid16;
                                        }
                                    }
                                }
                            }

                            if (cid16 == 0) continue;

                            SharedGlyphReplacement sh16{};
                            sh16.custom_id        = cid16;
                            sh16.source_font_hash = hash;
                            sh16.family_hash      = fm16;
                            sh16.glyph_dim        = 16u;
                            sh16.char_code        = cp16;
                            sh16.std_h            = static_cast<uint16_t>(std_h16);
                            sh16.is_master_atlas  = true;

                            if (eh16 != 0) g_SharedGlyphByExactHash[eh16] = sh16;
                            if (fm16 != 0 && th16 != 0) g_SharedGlyphByFamilyBitmapKey[MakeFamilyBitmapKey(fm16, th16)] = sh16;
                            if (g_SharedGlyphByBitmapHash.find(th16) == g_SharedGlyphByBitmapHash.end()) {
                                g_SharedGlyphByBitmapHash[th16] = sh16;
                            }

                            const char* alt_pos = std::strstr(obj_str.c_str(), "\"alt_hashes\"");
                            if (alt_pos) {
                                const char* ab_start = std::strchr(alt_pos, '[');
                                const char* ab_end   = ab_start ? std::strchr(ab_start, ']') : nullptr;
                                if (ab_start && ab_end) {
                                    const char* p_q = ab_start + 1;
                                    while (p_q < ab_end) {
                                        const char* q1 = std::strchr(p_q, '"');
                                        if (!q1 || q1 >= ab_end) break;
                                        const char* q2 = std::strchr(q1 + 1, '"');
                                        if (!q2 || q2 >= ab_end) break;
                                        uint64_t ah_val = static_cast<uint64_t>(std::strtoull(q1 + 1, nullptr, 16));
                                        if (ah_val != 0) {
                                            if (g_SharedGlyphByExactHash.find(ah_val) == g_SharedGlyphByExactHash.end()) {
                                                g_SharedGlyphByExactHash[ah_val] = sh16;
                                            }
                                            if (g_SharedGlyphByBitmapHash.find(ah_val) == g_SharedGlyphByBitmapHash.end()) {
                                                g_SharedGlyphByBitmapHash[ah_val] = sh16;
                                            }
                                            if (fm16 != 0) {
                                                g_SharedGlyphByFamilyBitmapKey[MakeFamilyBitmapKey(fm16, ah_val)] = sh16;
                                            }
                                        }
                                        p_q = q2 + 1;
                                    }
                                }
                            }

                            uint32_t ck16 = (16u << 16) | static_cast<uint32_t>(cp16);
                            if (g_SharedGlyphByCharKey.find(ck16) == g_SharedGlyphByCharKey.end()) {
                                g_SharedGlyphByCharKey[ck16] = std::move(sh16);
                            }
                            ++active_16_hd;
                        }

                        if (parsed_16_defs > 0) {
                            if (active_16_hd > 0) {
                                g_SharedGlyphEpoch.fetch_add(1, std::memory_order_release);
                            }
                            LogMsg("[ACZ_HDTex] Linked %u merged 16x16 glyphs (%u active HD cells) from companion JSON (%s)",
                                   parsed_16_defs, active_16_hd, comp_json.filename().string().c_str());
                        }
                    }
                }
                fclose(jf);
            }
        }
    } else if (!gim16_entries.empty()) {
        uint32_t registered_gims = 0;
        for (size_t g = 0; g < gim16_entries.size() && g < rep.glyph_custom_ids.size(); ++g) {
            uint16_t cell_slot = static_cast<uint16_t>(rep.glyph_custom_ids[g] >> 16);
            if (cell_slot == 0) continue;
            const auto& ge = gim16_entries[g];
            g_SlotExpectedW[cell_slot].store(16u, std::memory_order_release);
            g_SlotExpectedH[cell_slot].store(16u, std::memory_order_release);
            g_HashToSlot[ge.gim_hash] = cell_slot;
            if (ge.exact_hash != 0) g_HashToSlot[ge.exact_hash] = cell_slot;
            if (ge.fhm_hash   != 0) g_HashToSlot[ge.fhm_hash]   = cell_slot;
            ++registered_gims;
        }
        if (registered_gims > 0) {
            LogMsg("[ACZ_HDTex] Preloaded %u HD 16x16 GIM textures from Master 16x16 GIM Atlas (%s)",
                   registered_gims, found_txt.filename().string().c_str());
        }
    }
}

// Determine if a new font snapshot belongs to an existing FontFamilyInfo.
static uint64_t FindMatchingFontFamily(const OrigFontSnapshot& snap) {
    uint64_t best_family = 0;
    uint32_t best_exact  = 0;

    for (uint64_t fam_hash : g_FontFamilyOrder) {
        auto it = g_FontFamilies.find(fam_hash);
        if (it == g_FontFamilies.end()) continue;
        const FontFamilyInfo& fam = it->second;
        if (fam.glyph_dim != snap.glyph_dim || fam.glyphs.empty()) continue;

        uint32_t common_chars = 0;
        uint32_t exact_tiles  = 0;

        for (const auto& rec : snap.glyphs) {
            if (!rec.is_non_empty) continue;
            bool char_in_fam = false;
            bool tile_in_fam = false;
            for (const auto& fge : fam.glyphs) {
                if (fge.char_code == rec.char_code) {
                    char_in_fam = true;
                    if (fge.bitmap_tile_hash == rec.bitmap_tile_hash) {
                        tile_in_fam = true;
                        break;
                    }
                }
            }
            if (char_in_fam) {
                ++common_chars;
                if (tile_in_fam) {
                    ++exact_tiles;
                }
            }
        }

        if (exact_tiles >= 1 && common_chars > 0 && (exact_tiles * 4u >= common_chars * 3u)) {
            if (exact_tiles > best_exact) {
                best_exact  = exact_tiles;
                best_family = fam.family_hash;
            }
        }
    }

    return best_family;
}

static uint16_t FindDirectFontReplacementSlot(
    uint64_t primary_hash,
    const FontInspectionResult& fres,
    uint64_t* out_matched_hash
) {
    const uint64_t candidates[3] = {
        primary_hash,
        fres.hash_clean,
        fres.hash_visual
    };
    for (uint64_t cand : candidates) {
        auto it = g_HashToSlot.find(cand);
        if (it != g_HashToSlot.end()) {
            if (out_matched_hash) *out_matched_hash = cand;
            return it->second;
        }
    }
    if (out_matched_hash) *out_matched_hash = primary_hash;
    return 0;
}

static OrigFontSnapshot& GetOrCreateOrigFontSnapshotByHash(
    const uint8_t* start_ptr,
    const FontInspectionResult& fres
) {
    auto it = g_FontSnapshotsByHash.find(fres.hash_norm);
    if (it != g_FontSnapshotsByHash.end() &&
        it->second.num_glyphs == fres.num_glyphs &&
        it->second.glyph_dim == fres.glyph_dim) {
        uint64_t updated_fam = FindMatchingFontFamily(it->second);
        if (updated_fam != 0) {
            it->second.family_hash = updated_fam;
        }
        return it->second;
    }

    OrigFontSnapshot snap;
    snap.primary_hash = fres.hash_norm;
    snap.family_hash  = fres.hash_norm;
    snap.num_glyphs   = fres.num_glyphs;
    snap.glyph_dim    = fres.glyph_dim;
    snap.num_strings  = fres.num_strings;
    snap.total_size   = fres.total_size;
    std::memcpy(snap.orig_clut, fres.clut_ptr, 64);

    const uint32_t orig_tile_bytes = (static_cast<uint32_t>(fres.glyph_dim) * fres.glyph_dim) / 2u;
    const size_t total_tiles_bytes = static_cast<size_t>(fres.num_glyphs) * orig_tile_bytes;
    snap.orig_tiles.assign(fres.tiles_ptr, fres.tiles_ptr + total_tiles_bytes);
    snap.glyphs.resize(fres.num_glyphs);

    for (uint32_t g = 0; g < fres.num_glyphs; ++g) {
        const uint8_t* gm       = start_ptr + 16u + g * 16u;
        const uint8_t* tile_ptr = snap.orig_tiles.data() + static_cast<size_t>(g) * orig_tile_bytes;
        OrigGlyphRecord rec{};
        rec.adv_x     = *reinterpret_cast<const uint16_t*>(gm + 0);
        rec.height    = *reinterpret_cast<const uint16_t*>(gm + 2);
        rec.char_code = *reinterpret_cast<const uint16_t*>(gm + 8);

        bool non_zero = false;
        for (uint32_t b = 0; b < orig_tile_bytes; ++b) {
            if (tile_ptr[b] != 0) {
                non_zero = true;
                break;
            }
        }
        rec.is_non_empty     = (rec.char_code > 0x20u) && non_zero;
        rec.exact_tile_hash  = ComputeGlyphTileHash(fres.glyph_dim, rec.char_code, rec.adv_x, rec.height, snap.orig_clut, tile_ptr, orig_tile_bytes);
        rec.bitmap_tile_hash = ComputeGlyphTileHash(fres.glyph_dim, rec.char_code, rec.adv_x, rec.height, nullptr,        tile_ptr, orig_tile_bytes);
        rec.char_dim_key     = (static_cast<uint32_t>(fres.glyph_dim) << 16) | static_cast<uint32_t>(rec.char_code);
        snap.glyphs[g]       = rec;
    }

    uint64_t detected_family = FindMatchingFontFamily(snap);
    if (detected_family != 0) {
        snap.family_hash = detected_family;
    }

    g_FontSnapshotsByHash[fres.hash_norm] = std::move(snap);
    return g_FontSnapshotsByHash[fres.hash_norm];
}

static inline void MarkFontProcessedAtCurrentEpoch(uintptr_t font_addr) {
    uint32_t cur_epoch = g_SharedGlyphEpoch.load(std::memory_order_acquire);
    g_ProcessedFontEpoch[font_addr] = cur_epoch;
    g_FastFontEpochCache[(font_addr >> 4) & (FAST_FONT_CACHE_SIZE - 1)].store(
        font_addr ^ static_cast<uintptr_t>(cur_epoch),
        std::memory_order_release
    );
}

static void InspectAndProcessGameFont(uint8_t* font_obj, uint8_t* start_ptr, const char* caller_tag) {
    if (!font_obj || !start_ptr) return;

    uintptr_t font_addr = reinterpret_cast<uintptr_t>(start_ptr);

    std::lock_guard<std::mutex> lock(g_Mutex);

    // If we previously expanded glyph_desc[g].height (+0x02) to family std_h at this font_addr,
    // restore original heights via POD SEH helper before inspecting/hashing so hash_norm is 100% identical to disk!
    auto hit_orig = g_FontOrigHeights.find(font_addr);
    if (hit_orig != g_FontOrigHeights.end()) {
        if (!SafeRestoreFontOrigHeights(
                start_ptr,
                hit_orig->second.char_codes.data(),
                hit_orig->second.adv_xs.data(),
                hit_orig->second.orig_heights.data(),
                hit_orig->second.orig_heights.size())) {
            g_FontOrigHeights.erase(hit_orig);
        }
    }

    FontInspectionResult fres{};
    if (!SafeInspectAndHashGameFont(font_obj, start_ptr, &fres)) {
        MarkFontProcessedAtCurrentEpoch(font_addr);
        return;
    }

    InvalidatePixelBindingsInRange(font_addr, fres.total_size, font_addr);

    const uint32_t orig_tile_bytes = (static_cast<uint32_t>(fres.glyph_dim) * fres.glyph_dim) / 2u;

    OrigFontSnapshot& snap       = GetOrCreateOrigFontSnapshotByHash(start_ptr, fres);
    uint64_t primary_hash        = snap.primary_hash;
    uint64_t family_hash         = snap.family_hash;

    FontHeapInstance inst{};
    inst.primary_hash = primary_hash;
    inst.family_hash  = family_hash;
    inst.total_size   = fres.total_size;
    inst.num_glyphs   = fres.num_glyphs;
    inst.glyph_dim    = fres.glyph_dim;
    inst.num_strings  = fres.num_strings;
    g_FontHeapInstances[font_addr] = inst;

    uint64_t matched_hash = primary_hash;
    uint16_t direct_slot  = FindDirectFontReplacementSlot(primary_hash, fres, &matched_hash);

    // Check if glyphs of this font are already covered by a loaded Master Atlas or Family Atlas
    uint32_t pre_matched_glyphs = 0;
    for (uint32_t g = 0; g < fres.num_glyphs && g < snap.glyphs.size(); ++g) {
        const auto& grec = snap.glyphs[g];
        if (g_SharedGlyphByExactHash.find(grec.exact_tile_hash) != g_SharedGlyphByExactHash.end() ||
            g_SharedGlyphByFamilyBitmapKey.find(MakeFamilyBitmapKey(family_hash, grec.bitmap_tile_hash)) != g_SharedGlyphByFamilyBitmapKey.end() ||
            g_SharedGlyphByBitmapHash.find(grec.bitmap_tile_hash) != g_SharedGlyphByBitmapHash.end()) {
            ++pre_matched_glyphs;
        }
    }

    bool is_family_replaced = (direct_slot != 0) ||
                              (g_FamilyHashToSlot.find(family_hash) != g_FamilyHashToSlot.end()) ||
                              (g_HashToSlot.find(family_hash) != g_HashToSlot.end()) ||
                              (pre_matched_glyphs * 4u >= static_cast<uint32_t>(fres.num_glyphs) * 3u);

    // 1. Log & update FontFamilyInfo + MasterFontAtlasInfo on first sight of this font hash
    if (g_LoggedFontHashes.insert(primary_hash).second) {
        std::string preview = DecodeFontStringUtf8(start_ptr, fres, 0);
        if (preview.size() > 72) preview = preview.substr(0, 69) + "...";

        FontFamilyInfo& fam = g_FontFamilies[family_hash];
        if (fam.family_hash == 0) {
            fam.family_hash = family_hash;
            fam.glyph_dim   = fres.glyph_dim;
            std::memcpy(fam.clut_ps2, snap.orig_clut, 64);
            g_FontFamilyOrder.push_back(family_hash);
        }
        fam.member_hashes.push_back(primary_hash);
        if (fam.sample_strings.size() < 64) {
            fam.sample_strings.push_back(preview);
        }

        MasterFontAtlasInfo& mas = g_MasterFontAtlasesByDim[fres.glyph_dim];
        if (mas.glyph_dim == 0) {
            mas.glyph_dim = fres.glyph_dim;
        }
        mas.member_hashes.push_back(primary_hash);
        if (mas.sample_strings.size() < 64) {
            mas.sample_strings.push_back(preview);
        }

        uint32_t new_family_glyphs = 0;
        bool family_grew     = false;
        bool alt_hashes_grew = false;

        uint16_t block_std_h = fres.glyph_dim;
        {
            std::unordered_map<uint16_t, uint32_t> h_cnts;
            for (const auto& fge : fam.glyphs) {
                if (fge.height > 0 && fge.char_code > 0x20u) h_cnts[fge.height]++;
            }
            for (const auto& grec : snap.glyphs) {
                if (grec.height > 0 && grec.char_code > 0x20u) h_cnts[grec.height]++;
            }
            uint32_t best_c = 0;
            for (const auto& hc : h_cnts) {
                if (hc.second > best_c || (hc.second == best_c && hc.first > block_std_h)) {
                    best_c      = hc.second;
                    block_std_h = hc.first;
                }
            }
        }

        for (uint32_t g = 0; g < fres.num_glyphs && g < snap.glyphs.size(); ++g) {
            const auto& grec = snap.glyphs[g];
            const uint8_t* tptr = snap.orig_tiles.data() + static_cast<size_t>(g) * orig_tile_bytes;

            FamilyGlyphEntry cand_ge{};
            cand_ge.char_code        = grec.char_code;
            cand_ge.adv_x            = grec.adv_x;
            cand_ge.height           = grec.height;
            cand_ge.glyph_dim        = fres.glyph_dim;
            cand_ge.is_non_empty     = grec.is_non_empty;
            cand_ge.exact_tile_hash  = grec.exact_tile_hash;
            cand_ge.bitmap_tile_hash = grec.bitmap_tile_hash;
            cand_ge.family_hash      = family_hash;
            std::memcpy(cand_ge.clut_ps2, snap.orig_clut, 64);
            cand_ge.tile_4b.assign(tptr, tptr + orig_tile_bytes);
            Decode4BitGlyphTileToRgba(fres.glyph_dim, snap.orig_clut, tptr, cand_ge.rgba_tile);
            CleanRestoreAndCenter1xGlyphRgba(cand_ge.rgba_tile, fres.glyph_dim, grec.char_code, grec.adv_x, grec.height, block_std_h);

            bool exists_in_fam = false;
            for (auto& existing_ge : fam.glyphs) {
                if (existing_ge.bitmap_tile_hash == grec.bitmap_tile_hash ||
                    (existing_ge.char_code == grec.char_code && AreGlyphEntriesVisuallyIdentical(existing_ge, cand_ge, fres.glyph_dim))) {
                    if (AddUniqueAltHashToGlyph(existing_ge, grec.bitmap_tile_hash)) alt_hashes_grew = true;
                    if (AddUniqueAltHashToGlyph(existing_ge, grec.exact_tile_hash))  alt_hashes_grew = true;
                    exists_in_fam = true;
                    break;
                }
            }
            if (!exists_in_fam) {
                fam.glyphs.push_back(std::move(cand_ge));
                family_grew = true;
                ++new_family_glyphs;
            }
        }

        if (family_grew || alt_hashes_grew) {
            SortFamilyGlyphs(fam.glyphs);
            ConsolidateOverlappingFontFamilies(fres.glyph_dim);
            family_hash = snap.family_hash;
            g_FontHeapInstances[font_addr].family_hash = family_hash;
            RebuildMasterFontAtlasForDim(fres.glyph_dim);
        }

        bool allow_dump_for_replacement = (!is_family_replaced || g_DumpReplacedTextures);
        bool should_dump_individual = false;
        if (g_DumpAllGims && g_DumpIndividualFontAtlases && allow_dump_for_replacement) {
            if (g_DumpSingleLineSubtitles || fres.num_strings > 1 || fres.num_glyphs >= 32 || new_family_glyphs > 0) {
                should_dump_individual = true;
            }
        }

        LogMsg("[ACZ_HDTex] [%s] Seen Dynamic Font Hash=%016llX (Family=%016llX, Clean=%016llX | %u glyphs %ux%u (%u new in family), %u strings, %u bytes) | Str[0]: \"%s\"",
               caller_tag,
               static_cast<unsigned long long>(primary_hash),
               static_cast<unsigned long long>(family_hash),
               static_cast<unsigned long long>(fres.hash_clean),
               fres.num_glyphs, fres.glyph_dim, fres.glyph_dim,
               new_family_glyphs,
               fres.num_strings, fres.total_size,
               preview.c_str());

        if (g_DumpAllGims && g_DumpMasterFontAtlas && (family_grew || alt_hashes_grew)) {
            DumpMasterFontAtlasToFolder(g_FontDumpDir, g_MasterFontAtlasesByDim[fres.glyph_dim]);
        }
        if (should_dump_individual) {
            DumpDynamicFontAtlasAndInfo(start_ptr, fres, snap, primary_hash);
        }
        if (g_DumpAllGims && allow_dump_for_replacement && g_DumpMergedFamilyAtlases && family_grew) {
            auto fit_cur = g_FontFamilies.find(family_hash);
            if (fit_cur != g_FontFamilies.end() && fit_cur->second.member_hashes.size() > 1) {
                DumpMergedFamilyAtlas(fit_cur->second);
            }
        }
    }

    uint8_t* runtime_tiles_ptr = *reinterpret_cast<uint8_t**>(font_obj + 0x18);
    if (!runtime_tiles_ptr) runtime_tiles_ptr = fres.tiles_ptr;

    // 2A. Direct Font Atlas Replacement (if user placed <HASH>.png in mods/textures)
    if (direct_slot != 0) {
        auto git = g_FontReplacements.find(matched_hash);
        bool newly_built = false;
        if (g_HotReload || git == g_FontReplacements.end() || git->second.glyph_custom_ids.size() != fres.num_glyphs) {
            CachedFontReplacement rep;
            if (!BuildPerGlyphHdTexturesForFont(direct_slot, matched_hash, fres, 0, rep)) {
                MarkFontProcessedAtCurrentEpoch(font_addr);
                return;
            }
            g_FontReplacements[matched_hash] = std::move(rep);
            git = g_FontReplacements.find(matched_hash);
            newly_built = true;
        }

        const CachedFontReplacement& rep = git->second;

        if (g_AutoShareFontGlyphs) {
            uint32_t shared_cnt = 0;
            for (uint32_t g = 0; g < fres.num_glyphs && g < snap.glyphs.size(); ++g) {
                if (g >= rep.glyph_custom_ids.size() || rep.glyph_custom_ids[g] == 0) continue;
                const OrigGlyphRecord& grec = snap.glyphs[g];

                SharedGlyphReplacement sh{};
                sh.custom_id        = rep.glyph_custom_ids[g];
                sh.source_font_hash = matched_hash;
                sh.family_hash      = family_hash;
                sh.glyph_dim        = fres.glyph_dim;
                sh.char_code        = grec.char_code;
                sh.is_master_atlas  = false;

                g_SharedGlyphByExactHash[grec.exact_tile_hash]                                          = sh;
                g_SharedGlyphByFamilyBitmapKey[MakeFamilyBitmapKey(family_hash, grec.bitmap_tile_hash)] = sh;
                g_SharedGlyphByBitmapHash[grec.bitmap_tile_hash]                                        = sh;
                g_SharedGlyphByCharKey[grec.char_dim_key]                                               = std::move(sh);
                ++shared_cnt;
            }
            if (shared_cnt > 0 && newly_built) {
                uint32_t new_ep = g_SharedGlyphEpoch.fetch_add(1, std::memory_order_release) + 1u;
                LogMsg("[ACZ_HDTex] Registered %u shared HD glyphs from Font Hash=%016llX (Family=%016llX, %ux%u, epoch=%u)",
                       shared_cnt, static_cast<unsigned long long>(matched_hash),
                       static_cast<unsigned long long>(family_hash),
                       fres.glyph_dim, fres.glyph_dim, new_ep);
            }
        }

        for (uint32_t g = 0; g < fres.num_glyphs; ++g) {
            if (g < rep.glyph_custom_ids.size() && rep.glyph_custom_ids[g] != 0) {
                uintptr_t tile_addr = reinterpret_cast<uintptr_t>(runtime_tiles_ptr + g * orig_tile_bytes);
                RegisterReplacedPixelAddress(tile_addr, rep.glyph_custom_ids[g], fres.glyph_dim, fres.glyph_dim);
                uintptr_t static_tile_addr = reinterpret_cast<uintptr_t>(fres.tiles_ptr + g * orig_tile_bytes);
                if (static_tile_addr != tile_addr) {
                    RegisterReplacedPixelAddress(static_tile_addr, rep.glyph_custom_ids[g], fres.glyph_dim, fres.glyph_dim);
                }
            }
        }

        MarkFontProcessedAtCurrentEpoch(font_addr);

        if (g_LoggedBoundFontHashes.insert(primary_hash).second) {
            LogMsg("[ACZ_HDTex] [%s] Bound %u/%u HD glyph textures (direct atlas) for Dynamic Font Hash=%016llX",
                   caller_tag, fres.num_glyphs, fres.num_glyphs, static_cast<unsigned long long>(matched_hash));
        }
        return;
    }

    // 2B. Automatic Per-Glyph Inheritance (From Master Atlas, Same Font Family, or >=75% glyph coverage)
    if (g_AutoShareFontGlyphs && (!g_SharedGlyphByExactHash.empty() || !g_SharedGlyphByBitmapHash.empty())) {
        bool allow_char_fallback = g_ForceGlobalCharCodeSharing ||
                                   (g_ShareCharCodeToSingleLineSubs && fres.num_strings == 1);

        std::vector<const SharedGlyphReplacement*> candidate_matches(fres.num_glyphs, nullptr);
        uint32_t matched_total       = 0;
        uint32_t matched_same_family = 0;
        uint32_t matched_from_master = 0;
        uint64_t donor_hash          = 0;

        for (uint32_t g = 0; g < fres.num_glyphs && g < snap.glyphs.size(); ++g) {
            const OrigGlyphRecord& grec = snap.glyphs[g];
            const SharedGlyphReplacement* matched_sh = nullptr;

            // Priority 1: ExactHash (matches glyph_dim + char_code + metrics + 64-byte CLUT palette + 4-bit tile)
            auto it_exact = g_SharedGlyphByExactHash.find(grec.exact_tile_hash);
            if (it_exact != g_SharedGlyphByExactHash.end()) {
                matched_sh = &it_exact->second;
            } else {
                // Priority 2: Same Family + BitmapTileHash
                auto it_fb = g_SharedGlyphByFamilyBitmapKey.find(MakeFamilyBitmapKey(family_hash, grec.bitmap_tile_hash));
                if (it_fb != g_SharedGlyphByFamilyBitmapKey.end()) {
                    matched_sh = &it_fb->second;
                } else {
                    // Priority 3: BitmapTileHash fallback
                    auto it_bmp = g_SharedGlyphByBitmapHash.find(grec.bitmap_tile_hash);
                    if (it_bmp != g_SharedGlyphByBitmapHash.end()) {
                        matched_sh = &it_bmp->second;
                    } else if (allow_char_fallback) {
                        auto it_chr = g_SharedGlyphByCharKey.find(grec.char_dim_key);
                        if (it_chr != g_SharedGlyphByCharKey.end()) {
                            matched_sh = &it_chr->second;
                        }
                    }
                }
            }

            if (matched_sh != nullptr && matched_sh->custom_id != 0) {
                candidate_matches[g] = matched_sh;
                ++matched_total;
                if (matched_sh->is_master_atlas) {
                    ++matched_from_master;
                }
                if (matched_sh->family_hash == family_hash || matched_sh->source_font_hash == family_hash) {
                    ++matched_same_family;
                }
                donor_hash = matched_sh->source_font_hash;
            }
        }

        bool should_apply_inheritance = (matched_from_master > 0) ||
                                        (matched_same_family > 0) ||
                                        (matched_total * 4u >= static_cast<uint32_t>(fres.num_glyphs) * 3u) ||
                                        allow_char_fallback;

        if (should_apply_inheritance && matched_total > 0) {
            SavedFontOrigHeights saved_hs{};
            if (snap.glyphs.size() == fres.num_glyphs) {
                saved_hs.char_codes.resize(fres.num_glyphs);
                saved_hs.adv_xs.resize(fres.num_glyphs);
                saved_hs.orig_heights.resize(fres.num_glyphs);
                saved_hs.expanded_heights.resize(fres.num_glyphs);
                for (uint32_t g = 0; g < fres.num_glyphs; ++g) {
                    saved_hs.char_codes[g]       = snap.glyphs[g].char_code;
                    saved_hs.adv_xs[g]           = snap.glyphs[g].adv_x;
                    saved_hs.orig_heights[g]     = snap.glyphs[g].height;
                    saved_hs.expanded_heights[g] = snap.glyphs[g].height;
                }
            }
            for (uint32_t g = 0; g < fres.num_glyphs; ++g) {
                const SharedGlyphReplacement* matched_sh = candidate_matches[g];
                if (matched_sh == nullptr) continue;

                if (matched_sh->is_master_atlas && matched_sh->std_h > 0 && matched_sh->std_h <= fres.glyph_dim) {
                    if (g < saved_hs.expanded_heights.size() && saved_hs.expanded_heights[g] < matched_sh->std_h) {
                        saved_hs.expanded_heights[g] = matched_sh->std_h;
                    }
                }

                uintptr_t tile_addr = reinterpret_cast<uintptr_t>(runtime_tiles_ptr + g * orig_tile_bytes);
                RegisterReplacedPixelAddress(tile_addr, matched_sh->custom_id, fres.glyph_dim, fres.glyph_dim);
                uintptr_t static_tile_addr = reinterpret_cast<uintptr_t>(fres.tiles_ptr + g * orig_tile_bytes);
                if (static_tile_addr != tile_addr) {
                    RegisterReplacedPixelAddress(static_tile_addr, matched_sh->custom_id, fres.glyph_dim, fres.glyph_dim);
                }
            }
            if (!saved_hs.expanded_heights.empty()) {
                SafeApplyFontExpandedHeights(start_ptr, saved_hs.expanded_heights.data(), saved_hs.expanded_heights.size());
                g_FontOrigHeights[font_addr] = std::move(saved_hs);
            }

            if (g_LoggedBoundFontHashes.insert(primary_hash).second) {
                LogMsg("[ACZ_HDTex] [%s] Auto-bound %u/%u HD glyph textures (%s) for Dynamic Font Hash=%016llX (Family=%016llX, Donor=%016llX)",
                       caller_tag, matched_total, fres.num_glyphs,
                       matched_from_master > 0 ? "Master Atlas" : "Family Inheritance",
                       static_cast<unsigned long long>(primary_hash),
                       static_cast<unsigned long long>(family_hash),
                       static_cast<unsigned long long>(donor_hash));
            }
        }
    }

    MarkFontProcessedAtCurrentEpoch(font_addr);
}

// ============================================================================
// Cross-DLL Diagnostic Bridge for memdump.dll (Exported API)
// Allows memdump.dll to export live GIMs, Fonts, Master Atlases, and 4MB GS VRAM
// into the dump folder whenever F2 / Exit / Crash dump is triggered!
// ============================================================================
extern "C" __declspec(dllexport) void __fastcall AczHdTex_DumpLiveStateForQoL(
    const wchar_t* dump_folder_w,
    const char*    report_path_a,
    int            dump_live_resources
) {
    if (!dump_folder_w || !report_path_a) return;

    fs::path dump_folder(dump_folder_w);
    fs::path live_gims_dir  = dump_folder / "live_resources" / "gims";
    fs::path live_fonts_dir = dump_folder / "live_resources" / "fonts";

    void* gs_vram_ctx = g_LastGsVramCtx.load(std::memory_order_acquire);
    const uint8_t* vram_base = SafeReadGsVramPtr(gs_vram_ctx);
    bool vram_dumped = false;
    if (vram_base != nullptr) {
        std::vector<uint8_t> vram_copy(PS2_GS_VRAM_SIZE);
        if (SafeCopyMemoryBlock(vram_base, vram_copy.data(), PS2_GS_VRAM_SIZE)) {
            fs::path vram_path = dump_folder / "ps2_gs_vram_4mb.bin";
            FILE* vf = nullptr;
            fopen_s(&vf, vram_path.string().c_str(), "wb");
            if (vf) {
                fwrite(vram_copy.data(), 1, vram_copy.size(), vf);
                fclose(vf);
                vram_dumped = true;
            }
        }
    }

    size_t bloom_bits_set = CountBloomPopulatedBits();
    double bloom_pct      = (100.0 * static_cast<double>(bloom_bits_set)) / static_cast<double>(BLOOM_WORD_COUNT * 64u);

    std::unique_lock<std::mutex> lock(g_Mutex, std::try_to_lock);

    FILE* rf = nullptr;
    fopen_s(&rf, report_path_a, "ab");
    if (!rf) return;

    fprintf(rf, "\n================================================================================\n");
    fprintf(rf, " ACZ_HDTEX.DLL (%s) LIVE TEXTURE & FONT ENGINE STATE\n", PLUGIN_VERSION_STR);
    fprintf(rf, "================================================================================\n");
    fprintf(rf, "acz_hdtex.dll Base:        %p\n", reinterpret_cast<void*>(g_SelfModule));
    fprintf(rf, "PS2 GS VRAM Context:       %p (4 MB VRAM Base: %p, Dumped: %s)\n",
            gs_vram_ctx, reinterpret_cast<const void*>(vram_base), vram_dumped ? "YES" : "NO");
    fprintf(rf, "PS2 GS Render Context:     %p\n", g_LastGsRenderCtx.load(std::memory_order_relaxed));
    fprintf(rf, "Shared Glyph Epoch:        %u\n", g_SharedGlyphEpoch.load(std::memory_order_relaxed));
    fprintf(rf, "Replaced Pixel Range:      [%p .. %p]\n",
            reinterpret_cast<void*>(g_MinReplacedPixelAddr.load(std::memory_order_relaxed)),
            reinterpret_cast<void*>(g_MaxReplacedPixelAddr.load(std::memory_order_relaxed)));
    fprintf(rf, "512-Kbit Bloom Occupancy:  %zu / %zu bits (%.3f%%)\n",
            bloom_bits_set, BLOOM_WORD_COUNT * 64u, bloom_pct);

    if (lock.owns_lock()) {
        fprintf(rf, "Active Pixel Bindings:     %zu active pointers | %zu unique font snapshots\n",
                g_ActivePixelBindings.size(), g_FontSnapshotsByHash.size());

        fprintf(rf, "\n--------------------------------------------------------------------------------\n");
        fprintf(rf, " 1. DYNAMIC TILE FONTS TRACKED IN RAM (%zu active heap instances)\n", g_FontHeapInstances.size());
        fprintf(rf, "--------------------------------------------------------------------------------\n");
        uint32_t live_fonts_cnt = 0;
        for (const auto& kv : g_FontHeapInstances) {
            uintptr_t font_addr          = kv.first;
            const FontHeapInstance& inst = kv.second;
            uint8_t* start_ptr           = reinterpret_cast<uint8_t*>(font_addr);

            auto hit_orig_qol = g_FontOrigHeights.find(font_addr);
            bool restored_qol = false;
            if (hit_orig_qol != g_FontOrigHeights.end()) {
                restored_qol = SafeRestoreFontOrigHeights(
                    start_ptr,
                    hit_orig_qol->second.char_codes.data(),
                    hit_orig_qol->second.adv_xs.data(),
                    hit_orig_qol->second.orig_heights.data(),
                    hit_orig_qol->second.orig_heights.size()
                );
            }

            FontInspectionResult live_fres{};
            bool is_live_same = SafeInspectAndHashGameFont(nullptr, start_ptr, &live_fres) &&
                                (live_fres.hash_norm == inst.primary_hash);

            std::string str0;
            if (is_live_same) {
                ++live_fonts_cnt;
                str0 = DecodeFontStringUtf8(start_ptr, live_fres, 0);
                if (str0.size() > 65) str0 = str0.substr(0, 62) + "...";
                if (dump_live_resources != 0 && g_DumpIndividualFontAtlases) {
                    auto sit = g_FontSnapshotsByHash.find(inst.primary_hash);
                    if (sit != g_FontSnapshotsByHash.end()) {
                        DumpDynamicFontAtlasToFolder(live_fonts_dir, start_ptr, live_fres, sit->second, inst.primary_hash, true);
                    }
                }
            }

            if (restored_qol && hit_orig_qol != g_FontOrigHeights.end()) {
                SafeApplyFontExpandedHeights(
                    start_ptr,
                    hit_orig_qol->second.expanded_heights.data(),
                    hit_orig_qol->second.expanded_heights.size()
                );
            }

            bool is_bound = (g_LoggedBoundFontHashes.find(inst.primary_hash) != g_LoggedBoundFontHashes.end());
            fprintf(rf, "  Addr=%p | Status=%-14s | Hash=%016llX | Fam=%016llX | %3ugl %ux%u | %3u strs | HD=%s | Str[0]: \"%s\"\n",
                    reinterpret_cast<void*>(font_addr),
                    is_live_same ? "LIVE_IN_RAM" : "OVERWRITTEN",
                    static_cast<unsigned long long>(inst.primary_hash),
                    static_cast<unsigned long long>(inst.family_hash),
                    inst.num_glyphs, inst.glyph_dim, inst.glyph_dim,
                    inst.num_strings,
                    is_bound ? "YES" : "NO ",
                    str0.c_str());
        }
        fprintf(rf, "  -> Summary: %u / %zu Dynamic Font heap instances are currently LIVE_IN_RAM.\n",
                live_fonts_cnt, g_FontHeapInstances.size());

        if (dump_live_resources != 0) {
            for (auto& mkv : g_MasterFontAtlasesByDim) {
                RebuildMasterFontAtlasForDim(mkv.first);
                DumpMasterFontAtlasToFolder(live_fonts_dir, mkv.second);
            }
            if (!g_MasterGim16Tiles.empty()) {
                DumpMasterGim16AtlasToFolder(live_fonts_dir);
            }
        }

        fprintf(rf, "\n--------------------------------------------------------------------------------\n");
        fprintf(rf, " 2. GIM TEXTURE BUFFERS TRACKED IN RAM (%zu active heap instances)\n", g_GimFastCache.size());
        fprintf(rf, "--------------------------------------------------------------------------------\n");
        uint32_t live_gims_cnt = 0;
        for (const auto& kv : g_GimFastCache) {
            uintptr_t gim_addr            = kv.first;
            const GimFastCacheEntry& cent = kv.second;
            const uint8_t* gim_ptr        = reinterpret_cast<const uint8_t*>(gim_addr);

            GimQuickInfo live_qinfo{};
            GimInspectionResult live_gres{};
            bool is_valid_gim = SafeQuickInspectGim(gim_ptr, &live_qinfo) &&
                                SafeInspectAndHashGim(gim_ptr, &live_gres);

            bool is_same_texture = is_valid_gim &&
                                   (live_qinfo.total_size == cent.total_size) &&
                                   (live_qinfo.quick_fp == cent.quick_fp);

            uint64_t export_hash = is_same_texture ? cent.primary_hash : (is_valid_gim ? live_gres.hash_psm_norm : 0ULL);
            uint8_t  export_psm  = is_same_texture ? cent.orig_psm     : (is_valid_gim ? live_gres.psm           : 0u);

            if (is_valid_gim) {
                ++live_gims_cnt;
                if (dump_live_resources != 0) {
                    DumpGimBufferToFolder(
                        live_gims_dir,
                        gim_ptr,
                        live_gres,
                        export_hash,
                        export_psm,
                        true,
                        (is_same_texture && cent.has_orig_headers) ? cent.orig_hdr      : nullptr,
                        (is_same_texture && cent.has_orig_headers) ? cent.orig_clut_hdr : nullptr
                    );
                }
            }

            uint16_t slot_idx = 0;
            if ((cent.custom_id & 0xFFFFu) == CUSTOM_EXTRATEX_TAG) {
                slot_idx = static_cast<uint16_t>(cent.custom_id >> 16);
            } else if (IsBuiltinHudFont000Id(cent.custom_id)) {
                slot_idx = g_BuiltinExtratexSlot[0].load(std::memory_order_relaxed);
            } else if (IsBuiltinHudFont001Id(cent.custom_id)) {
                slot_idx = g_BuiltinExtratexSlot[1].load(std::memory_order_relaxed);
            }
            const char* status_str = "UNMAPPED/NON_GIM";
            if (is_valid_gim) {
                status_str = is_same_texture ? "LIVE_IN_RAM" : "NEW_GIM_AT_ADDR";
            }
            fprintf(rf, "  Addr=%p | Status=%-16s | DiskHash=%016llX | FhmNorm=%016llX | %4ux%-4u PSM=0x%02X | Size=%7u | Replaced=%s (Slot #%u)\n",
                    reinterpret_cast<void*>(gim_addr),
                    status_str,
                    static_cast<unsigned long long>(export_hash),
                    static_cast<unsigned long long>(is_valid_gim ? live_gres.hash_fhm_norm : 0ULL),
                    is_valid_gim ? live_gres.width : 0u,
                    is_valid_gim ? live_gres.height : 0u,
                    export_psm,
                    cent.total_size,
                    slot_idx != 0 ? "YES" : "NO ",
                    slot_idx);
        }
        fprintf(rf, "  -> Summary: %u / %zu GIM Buffers are currently LIVE_IN_RAM.\n",
                live_gims_cnt, g_GimFastCache.size());

        if (!g_Tim2FastCache.empty()) {
            fs::path live_tim2_dir = dump_folder / "live_resources" / "tim2";
            fprintf(rf, "\n--------------------------------------------------------------------------------\n");
            fprintf(rf, " 2B. TIM2 TEXTURE BUFFERS TRACKED IN RAM (%zu active instances)\n", g_Tim2FastCache.size());
            fprintf(rf, "--------------------------------------------------------------------------------\n");
            uint32_t live_tim2_cnt = 0;
            for (const auto& kv : g_Tim2FastCache) {
                uintptr_t pix_addr             = kv.first;
                const Tim2FastCacheEntry& tent = kv.second;
                const uint8_t* pix_ptr         = reinterpret_cast<const uint8_t*>(pix_addr);
                Tim2InspectionResult live_tres{};
                bool is_valid_tim2 = SafeInspectAndHashTim2(pix_ptr, &live_tres);
                if (is_valid_tim2) {
                    ++live_tim2_cnt;
                    if (dump_live_resources != 0) {
                        DumpTim2BufferToFolder(live_tim2_dir, pix_ptr, live_tres, live_tres.hash_tim2_norm, true);
                    }
                }
                uint16_t slot_idx = static_cast<uint16_t>(tent.custom_id >> 16);
                fprintf(rf, "  PixAddr=%p | Status=%-14s | Tim2Hash=%016llX | %4ux%-4u PSM=0x%02X | Size=%6u | Replaced=%s (Slot #%u)\n",
                        reinterpret_cast<void*>(pix_addr),
                        is_valid_tim2 ? "LIVE_IN_RAM" : "OVERWRITTEN",
                        static_cast<unsigned long long>(is_valid_tim2 ? live_tres.hash_tim2_norm : tent.primary_hash),
                        tent.width, tent.height, tent.psm, tent.total_size,
                        slot_idx != 0 ? "YES" : "NO ", slot_idx);
            }
            fprintf(rf, "  -> Summary: %u / %zu TIM2 Buffers are currently LIVE_IN_RAM.\n",
                    live_tim2_cnt, g_Tim2FastCache.size());
        }

        fprintf(rf, "\n--------------------------------------------------------------------------------\n");
        fprintf(rf, " 3. COLLECTED FONT FAMILIES (%zu families)\n", g_FontFamilies.size());
        fprintf(rf, "--------------------------------------------------------------------------------\n");
        for (uint64_t fam_hash : g_FontFamilyOrder) {
            auto fit = g_FontFamilies.find(fam_hash);
            if (fit == g_FontFamilies.end()) continue;
            const FontFamilyInfo& fam = fit->second;
            fprintf(rf, "  FamilyHash=%016llX | Dim=%ux%u | UniqueGlyphs=%zu | MemberBlocks=%zu | Sample: \"%s\"\n",
                    static_cast<unsigned long long>(fam.family_hash),
                    fam.glyph_dim, fam.glyph_dim,
                    fam.glyphs.size(),
                    fam.member_hashes.size(),
                    fam.sample_strings.empty() ? "" : fam.sample_strings[0].c_str());
            if (dump_live_resources != 0 && g_DumpMergedFamilyAtlases && fam.member_hashes.size() > 1) {
                DumpMergedFamilyAtlasToFolder(live_fonts_dir, fam);
            }
        }

        uint32_t loaded_d3d12_slots = 0;
        for (size_t s = 1; s < g_Slots.size(); ++s) {
            if (g_Slots[s].loaded_tex != nullptr) ++loaded_d3d12_slots;
        }
        fprintf(rf, "\n--------------------------------------------------------------------------------\n");
        fprintf(rf, " 4. CUSTOM HD TEXTURE SLOTS (%zu total slots, %u uploaded to D3D12 GPU)\n",
                g_Slots.size() > 0 ? g_Slots.size() - 1 : 0, loaded_d3d12_slots);
        fprintf(rf, "--------------------------------------------------------------------------------\n");
        for (size_t s = 1; s < g_Slots.size(); ++s) {
            const CustomSlotInfo& sl = g_Slots[s];
            if (sl.loaded_tex == nullptr && !sl.load_failed) continue;
            uint32_t lw = 0, lh = 0, ltbp = 0, lcbp = 0, lsrv = 0, gpu_w = 0, gpu_h = 0;
            if (sl.loaded_tex != nullptr) {
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex + 0x14), &lw);
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex + 0x18), &lh);
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex + 0x20), &ltbp);
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex + 0x24), &lcbp);
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex - 0x2C), &lsrv);
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex - 0x24), &gpu_w);
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex - 0x20), &gpu_h);
            }
            fprintf(rf, "  Slot #%05zu | Hash=%016llX | GsTexObj=%p | GPU=%ux%u (Logical=%ux%u) | TBP=0x%04X CBP=0x%04X SRV=#%u | %s\n",
                    s, static_cast<unsigned long long>(sl.hash),
                    reinterpret_cast<void*>(sl.loaded_tex),
                    gpu_w, gpu_h, lw, lh, ltbp, lcbp, lsrv,
                    sl.png_path.c_str());
        }
    } else {
        fprintf(rf, "\n[WARNING] g_Mutex in acz_hdtex.dll was locked by another thread during dump; skipped internal tables.\n");
    }

    fclose(rf);
}

static void ProcessGimDescriptorIfNeeded(uint8_t* desc, const char* caller_tag) {
    if (!desc) return;

    uint8_t* gim_data = *reinterpret_cast<uint8_t**>(desc + 0x08);
    if (!gim_data || gim_data == desc + 0x68) return; // Skip .swa TIM2 descriptors (handled in Hook_GsUploadVramBlock)

    uint32_t current_id = *reinterpret_cast<uint32_t*>(desc + 0x40);

    uintptr_t gim_addr = reinterpret_cast<uintptr_t>(gim_data);
    size_t cache_idx   = (gim_addr >> 4) & (FAST_GIM_CACHE_SIZE - 1);

    uint64_t live_tag = 0;
    if (!g_HotReload &&
        g_FastGimTable[cache_idx].gim_addr.load(std::memory_order_acquire) == gim_addr &&
        SafeReadGimContentTag(gim_data, &live_tag) &&
        g_FastGimTable[cache_idx].content_tag.load(std::memory_order_acquire) == live_tag) {
        uint32_t cached_cid = g_FastGimTable[cache_idx].custom_id.load(std::memory_order_acquire);
        if (cached_cid != 0 || (current_id & 0xFFFF) == CUSTOM_EXTRATEX_TAG) {
            if (current_id != cached_cid) {
                *reinterpret_cast<uint32_t*>(desc + 0x40) = cached_cid;
            }
        }
        return;
    }

    uint64_t inspected_tag = 0;
    uint32_t custom_id = InspectGimBufferAndGetCustomId(gim_data, caller_tag, &inspected_tag);
    // Preserve built-in extratex IDs (0x00000001 / 0x00010001) when custom_id == 0 so GsFlushDrawBatch
    // uses vector[1][0..1] while keeping the 1x PS2 VRAM texture in [rbp-0x60] for CropGsTextureRegion!
    if (custom_id != 0 || current_id == 0 || (current_id & 0xFFFF) == CUSTOM_EXTRATEX_TAG) {
        *reinterpret_cast<uint32_t*>(desc + 0x40) = custom_id;
    }

    g_FastGimTable[cache_idx].content_tag.store(inspected_tag, std::memory_order_release);
    g_FastGimTable[cache_idx].custom_id.store(custom_id, std::memory_order_release);
    g_FastGimTable[cache_idx].gim_addr.store(gim_addr, std::memory_order_release);
}

// ----------------------------------------------------------------------------
// Hook 1: InitGimDescriptor (+0x2038C0)
// ----------------------------------------------------------------------------
static void __fastcall Hook_InitGimDescriptor(uint8_t* desc, uint8_t* gim_data) {
    uint32_t custom_id     = 0;
    uint64_t inspected_tag = 0;
    if (gim_data != nullptr) {
        custom_id = InspectGimBufferAndGetCustomId(gim_data, "InitGimPre", &inspected_tag);
    }

    g_OrigInitGimDescriptor(desc, gim_data);

    if (desc != nullptr) {
        uint32_t current_id = *reinterpret_cast<uint32_t*>(desc + 0x40);
        if (custom_id != 0 || current_id == 0 || (current_id & 0xFFFF) == CUSTOM_EXTRATEX_TAG) {
            *reinterpret_cast<uint32_t*>(desc + 0x40) = custom_id;
        }
        if (gim_data != nullptr) {
            uintptr_t gim_addr = reinterpret_cast<uintptr_t>(gim_data);
            size_t cache_idx   = (gim_addr >> 4) & (FAST_GIM_CACHE_SIZE - 1);
            g_FastGimTable[cache_idx].content_tag.store(inspected_tag, std::memory_order_release);
            g_FastGimTable[cache_idx].custom_id.store(custom_id, std::memory_order_release);
            g_FastGimTable[cache_idx].gim_addr.store(gim_addr, std::memory_order_release);
        }
    }
}

// ----------------------------------------------------------------------------
// Hook 2: BuildGimGifPacket (+0x203B30)
// ----------------------------------------------------------------------------
static uint8_t* __fastcall Hook_BuildGimGifPacket(uint8_t* desc, uint8_t* gif_buf) {
    ProcessGimDescriptorIfNeeded(desc, "BuildGif");
    return g_OrigBuildGimGifPacket(desc, gif_buf);
}

// ----------------------------------------------------------------------------
// Hook 3: BuildGimPixelOnlyGifPacket (+0x203D90)
// ----------------------------------------------------------------------------
static uint8_t* __fastcall Hook_BuildGimPixelOnlyGifPacket(uint8_t* desc, uint8_t* gif_buf) {
    ProcessGimDescriptorIfNeeded(desc, "BuildGifPix");
    uint8_t* ret = g_OrigBuildGimPixelOnlyGifPacket(desc, gif_buf);

    if (desc && gif_buf && *reinterpret_cast<uint64_t*>(desc + 0x10) != 0) {
        uint32_t ext_id = *reinterpret_cast<uint32_t*>(desc + 0x40);
        if (ext_id != 0) {
            *reinterpret_cast<uint32_t*>(gif_buf + 0x54) = ext_id;
        }
    }
    return ret;
}

// ----------------------------------------------------------------------------
// Hook 4: BuildGimArrayGifPackets (+0x14CA50)
// ----------------------------------------------------------------------------
static uint8_t* __fastcall Hook_BuildGimArrayGifPackets(uint8_t* array_ctx, uint8_t* gif_buf) {
    if (array_ctx) {
        uint32_t count = *reinterpret_cast<uint32_t*>(array_ctx + 0x00);
        uint8_t* arr   = *reinterpret_cast<uint8_t**>(array_ctx + 0x10);
        if (arr && count > 0 && count <= 1024) {
            for (uint32_t i = 0; i < count; ++i) {
                ProcessGimDescriptorIfNeeded(arr + (i * 0x48), "BuildGifArr");
            }
        }
    }
    return g_OrigBuildGimArrayGifPackets(array_ctx, gif_buf);
}

// ----------------------------------------------------------------------------
// Forward declarations for aspect resolution and hot-reload checking:
static inline int ResolveSlotEffectiveAspectTier(uint16_t slot_idx);
static bool ShouldHotReloadSlotTier(uint16_t slot_idx, int eff_tier);

// ----------------------------------------------------------------------------
// Hook 5: GsUploadVramBlock (+0x191A00)
// 100% Lock-Free Range + 512-Kbit Bloom Filter + 64K Open-Addressed Table Lookup!
// ----------------------------------------------------------------------------
static void __fastcall Hook_GsUploadVramBlock(
    void* gs_vram_ctx,
    uint32_t dbp,
    uint32_t rrw,
    uint32_t rrh,
    uint32_t qwc,
    uint8_t* src_data,
    uint32_t dpsm,
    uint32_t extratex_id
) {
    if (gs_vram_ctx != nullptr && g_LastGsVramCtx.load(std::memory_order_relaxed) != gs_vram_ctx) {
        g_LastGsVramCtx.store(gs_vram_ctx, std::memory_order_release);
    }

    bool is_builtin_pad = IsBuiltinPadTexId(extratex_id);
    bool is_builtin_tex = IsBuiltinTextureExtId(extratex_id);
    bool is_gim_or_tim2_upload = false;
    if ((extratex_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG && !is_builtin_pad && !is_builtin_tex && src_data != nullptr) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(src_data);

        // 1. Direct GIM pixel upload check first (GIM pixel payload starts at gim_data + 0x20)
        if (addr > 0x10020u) {
            uint8_t* gim_data = src_data - 0x20;
            bool may_be_gim = false;
            if ((addr & 0xFFFu) >= 0x20u) {
                may_be_gim = (*reinterpret_cast<const uint32_t*>(gim_data) == 0x004D4947u); // "GIM\0"
            } else {
                uint32_t magic = 0;
                may_be_gim = SafeReadU32(addr - 0x20u, &magic) && (magic == 0x004D4947u);
            }
            if (may_be_gim) {
                is_gim_or_tim2_upload = true;
                uint64_t live_tag = 0;
                if (SafeReadGimContentTag(gim_data, &live_tag)) {
                    uintptr_t gim_addr = addr - 0x20u;
                    size_t cache_idx   = (gim_addr >> 4) & (FAST_GIM_CACHE_SIZE - 1);
                    if (!g_HotReload &&
                        g_FastGimTable[cache_idx].gim_addr.load(std::memory_order_acquire) == gim_addr &&
                        g_FastGimTable[cache_idx].content_tag.load(std::memory_order_acquire) == live_tag) {
                        uint32_t cached_cid = g_FastGimTable[cache_idx].custom_id.load(std::memory_order_acquire);
                        if (cached_cid != 0) {
                            extratex_id = cached_cid;
                        }
                    } else {
                        uint64_t inspected_tag = 0;
                        uint32_t custom_id = InspectGimBufferAndGetCustomId(gim_data, "GsUploadGim", &inspected_tag);
                        g_FastGimTable[cache_idx].content_tag.store(inspected_tag, std::memory_order_release);
                        g_FastGimTable[cache_idx].custom_id.store(custom_id, std::memory_order_release);
                        g_FastGimTable[cache_idx].gim_addr.store(gim_addr, std::memory_order_release);
                        if (custom_id != 0) {
                            extratex_id = custom_id;
                        }
                    }
                }
            }
        }

        // 2. Direct TIM2 pixel upload check second (SWAD TIM2 starts at tim2_base + 0x60 or + 0x40)
        //    Checked before g_LockFreePixelTable so freed GameFont glyph heap addresses never hijack TIM2 sprites!
        if (!is_gim_or_tim2_upload && (extratex_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG && addr > 0x10060u) {
            bool may_be_tim2 = false;
            if ((addr & 0xFFFu) >= 0x60u) {
                may_be_tim2 = (*reinterpret_cast<const uint32_t*>(src_data - 0x20) == 0x00745865u &&
                               *reinterpret_cast<const uint32_t*>(src_data - 0x10) == 0x58444947u) ||
                              (*reinterpret_cast<const uint32_t*>(src_data - 0x40) == 0x324D4954u);
            } else {
                uint32_t m_ext = 0, m_gidx = 0, m_tim2 = 0;
                may_be_tim2 = (SafeReadU32(addr - 0x20u, &m_ext) && m_ext == 0x00745865u &&
                               SafeReadU32(addr - 0x10u, &m_gidx) && m_gidx == 0x58444947u) ||
                              (SafeReadU32(addr - 0x40u, &m_tim2) && m_tim2 == 0x324D4954u);
            }
            if (may_be_tim2) {
                is_gim_or_tim2_upload = true;
                uint64_t live_tag = 0;
                if (SafeReadTim2ContentTag(src_data, &live_tag)) {
                    size_t cache_idx = (addr >> 4) & (FAST_GIM_CACHE_SIZE - 1);
                    if (!g_HotReload &&
                        g_FastTim2Table[cache_idx].gim_addr.load(std::memory_order_acquire) == addr &&
                        g_FastTim2Table[cache_idx].content_tag.load(std::memory_order_acquire) == live_tag) {
                        uint32_t cached_cid = g_FastTim2Table[cache_idx].custom_id.load(std::memory_order_acquire);
                        if (cached_cid != 0) {
                            extratex_id = cached_cid;
                        }
                    } else {
                        uint64_t inspected_tag = 0;
                        uint32_t custom_id = InspectTim2BufferAndGetCustomId(src_data, "GsUploadTim2", &inspected_tag);
                        g_FastTim2Table[cache_idx].content_tag.store(inspected_tag, std::memory_order_release);
                        g_FastTim2Table[cache_idx].custom_id.store(custom_id, std::memory_order_release);
                        g_FastTim2Table[cache_idx].gim_addr.store(addr, std::memory_order_release);
                        if (custom_id != 0) {
                            extratex_id = custom_id;
                        }
                    }
                }
            }
        }

        // 3. Registered pixel address table (for GameFont glyph tiles and pre-bound descriptors)
        if (!is_gim_or_tim2_upload && (extratex_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG) {
            if (addr >= g_MinReplacedPixelAddr.load(std::memory_order_relaxed) &&
                addr <= g_MaxReplacedPixelAddr.load(std::memory_order_relaxed)) {
                if (BloomTestPixelAddr(addr)) {
                    uint32_t bound_id = LockFreeLookupPixelBinding(addr, rrw, rrh);
                    if (bound_id != 0) {
                        extratex_id = bound_id;
                    }
                }
            }
        }

        // 4. Fallback: Lock-free content hash check on scratch TBP0=0x3000 .swa TIM2 buttons (40x40 qwc=50 or 64x64 qwc=128)
        if (!is_gim_or_tim2_upload && (extratex_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG && dbp == 0x3000u && (qwc == 50u || qwc == 128u)) {
            uint64_t pix_hash = SafeHashByteRange(src_data, static_cast<size_t>(qwc) * 16u);
            uint16_t h_slot   = LockFreeLookupContentHash(pix_hash);
            if (h_slot != 0) {
                extratex_id = (static_cast<uint32_t>(h_slot) << 16) | CUSTOM_EXTRATEX_TAG;
                is_gim_or_tim2_upload = true;
            }
        }
    }

    // Track per-TBP active custom_id (including native Category 2 gamepad extratex_id and built-in HUD fonts 000/001.png) and preserve across CLUT uploads
    bool is_builtin_hud = IsBuiltinHudFont000Id(extratex_id) || IsBuiltinHudFont001Id(extratex_id);
    bool is_clut_upload = (qwc == 4u && rrw == 8u && rrh == 2u) || (qwc == 64u && rrw == 16u && rrh == 16u);
    if (!is_clut_upload) {
        g_LastUploadWasSkipped.store(false, std::memory_order_relaxed);
        uint32_t tbp_idx = dbp & 0x3FFFu;
        g_LastTbpOrigW[tbp_idx].store(static_cast<uint16_t>(rrw), std::memory_order_relaxed);
        g_LastTbpOrigH[tbp_idx].store(static_cast<uint16_t>(rrh), std::memory_order_relaxed);
        if (qwc >= 8u && ((extratex_id & 0xFFFF) == CUSTOM_EXTRATEX_TAG || is_builtin_pad || is_builtin_tex || is_builtin_hud)) {
            g_LastTbpCustomId[tbp_idx].store(extratex_id, std::memory_order_release);
            if (is_gim_or_tim2_upload || is_builtin_pad || is_builtin_tex || is_builtin_hud) {
                g_LastUploadedTim2CustomId.store(extratex_id, std::memory_order_release);
            } else {
                g_LastUploadedTim2CustomId.store(0, std::memory_order_release);
            }
            // Zero-overhead fast-path for scratch TBP0=0x3000 (.swa TIM2 sprites): if this sprite is replaced by an
            // already-loaded HD texture on the GPU, skip g_OrigGsUploadVramBlock so acz.exe does not evict and recreate
            // D3D12 textures or stall on the worker mutex every time multiple .swa sprites share TBP0=0x3000 in one frame!
            if (tbp_idx == 0x3000u && (is_gim_or_tim2_upload || is_builtin_pad || is_builtin_tex) && !is_builtin_hud) {
                uint16_t check_slot = 0;
                uint32_t pad_idx = 0;
                uint32_t tex_idx = 0;
                if (IsBuiltinPadTexId(extratex_id, &pad_idx)) {
                    check_slot = g_BuiltinPadExtratexSlot[pad_idx].load(std::memory_order_acquire);
                } else if (IsBuiltinTextureExtId(extratex_id, &tex_idx) && tex_idx < 32) {
                    check_slot = g_BuiltinTextureExtratexSlot[tex_idx].load(std::memory_order_acquire);
                } else {
                    check_slot = static_cast<uint16_t>(extratex_id >> 16);
                }
                if (check_slot > 0 && check_slot <= MAX_CUSTOM_TEXTURES) {
                    int eff_tier = ResolveSlotEffectiveAspectTier(check_slot);
                    uint8_t* loaded_hd = g_SlotLoadedTexTier[eff_tier][check_slot].load(std::memory_order_acquire);
                    if (loaded_hd == nullptr && eff_tier == 0) {
                        loaded_hd = g_SlotLoadedTex[check_slot].load(std::memory_order_acquire);
                    }
                    if (loaded_hd != nullptr && !ShouldHotReloadSlotTier(check_slot, eff_tier)) {
                        g_LastUploadWasSkipped.store(true, std::memory_order_relaxed);
                        return;
                    }
                }
            }
        } else {
            g_LastTbpCustomId[tbp_idx].store(0, std::memory_order_release);
            g_LastUploadedTim2CustomId.store(0, std::memory_order_release);
        }
    } else {
        bool prev_skipped = g_LastUploadWasSkipped.exchange(false, std::memory_order_relaxed);
        if (extratex_id == 0) {
            uint32_t prev_cid = g_LastUploadedTim2CustomId.load(std::memory_order_acquire);
            if ((prev_cid & 0xFFFF) == CUSTOM_EXTRATEX_TAG || IsBuiltinPadTexId(prev_cid) || IsBuiltinTextureExtId(prev_cid) || IsBuiltinHudFont000Id(prev_cid) || IsBuiltinHudFont001Id(prev_cid)) {
                extratex_id = prev_cid;
                if (prev_skipped) {
                    return;
                }
            }
        }
    }

    g_OrigGsUploadVramBlock(gs_vram_ctx, dbp, rrw, rrh, qwc, src_data, dpsm, extratex_id);
}

// ----------------------------------------------------------------------------
// Hook 7: GameFont_Init (+0x1FFBD0)
// ----------------------------------------------------------------------------
static bool __fastcall Hook_GameFontInit(uint8_t* font_obj, uint8_t** data_ptr_in_out) {
    uint8_t* start_ptr = (data_ptr_in_out != nullptr) ? *data_ptr_in_out : nullptr;
    bool ok = g_OrigGameFontInit(font_obj, data_ptr_in_out);
    if (ok && font_obj != nullptr && start_ptr != nullptr) {
        InspectAndProcessGameFont(font_obj, start_ptr, "GameFontInit");
    }
    return ok;
}

// ----------------------------------------------------------------------------
// Hook 8: GameFont_UploadStringGlyphs (+0x1FFCD0)
// ----------------------------------------------------------------------------
static bool __fastcall Hook_GameFontUploadStringGlyphs(
    uint8_t* font_obj,
    int string_idx,
    uint32_t* vram_tbp_cursor,
    uint8_t** gif_buf_in_out
) {
    if (font_obj != nullptr && (font_obj[0x52] & 1) != 0) {
        uint8_t* glyph_meta = *reinterpret_cast<uint8_t**>(font_obj + 0x08);
        if (glyph_meta != nullptr && reinterpret_cast<uintptr_t>(glyph_meta) > 0x10010) {
            uint8_t*  start_ptr  = glyph_meta - 0x10;
            uintptr_t font_addr  = reinterpret_cast<uintptr_t>(start_ptr);
            uint32_t  cur_epoch  = g_SharedGlyphEpoch.load(std::memory_order_acquire);
            uintptr_t expected   = font_addr ^ static_cast<uintptr_t>(cur_epoch);
            size_t    cache_idx  = (font_addr >> 4) & (FAST_FONT_CACHE_SIZE - 1);

            if (g_FastFontEpochCache[cache_idx].load(std::memory_order_acquire) != expected) {
                bool up_to_date = false;
                {
                    std::lock_guard<std::mutex> lock(g_Mutex);
                    auto it = g_ProcessedFontEpoch.find(font_addr);
                    if (it != g_ProcessedFontEpoch.end() && it->second == cur_epoch) {
                        up_to_date = true;
                        g_FastFontEpochCache[cache_idx].store(expected, std::memory_order_release);
                    }
                }
                if (!up_to_date) {
                    InspectAndProcessGameFont(font_obj, start_ptr, "GameFontUpload");
                }
            }
        }
    }

    return g_OrigGameFontUploadStringGlyphs(font_obj, string_idx, vram_tbp_cursor, gif_buf_in_out);
}

static int GetGameAspectOption() {
    __try {
        const int32_t* pScreenSize = reinterpret_cast<const int32_t*>(g_ExeBase + AczRva(RVA_ScreenSizeOption));
        int32_t val = *pScreenSize;
        if (val >= 2) return 2; // 2 = 21:9 ultrawide
        if (val == 1) return 1; // 1 = 16:9 widescreen
        return 0;               // 0 = 4:3 standard
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static bool IsGameWidescreenEnabled() {
    return GetGameAspectOption() != 0;
}

// Returns the active aspect-ratio tier: 0 = 4:3, 1 = 16:9, 2 = 21:9
static inline int GetActiveTextureAspectTier() {
    if (!g_EnableMenuOverrides || g_MenuAspectMode == 2) return 0;
    if (g_MenuAspectMode == 3) return 2;
    if (g_MenuAspectMode == 1) return 1;
    return GetGameAspectOption();
}

// Selects the highest-priority available texture tier for a slot given the active aspect ratio:
//   - Active 21:9 (tier 2): prefers 21x9 (2) -> 16x9 (1) -> 4x3 (0)
//   - Active 16:9 (tier 1): prefers 16x9 (1) -> 4x3 (0)  (or 21x9 if only 21x9 exists)
//   - Active 4:3  (tier 0): prefers 4x3  (0) -> 16x9 (1) -> 21x9 (2)
// Pure POD SEH helper (no C++ objects with destructors, avoiding MSVC C2712)
using FnPollAllInputDevices = void(__fastcall*)(uint8_t* state_array);
static FnPollAllInputDevices g_OrigPollAllInputDevices = nullptr;
static std::atomic<int> s_last_active_input_variant{-1};

static bool IsForegroundKeyboardActive() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD fg_pid = 0;
    GetWindowThreadProcessId(fg, &fg_pid);
    if (fg_pid != GetCurrentProcessId()) return false;
    static const int kKeys[] = {
        VK_RETURN, VK_ESCAPE, VK_SPACE, VK_BACK, VK_TAB,
        VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
        VK_SHIFT, VK_CONTROL, VK_MENU,
        'W', 'A', 'S', 'D', 'Q', 'E', 'R', 'F', 'Z', 'X', 'C', 'V',
        'I', 'J', 'K', 'L', 'U', 'O', 'M', 'P'
    };
    for (int vk : kKeys) {
        if ((GetAsyncKeyState(vk) & 0x8000) != 0) {
            return true;
        }
    }
    return false;
}

static int DetectEngineActiveInputVariantSEH(uintptr_t exe_base, bool has_ps_hid, bool has_any_hid_pad) {
    int pad_type = has_ps_hid ? 0 : 1; // 0 = PS, 1 = XBOX
    int default_fallback = has_ps_hid ? 0 : (has_any_hid_pad ? 1 : 2);
    if (!exe_base) return default_fallback;

    bool any_gamepad_connected = has_any_hid_pad;
    bool pad_active_now = false;
    bool kb_active_now  = false;

    __try {
        // 1. Inspect the engine's active gamepad device in g_PlayerSlots (0x33B938) & g_InputDevices (0x33B180)
        uintptr_t player_slots = *reinterpret_cast<const uintptr_t*>(exe_base + AczRva(0x33B938u));
        uintptr_t dev_begin    = *reinterpret_cast<const uintptr_t*>(exe_base + AczRva(0x33B180u));
        uintptr_t dev_end      = *reinterpret_cast<const uintptr_t*>(exe_base + AczRva(0x33B188u));
        if (player_slots != 0 && dev_begin != 0 && dev_end > dev_begin) {
            int32_t active_idx = *reinterpret_cast<const int32_t*>(player_slots + 0x00);
            size_t num_devs    = (dev_end - dev_begin) / 0xA0u;
            if (active_idx >= 0 && static_cast<size_t>(active_idx) < num_devs) {
                uintptr_t dev     = dev_begin + static_cast<size_t>(active_idx) * 0xA0u;
                uint32_t dev_type = *reinterpret_cast<const uint32_t*>(dev + 0x00);
                if (dev_type == 2u) {
                    any_gamepad_connected = true;
                    // Steam Input controller: ESteamInputType at +0x94 (5=PS4, 12=PS3, 13=PS5 DualSense)
                    uint32_t steam_type = *reinterpret_cast<const uint32_t*>(dev + 0x94u);
                    pad_type = (steam_type == 5u || steam_type == 12u || steam_type == 13u || has_ps_hid) ? 0 : 1;
                } else if (dev_type == 1u) {
                    any_gamepad_connected = true;
                    pad_type = has_ps_hid ? 0 : 1;
                } else if (dev_type == 0u) {
                    any_gamepad_connected = true;
                    // DirectInput controller: GUIDProduct.Data1 is at dev + 0x04 (low 16 bits = Vendor ID, 0x054C = Sony)
                    uint32_t guid_d1 = *reinterpret_cast<const uint32_t*>(dev + 0x04u);
                    pad_type = ((guid_d1 & 0xFFFFu) == 0x054Cu || has_ps_hid) ? 0 : 1;
                }
            }
        }

        // 2. Inspect live per-frame input activity in ControllerState[0] (Gamepad, 0x33ABE0) & ControllerState[1] (Keyboard, 0x33AC10)
        uintptr_t pad_state = exe_base + AczRva(0x33ABE0u);
        uint32_t  pad_conn  = *reinterpret_cast<const uint32_t*>(pad_state + 0x2Cu);
        if (pad_conn != 0) {
            any_gamepad_connected = true;
            uint16_t pad_btns = *reinterpret_cast<const uint16_t*>(pad_state + 0x28u);
            if (pad_btns != 0) {
                pad_active_now = true;
            } else {
                const float* axes = reinterpret_cast<const float*>(pad_state + 0x08u);
                for (int i = 0; i < 8; ++i) {
                    float v = axes[i];
                    if (v > 0.22f || v < -0.22f) {
                        pad_active_now = true;
                        break;
                    }
                }
            }
        }

        uintptr_t kb_state = exe_base + AczRva(0x33AC10u);
        uint32_t  kb_conn  = *reinterpret_cast<const uint32_t*>(kb_state + 0x2Cu);
        if (kb_conn != 0) {
            uint16_t kb_btns = *reinterpret_cast<const uint16_t*>(kb_state + 0x28u);
            if (kb_btns != 0) {
                kb_active_now = true;
            } else {
                const float* kb_axes = reinterpret_cast<const float*>(kb_state + 0x08u);
                for (int i = 0; i < 8; ++i) {
                    float v = kb_axes[i];
                    if (v > 0.22f || v < -0.22f) {
                        kb_active_now = true;
                        break;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    if (!kb_active_now && !pad_active_now && IsForegroundKeyboardActive()) {
        kb_active_now = true;
    }

    if (g_OrigPollAllInputDevices != nullptr) {
        int live_var = s_last_active_input_variant.load(std::memory_order_relaxed);
        if (live_var >= 0 && live_var <= 2) {
            return (live_var == 2) ? 2 : (any_gamepad_connected ? pad_type : 2);
        }
    }
    int prev = s_last_active_input_variant.load(std::memory_order_relaxed);
    if (pad_active_now) {
        s_last_active_input_variant.store(pad_type, std::memory_order_relaxed);
        return pad_type;
    }
    if (kb_active_now) {
        s_last_active_input_variant.store(2, std::memory_order_relaxed);
        return 2; // KB
    }
    if (prev >= 0 && prev <= 2) {
        // If previously on a gamepad and the gamepad is still connected, keep using its current pad_type (PS or XBOX)
        if (prev != 2) {
            if (any_gamepad_connected) {
                s_last_active_input_variant.store(pad_type, std::memory_order_relaxed);
                return pad_type;
            }
            s_last_active_input_variant.store(2, std::memory_order_relaxed);
            return 2;
        }
        return 2;
    }

    int init_var = any_gamepad_connected ? pad_type : 2;
    s_last_active_input_variant.store(init_var, std::memory_order_relaxed);
    return init_var;
}

static void PollConnectedHidGamepads(bool* out_has_ps_hid, bool* out_has_any_hid) {
    static std::atomic<uint64_t> s_last_hid_poll_ms{0};
    static std::atomic<bool>     s_cached_ps_hid{false};
    static std::atomic<bool>     s_cached_any_hid{false};

    uint64_t now_ms = GetTickCount64();
    uint64_t prev_ms = s_last_hid_poll_ms.load(std::memory_order_relaxed);
    if (prev_ms == 0 || (now_ms - prev_ms) >= 1000ULL) {
        s_last_hid_poll_ms.store(now_ms, std::memory_order_relaxed);
        bool found_ps  = false;
        bool found_any = false;
        UINT num_devs  = 0;
        if (GetRawInputDeviceList(nullptr, &num_devs, sizeof(RAWINPUTDEVICELIST)) == 0 && num_devs > 0 && num_devs < 512) {
            RAWINPUTDEVICELIST dev_list[128];
            UINT cap = (num_devs > 128) ? 128 : num_devs;
            if (GetRawInputDeviceList(dev_list, &cap, sizeof(RAWINPUTDEVICELIST)) != static_cast<UINT>(-1)) {
                for (UINT i = 0; i < cap; ++i) {
                    if (dev_list[i].dwType != RIM_TYPEHID) continue;
                    RID_DEVICE_INFO rdi{};
                    rdi.cbSize = sizeof(RID_DEVICE_INFO);
                    UINT rdi_sz = sizeof(RID_DEVICE_INFO);
                    if (GetRawInputDeviceInfoA(dev_list[i].hDevice, RIDI_DEVICEINFO, &rdi, &rdi_sz) != static_cast<UINT>(-1)) {
                        if (rdi.hid.usUsagePage == 0x01u &&
                            (rdi.hid.usUsage == 0x04u || rdi.hid.usUsage == 0x05u || rdi.hid.usUsage == 0x08u)) {
                            found_any = true;
                            if (rdi.hid.dwVendorId == 0x054Cu) {
                                found_ps = true;
                            }
                        }
                    }
                }
            }
        }
        s_cached_ps_hid.store(found_ps, std::memory_order_relaxed);
        s_cached_any_hid.store(found_any, std::memory_order_relaxed);
    }
    if (out_has_ps_hid)  *out_has_ps_hid  = s_cached_ps_hid.load(std::memory_order_relaxed);
    if (out_has_any_hid) *out_has_any_hid = s_cached_any_hid.load(std::memory_order_relaxed);
}

static void SyncManualAndBtnForActivePadSEH(int active_var) {
    if (active_var < 0 || active_var > 2) active_var = 0;
    __try {
        uint8_t* manual_tex = g_ManualTargetTexObj.load(std::memory_order_acquire);
        if (manual_tex != nullptr) {
            void*    ps_hdr   = g_ManualPsSlotHdr.load(std::memory_order_acquire);
            uint32_t ps_hdl   = g_ManualPsHandle.load(std::memory_order_acquire);
            void*    xbox_hdr = g_ManualXboxSlotHdr.load(std::memory_order_acquire);
            uint32_t xbox_hdl = g_ManualXboxHandle.load(std::memory_order_acquire);
            void*    kb_hdr   = g_ManualKbSlotHdr.load(std::memory_order_acquire);
            uint32_t kb_hdl   = g_ManualKbHandle.load(std::memory_order_acquire);
            void*    chosen_hdr = xbox_hdr;
            uint32_t chosen_hdl = xbox_hdl;
            if (active_var == 0 && ps_hdr != nullptr) {
                chosen_hdr = ps_hdr;
                chosen_hdl = ps_hdl;
            } else if (active_var == 2 && kb_hdr != nullptr) {
                chosen_hdr = kb_hdr;
                chosen_hdl = kb_hdl;
            }
            if (chosen_hdr != nullptr && *reinterpret_cast<void**>(manual_tex + 0x00) != chosen_hdr) {
                *reinterpret_cast<void**>(manual_tex + 0x00) = chosen_hdr;
            }
            if (chosen_hdl != 0xFFFFFFFFu && *reinterpret_cast<uint32_t*>(manual_tex + 0x28) != chosen_hdl) {
                *reinterpret_cast<uint32_t*>(manual_tex + 0x28) = chosen_hdl;
            }
        }
        for (size_t s = 1; s <= MAX_CUSTOM_TEXTURES; ++s) {
            if (!g_SlotIsPadSelector[s].load(std::memory_order_relaxed)) continue;
            for (int t = 0; t < 3; ++t) {
                uint8_t* tex_t = g_SlotLoadedTexTier[t][s].load(std::memory_order_acquire);
                void*    hdr_t = g_PadSlotOrigHdr[t][s].load(std::memory_order_acquire);
                uint32_t hdl_t = g_PadSlotOrigHandle[t][s].load(std::memory_order_acquire);
                if (tex_t != nullptr) {
                    if (hdr_t != nullptr && *reinterpret_cast<void**>(tex_t + 0x00) != hdr_t) {
                        *reinterpret_cast<void**>(tex_t + 0x00) = hdr_t;
                    }
                    if (hdl_t != 0xFFFFFFFFu && *reinterpret_cast<uint32_t*>(tex_t + 0x28) != hdl_t) {
                        *reinterpret_cast<uint32_t*>(tex_t + 0x28) = hdl_t;
                    }
                }
            }
            if (active_var != 0) {
                uint8_t* tex_0   = g_SlotLoadedTexTier[0][s].load(std::memory_order_acquire);
                void*    hdr_act = g_PadSlotOrigHdr[active_var][s].load(std::memory_order_acquire);
                uint32_t hdl_act = g_PadSlotOrigHandle[active_var][s].load(std::memory_order_acquire);
                if (tex_0 != nullptr) {
                    if (hdr_act != nullptr && *reinterpret_cast<void**>(tex_0 + 0x00) != hdr_act) {
                        *reinterpret_cast<void**>(tex_0 + 0x00) = hdr_act;
                    }
                    if (hdl_act != 0xFFFFFFFFu && *reinterpret_cast<uint32_t*>(tex_0 + 0x28) != hdl_act) {
                        *reinterpret_cast<uint32_t*>(tex_0 + 0x28) = hdl_act;
                    }
                }
            }
        }
        uint8_t* btn_tex = g_BtnTargetTexObj.load(std::memory_order_acquire);
        if (btn_tex != nullptr) {
            uint16_t btn_slot = g_BuiltinPadExtratexSlot[17].load(std::memory_order_acquire);
            if (btn_slot != 0 && btn_slot <= MAX_CUSTOM_TEXTURES) {
                void*    hdr_act = g_PadSlotOrigHdr[active_var][btn_slot].load(std::memory_order_acquire);
                uint32_t hdl_act = g_PadSlotOrigHandle[active_var][btn_slot].load(std::memory_order_acquire);
                if (hdr_act != nullptr && *reinterpret_cast<void**>(btn_tex + 0x00) != hdr_act) {
                    *reinterpret_cast<void**>(btn_tex + 0x00) = hdr_act;
                }
                if (hdl_act != 0xFFFFFFFFu && *reinterpret_cast<uint32_t*>(btn_tex + 0x28) != hdl_act) {
                    *reinterpret_cast<uint32_t*>(btn_tex + 0x28) = hdl_act;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Returns active gamepad/keyboard variant index: 0 = PS, 1 = XBOX, 2 = KB
static int GetEffectiveGamepadVariantIndex() {
    int chosen = 0;
    if (g_GamepadGlyphsMode == 1) {
        chosen = 0; // Forced PS
    } else if (g_GamepadGlyphsMode == 2) {
        chosen = 1; // Forced XBOX
    } else if (g_GamepadGlyphsMode == 3) {
        chosen = 2; // Forced KB
    } else {
        static std::atomic<uint64_t> s_last_poll_ms{0};
        static std::atomic<int>      s_cached_var{0};
        uint64_t now_ms  = GetTickCount64();
        uint64_t prev_ms = s_last_poll_ms.load(std::memory_order_relaxed);
        if (prev_ms == 0 || (now_ms - prev_ms) >= 30ULL) {
            s_last_poll_ms.store(now_ms, std::memory_order_relaxed);
            bool has_ps_hid = false, has_any_hid = false;
            PollConnectedHidGamepads(&has_ps_hid, &has_any_hid);
            int det = DetectEngineActiveInputVariantSEH(g_ExeBase, has_ps_hid, has_any_hid);
            s_cached_var.store(det, std::memory_order_relaxed);
        }
        chosen = s_cached_var.load(std::memory_order_relaxed);
    }
    int prev_synced = g_LastSyncedPadVariant.load(std::memory_order_relaxed);
    if (prev_synced != chosen) {
        g_LastSyncedPadVariant.store(chosen, std::memory_order_relaxed);
        SyncManualAndBtnForActivePadSEH(chosen);
    }
    return chosen;
}

static inline int ResolveSlotEffectiveAspectTier(uint16_t slot_idx) {
    if (g_SlotIsPadSelector[slot_idx].load(std::memory_order_relaxed)) {
        return GetEffectiveGamepadVariantIndex();
    }
    uint8_t mask = g_SlotAvailMask[slot_idx].load(std::memory_order_relaxed);
    if (mask <= 1u) return 0;
    int active = GetActiveTextureAspectTier();
    if (active == 2) {
        if (mask & 4u) return 2;
        if (mask & 2u) return 1;
        return 0;
    } else if (active == 1) {
        if (mask & 2u) return 1;
        if (mask & 1u) return 0;
        return 2;
    } else {
        if (mask & 1u) return 0;
        if (mask & 2u) return 1;
        return 2;
    }
}

// ----------------------------------------------------------------------------
// Load Replacement PNG for Slot on the Render Thread (+0x214420)
// ----------------------------------------------------------------------------
static uint8_t* LoadReplacementPngForSlot(void* gs_ctx, uint16_t slot_idx, uint32_t orig_w, uint32_t orig_h, int eff_tier = 0) {
    (void)gs_ctx;
    if (eff_tier < 0 || eff_tier > 2) eff_tier = 0;
    std::string          path_to_load;
    std::vector<uint8_t> png_bytes;
    uint64_t             hash       = 0;
    bool                 is_mem_png = false;

    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (slot_idx == 0 || slot_idx >= g_Slots.size()) return nullptr;
        CustomSlotInfo& slot = g_Slots[slot_idx];
        if (!g_HotReload || !slot.mem_png.empty()) {
            uint8_t* cached = slot.loaded_tex_tier[eff_tier] ? slot.loaded_tex_tier[eff_tier] : (eff_tier == 0 ? slot.loaded_tex : nullptr);
            if (cached != nullptr) {
                return cached;
            }
            if (slot.load_failed_tier[eff_tier]) return nullptr;
        }
        path_to_load = !slot.png_path_tier[eff_tier].empty() ? slot.png_path_tier[eff_tier] : slot.png_path;
        hash         = slot.hash;
        if (!slot.mem_png.empty()) {
            png_bytes  = slot.mem_png;
            is_mem_png = true;
        }
    }

    bool is_hud_slot = (slot_idx == g_BuiltinExtratexSlot[0].load(std::memory_order_relaxed) ||
                        slot_idx == g_BuiltinExtratexSlot[1].load(std::memory_order_relaxed));

    if (!is_mem_png) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        uint64_t src_mtime = 0;
        uint32_t src_fsize = 0;
        if (GetFileAttributesExA(path_to_load.c_str(), GetFileExInfoStandard, &fad)) {
            src_mtime = (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                        static_cast<uint64_t>(fad.ftLastWriteTime.dwLowDateTime);
            src_fsize = fad.nFileSizeLow;
            g_SlotLastWriteTime[eff_tier][slot_idx] = src_mtime;
            g_SlotLastHotCheckTick[eff_tier][slot_idx] = GetTickCount64();
        }

        // Autonomous DDS Cache Fast-Path: load pre-baked BC7/BGRA8 .dds directly into D3D12 with zero PNG decode overhead!
        if (g_EnableDdsCache && src_fsize > 8 && g_AllocatePooledTexture != nullptr && g_UploadTextureToGpu != nullptr) {
            fs::path dds_path;
            if (BuildOrUpdateDdsCacheForPng(path_to_load, is_hud_slot, &dds_path)) {
                uint8_t* dds_tex = LoadTextureFromDdsCacheFile(dds_path, src_mtime, src_fsize);
                if (dds_tex != nullptr) {
                    std::lock_guard<std::mutex> lock(g_Mutex);
                    if (slot_idx < g_Slots.size()) {
                        uint32_t tex_w = *reinterpret_cast<uint32_t*>(dds_tex + 0x14);
                        uint32_t tex_h = *reinterpret_cast<uint32_t*>(dds_tex + 0x18);
                        *reinterpret_cast<uint32_t*>(dds_tex + 0x14) = (orig_w > 0) ? orig_w : tex_w;
                        *reinterpret_cast<uint32_t*>(dds_tex + 0x18) = (orig_h > 0) ? orig_h : tex_h;
                        *reinterpret_cast<uint32_t*>(dds_tex + 0x38) = 0;

                        g_Slots[slot_idx].loaded_tex_tier[eff_tier]  = dds_tex;
                        g_Slots[slot_idx].load_failed_tier[eff_tier] = false;
                        if (eff_tier == 0) {
                            g_Slots[slot_idx].loaded_tex  = dds_tex;
                            g_Slots[slot_idx].load_failed = false;
                            g_SlotLoadedTex[slot_idx].store(dds_tex, std::memory_order_release);
                        }
                        g_PadSlotOrigHdr[eff_tier][slot_idx].store(*reinterpret_cast<void**>(dds_tex + 0x00), std::memory_order_release);
                        g_PadSlotOrigHandle[eff_tier][slot_idx].store(*reinterpret_cast<uint32_t*>(dds_tex + 0x28), std::memory_order_release);
                        g_SlotLoadedTexTier[eff_tier][slot_idx].store(dds_tex, std::memory_order_release);

                        uint32_t fmt = *reinterpret_cast<uint32_t*>(*reinterpret_cast<uint8_t**>(dds_tex));
                        LogMsg("[ACZ_HDTex] [DDSCache] Loaded %s [Tier=%s] (%ux%u -> logical %ux%u) for Hash=%016llX (Slot #%u) -> tex=%p (%s)",
                               fmt == DXGI_FMT_BC7_UNORM ? "BC7_DDS" : "BGRA8_DDS",
                               eff_tier == 2 ? "21x9" : (eff_tier == 1 ? "16x9" : "4x3"),
                               tex_w, tex_h,
                               *reinterpret_cast<uint32_t*>(dds_tex + 0x14),
                               *reinterpret_cast<uint32_t*>(dds_tex + 0x18),
                               static_cast<unsigned long long>(hash), slot_idx, dds_tex, dds_path.filename().string().c_str());
                    }
                    return dds_tex;
                }
            }
        }
        FILE* f = nullptr;
        fopen_s(&f, path_to_load.c_str(), "rb");
        if (!f) {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (slot_idx < g_Slots.size()) {
                g_Slots[slot_idx].load_failed_tier[eff_tier] = true;
                if (eff_tier == 0) g_Slots[slot_idx].load_failed = true;
            }
            g_SlotLoadFailedTier[eff_tier][slot_idx].store(true, std::memory_order_release);
            if (eff_tier == 0) g_SlotLoadFailed[slot_idx].store(true, std::memory_order_release);
            LogMsg("[ACZ_HDTex] ERROR: Could not open PNG file on disk: %s", path_to_load.c_str());
            return nullptr;
        }

        fseek(f, 0, SEEK_END);
        long file_len = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (file_len <= 8) {
            fclose(f);
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (slot_idx < g_Slots.size()) {
                g_Slots[slot_idx].load_failed_tier[eff_tier] = true;
                if (eff_tier == 0) g_Slots[slot_idx].load_failed = true;
            }
            g_SlotLoadFailedTier[eff_tier][slot_idx].store(true, std::memory_order_release);
            if (eff_tier == 0) g_SlotLoadFailed[slot_idx].store(true, std::memory_order_release);
            LogMsg("[ACZ_HDTex] ERROR: Invalid/empty PNG file: %s", path_to_load.c_str());
            return nullptr;
        }

        png_bytes.resize(static_cast<size_t>(file_len));
        size_t read_bytes = fread(png_bytes.data(), 1, png_bytes.size(), f);
        fclose(f);

        if (read_bytes != png_bytes.size()) {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (slot_idx < g_Slots.size()) {
                g_Slots[slot_idx].load_failed_tier[eff_tier] = true;
                if (eff_tier == 0) g_Slots[slot_idx].load_failed = true;
            }
            g_SlotLoadFailedTier[eff_tier][slot_idx].store(true, std::memory_order_release);
            if (eff_tier == 0) g_SlotLoadFailed[slot_idx].store(true, std::memory_order_release);
            LogMsg("[ACZ_HDTex] ERROR: Incomplete read of PNG file: %s", path_to_load.c_str());
            return nullptr;
        }
    }

    // Check if this PNG is an exact upscale of a non-power-of-two TIM2/GIM sprite (e.g. 40x40 -> 320x320)
    // being bound to a power-of-two PS2 GS texture allocation (e.g. orig_w=64, orig_h=64).
    // If so, pad the PNG into the top-left corner of a (orig_w * scale) x (orig_h * scale) transparent canvas
    // so that UV coordinates [0..exp_w] / orig_w map 1:1 with zero stretching!
    // For built-in HUD font atlases (000.png / 001.png), ensure all transparent pixels (A=0) have
    // neutral grey RGB=(105, 105, 105) matching the original ACZ HUD atlas so bilinear filtering
    // never produces dark fringes around HUD characters or boxes.
    if (!is_mem_png &&
        (slot_idx == g_BuiltinExtratexSlot[0].load(std::memory_order_relaxed) ||
         slot_idx == g_BuiltinExtratexSlot[1].load(std::memory_order_relaxed))) {
        std::vector<uint8_t> hud_rgba;
        uint32_t dec_w = 0, dec_h = 0;
        if (DecodePngFileViaWic(path_to_load, hud_rgba, &dec_w, &dec_h) && dec_w > 0 && dec_h > 0) {
            bool needs_rgb105_fix = false;
            const size_t total_px = static_cast<size_t>(dec_w) * dec_h;
            for (size_t i = 0; i < total_px; ++i) {
                if (hud_rgba[i * 4u + 3] == 0 &&
                    (hud_rgba[i * 4u + 0] != 105u || hud_rgba[i * 4u + 1] != 105u || hud_rgba[i * 4u + 2] != 105u)) {
                    needs_rgb105_fix = true;
                    break;
                }
            }
            if (needs_rgb105_fix) {
                for (size_t i = 0; i < total_px; ++i) {
                    hud_rgba[i * 4u + 0] = 105u;
                    hud_rgba[i * 4u + 1] = 105u;
                    hud_rgba[i * 4u + 2] = 105u;
                }
                std::vector<uint8_t> norm_png;
                EncodeRgbaToPngBytes(hud_rgba.data(), dec_w, dec_h, norm_png);
                if (!norm_png.empty()) {
                    png_bytes = std::move(norm_png);
                    LogMsg("[ACZ_HDTex] Normalized transparent pixel RGB=(105,105,105) for built-in HUD atlas Slot #%u (%s)",
                           slot_idx, path_to_load.c_str());
                }
            }
        }
    }



    uint8_t* hd_tex = g_CreateTextureFromPngMemory(png_bytes.data(), png_bytes.size());

    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (slot_idx < g_Slots.size()) {
            if (hd_tex != nullptr) {
                uint32_t png_w = *reinterpret_cast<uint32_t*>(hd_tex + 0x14);
                uint32_t png_h = *reinterpret_cast<uint32_t*>(hd_tex + 0x18);

                *reinterpret_cast<uint32_t*>(hd_tex + 0x14) = (orig_w > 0) ? orig_w : png_w;
                *reinterpret_cast<uint32_t*>(hd_tex + 0x18) = (orig_h > 0) ? orig_h : png_h;
                *reinterpret_cast<uint32_t*>(hd_tex + 0x38) = 0;

                g_Slots[slot_idx].loaded_tex_tier[eff_tier]  = hd_tex;
                g_Slots[slot_idx].load_failed_tier[eff_tier] = false;
                if (eff_tier == 0) {
                    g_Slots[slot_idx].loaded_tex  = hd_tex;
                    g_Slots[slot_idx].load_failed = false;
                    g_SlotLoadedTex[slot_idx].store(hd_tex, std::memory_order_release);
                }
                if (!g_HotReload && !g_Slots[slot_idx].mem_png.empty()) {
                    std::vector<uint8_t>().swap(g_Slots[slot_idx].mem_png);
                }
                g_PadSlotOrigHdr[eff_tier][slot_idx].store(*reinterpret_cast<void**>(hd_tex + 0x00), std::memory_order_release);
                g_PadSlotOrigHandle[eff_tier][slot_idx].store(*reinterpret_cast<uint32_t*>(hd_tex + 0x28), std::memory_order_release);
                g_SlotLoadedTexTier[eff_tier][slot_idx].store(hd_tex, std::memory_order_release);

                if (!is_mem_png) {
                    LogMsg("[ACZ_HDTex] Loaded HD PNG [Tier=%s] (%ux%u -> logical %ux%u) for Hash=%016llX (Slot #%u) -> tex=%p (%s)",
                           eff_tier == 2 ? "21x9" : (eff_tier == 1 ? "16x9" : "4x3"),
                           png_w, png_h,
                           *reinterpret_cast<uint32_t*>(hd_tex + 0x14),
                           *reinterpret_cast<uint32_t*>(hd_tex + 0x18),
                           static_cast<unsigned long long>(hash), slot_idx, hd_tex, path_to_load.c_str());
                }
            } else {
                g_Slots[slot_idx].load_failed_tier[eff_tier] = true;
                g_SlotLoadFailedTier[eff_tier][slot_idx].store(true, std::memory_order_release);
                if (eff_tier == 0) {
                    g_Slots[slot_idx].load_failed = true;
                    g_SlotLoadFailed[slot_idx].store(true, std::memory_order_release);
                }
                LogMsg("[ACZ_HDTex] ERROR: CreateTextureFromPngMemory (+0x214420) failed for Slot #%u (%s)",
                       slot_idx, path_to_load.c_str());
            }
        }
    }

    return hd_tex;
}

// ----------------------------------------------------------------------------
// Hook 6: LookupOrCreateGsTexture (+0x191040)
// ----------------------------------------------------------------------------
// Smart HotReload check: instead of re-reading PNGs on every draw call (thousands of times/sec),
// checks the file's last-write timestamp at most once every 500ms per slot and only reloads when modified on disk!
static bool ShouldHotReloadSlotTier(uint16_t slot_idx, int eff_tier) {
    if (!g_HotReload || slot_idx == 0 || slot_idx > MAX_CUSTOM_TEXTURES) return false;
    if (eff_tier < 0 || eff_tier > 2) eff_tier = 0;
    uint64_t now_ms = GetTickCount64();
    if (now_ms - g_SlotLastHotCheckTick[eff_tier][slot_idx] < 500ULL) {
        return false;
    }
    g_SlotLastHotCheckTick[eff_tier][slot_idx] = now_ms;

    std::string path_to_check;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (slot_idx >= g_Slots.size()) return false;
        const CustomSlotInfo& slot = g_Slots[slot_idx];
        if (!slot.mem_png.empty()) return false;
        path_to_check = !slot.png_path_tier[eff_tier].empty() ? slot.png_path_tier[eff_tier] : slot.png_path;
    }
    if (path_to_check.empty()) return false;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(path_to_check.c_str(), GetFileExInfoStandard, &fad)) {
        return false;
    }
    uint64_t write_time = (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                          static_cast<uint64_t>(fad.ftLastWriteTime.dwLowDateTime);
    uint64_t prev_time  = g_SlotLastWriteTime[eff_tier][slot_idx];
    g_SlotLastWriteTime[eff_tier][slot_idx] = write_time;
    return (prev_time != 0 && write_time != prev_time);
}

static uint8_t* __fastcall Hook_LookupOrCreateGsTexture(
    void* gs_ctx,
    uint64_t tex0_reg,
    uint8_t flag_r8
) {
    if (gs_ctx != nullptr && g_LastGsRenderCtx.load(std::memory_order_relaxed) != gs_ctx) {
        g_LastGsRenderCtx.store(gs_ctx, std::memory_order_relaxed);
    }
    (void)GetEffectiveGamepadVariantIndex();

    uint32_t tbp0_pre = static_cast<uint32_t>(tex0_reg & 0x3FFFull);
    uint32_t cbp_pre  = static_cast<uint32_t>((tex0_reg >> 37) & 0x3FFFull);

    // Zero-overhead fast path for scratch TBP0=0x3000 (.swa TIM2 sprites): when a replaced .swa sprite is active
    // at 0x3000 and its HD texture is already on the GPU, return fast_hd directly without calling
    // g_OrigLookupOrCreateGsTexture (which would otherwise allocate a 1x D3D12 texture and wait on the worker thread)!
    if (tbp0_pre == 0x3000u) {
        uint32_t tracked_cid = g_LastTbpCustomId[tbp0_pre].load(std::memory_order_acquire);
        uint32_t pad_idx = 0;
        uint32_t tex_idx = 0;
        if (IsBuiltinPadTexId(tracked_cid, &pad_idx)) {
            uint16_t pad_slot = g_BuiltinPadExtratexSlot[pad_idx].load(std::memory_order_acquire);
            if (pad_slot != 0) {
                tracked_cid = (static_cast<uint32_t>(pad_slot) << 16) | CUSTOM_EXTRATEX_TAG;
            }
        } else if (IsBuiltinTextureExtId(tracked_cid, &tex_idx) && tex_idx < 32) {
            uint16_t tex_slot = g_BuiltinTextureExtratexSlot[tex_idx].load(std::memory_order_acquire);
            if (tex_slot != 0) {
                tracked_cid = (static_cast<uint32_t>(tex_slot) << 16) | CUSTOM_EXTRATEX_TAG;
            }
        }
        if ((tracked_cid & 0xFFFF) == CUSTOM_EXTRATEX_TAG) {
            uint16_t slot_idx = static_cast<uint16_t>(tracked_cid >> 16);
            if (slot_idx > 0 && slot_idx <= MAX_CUSTOM_TEXTURES) {
                uint32_t orig_w = g_LastTbpOrigW[tbp0_pre].load(std::memory_order_relaxed);
                uint32_t orig_h = g_LastTbpOrigH[tbp0_pre].load(std::memory_order_relaxed);
                uint32_t exp_w  = g_SlotExpectedW[slot_idx].load(std::memory_order_relaxed);
                uint32_t exp_h  = g_SlotExpectedH[slot_idx].load(std::memory_order_relaxed);
                if (orig_w == 0 || orig_h == 0) {
                    orig_w = exp_w;
                    orig_h = exp_h;
                }
                bool is_pad_slot = g_SlotIsPadSelector[slot_idx].load(std::memory_order_relaxed);
                bool dim_ok = true;
                if (!is_pad_slot && exp_w != 0 && exp_h != 0 && orig_w != 0 && orig_h != 0) {
                    uint32_t pow2_w = NextPow2U32(exp_w);
                    uint32_t pow2_h = NextPow2U32(exp_h);
                    uint32_t alt_w  = g_SlotAltExpectedW[slot_idx].load(std::memory_order_relaxed);
                    uint32_t alt_h  = g_SlotAltExpectedH[slot_idx].load(std::memory_order_relaxed);
                    bool match_full = (orig_w == exp_w || orig_w == pow2_w) && (orig_h == exp_h || orig_h == pow2_h);
                    bool match_alt  = (alt_w != 0 && alt_h != 0) &&
                                      (orig_w == alt_w || orig_w == NextPow2U32(alt_w)) &&
                                      (orig_h == alt_h || orig_h == NextPow2U32(alt_h));
                    bool match_half = g_SlotAllowHalfDim[slot_idx].load(std::memory_order_relaxed) &&
                                      ((orig_w * 2u == exp_w || orig_w * 2u == pow2_w) ||
                                       (orig_w == exp_w * 2u || orig_w == pow2_w * 2u)) &&
                                      ((orig_h * 2u == exp_h || orig_h * 2u == pow2_h) ||
                                       (orig_h == exp_h * 2u || orig_h == pow2_h * 2u));
                    bool match_double = (orig_w == exp_w * 2u || orig_w == pow2_w * 2u) &&
                                        (orig_h == exp_h * 2u || orig_h == pow2_h * 2u);
                    dim_ok = (match_full || match_alt || match_half || match_double);
                }
                if (dim_ok) {
                    int eff_tier = ResolveSlotEffectiveAspectTier(slot_idx);
                    if (!ShouldHotReloadSlotTier(slot_idx, eff_tier)) {
                        uint8_t* fast_hd = g_SlotLoadedTexTier[eff_tier][slot_idx].load(std::memory_order_acquire);
                        if (fast_hd == nullptr && eff_tier == 0) {
                            fast_hd = g_SlotLoadedTex[slot_idx].load(std::memory_order_acquire);
                        }
                        if (fast_hd != nullptr) {
                            if (is_pad_slot) {
                                void*    true_hdr = g_PadSlotOrigHdr[eff_tier][slot_idx].load(std::memory_order_acquire);
                                uint32_t true_hdl = g_PadSlotOrigHandle[eff_tier][slot_idx].load(std::memory_order_acquire);
                                if (true_hdr != nullptr && *reinterpret_cast<void**>(fast_hd + 0x00) != true_hdr) {
                                    *reinterpret_cast<void**>(fast_hd + 0x00) = true_hdr;
                                }
                                if (true_hdl != 0xFFFFFFFFu && *reinterpret_cast<uint32_t*>(fast_hd + 0x28) != true_hdl) {
                                    *reinterpret_cast<uint32_t*>(fast_hd + 0x28) = true_hdl;
                                }
                            }
                            if (orig_w > 0 && orig_h > 0) {
                                if (*reinterpret_cast<uint32_t*>(fast_hd + 0x14) != orig_w) {
                                    *reinterpret_cast<uint32_t*>(fast_hd + 0x14) = orig_w;
                                }
                                if (*reinterpret_cast<uint32_t*>(fast_hd + 0x18) != orig_h) {
                                    *reinterpret_cast<uint32_t*>(fast_hd + 0x18) = orig_h;
                                }
                            }
                            if (*reinterpret_cast<uint32_t*>(fast_hd + 0x20) != tbp0_pre) {
                                *reinterpret_cast<uint32_t*>(fast_hd + 0x20) = tbp0_pre;
                            }
                            if (*reinterpret_cast<uint32_t*>(fast_hd + 0x24) != cbp_pre) {
                                *reinterpret_cast<uint32_t*>(fast_hd + 0x24) = cbp_pre;
                            }
                            if (*reinterpret_cast<uint32_t*>(fast_hd + 0x38) != 0) {
                                *reinterpret_cast<uint32_t*>(fast_hd + 0x38) = 0;
                            }
                            return fast_hd;
                        }
                    }
                }
            }
        }
    }

    uint8_t* gs_tex = g_OrigLookupOrCreateGsTexture(gs_ctx, tex0_reg, flag_r8);
    if (!gs_tex) {
        return nullptr;
    }

    uint32_t ext_id = *reinterpret_cast<uint32_t*>(gs_tex + 0x38);

    uint32_t tbp0 = static_cast<uint32_t>(tex0_reg & 0x3FFFull);

    // Dynamic SWG/TIM2 scratch VRAM slot check ONLY for TBP0 = 0x3000 (AircraftSelect.swa / Shop.swa)
    // Never override ext_id for gameplay HUD / render targets at other TBP0 addresses!
    if (tbp0 == 0x3000u) {
        uint32_t tracked_cid = g_LastTbpCustomId[tbp0 & 0x3FFFu].load(std::memory_order_acquire);
        if ((tracked_cid & 0xFFFF) == CUSTOM_EXTRATEX_TAG || IsBuiltinPadTexId(tracked_cid) || IsBuiltinTextureExtId(tracked_cid)) {
            ext_id = tracked_cid;
        } else if (IsBuiltinHudFont000Id(tracked_cid) || IsBuiltinHudFont001Id(tracked_cid)) {
            // Preserve built-in HUD font tag (0x00000001 for 000.png / 0x00010001 for 001.png) at TBP0=0x3000 (used by AVModeDebriefing MISSION RESULT)
            ext_id = tracked_cid;
            if (*reinterpret_cast<uint32_t*>(gs_tex + 0x38) != tracked_cid) {
                *reinterpret_cast<uint32_t*>(gs_tex + 0x38) = tracked_cid;
            }
        } else if (tracked_cid == 0) {
            // An unreplaced non-button TIM2 sprite was uploaded to scratch TBP0=0x3000; clear any stale tag
            ext_id = 0;
            if (*reinterpret_cast<uint32_t*>(gs_tex + 0x38) != 0) {
                *reinterpret_cast<uint32_t*>(gs_tex + 0x38) = 0;
            }
        }
    }

    // If gs_tex carries a native Category 2 gamepad glyph extratex_id ((idx << 16) | 2),
    // route it directly to our registered mods\textures\pads\ slot!
    uint32_t native_pad_idx = 0;
    uint32_t native_tex_idx = 0;
    if (IsBuiltinPadTexId(ext_id, &native_pad_idx)) {
        uint16_t pad_slot = g_BuiltinPadExtratexSlot[native_pad_idx].load(std::memory_order_acquire);
        if (pad_slot != 0) {
            ext_id = (static_cast<uint32_t>(pad_slot) << 16) | CUSTOM_EXTRATEX_TAG;
        }
    } else if (IsBuiltinTextureExtId(ext_id, &native_tex_idx) && native_tex_idx < 32) {
        uint16_t tex_slot = g_BuiltinTextureExtratexSlot[native_tex_idx].load(std::memory_order_acquire);
        if (tex_slot != 0) {
            ext_id = (static_cast<uint32_t>(tex_slot) << 16) | CUSTOM_EXTRATEX_TAG;
        }
    }

    if ((ext_id & 0xFFFF) == CUSTOM_EXTRATEX_TAG) {
        uint16_t slot_idx = static_cast<uint16_t>(ext_id >> 16);
        if (slot_idx == 0 || slot_idx > MAX_CUSTOM_TEXTURES) {
            *reinterpret_cast<uint32_t*>(gs_tex + 0x38) = 0;
            return gs_tex;
        }
        uint32_t orig_w   = *reinterpret_cast<uint32_t*>(gs_tex + 0x14);
        uint32_t orig_h   = *reinterpret_cast<uint32_t*>(gs_tex + 0x18);
        uint32_t orig_tbp = *reinterpret_cast<uint32_t*>(gs_tex + 0x20);
        uint32_t orig_cbp = *reinterpret_cast<uint32_t*>(gs_tex + 0x24);

        bool is_pad_slot = g_SlotIsPadSelector[slot_idx].load(std::memory_order_relaxed);
        uint32_t exp_w = g_SlotExpectedW[slot_idx].load(std::memory_order_relaxed);
        uint32_t exp_h = g_SlotExpectedH[slot_idx].load(std::memory_order_relaxed);
        if (!is_pad_slot && exp_w != 0 && exp_h != 0 && orig_w != 0 && orig_h != 0) {
            uint32_t pow2_w = NextPow2U32(exp_w);
            uint32_t pow2_h = NextPow2U32(exp_h);
            uint32_t alt_w  = g_SlotAltExpectedW[slot_idx].load(std::memory_order_relaxed);
            uint32_t alt_h  = g_SlotAltExpectedH[slot_idx].load(std::memory_order_relaxed);
            bool match_full = (orig_w == exp_w || orig_w == pow2_w) && (orig_h == exp_h || orig_h == pow2_h);
            bool match_alt  = (alt_w != 0 && alt_h != 0) &&
                              (orig_w == alt_w || orig_w == NextPow2U32(alt_w)) &&
                              (orig_h == alt_h || orig_h == NextPow2U32(alt_h));
            bool match_half = g_SlotAllowHalfDim[slot_idx].load(std::memory_order_relaxed) &&
                              ((orig_w * 2u == exp_w) || (orig_w == exp_w * 2u)) &&
                              ((orig_h * 2u == exp_h) || (orig_h == exp_h * 2u));
            bool match_double = (orig_w == exp_w * 2u) && (orig_h == exp_h * 2u);
            if (!match_full && !match_alt && !match_half && !match_double) {
                // CRITICAL: Clear [gs_tex + 0x38] on dimension mismatch (e.g. unreplaced 112x32 TIM2 in MemCheck.swa
                // inheriting 480x160 Slot #246 tag at TBP0=0x3000) so GsFlushDrawBatch (+0x1950C1) never indexes
                // g_GlobalExtratexVectors[0x7FFF] (NULL) when falling back to gs_tex!
                *reinterpret_cast<uint32_t*>(gs_tex + 0x38) = 0;
                return gs_tex;
            }
        }

        int eff_tier = ResolveSlotEffectiveAspectTier(slot_idx);
        uint8_t* hd_tex = nullptr;
        if (!ShouldHotReloadSlotTier(slot_idx, eff_tier)) {
            hd_tex = g_SlotLoadedTexTier[eff_tier][slot_idx].load(std::memory_order_acquire);
            if (hd_tex == nullptr && eff_tier == 0) {
                hd_tex = g_SlotLoadedTex[slot_idx].load(std::memory_order_acquire);
            }
            if (hd_tex == nullptr && g_SlotLoadFailedTier[eff_tier][slot_idx].load(std::memory_order_acquire)) {
                *reinterpret_cast<uint32_t*>(gs_tex + 0x38) = 0;
                return gs_tex;
            }
        } else {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (slot_idx < g_Slots.size()) {
                g_Slots[slot_idx].loaded_tex_tier[eff_tier] = nullptr;
                if (eff_tier == 0) g_Slots[slot_idx].loaded_tex = nullptr;
            }
            g_SlotLoadedTexTier[eff_tier][slot_idx].store(nullptr, std::memory_order_release);
            if (eff_tier == 0) g_SlotLoadedTex[slot_idx].store(nullptr, std::memory_order_release);
        }

        if (hd_tex == nullptr) {
            hd_tex = LoadReplacementPngForSlot(gs_ctx, slot_idx, orig_w, orig_h, eff_tier);
        }

        if (hd_tex != nullptr) {
            if (is_pad_slot) {
                void*    true_hdr = g_PadSlotOrigHdr[eff_tier][slot_idx].load(std::memory_order_acquire);
                uint32_t true_hdl = g_PadSlotOrigHandle[eff_tier][slot_idx].load(std::memory_order_acquire);
                if (true_hdr != nullptr && *reinterpret_cast<void**>(hd_tex + 0x00) != true_hdr) {
                    *reinterpret_cast<void**>(hd_tex + 0x00) = true_hdr;
                }
                if (true_hdl != 0xFFFFFFFFu && *reinterpret_cast<uint32_t*>(hd_tex + 0x28) != true_hdl) {
                    *reinterpret_cast<uint32_t*>(hd_tex + 0x28) = true_hdl;
                }
            }
            // NOTE: Keep [gs_tex + 0x38] intact on the underlying 1x gs_tex in gs_ctx so that both
            // flag_r8=0 (GsKickVertex) and flag_r8=1 (GsFlushDrawBatch) as well as subsequent frames
            // continue to see ext_id and return hd_tex (which itself has [hd_tex + 0x38] = 0)!
            if (orig_w > 0 && orig_h > 0) {
                *reinterpret_cast<uint32_t*>(hd_tex + 0x14) = orig_w;
                *reinterpret_cast<uint32_t*>(hd_tex + 0x18) = orig_h;
            }
            *reinterpret_cast<uint32_t*>(hd_tex + 0x20) = orig_tbp;
            *reinterpret_cast<uint32_t*>(hd_tex + 0x24) = orig_cbp;
            *reinterpret_cast<uint32_t*>(hd_tex + 0x38) = 0;
            return hd_tex;
        }

        *reinterpret_cast<uint32_t*>(gs_tex + 0x38) = 0;
    }

    return gs_tex;
}

// ----------------------------------------------------------------------------
// Hook 9: LoadTextureFromArcPath (+0x214230)
// Intercepts the engine's built-in extratex PNG loader (called by 0x140190B30
// for "extratex/hudfont/000.png", "extratex/hudfont/001.png", etc.) so that
// custom .arc archives mounted by version.dll cannot override our replacements
// in mods\textures\!
// ----------------------------------------------------------------------------
static bool SafeReadCString(const char* const* str_ptr_addr, char* out_buf, size_t max_len) {
    if (!str_ptr_addr || !out_buf || max_len == 0) return false;
    out_buf[0] = '\0';
    __try {
        const char* p = *str_ptr_addr;
        if (!p) return false;
        size_t i = 0;
        for (; i + 1 < max_len; ++i) {
            char c = p[i];
            out_buf[i] = c;
            if (c == '\0') break;
        }
        out_buf[i] = '\0';
        return (i > 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out_buf[0] = '\0';
        return false;
    }
}

static bool s_InRecursiveManualLoad = false;

static uint8_t* __fastcall Hook_LoadTextureFromArcPath(const char* const* arc_path_ptr) {
    if (s_InRecursiveManualLoad) {
        return g_OrigLoadTextureFromArcPath(arc_path_ptr);
    }
    char rel_path[260] = {};
    if (SafeReadCString(arc_path_ptr, rel_path, sizeof(rel_path))) {
        std::string rel_low = rel_path;
        for (char& c : rel_low) {
            if (c == '\\') c = '/';
            else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        // Special dynamic auto-detect handling for extratex/manual/000.png (Controller diagram) & 001.png (Keyboard diagram):
        // Load both stock 000.png (Xbox) and stock 001.png (Keyboard) from a.arc plus custom PS manual (ps_manual_000.png),
        // and dynamically swap the D3D12 slot_hdr* at [manual_tex + 0x00] based on active input device (PS / XBOX / KB)!
        if (rel_low.find("extratex/manual/000") != std::string::npos ||
            rel_low.find("extratex/manual/001") != std::string::npos) {
            s_InRecursiveManualLoad = true;
            uint8_t* stock_active = g_OrigLoadTextureFromArcPath(arc_path_ptr);
            if (stock_active != nullptr) {
                g_ManualTargetTexObj.store(stock_active, std::memory_order_release);
                void*    orig_hdr = *reinterpret_cast<void**>(stock_active + 0x00);
                uint32_t orig_hdl = *reinterpret_cast<uint32_t*>(stock_active + 0x28);
                g_ManualOrigSlotHdr.store(orig_hdr, std::memory_order_release);
                g_ManualOrigHandle.store(orig_hdl, std::memory_order_release);

                // Load dedicated persistent copies of 000.png (Xbox) and 001.png (Keyboard) once
                // so CloseControlsManual (+0x174E10) freeing stock_active never invalidates them!
                if (g_ManualXboxSlotHdr.load(std::memory_order_acquire) == nullptr) {
                    const char* p000 = "extratex/manual/000.png";
                    uint8_t* t000 = g_OrigLoadTextureFromArcPath(&p000);
                    if (t000 != nullptr) {
                        g_ManualXboxSlotHdr.store(*reinterpret_cast<void**>(t000 + 0x00), std::memory_order_release);
                        g_ManualXboxHandle.store(*reinterpret_cast<uint32_t*>(t000 + 0x28), std::memory_order_release);
                    }
                }
                if (g_ManualKbSlotHdr.load(std::memory_order_acquire) == nullptr) {
                    const char* p001 = "extratex/manual/001.png";
                    uint8_t* t001 = g_OrigLoadTextureFromArcPath(&p001);
                    if (t001 != nullptr) {
                        g_ManualKbSlotHdr.store(*reinterpret_cast<void**>(t001 + 0x00), std::memory_order_release);
                        g_ManualKbHandle.store(*reinterpret_cast<uint32_t*>(t001 + 0x28), std::memory_order_release);
                    }
                }
            }
            s_InRecursiveManualLoad = false;

            uint16_t manual_slot = g_BuiltinManual000Slot.load(std::memory_order_acquire);
            if (manual_slot != 0) {
                uint8_t* ps_tex = LoadReplacementPngForSlot(nullptr, manual_slot, 1920u, 1080u, 0);
                if (ps_tex != nullptr) {
                    g_ManualPsSlotHdr.store(*reinterpret_cast<void**>(ps_tex + 0x00), std::memory_order_release);
                    g_ManualPsHandle.store(*reinterpret_cast<uint32_t*>(ps_tex + 0x28), std::memory_order_release);
                }
            }
            int active_var = GetEffectiveGamepadVariantIndex();
            g_LastSyncedPadVariant.store(active_var, std::memory_order_relaxed);
            SyncManualAndBtnForActivePadSEH(active_var);
            LogMsg("[ACZ_HDTex] [ArcTexHook] Initialized dynamic Controls Manual ('%s', Slot #%u, ActiveVariant=%s) -> tex=%p",
                   rel_path, manual_slot,
                   active_var == 0 ? "PS" : (active_var == 1 ? "XBOX" : "KB"),
                   reinterpret_cast<void*>(stock_active));
            return stock_active;
        }

        uint32_t ext_id = 0;
        uint16_t slot_idx = 0;
        uint32_t logical_w = 256u;
        uint32_t logical_h = 256u;
        uint32_t pad_idx = 0;
        uint32_t tex_idx = 0;
        if (TryMatchBuiltinExtratexName(rel_path, &ext_id)) {
            if (IsBuiltinHudFont000Id(ext_id)) {
                slot_idx = g_BuiltinExtratexSlot[0].load(std::memory_order_acquire);
                logical_w = 256u;
                logical_h = 256u;
            } else if (IsBuiltinHudFont001Id(ext_id)) {
                slot_idx = g_BuiltinExtratexSlot[1].load(std::memory_order_acquire);
                logical_w = 256u;
                logical_h = 256u;
            } else if (IsBuiltinPadTexId(ext_id, &pad_idx)) {
                slot_idx  = g_BuiltinPadExtratexSlot[pad_idx].load(std::memory_order_acquire);
                logical_w = (pad_idx == 15u) ? 512u : ((pad_idx == 17u) ? 128u : 32u);
                logical_h = (pad_idx == 15u) ? 64u  : ((pad_idx == 17u) ? 128u : 32u);
            } else if (IsBuiltinTextureExtId(ext_id, &tex_idx)) {
                if (tex_idx < 32) {
                    slot_idx = g_BuiltinTextureExtratexSlot[tex_idx].load(std::memory_order_acquire);
                }
                uint16_t exp_w = 0, exp_h = 0;
                GetDefaultBuiltinTextureDims(tex_idx, &exp_w, &exp_h);
                logical_w = exp_w;
                logical_h = exp_h;
            }
        }

        // Automatic fallback: if slot_idx was not explicitly bound, look up via known content hashes
        if (slot_idx == 0 && tex_idx >= 11 && tex_idx <= 16) {
            uint64_t fallback_hashes[4] = {};
            size_t num_fback = 0;
            switch (tex_idx) {
                case 11:
                    fallback_hashes[0] = 0x3CFCF1D871552129ULL;
                    fallback_hashes[1] = 0xCC972B5039A12E5AULL;
                    fallback_hashes[2] = 0xF7A5F8E6B4A645BEULL;
                    num_fback = 3;
                    if (logical_w == 0) { logical_w = 256u; logical_h = 16u; }
                    break;
                case 12:
                    fallback_hashes[0] = 0x4290455F2822D245ULL;
                    fallback_hashes[1] = 0x14A2EC170150DA16ULL;
                    fallback_hashes[2] = 0x7C36847EB984ABB1ULL;
                    num_fback = 3;
                    if (logical_w == 0) { logical_w = 288u; logical_h = 32u; }
                    break;
                case 13:
                    fallback_hashes[0] = 0x795A02DE9053F52FULL;
                    fallback_hashes[1] = 0x44D477F64514B65CULL;
                    fallback_hashes[2] = 0xFB4142D38FB0E60AULL;
                    num_fback = 3;
                    if (logical_w == 0) { logical_w = 448u; logical_h = 80u; }
                    break;
                case 14:
                    fallback_hashes[0] = 0x5FE26278F9447E6FULL;
                    fallback_hashes[1] = 0xD017F35C51844095ULL;
                    num_fback = 2;
                    if (logical_w == 0) { logical_w = 64u; logical_h = 16u; }
                    break;
                case 15:
                    fallback_hashes[0] = 0x04F9AC4DBD57B9D6ULL;
                    fallback_hashes[1] = 0xB7EC6B6B1D13C8C9ULL;
                    fallback_hashes[2] = 0xA779AB3E20AB7C4FULL;
                    fallback_hashes[3] = 0x03BB0D1EE77250E6ULL;
                    num_fback = 4;
                    if (logical_w == 0) { logical_w = 512u; logical_h = 64u; }
                    break;
                case 16:
                    fallback_hashes[0] = 0x134990740A53EDA7ULL;
                    fallback_hashes[1] = 0x431F53103CDACF17ULL;
                    fallback_hashes[2] = 0x1B59E715D55F199AULL;
                    num_fback = 3;
                    if (logical_w == 0) { logical_w = 496u; logical_h = 256u; }
                    break;
                default: break;
            }
            for (size_t fi = 0; fi < num_fback; ++fi) {
                uint16_t found_slot = LockFreeLookupContentHash(fallback_hashes[fi]);
                if (found_slot != 0) {
                    slot_idx = found_slot;
                    if (tex_idx < 32) {
                        g_BuiltinTextureExtratexSlot[tex_idx].store(slot_idx, std::memory_order_release);
                    }
                    if (logical_w > 0 && g_SlotExpectedW[slot_idx].load(std::memory_order_relaxed) == 0) {
                        g_SlotExpectedW[slot_idx].store(static_cast<uint16_t>(logical_w), std::memory_order_release);
                    }
                    if (logical_h > 0 && g_SlotExpectedH[slot_idx].load(std::memory_order_relaxed) == 0) {
                        g_SlotExpectedH[slot_idx].store(static_cast<uint16_t>(logical_h), std::memory_order_release);
                    }
                    break;
                }
            }
        }

        if (slot_idx != 0) {
            uint16_t sw = g_SlotExpectedW[slot_idx].load(std::memory_order_relaxed);
            uint16_t sh = g_SlotExpectedH[slot_idx].load(std::memory_order_relaxed);
            if (sw > 0) logical_w = sw;
            if (sh > 0) logical_h = sh;

            if (g_SlotIsPadSelector[slot_idx].load(std::memory_order_relaxed)) {
                uint8_t avail = g_SlotAvailMask[slot_idx].load(std::memory_order_relaxed);
                for (int t = 0; t < 3; ++t) {
                    if (avail & (1u << t)) {
                        LoadReplacementPngForSlot(nullptr, slot_idx, logical_w, logical_h, t);
                    }
                }
                int active_var = GetEffectiveGamepadVariantIndex();
                // For extratex/texture/btn.png (loaded & destroyed dynamically when opening/closing Controls Manual),
                // return the engine's disposable stock texture object and swap its +0x00 and +0x28 handles!
                if (rel_low.find("btn.png") != std::string::npos) {
                    s_InRecursiveManualLoad = true;
                    uint8_t* stock_btn = g_OrigLoadTextureFromArcPath(arc_path_ptr);
                    s_InRecursiveManualLoad = false;
                    if (stock_btn != nullptr) {
                        g_BtnTargetTexObj.store(stock_btn, std::memory_order_release);
                        g_BtnOrigSlotHdr.store(*reinterpret_cast<void**>(stock_btn + 0x00), std::memory_order_release);
                        g_BtnOrigHandle.store(*reinterpret_cast<uint32_t*>(stock_btn + 0x28), std::memory_order_release);
                        SyncManualAndBtnForActivePadSEH(active_var);
                        return stock_btn;
                    }
                }
                SyncManualAndBtnForActivePadSEH(active_var);
                uint8_t* hd_tex = g_SlotLoadedTexTier[0][slot_idx].load(std::memory_order_acquire);
                if (hd_tex == nullptr) {
                    hd_tex = LoadReplacementPngForSlot(nullptr, slot_idx, logical_w, logical_h, active_var);
                }
                if (hd_tex != nullptr) {
                    LogMsg("[ACZ_HDTex] [ArcTexHook] Overrode built-in pad extratex '%s' with dynamic Slot #%u (%ux%u) -> tex=%p",
                           rel_path, slot_idx, logical_w, logical_h, reinterpret_cast<void*>(hd_tex));
                    return hd_tex;
                }
            } else {
                uint8_t* hd_tex = LoadReplacementPngForSlot(nullptr, slot_idx, logical_w, logical_h);
                if (hd_tex != nullptr) {
                    LogMsg("[ACZ_HDTex] [ArcTexHook] Overrode built-in extratex '%s' with HD replacement Slot #%u (%ux%u) -> tex=%p",
                           rel_path, slot_idx, logical_w, logical_h, reinterpret_cast<void*>(hd_tex));
                    return hd_tex;
                }
            }
        }
    }

    return g_OrigLoadTextureFromArcPath(arc_path_ptr);
}

// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Controls Manual Screen Hook (+0x174F90) & Standalone DualSense Fallback Hook (+0x21A480)
// Ensures the Options -> Controls diagram (extratex/manual/000.png & extratex/texture/btn.png)
// updates live every frame while open and cleanly restores stock handles afterwards so
// CloseControlsManual (+0x174E10) never frees our persistent HD textures!
// ----------------------------------------------------------------------------
using FnRenderControlsManualScreen = void(__fastcall*)();
static FnRenderControlsManualScreen g_OrigRenderControlsManualScreen = nullptr;
static constexpr uintptr_t RVA_RenderControlsManualScreen = 0x00174F90;
static const uint8_t PROLOGUE_RenderControlsManualScreen[19] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x4C, 0x89, 0x70, 0x20
};

static void SwapControlsManualHandlesSEH(int active_var, bool restore_stock, void** io_saved_manual_hdr, void** io_saved_btn_hdr) {
    __try {
        uint8_t* manual_tex = *reinterpret_cast<uint8_t**>(g_ExeBase + AczRva(0x33BBB8u));
        uint8_t* btn_tex    = *reinterpret_cast<uint8_t**>(g_ExeBase + AczRva(0x33BBB0u));
        if (manual_tex != nullptr) {
            if (restore_stock) {
                if (io_saved_manual_hdr && *io_saved_manual_hdr != nullptr) {
                    *reinterpret_cast<void**>(manual_tex + 0x00) = *io_saved_manual_hdr;
                }
            } else {
                if (io_saved_manual_hdr) {
                    *io_saved_manual_hdr = *reinterpret_cast<void**>(manual_tex + 0x00);
                }
                void* ps_hdr   = g_ManualPsSlotHdr.load(std::memory_order_acquire);
                void* xbox_hdr = g_ManualXboxSlotHdr.load(std::memory_order_acquire);
                void* kb_hdr   = g_ManualKbSlotHdr.load(std::memory_order_acquire);
                if (xbox_hdr == nullptr && io_saved_manual_hdr) {
                    xbox_hdr = *io_saved_manual_hdr;
                }
                void* chosen_hdr = xbox_hdr;
                if (active_var == 0 && ps_hdr != nullptr) {
                    chosen_hdr = ps_hdr;
                } else if (active_var == 2 && kb_hdr != nullptr) {
                    chosen_hdr = kb_hdr;
                }
                if (chosen_hdr != nullptr) {
                    *reinterpret_cast<void**>(manual_tex + 0x00) = chosen_hdr;
                }
            }
        }
        if (btn_tex != nullptr) {
            if (restore_stock) {
                if (io_saved_btn_hdr && *io_saved_btn_hdr != nullptr) {
                    *reinterpret_cast<void**>(btn_tex + 0x00) = *io_saved_btn_hdr;
                }
            } else {
                if (io_saved_btn_hdr) {
                    *io_saved_btn_hdr = *reinterpret_cast<void**>(btn_tex + 0x00);
                }
                uint16_t btn_slot = g_BuiltinPadExtratexSlot[17].load(std::memory_order_acquire);
                if (btn_slot != 0 && btn_slot <= MAX_CUSTOM_TEXTURES) {
                    void* hdr_act = g_PadSlotOrigHdr[active_var][btn_slot].load(std::memory_order_acquire);
                    if (hdr_act != nullptr) {
                        *reinterpret_cast<void**>(btn_tex + 0x00) = hdr_act;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void __fastcall Hook_RenderControlsManualScreen() {
    int active_var = GetEffectiveGamepadVariantIndex();
    void* saved_manual_hdr = nullptr;
    void* saved_btn_hdr    = nullptr;
    SwapControlsManualHandlesSEH(active_var, false, &saved_manual_hdr, &saved_btn_hdr);
    g_OrigRenderControlsManualScreen();
    SwapControlsManualHandlesSEH(active_var, true, &saved_manual_hdr, &saved_btn_hdr);
}

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

// Validated 5-argument signature of PollDirectInputDevice (+0x21A480 in v1.0.2.1 / +0x21AB00 in v2.0.0.1)
using FnPollDirectInputDevice = char(__fastcall*)(
    uint8_t*       out_state,      // rcx: ControllerState* (0x30 bytes at 0x14033ABE0)
    const uint8_t* dev_info,       // rdx: InputDevice* (0xA0 bytes in g_InputDevices)
    void**         pp_dinput_dev,  // r8:  IDirectInputDevice8W** (&slot->pDInputDevice)
    void*          ff_effects_v1,  // r9:  std::vector<IDirectInputEffect*>* (&slot->vec1)
    void*          ff_effects_v2   // [rsp+0x28]: std::vector<IDirectInputEffect*>* (&slot->vec2)
);
static FnPollDirectInputDevice g_OrigPollDirectInputDevice = nullptr;
static constexpr uintptr_t RVA_PollDirectInputDevice = 0x0021A480;
static const uint8_t PROLOGUE_PollDirectInputDevice[19] = {
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xA0, 0xFE, 0xFF, 0xFF
};

// (g_OrigPollAllInputDevices forward-declared above)
static constexpr uintptr_t RVA_PollAllInputDevices = 0x0021D4C0;
static const uint8_t PROLOGUE_PollAllInputDevices[20] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x20, 0xFA, 0xFF, 0xFF
};

using FnInitPlayerSlot = bool(__fastcall*)(void* slot, uint64_t dev_uid);
static constexpr uintptr_t RVA_InitPlayerSlot = 0x0021CAE0;

static void FixDefaultDinputPovTypesStandaloneSEH(uintptr_t exe_base) {
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
        uint32_t pov = js.rgdwPOV[0];
        if ((pov & 0xFFFFu) != 0xFFFFu && pov <= 35999u) {
            if (pov >= 31500u || pov <= 4500u)  pov_btns |= 0x0010u; // D-Pad Up
            if (pov >= 4500u  && pov <= 13500u) pov_btns |= 0x0020u; // D-Pad Right
            if (pov >= 13500u && pov <= 22500u) pov_btns |= 0x0040u; // D-Pad Down
            if (pov >= 22500u && pov <= 31500u) pov_btns |= 0x0080u; // D-Pad Left
        }
        btns |= pov_btns;

        // GUIDProduct.Data1 is at dev_info + 0x04 (low 16 bits = Vendor ID, 0x054C = Sony)
        uint32_t guid_d1  = *reinterpret_cast<const uint32_t*>(dev_info + 0x04u);
        uint32_t guid_alt = *reinterpret_cast<const uint32_t*>(dev_info + 0x00u);
        bool is_sony = ((guid_d1 & 0xFFFFu) == 0x054Cu) || ((guid_alt & 0xFFFFu) == 0x054Cu);
        if (is_sony) {
            uint16_t ps_btns = pov_btns;
            if (js.rgbButtons[0]  & 0x80u) ps_btns |= 0x8000u; // Square   -> X (Toggle weapon)
            if (js.rgbButtons[1]  & 0x80u) ps_btns |= 0x4000u; // Cross    -> A (Confirm / Machine gun)
            if (js.rgbButtons[2]  & 0x80u) ps_btns |= 0x2000u; // Circle   -> B (Cancel / Fire missile)
            if (js.rgbButtons[3]  & 0x80u) ps_btns |= 0x1000u; // Triangle -> Y (Switch target)
            if (js.rgbButtons[4]  & 0x80u) ps_btns |= 0x0400u; // L1       -> LB (Yaw left)
            if (js.rgbButtons[5]  & 0x80u) ps_btns |= 0x0800u; // R1       -> RB (Yaw right)
            if (js.rgbButtons[9]  & 0x80u) ps_btns |= 0x0008u; // Options  -> Menu (Display pause menu)
            if (js.rgbButtons[10] & 0x80u) ps_btns |= 0x0002u; // L3       -> Click left stick (Boresight)
            if (js.rgbButtons[11] & 0x80u) ps_btns |= 0x0004u; // R3       -> Click right stick (Change view)
            // IMPORTANT: On PS5 DualSense / DS4, ONLY Touchpad click (Button 14 = rgbButtons[13]) is View (Switch radar)!
            // Create/Share (Button 9 = rgbButtons[8]) is NOT mapped to View.
            if (js.rgbButtons[13] & 0x80u) ps_btns |= 0x0001u; // Touchpad -> View (Switch radar display mode)

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
        FixDefaultDinputPovTypesStandaloneSEH(g_ExeBase);

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

    char res = 0;
    __try {
        res = g_OrigPollDirectInputDevice(out_state, dev_info, pp_dinput_dev, ff_effects_v1, ff_effects_v2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        res = 0;
    }
    if (res) {
        ApplyDirectInputDualSenseAndPovEnhancementsSEH(out_state, dev_info, pp_dinput_dev);
        uint32_t guid_d1 = *reinterpret_cast<const uint32_t*>(dev_info + 0x04u);
        if ((guid_d1 & 0xFFFFu) == 0x054Cu) {
            UpdateSonyVibrationFromGameSEH(g_ExeBase, req_large, req_small);
        }
    }
    return res;
}

static bool IsControllerStateActiveNow(const uint8_t* st) {
    if (!st) return false;
    if (*reinterpret_cast<const uint32_t*>(st + 0x2Cu) == 0u) return false;
    if (*reinterpret_cast<const uint16_t*>(st + 0x28u) != 0u) return true;
    const float* axes = reinterpret_cast<const float*>(st + 0x08u);
    for (int i = 0; i < 8; ++i) {
        if (axes[i] > 0.22f || axes[i] < -0.22f) return true;
    }
    return false;
}

static void __fastcall Hook_PollAllInputDevices(uint8_t* state_array) {
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

            bool has_ps_hid = false, has_any_hid = false;
            PollConnectedHidGamepads(&has_ps_hid, &has_any_hid);
            int pad_var = has_ps_hid ? 0 : 1;
            if (pad_dev != 0) {
                uint32_t dtype = *reinterpret_cast<const uint32_t*>(pad_dev + 0x00u);
                if (dtype == 2u) {
                    uint32_t steam_t = *reinterpret_cast<const uint32_t*>(pad_dev + 0x94u);
                    pad_var = (steam_t == 5u || steam_t == 12u || steam_t == 13u || has_ps_hid) ? 0 : 1;
                } else if (dtype == 0u) {
                    uint32_t guid_d1 = *reinterpret_cast<const uint32_t*>(pad_dev + 0x04u);
                    pad_var = ((guid_d1 & 0xFFFFu) == 0x054Cu || has_ps_hid) ? 0 : 1;
                }
            }

            if (has_ps_hid) {
                UpdateSonyVibrationFromGameSEH(g_ExeBase, p1_large, p1_small);
            }

            if (IsControllerStateActiveNow(pad_st)) {
                s_last_active_input_variant.store(pad_var, std::memory_order_relaxed);
            } else if (IsControllerStateActiveNow(kb_st) || IsForegroundKeyboardActive()) {
                s_last_active_input_variant.store(2, std::memory_order_relaxed);
            } else if (s_last_active_input_variant.load(std::memory_order_relaxed) < 0) {
                s_last_active_input_variant.store((pad_dev != 0 || has_any_hid) ? pad_var : 2, std::memory_order_relaxed);
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

// Forward declaration of minimal x64 Detour Installer
static bool InstallDetour64(
    const char* name,
    uintptr_t target_addr,
    const uint8_t* expected_prologue,
    size_t prologue_len,
    const void* hook_fn,
    void** out_trampoline
);

// Hook 16: UploadTextureToGpu (+0x20DCF0 in v1.0.2.1 / +0x20E370 in v2.0.0.1)
//   Serializes access to the engine's non-thread-safe D3D12 upload ring buffer
//   (0x14033B088), ring offset (0x1403567A8), and upload command list (0x14033ACE8)
//   between the GS background unswizzle worker thread (+0x18FD00) and the render
//   thread (CreateTextureFromPngMemory / LoadTextureFromDdsCacheFile).
// ----------------------------------------------------------------------------
static void __fastcall Hook_UploadTextureToGpu(uint8_t* hd_tex) {
    AcquireSRWLockExclusive(&g_UploadTextureGpuLock);
    g_OrigUploadTextureToGpu(hd_tex);
    ReleaseSRWLockExclusive(&g_UploadTextureGpuLock);
}

// ----------------------------------------------------------------------------
// Cross-DLL Bridge Exports for enhancer.dll (Widescreen Menu & Title Card Sync)
// Allows enhancer.dll and acz_hdtex.dll to operate together or standalone!
// ----------------------------------------------------------------------------
static uint8_t AczHdTex_GetTim2AspectMask_Internal(const uint8_t* tim2_base, uint32_t tim2_size) {
    uint16_t hdr_sz = (tim2_size >= 0x20 && std::memcmp(tim2_base, "TIM2", 4) == 0)
        ? *reinterpret_cast<const uint16_t*>(tim2_base + 0x1C)
        : 0u;
    if (hdr_sz == 0 || static_cast<uint32_t>(0x10u + hdr_sz) >= tim2_size) return 0u;
    const uint8_t* pix_ptr = tim2_base + 0x10 + hdr_sz;
    Tim2InspectionResult tim2_res{};
    if (!SafeInspectAndHashTim2(pix_ptr, &tim2_res)) return 0u;

    std::lock_guard<std::mutex> tex_lock(g_Mutex);
    uint16_t slot_idx = 0;
    const uint64_t cands[3] = {
        tim2_res.hash_tim2_norm,
        tim2_res.hash_tim2_pix_clut,
        tim2_res.hash_tim2_pixels
    };
    for (uint64_t h : cands) {
        auto it = g_HashToSlot.find(h);
        if (it != g_HashToSlot.end() && it->second > 0 && it->second < g_Slots.size()) {
            slot_idx = it->second;
            break;
        }
    }
    return (slot_idx > 0) ? g_SlotAvailMask[slot_idx].load(std::memory_order_relaxed) : 0u;
}

extern "C" __declspec(dllexport) uint8_t __fastcall AczHdTex_GetTim2AspectMask(
    const uint8_t* tim2_base,
    uint32_t       tim2_size
) {
    if (!tim2_base || tim2_size < 0x40) return 0u;
    __try {
        return AczHdTex_GetTim2AspectMask_Internal(tim2_base, tim2_size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0u;
    }
}

static int AczHdTex_ResolveGimDescriptorAspectTier_Internal(uint8_t* gim_desc, const char* caller_tag) {
    ProcessGimDescriptorIfNeeded(gim_desc, caller_tag ? caller_tag : "EnhancerBridge");
    uint32_t ext_id = *reinterpret_cast<const uint32_t*>(gim_desc + 0x40);
    if ((ext_id & 0xFFFFu) == CUSTOM_EXTRATEX_TAG) {
        uint16_t slot_idx = static_cast<uint16_t>(ext_id >> 16);
        if (slot_idx > 0 && slot_idx <= MAX_CUSTOM_TEXTURES) {
            return ResolveSlotEffectiveAspectTier(slot_idx);
        }
    }
    return 0;
}

extern "C" __declspec(dllexport) int __fastcall AczHdTex_ResolveGimDescriptorAspectTier(
    uint8_t*    gim_desc,
    const char* caller_tag
) {
    if (!gim_desc) return 0;
    __try {
        return AczHdTex_ResolveGimDescriptorAspectTier_Internal(gim_desc, caller_tag);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int __fastcall AczHdTex_GetActiveTextureAspectTier() {
    return GetActiveTextureAspectTier();
}

// ----------------------------------------------------------------------------
// Minimal x64 Detour Installer
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
    if (prologue_len < 14 || prologue_len > 32) {
        LogMsg("[ACZ_HDTex] ERROR: Invalid prologue length for %s (%zu)", name, prologue_len);
        return false;
    }

    const uint8_t* target_bytes = reinterpret_cast<const uint8_t*>(target_addr);
    const bool has_rip_mov_rax_at_11 = (prologue_len == 18 &&
                                        expected_prologue[11] == 0x48 &&
                                        expected_prologue[12] == 0x8B &&
                                        expected_prologue[13] == 0x05);
    const size_t verify_len = has_rip_mov_rax_at_11 ? 14u : prologue_len;
    if (std::memcmp(target_bytes, expected_prologue, verify_len) != 0) {
        LogMsg("[ACZ_HDTex] ERROR: Prologue mismatch at %s (%p)!", name, reinterpret_cast<void*>(target_addr));
        LogMsg("  Expected: %02X %02X %02X %02X %02X %02X %02X %02X",
               expected_prologue[0], expected_prologue[1], expected_prologue[2], expected_prologue[3],
               expected_prologue[4], expected_prologue[5], expected_prologue[6], expected_prologue[7]);
        LogMsg("  Actual:   %02X %02X %02X %02X %02X %02X %02X %02X",
               target_bytes[0], target_bytes[1], target_bytes[2], target_bytes[3],
               target_bytes[4], target_bytes[5], target_bytes[6], target_bytes[7]);
        return false;
    }

    constexpr size_t kTrampAllocSize = 64;
    uint8_t* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, kTrampAllocSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)
    );
    if (!tramp) {
        LogMsg("[ACZ_HDTex] ERROR: VirtualAlloc failed for %s trampoline", name);
        return false;
    }

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
        LogMsg("[ACZ_HDTex] ERROR: VirtualProtect failed for %s", name);
        return false;
    }

    WriteAbsoluteJump64(reinterpret_cast<void*>(target_addr), hook_fn);
    for (size_t i = 14; i < prologue_len; ++i) {
        reinterpret_cast<uint8_t*>(target_addr)[i] = 0x90; // NOP padding
    }

    VirtualProtect(reinterpret_cast<void*>(target_addr), prologue_len, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(target_addr), prologue_len);

    *out_trampoline = tramp;
    LogMsg("[ACZ_HDTex] Installed hook '%s' at %p (trampoline at %p)",
           name, reinterpret_cast<void*>(target_addr), tramp);
    return true;
}
#pragma warning(pop)


extern "C" __declspec(dllexport) void __fastcall AczHdTex_DumpLiveState(
    const wchar_t* dump_folder_w,
    const char*    report_path_a,
    int            dump_live_resources
) {
    AczHdTex_DumpLiveStateForQoL(dump_folder_w, report_path_a, dump_live_resources);
}

// ----------------------------------------------------------------------------
// Initialization


// Standalone-safe D3D12 placed-resource texture pool expansion (+0x2A9DE0):
// Ensures high-resolution BC7/BGRA8 textures never overflow the stock D3D12 pool
// even when acz_hdtex.dll is used standalone without enhancer.dll.
static void EnsureD3D12TexturePoolsExpanded() {
    if (!g_ExeBase) return;
    __try {
        uint32_t* pool_tbl = reinterpret_cast<uint32_t*>(g_ExeBase + AczRva(0x2A9DE0));
        if (pool_tbl[0] == 0x04000000u && pool_tbl[1] == 1u &&
            pool_tbl[2] == 0x02040000u && pool_tbl[3] == 8u &&
            pool_tbl[4] == 0x00420000u && pool_tbl[5] == 8u) {
            DWORD old_prot = 0;
            if (VirtualProtect(pool_tbl, 64, PAGE_READWRITE, &old_prot)) {
                pool_tbl[1]  = 8u;          // Pool[0] (64 MB):   1 -> 8    (512 MB)
                pool_tbl[3]  = 32u;         // Pool[1] (32 MB):   8 -> 32   (1032 MB)
                pool_tbl[4]  = 0x00640000u; // Pool[2] slot size: 4.125 MB -> 6.25 MB for 16:9 & 21:9 BC7
                pool_tbl[5]  = 128u;        // Pool[2] (6.25 MB): 8 -> 128  (800 MB)
                pool_tbl[7]  = 128u;        // Pool[3] (2.7 MB):  64 -> 128 (344 MB)
                pool_tbl[9]  = 256u;        // Pool[4] (1.1 MB):  64 -> 256 (272 MB)
                pool_tbl[11] = 512u;        // Pool[5] (320 KB):  128 -> 512 (160 MB)
                VirtualProtect(pool_tbl, 64, old_prot, &old_prot);
                LogMsg("[ACZ_HDTex] Expanded D3D12 placed-resource texture pool capacities.");
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void AutoResolveD3D12TextureCallTargets(const uint8_t* fn_base) {
    if (!fn_base) return;
    __try {
        if (fn_base[0x1D9] == 0xE8) {
            int32_t rel = *reinterpret_cast<const int32_t*>(fn_base + 0x1DA);
            g_AllocatePooledTexture = reinterpret_cast<FnAllocatePooledTexture>(const_cast<uint8_t*>(fn_base + 0x1DE + rel));
        }
        if (fn_base[0x1F7] == 0xE8) {
            int32_t rel = *reinterpret_cast<const int32_t*>(fn_base + 0x1F8);
            g_EngineMalloc = reinterpret_cast<FnEngineMalloc>(const_cast<uint8_t*>(fn_base + 0x1FC + rel));
        }
        if (fn_base[0x239] == 0xE8) {
            int32_t rel = *reinterpret_cast<const int32_t*>(fn_base + 0x23A);
            g_UploadTextureToGpu = reinterpret_cast<FnUploadTextureToGpu>(const_cast<uint8_t*>(fn_base + 0x23E + rel));
        }
        if (fn_base[0x2B4] == 0xE8) {
            int32_t rel = *reinterpret_cast<const int32_t*>(fn_base + 0x2B5);
            g_DestroyPooledTexture = reinterpret_cast<FnDestroyPooledTexture>(const_cast<uint8_t*>(fn_base + 0x2B9 + rel));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void InstallControlsManualAndPadHooks() {
    FixDefaultDinputPovTypesStandaloneSEH(g_ExeBase);
    InstallDetour64(
        "RenderControlsManualScreen",
        g_ExeBase + AczRva(RVA_RenderControlsManualScreen),
        PROLOGUE_RenderControlsManualScreen,
        sizeof(PROLOGUE_RenderControlsManualScreen),
        reinterpret_cast<const void*>(&Hook_RenderControlsManualScreen),
        reinterpret_cast<void**>(&g_OrigRenderControlsManualScreen)
    );
    const uint8_t* poll_addr = reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_PollDirectInputDevice));
    if (std::memcmp(poll_addr, PROLOGUE_PollDirectInputDevice, sizeof(PROLOGUE_PollDirectInputDevice)) == 0) {
        InstallDetour64(
            "PollDirectInputDevice",
            g_ExeBase + AczRva(RVA_PollDirectInputDevice),
            PROLOGUE_PollDirectInputDevice,
            sizeof(PROLOGUE_PollDirectInputDevice),
            reinterpret_cast<const void*>(&Hook_PollDirectInputDevice),
            reinterpret_cast<void**>(&g_OrigPollDirectInputDevice)
        );
    }
    const uint8_t* poll_all_addr = reinterpret_cast<const uint8_t*>(g_ExeBase + AczRva(RVA_PollAllInputDevices));
    if (std::memcmp(poll_all_addr, PROLOGUE_PollAllInputDevices, sizeof(PROLOGUE_PollAllInputDevices)) == 0) {
        InstallDetour64(
            "PollAllInputDevices",
            g_ExeBase + AczRva(RVA_PollAllInputDevices),
            PROLOGUE_PollAllInputDevices,
            sizeof(PROLOGUE_PollAllInputDevices),
            reinterpret_cast<const void*>(&Hook_PollAllInputDevices),
            reinterpret_cast<void**>(&g_OrigPollAllInputDevices)
        );
    }
}

static void InitializePlugin(const AczAddonApi* api) {
    bool expected = false;
    if (!g_PluginInitialized.compare_exchange_strong(expected, true)) {
        return;
    }

    if (api != nullptr && api->struct_size >= sizeof(AczAddonApi) && api->log_fn != nullptr) {
        g_ModLoaderLogFn = api->log_fn;
    }

    char exe_path_buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_path_buf, MAX_PATH);
    g_GameDir     = fs::path(exe_path_buf).parent_path();
    g_ModsDir     = g_GameDir / "mods";
    g_TexturesDir = g_ModsDir / "textures";
    g_DdsCacheDir = g_TexturesDir / ".dds_cache";
    g_DumpDir     = g_ModsDir / "textures_dump";
    g_FontDumpDir = g_DumpDir / "fonts";

    std::error_code ec;
    fs::create_directories(g_ModsDir, ec);

    LoadConfigIni();

    fs::path legacy_log = g_ModsDir / "acz_hdtex.log";
    if (fs::exists(legacy_log, ec)) {
        fs::remove(legacy_log, ec);
    }

    if (g_EnableLogging && g_ModLoaderLogFn == nullptr) {
        fs::path modloader_log_path = g_GameDir / "modloader.log";
        g_FallbackLogFile = _fsopen(modloader_log_path.string().c_str(), "a", _SH_DENYNO);
    }

    g_ExeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    g_AczVersion = DetectAczExeVersion(g_ExeBase);
    LogMsg("[ACZ_HDTex] ================================================================");
    LogMsg("[ACZ_HDTex]  ACZ Universal HD Texture & Font Replacer %s", PLUGIN_VERSION_STR);
    LogMsg("[ACZ_HDTex] ================================================================");
    LogMsg("[ACZ_HDTex] Game executable base: %p (Detected version: %s)",
           reinterpret_cast<void*>(g_ExeBase), GetAczExeVersionString(g_AczVersion));
    LogMsg("[ACZ_HDTex] Textures folder:      %s", g_TexturesDir.string().c_str());
    LogMsg("[ACZ_HDTex] Config: EnableLogging=%d, EnableMenuOverrides=%d, MenuAspectMode=%d, GamepadGlyphs=%s, EnableDdsCache=%d (Mode=%d), HotReload=%d, AutoShareFontGlyphs=%d, DumpMasterFontAtlas=%d",
           g_EnableLogging ? 1 : 0, g_EnableMenuOverrides ? 1 : 0, g_MenuAspectMode,
           GetGamepadGlyphsConfigModeStr(g_GamepadGlyphsMode),
           g_EnableDdsCache ? 1 : 0, g_DdsCacheCompression,
           g_HotReload ? 1 : 0, g_AutoShareFontGlyphs ? 1 : 0, g_DumpMasterFontAtlas ? 1 : 0);

    g_CreateTextureFromPngMemory = reinterpret_cast<FnCreateTextureFromPngMemory>(
        g_ExeBase + AczRva(RVA_CreateTextureFromPngMemory)
    );
    g_AllocatePooledTexture = reinterpret_cast<FnAllocatePooledTexture>(
        g_ExeBase + AczRva(RVA_AllocatePooledTexture)
    );
    g_UploadTextureToGpu = reinterpret_cast<FnUploadTextureToGpu>(
        g_ExeBase + AczRva(RVA_UploadTextureToGpu)
    );
    g_DestroyPooledTexture = reinterpret_cast<FnDestroyPooledTexture>(
        g_ExeBase + AczRva(RVA_DestroyPooledTexture)
    );
    g_EngineMalloc = reinterpret_cast<FnEngineMalloc>(
        g_ExeBase + AczRva(RVA_EngineMalloc)
    );
    // Also auto-resolve exact call targets directly from CreateTextureFromPngMemory instructions:
    AutoResolveD3D12TextureCallTargets(reinterpret_cast<const uint8_t*>(g_CreateTextureFromPngMemory));

    EnsureD3D12TexturePoolsExpanded();
    ScanTexturesDirectory();
    PreloadExistingDumpedMasterFontAtlases();
    PrebuildDdsCacheOnStartup();

    bool ok1 = InstallDetour64(
        "InitGimDescriptor",
        g_ExeBase + AczRva(RVA_InitGimDescriptor),
        PROLOGUE_InitGim,
        sizeof(PROLOGUE_InitGim),
        reinterpret_cast<const void*>(&Hook_InitGimDescriptor),
        reinterpret_cast<void**>(&g_OrigInitGimDescriptor)
    );

    bool ok2 = InstallDetour64(
        "BuildGimGifPacket",
        g_ExeBase + AczRva(RVA_BuildGimGifPacket),
        PROLOGUE_BuildGif,
        sizeof(PROLOGUE_BuildGif),
        reinterpret_cast<const void*>(&Hook_BuildGimGifPacket),
        reinterpret_cast<void**>(&g_OrigBuildGimGifPacket)
    );

    bool ok3 = InstallDetour64(
        "BuildGimPixelOnlyGifPacket",
        g_ExeBase + AczRva(RVA_BuildGimPixelOnlyGifPacket),
        PROLOGUE_BuildGifPix,
        sizeof(PROLOGUE_BuildGifPix),
        reinterpret_cast<const void*>(&Hook_BuildGimPixelOnlyGifPacket),
        reinterpret_cast<void**>(&g_OrigBuildGimPixelOnlyGifPacket)
    );

    bool ok4 = InstallDetour64(
        "BuildGimArrayGifPackets",
        g_ExeBase + AczRva(RVA_BuildGimArrayGifPackets),
        PROLOGUE_BuildGifArr,
        sizeof(PROLOGUE_BuildGifArr),
        reinterpret_cast<const void*>(&Hook_BuildGimArrayGifPackets),
        reinterpret_cast<void**>(&g_OrigBuildGimArrayGifPackets)
    );

    bool ok5 = InstallDetour64(
        "GsUploadVramBlock",
        g_ExeBase + AczRva(RVA_GsUploadVramBlock),
        PROLOGUE_GsUploadVram,
        sizeof(PROLOGUE_GsUploadVram),
        reinterpret_cast<const void*>(&Hook_GsUploadVramBlock),
        reinterpret_cast<void**>(&g_OrigGsUploadVramBlock)
    );

    bool ok6 = InstallDetour64(
        "LookupOrCreateGsTexture",
        g_ExeBase + AczRva(RVA_LookupOrCreateGsTexture),
        PROLOGUE_LookupTex,
        sizeof(PROLOGUE_LookupTex),
        reinterpret_cast<const void*>(&Hook_LookupOrCreateGsTexture),
        reinterpret_cast<void**>(&g_OrigLookupOrCreateGsTexture)
    );

    bool ok7 = InstallDetour64(
        "GameFont_Init",
        g_ExeBase + AczRva(RVA_GameFontInit),
        PROLOGUE_GameFontInit,
        sizeof(PROLOGUE_GameFontInit),
        reinterpret_cast<const void*>(&Hook_GameFontInit),
        reinterpret_cast<void**>(&g_OrigGameFontInit)
    );

    bool ok8 = InstallDetour64(
        "GameFont_UploadStringGlyphs",
        g_ExeBase + AczRva(RVA_GameFontUploadStringGlyphs),
        PROLOGUE_GameFontUpload,
        sizeof(PROLOGUE_GameFontUpload),
        reinterpret_cast<const void*>(&Hook_GameFontUploadStringGlyphs),
        reinterpret_cast<void**>(&g_OrigGameFontUploadStringGlyphs)
    );

    bool ok9 = InstallDetour64(
        "LoadTextureFromArcPath",
        g_ExeBase + AczRva(RVA_LoadTextureFromArcPath),
        PROLOGUE_LoadTextureFromArc,
        sizeof(PROLOGUE_LoadTextureFromArc),
        reinterpret_cast<const void*>(&Hook_LoadTextureFromArcPath),
        reinterpret_cast<void**>(&g_OrigLoadTextureFromArcPath)
    );

    bool ok10 = InstallDetour64(
        "UploadTextureToGpu",
        reinterpret_cast<uintptr_t>(g_UploadTextureToGpu),
        PROLOGUE_UploadTextureToGpu,
        sizeof(PROLOGUE_UploadTextureToGpu),
        reinterpret_cast<const void*>(&Hook_UploadTextureToGpu),
        reinterpret_cast<void**>(&g_OrigUploadTextureToGpu)
    );

    InstallControlsManualAndPadHooks();

    if (ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7 && ok8 && ok9 && ok10) {
        LogMsg("[ACZ_HDTex] All 10 texture & font hooks installed! Ready to replace GIM, FHM, TIM2/SWAD, Dynamic GameFont, Master Atlas, and .arc extratex textures.");
    } else {
        LogMsg("[ACZ_HDTex] WARNING: One or more texture hooks failed to install.");
    }
}

// ----------------------------------------------------------------------------
// Official ACZ Mod Loader 1.2 Add-on Entry Point (Called by version.dll)
// ----------------------------------------------------------------------------
extern "C" __declspec(dllexport) void __fastcall AczAddonInit(const AczAddonApi* api) {
    InitializePlugin(api);
}

// ----------------------------------------------------------------------------
// DLL Entry Point
// ----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_SelfModule = hModule;
        DisableThreadLibraryCalls(hModule);
        if (GetModuleHandleA("version.dll") == nullptr) {
            InitializePlugin(nullptr);
        }
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        LogMsg("[ACZ_HDTex] Shutting down.");
        if (g_FallbackLogFile != nullptr) {
            fclose(g_FallbackLogFile);
            g_FallbackLogFile = nullptr;
        }
    }
    return TRUE;
}