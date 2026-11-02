// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — Universal HD Texture Replacer
// File: acz_hdtex.cpp (v0.21.1 — Dual-Version v1.0.2.1 & v2.0.0.1 Support + Full-Cell Centered HD Glyphs)
// Build: Visual Studio 2026 x64 (Release) -> acz_hdtex.dll
// Usage: Place acz_hdtex.dll into "<GameDir>\mods\acz_hdtex.dll"
//        Configuration is stored in "<GameDir>\mods\enhancer\acz_hdtex.ini"
//        Place replacement PNGs (or MASTER_<LANG>_font_32x32.png + .txt) into "<GameDir>\mods\textures\"
//        Note: Visual enhancements/.pnach patches (enhancer.dll) and memory dumping (memdump.dll) are separate independent modules.
//
// Version History:
//   v0.1..v0.6 : Initial GIM descriptor hooks (+0x2038C0, +0x203B30, +0x191040),
//                FNV-1a 64-bit hashing, and D3D12 PNG texture replacement (+0x214420).
//   v0.7..v0.10: Added FHM sub-texture hooks (+0x203D90, +0x14CA50, +0x191A00),
//                Dynamic GameFont hooks (+0x1FFBD0, +0x1FFCD0), and per-glyph slicing.
//   v0.11..v0.12: Added transitive Font Family clustering (FAMILY_*) and unified
//                per-language Master Font & 16x16 GIM Atlases (MASTER_*).
//   v0.13      : Separated Visuals/Patcher (enhancer.dll) and Memory Dumper (memdump.dll) into independent modules; added 512-Kbit Bloom
//                filter + 64K lock-free pixel table for zero-contention rendering.
//   v0.14      : Added companion <name>.json multi-hash & built-in extratex (000.png /
//                001.png) binding + direct GIM upload detection in GsUploadVramBlock.
//   v0.15      : Added expected_w / expected_h dimension guards across pixel & GS
//                texture hooks to prevent hash/address collisions on shared HUD buffers,
//                and explicit Win32 FindFirstFileW recursive directory scanning.
//   v0.16      : Added full PS2 TIM2 (.tm2 / .swa SWAD menu archive) texture replacement
//                and dumping support! Reconstructs in-memory SWAD-mutated TIM2 headers
//                via eXt/GIDX signatures, tracks dynamic SWG scratch VRAM uploads
//                (TBP0=0x3000), and auto-pads non-power-of-two sub-region PNGs (e.g.
//                40x40 Hangar weapon icons in 64x64 GS slots) for 1:1 UV alignment.
//   v0.17      : Added Hook 9 on LoadTextureFromArcPath (+0x214230) to intercept built-in
//                extratex PNG loads (e.g. extratex/hudfont/000.png and 001.png) before custom
//                .arc archives mounted by version.dll can override mods\textures\ replacements.
//                Fixed engine extratex_id encoding for 000.png (0x00000001 = category 1, index 0,
//                plus legacy 0x00010000 alias).
//   v0.18      : Fixed Pause Menu background, Minimap background, and MISSION START box stripes
//                when using HUD replacements (000.png / 001.png):
//                  - Preserves acz.exe's native extratex_id (0x00000001 / 0x00010001) on PS2 GS
//                    VRAM textures so GsFlushDrawBatch (+0x1950AD) keeps the 1x PS2 VRAM texture
//                    in [rbp-0x60] for sub-region dither background cropping (+0x1924E0) while
//                    binding our 2048x2048 HD 000.png / 001.png from vector[1][0..1] (+0x214230).
//                  - Prevents GIM hashes 1C2024A9952DBC00, 8C7D0EAD0A3B1421, 30F023F86A144D5D,
//                    D00645D18395B834, and 9F1B6CEC8FF3284D from clobbering [rbp-0x60].
//                  - Normalizes transparent pixel RGB=(105, 105, 105) on 000.png / 001.png at load
//                    time to prevent dark bilinear filtering fringes.
//   v0.19      : Added smart per-tile HD detection (IsTileEmptyOrNearestPlaceholder) when slicing
//                upscaled Master Font Atlases (MASTER_<LANG>_font_64x64 / 32x32 / 16x16):
//                  - Automatically detects and registers genuine HD glyph cells (such as the 23
//                    64x64 Squadron Intro letterhead glyphs and 38 32x32 Squadron Intro subtitle
//                    glyphs) while skipping empty space cells and unedited 1x Nearest-Neighbor
//                    placeholder cells so unedited fonts keep native bilinear filtering and save
//                    ~250 MB of RAM.
//   v0.20      : Fixed runtime Master Font Atlas dumper & added CLUT-variant AltHashes support:
//                  - PreloadSharedGlyphsFromCompanionTxt now parses optional "AltHashes=<H1>,<H2>,..."
//                    on each Glyph # line and binds all alternate TileHash and ExactHash values to
//                    the canonical glyph slot (eliminating 1us/2uk duplicate sections in PNGs while
//                    matching 100% of runtime font hashes on Priority 1 ExactHash!).
//                  - Added visual RGBA similarity deduplication in ConsolidateOverlappingFontFamilies
//                    and InspectAndProcessGameFont so CLUT-variant blocks record their ExactHash in
//                    AltHashes instead of creating duplicate cells.
//                  - Fixed runtime font dumper (MASTER_FONT_64x64 / 32x32 / 16x16): preloads existing
//                    MASTER_FONT_<dim>x<dim>.png + .txt from mods\textures_dump\fonts\ on startup so
//                    dumped Master Atlases accumulate glyphs across play sessions instead of being
//                    overwritten with only the current session's fonts, and always updates
//                    MASTER_FONT_* when DumpAllGims=1 & DumpMasterFontAtlas=1 even if replacements exist.
//   v0.21      : Fixed left-edge glyph clipping, vertical family alignment & subtitle brackets:
//                - Added Hook 10: GameFont_DrawString (+0x2005C0) and full-cell centered HD glyph
//                  rendering. Centered HD glyphs in MASTER_EN_font_32x32.png (128x128) and
//                  MASTER_EN_font_64x64.png (512x512) are no longer cropped at src_x0 = pad_x * scale
//                  (which previously sliced off left-edge outer glow/drop-shadow pixels). Instead,
//                  each slot retains its full uncropped cell and Hook_GameFontDrawString expands
//                  UV1 to (dim << 4) and expands screen quad X0/Y0/X1/Y1 by pad_x / pad_y.
//                - Fixed vertical alignment of punctuation and lowercase letters across all UI/HUD
//                  modes (including Hangar mode font_obj[0x86]==4 at +0x00B0E0) by computing each
//                  typeface family's standard height (std_h) and centering/drawing all glyphs in
//                  the family using std_pad_y = (dim - std_h) / 2.
//                - Removed duplicate CleanRestoreAndCenter1xGlyphRgba call in DumpMasterFontAtlasToFolder
//                  that caused right-bleed cleaning to erase parts of already-centered glyphs in dumps.
//                - Added automatic 4x HD compositing for subtitle bracket GIM texture BB10DED7AB6393C6
//                  (<<, >>, [, ], —) from cells #701..#703 at the bottom of MASTER_EN_font_32x32.png.
//   v0.21.1    : Added automatic dual-version executable support (v1.0.2.1 & v2.0.0.1) via
//                acz_version_map.h (DetectAczExeVersion, AczRva) and prologue verification.
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
#include <filesystem>
#include <share.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
// Configuration & Constants
// ----------------------------------------------------------------------------
static constexpr const char* PLUGIN_VERSION_STR = "v0.21.1";
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
    if (rva_1021 >= 0x00308F00u && rva_1021 < 0x00309000u) return rva_1021 + 0x2600u;
    if (rva_1021 < 0x00309000u) return rva_1021 + 0x2238u;
    if (rva_1021 < 0x00330000u) return rva_1021 + 0x3000u;
    return rva_1021 + 0x3020u;
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
static constexpr uintptr_t RVA_GlobalExtratexVectors       = 0x0033AC70; // 3 std::vector<GsTexObj*> (0=manual, 1=hudfont, 2=texture)

// Helper in acz.exe (+0x190400): Waits for background texture worker queue (0x14033AC58)
// to drain overlapping VRAM jobs before mutating D3D12 upload command list (0x14033ACE8).
static constexpr uintptr_t RVA_InvalidateGsVramRange       = 0x00190400;

// Native WIC PNG decoder + D3D12 GsTexObj creator inside acz.exe (+0x214420)
static constexpr uintptr_t RVA_CreateTextureFromPngMemory  = 0x00214420;

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

using FnInvalidateGsVramRange      = void      (__fastcall*)(
    void* gs_ctx,
    uint32_t start_block,
    uint32_t num_blocks
);

using FnLoadTextureFromArcPath     = uint8_t*  (__fastcall*)(
    const char* const* arc_path_ptr
);

using FnCreateTextureFromPngMemory = uint8_t*  (__fastcall*)(
    const void* png_bytes,
    size_t png_len
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
static FnInvalidateGsVramRange      g_InvalidateGsVramRange          = nullptr;
static FnCreateTextureFromPngMemory g_CreateTextureFromPngMemory     = nullptr;

// ----------------------------------------------------------------------------
// Plugin State & Configuration
// ----------------------------------------------------------------------------
struct CustomSlotInfo {
    uint64_t             hash        = 0;
    std::string          png_path;
    std::vector<uint8_t> mem_png;     // Used for sliced dynamic font / 16x16 GIM tile PNGs (loaded lazily on render thread)
    uint8_t*             loaded_tex  = nullptr;
    bool                 load_failed = false;
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

// 4. Lock-free array of loaded D3D12 GsTexObj* per slot (eliminates mutex locking in Hook_LookupOrCreateGsTexture)
static std::atomic<uint8_t*>                                       g_SlotLoadedTex[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<bool>                                           g_SlotLoadFailed[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint16_t>                                       g_SlotExpectedW[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<uint16_t>                                       g_SlotExpectedH[MAX_CUSTOM_TEXTURES + 1]{};
static std::atomic<bool>                                           g_SlotAllowHalfDim[MAX_CUSTOM_TEXTURES + 1]{};
// 5. Built-in PC port extratex slots: [0] = a\extratex\hudfont\000.png (0x00010000), [1] = d\lang\<lang>\extratex\hudfont\001.png (0x00010001)
static std::atomic<uint16_t>                                       g_BuiltinExtratexSlot[2]{};

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
static std::atomic<uint32_t>                                       g_LastUploadedTim2CustomId{0};

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
// Cache original unmutated disk hash per in-memory GIM / Font pointer
static std::unordered_map<uintptr_t, uint64_t>                     g_GimPtrToDiskHash;
static std::unordered_map<uintptr_t, uint64_t>                     g_FontPtrToDiskHash;
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

static inline void LockFreeUpsertPixelBinding(uintptr_t addr, uint32_t custom_id, uint16_t expected_w, uint16_t expected_h) {
    if (addr == 0) return;
    uint64_t packed = static_cast<uint64_t>(custom_id) |
                      (static_cast<uint64_t>(expected_w) << 32) |
                      (static_cast<uint64_t>(expected_h) << 48);

    size_t idx = HashPixelAddr(addr) & LOCKFREE_PIXEL_TABLE_MASK;
    for (size_t step = 0; step < 128; ++step) {
        uintptr_t cur = g_LockFreePixelTable[idx].addr.load(std::memory_order_relaxed);
        if (cur == addr) {
            g_LockFreePixelTable[idx].packed_meta.store(packed, std::memory_order_release);
            return;
        }
        if (cur == 0) {
            uintptr_t expected = 0;
            if (g_LockFreePixelTable[idx].addr.compare_exchange_strong(expected, addr, std::memory_order_acq_rel)) {
                g_LockFreePixelTable[idx].packed_meta.store(packed, std::memory_order_release);
                return;
            }
            if (expected == addr) {
                g_LockFreePixelTable[idx].packed_meta.store(packed, std::memory_order_release);
                return;
            }
        }
        idx = (idx + 1) & LOCKFREE_PIXEL_TABLE_MASK;
    }
    size_t base_idx = HashPixelAddr(addr) & LOCKFREE_PIXEL_TABLE_MASK;
    g_LockFreePixelTable[base_idx].addr.store(addr, std::memory_order_release);
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
            uint16_t cur_cp  = *reinterpret_cast<const uint16_t*>(start_ptr + 16u + g * 16u + 0u);
            uint16_t cur_adv = *reinterpret_cast<const uint16_t*>(start_ptr + 16u + g * 16u + 8u);
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
    frame->Release();
    decoder->Release();
    factory->Release();

    if (FAILED(hr)) {
        out_rgba.clear();
        return false;
    }

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
static bool SafeInspectAndHashGameFont(uint8_t* font_obj, uint8_t* start_ptr, FontInspectionResult* out) {
    (void)font_obj;
    if (!start_ptr || !out) return false;
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

static uint64_t SafeHashStridedByteRows(const uint8_t* ptr, uint32_t rows, uint32_t row_bytes, uint32_t stride_bytes) {
    if (!ptr || rows == 0 || row_bytes == 0) return 0;
    __try {
        uint64_t h = FNV1A_64_OFFSET;
        for (uint32_t r = 0; r < rows; ++r) {
            const uint8_t* row_p = ptr + static_cast<size_t>(r) * stride_bytes;
            for (uint32_t i = 0; i < row_bytes; ++i) {
                h ^= static_cast<uint64_t>(row_p[i]);
                h *= FNV1A_64_PRIME;
            }
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
        out->content_tag        = (meta_q * FNV1A_64_PRIME) ^ pix0_q ^ (pix1_q << 1) ^ h_pix;
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
        return false;
    }
    if (s == "000" || s == "hudfont_000" || s == "hudfont/000" || s.find("extratex/hudfont/000") != std::string::npos) {
        *out_ext_id = 0x00000001u;
        return true;
    }
    if (s == "001" || s == "hudfont_001" || s == "hudfont/001" || s.find("extratex/hudfont/001") != std::string::npos) {
        *out_ext_id = 0x00010001u;
        return true;
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

static void ScanTexturesDirectory() {
    g_Slots.clear();
    g_Slots.resize(1); // Slot 0 is unused so indices start at 1
    g_HashToSlot.clear();
    g_FamilyHashToSlot.clear();
    g_MasterHashToSlot.clear();
    g_BuiltinExtratexSlot[0].store(0, std::memory_order_relaxed);
    g_BuiltinExtratexSlot[1].store(0, std::memory_order_relaxed);
    LockFreeClearContentHashTable();
    for (size_t i = 0; i <= MAX_CUSTOM_TEXTURES; ++i) {
        g_SlotLoadedTex[i].store(nullptr, std::memory_order_relaxed);
        g_SlotLoadFailed[i].store(false, std::memory_order_relaxed);
        g_SlotExpectedW[i].store(0, std::memory_order_relaxed);
        g_SlotExpectedH[i].store(0, std::memory_order_relaxed);
        g_SlotAllowHalfDim[i].store(false, std::memory_order_relaxed);
    }

    std::error_code ec;
    if (!fs::exists(g_TexturesDir, ec)) {
        fs::create_directories(g_TexturesDir, ec);
        LogMsg("[ACZ_HDTex] Created textures directory: %s", g_TexturesDir.string().c_str());
        return;
    }

    std::vector<fs::path> discovered_pngs;
    auto collect_dir_recursive = [&](auto& self, const fs::path& cur_dir) -> void {
        std::wstring search_pat = (cur_dir / L"*").wstring();
        WIN32_FIND_DATAW wfd{};
        HANDLE hFind = FindFirstFileW(search_pat.c_str(), &wfd);
        if (hFind == INVALID_HANDLE_VALUE) return;

        std::vector<fs::path> subdirs;
        std::vector<fs::path> png_files;
        do {
            if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
            if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                // Skip converter raw source folder 'spw' (mods\textures\ostrichmods\icons-weapons\spw)
                if (_wcsicmp(wfd.cFileName, L"spw") == 0) continue;
                subdirs.push_back(cur_dir / wfd.cFileName);
            } else {
                fs::path fp = cur_dir / wfd.cFileName;
                std::wstring wext = fp.extension().wstring();
                if (_wcsicmp(wext.c_str(), L".png") == 0) {
                    png_files.push_back(std::move(fp));
                }
            }
        } while (FindNextFileW(hFind, &wfd));
        FindClose(hFind);

        std::sort(subdirs.begin(), subdirs.end());
        std::sort(png_files.begin(), png_files.end());

        if (!png_files.empty()) {
            LogMsg("[ACZ_HDTex] Scanning directory: %s (%zu PNG files)",
                   cur_dir.string().c_str(), png_files.size());
            for (auto& pf : png_files) {
                discovered_pngs.push_back(std::move(pf));
            }
        }
        for (const auto& sd : subdirs) {
            self(self, sd);
        }
    };
    collect_dir_recursive(collect_dir_recursive, g_TexturesDir);

    size_t count = 0;
    size_t total_bound_hashes = 0;
    for (const fs::path& p : discovered_pngs) {

        std::string stem = p.stem().string();
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

        // 3. Check for companion <png_stem>.json in the same folder for multi-hash bindings & expected dimensions
        fs::path json_path = p;
        json_path.replace_extension(".json");
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

        if (target_hashes.empty() && target_builtin_ext.empty()) {
            LogMsg("[ACZ_HDTex] Skipping PNG without 16-digit hex hash, companion .json, or 000/001.png name: %s",
                   p.filename().string().c_str());
            continue;
        }

        if (g_Slots.size() >= MAX_CUSTOM_TEXTURES) {
            LogMsg("[ACZ_HDTex] WARNING: Maximum custom texture slots (%u) reached!", MAX_CUSTOM_TEXTURES);
            break;
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

        uint16_t slot_idx = static_cast<uint16_t>(g_Slots.size());
        CustomSlotInfo info;
        info.hash        = primary_hash;
        info.png_path    = effective_png_path.string();
        info.loaded_tex  = nullptr;
        info.load_failed = false;

        g_Slots.push_back(std::move(info));
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
            if (IsBuiltinHudFont000Id(ext_id)) {
                g_BuiltinExtratexSlot[0].store(slot_idx, std::memory_order_release);
                g_SlotExpectedW[slot_idx].store(256u, std::memory_order_release);
                g_SlotExpectedH[slot_idx].store(256u, std::memory_order_release);
            } else if (IsBuiltinHudFont001Id(ext_id)) {
                g_BuiltinExtratexSlot[1].store(slot_idx, std::memory_order_release);
                g_SlotExpectedW[slot_idx].store(256u, std::memory_order_release);
                g_SlotExpectedH[slot_idx].store(256u, std::memory_order_release);
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
                "DumpSingleLineSubtitles=0\n",
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

    uint32_t final_custom_id = 0;
    if (matched_slot != 0) {
        final_custom_id = (static_cast<uint32_t>(matched_slot) << 16) | CUSTOM_EXTRATEX_TAG;
        g_SlotExpectedW[matched_slot].store(static_cast<uint16_t>(res.width), std::memory_order_release);
        g_SlotExpectedH[matched_slot].store(static_cast<uint16_t>(res.height), std::memory_order_release);
        RegisterReplacedPixelAddress(gim_addr + 0x20, final_custom_id, res.width, res.height);

        if (g_LoggedMatchedGimHashes.insert(matched_hash).second) {
            LogMsg("[ACZ_HDTex] [%s] Matched GIM Hash=%016llX (%ux%u, PSM=0x%02X, %u bytes) -> Slot #%u (extratex_id=0x%08X)",
                   caller_tag, static_cast<unsigned long long>(matched_hash),
                   res.width, res.height, res.psm, res.total_size, matched_slot, final_custom_id);
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
    g_GimPtrToDiskHash[gim_addr] = primary_hash;

    return final_custom_id;
}

static uint32_t InspectTim2BufferAndGetCustomId(uint8_t* src_pixels, const char* caller_tag, uint64_t* out_content_tag = nullptr) {
    if (!src_pixels) return 0;

    Tim2InspectionResult res{};
    if (!SafeInspectAndHashTim2(src_pixels, &res)) {
        return 0;
    }
    if (out_content_tag) {
        *out_content_tag = res.content_tag;
    }

    uintptr_t pix_addr = reinterpret_cast<uintptr_t>(src_pixels);
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        auto fit = g_Tim2FastCache.find(pix_addr);
        if (fit != g_Tim2FastCache.end() &&
            fit->second.total_size == res.total_size &&
            fit->second.content_tag == res.content_tag) {
            return fit->second.custom_id;
        }
    }

    std::lock_guard<std::mutex> lock(g_Mutex);

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

    uint32_t final_custom_id = 0;
    if (matched_slot != 0) {
        final_custom_id = (static_cast<uint32_t>(matched_slot) << 16) | CUSTOM_EXTRATEX_TAG;
        g_SlotExpectedW[matched_slot].store(res.width, std::memory_order_release);
        g_SlotExpectedH[matched_slot].store(res.height, std::memory_order_release);
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
                    LockFreeInsertContentHash(SUBTITLE_BRACKETS_GIM_HASH, sb_slot);
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
    g_FontPtrToDiskHash[font_addr] = primary_hash;

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
extern "C" __declspec(dllexport) void __fastcall AczHdTex_DumpLiveState(
    const wchar_t* dump_folder_w,
    const char*    report_path_a,
    int            dump_live_resources
);

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

            uint16_t slot_idx = static_cast<uint16_t>(cent.custom_id >> 16);
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
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex + 0x54), &lsrv);
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex + 0x5C), &gpu_w);
                SafeReadU32(reinterpret_cast<uintptr_t>(sl.loaded_tex + 0x60), &gpu_h);
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
    if (!gim_data) return;

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

    if ((extratex_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG && src_data != nullptr) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(src_data);
        if (addr >= g_MinReplacedPixelAddr.load(std::memory_order_relaxed) &&
            addr <= g_MaxReplacedPixelAddr.load(std::memory_order_relaxed)) {
            if (BloomTestPixelAddr(addr)) {
                uint32_t bound_id = LockFreeLookupPixelBinding(addr, rrw, rrh);
                if (bound_id != 0) {
                    extratex_id = bound_id;
                }
            }
        }

        // Direct GIM pixel upload check (e.g. Hangar select.msa SPW weapon atlases uploaded directly via GsUploadVramBlock
        // without calling InitGimDescriptor or BuildGim* first): GIM pixel payload starts at gim_data + 0x20.
        if ((extratex_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG && addr > 0x10020u) {
            uint8_t* gim_data = src_data - 0x20;
            bool may_be_gim = false;
            if ((addr & 0xFFFu) >= 0x20u) {
                may_be_gim = (*reinterpret_cast<const uint32_t*>(gim_data) == 0x004D4947u); // "GIM\0"
            } else {
                uint32_t magic = 0;
                may_be_gim = SafeReadU32(addr - 0x20u, &magic) && (magic == 0x004D4947u);
            }
            if (may_be_gim) {
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

        // Direct TIM2 pixel upload check (e.g. Hangar AircraftSelect.swa / Shop.swa SWG weapon icons & [STATISTICS] diagrams):
        // SWAD TIM2 pixel payload starts at tim2_base + 0x60 (with "eXt\0" at src_data - 0x20 and "GIDX" at src_data - 0x10),
        // or at tim2_base + 0x40 for standard 48-byte TIM2 headers.
        if ((extratex_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG && addr > 0x10060u) {
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

        // Fallback: Lock-free content hash check on the uploaded pixel block (for stripped TIM2/SWG buffers)
        if ((extratex_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG && qwc >= 16u && qwc <= 1024u && qwc != 64u) {
            uint64_t pix_hash = SafeHashByteRange(src_data, static_cast<size_t>(qwc) * 16u);
            uint16_t h_slot   = LockFreeLookupContentHash(pix_hash);
            if (h_slot != 0) {
                extratex_id = (static_cast<uint32_t>(h_slot) << 16) | CUSTOM_EXTRATEX_TAG;
            }
        }
    }



    // Track per-TBP active custom_id and preserve across immediate follow-up 16-color / 256-color CLUT uploads
    bool is_clut_upload = (qwc == 4u && rrw == 8u && rrh == 2u) || (qwc == 64u && rrw == 16u && rrh == 16u);
    if (!is_clut_upload && qwc >= 8u) {
        if ((extratex_id & 0xFFFF) == CUSTOM_EXTRATEX_TAG) {
            g_LastTbpCustomId[dbp & 0x3FFFu].store(extratex_id, std::memory_order_release);
            g_LastUploadedTim2CustomId.store(extratex_id, std::memory_order_release);
        } else {
            g_LastTbpCustomId[dbp & 0x3FFFu].store(0, std::memory_order_release);
            g_LastUploadedTim2CustomId.store(0, std::memory_order_release);
        }
    } else if (is_clut_upload && extratex_id == 0) {
        uint32_t prev_cid = g_LastUploadedTim2CustomId.load(std::memory_order_acquire);
        if ((prev_cid & 0xFFFF) == CUSTOM_EXTRATEX_TAG) {
            extratex_id = prev_cid;
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

// ----------------------------------------------------------------------------
// Load Replacement PNG for Slot on the Render Thread (+0x214420)
// ----------------------------------------------------------------------------
static uint8_t* LoadReplacementPngForSlot(void* gs_ctx, uint16_t slot_idx, uint32_t orig_w, uint32_t orig_h) {
    std::string          path_to_load;
    std::vector<uint8_t> png_bytes;
    uint64_t             hash       = 0;
    bool                 is_mem_png = false;

    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (slot_idx == 0 || slot_idx >= g_Slots.size()) return nullptr;
        CustomSlotInfo& slot = g_Slots[slot_idx];
        if (!g_HotReload || !slot.mem_png.empty()) {
            if (slot.loaded_tex != nullptr) {
                return slot.loaded_tex;
            }
            if (slot.load_failed) return nullptr;
        }
        path_to_load = slot.png_path;
        hash         = slot.hash;
        if (!slot.mem_png.empty()) {
            png_bytes  = slot.mem_png;
            is_mem_png = true;
        }
    }

    if (!is_mem_png) {
        FILE* f = nullptr;
        fopen_s(&f, path_to_load.c_str(), "rb");
        if (!f) {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (slot_idx < g_Slots.size()) g_Slots[slot_idx].load_failed = true;
            g_SlotLoadFailed[slot_idx].store(true, std::memory_order_release);
            LogMsg("[ACZ_HDTex] ERROR: Could not open PNG file on disk: %s", path_to_load.c_str());
            return nullptr;
        }

        fseek(f, 0, SEEK_END);
        long file_len = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (file_len <= 8) {
            fclose(f);
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (slot_idx < g_Slots.size()) g_Slots[slot_idx].load_failed = true;
            g_SlotLoadFailed[slot_idx].store(true, std::memory_order_release);
            LogMsg("[ACZ_HDTex] ERROR: Invalid/empty PNG file: %s", path_to_load.c_str());
            return nullptr;
        }

        png_bytes.resize(static_cast<size_t>(file_len));
        size_t read_bytes = fread(png_bytes.data(), 1, png_bytes.size(), f);
        fclose(f);

        if (read_bytes != png_bytes.size()) {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (slot_idx < g_Slots.size()) g_Slots[slot_idx].load_failed = true;
            g_SlotLoadFailed[slot_idx].store(true, std::memory_order_release);
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

    uint16_t exp_w = g_SlotExpectedW[slot_idx].load(std::memory_order_acquire);
    uint16_t exp_h = g_SlotExpectedH[slot_idx].load(std::memory_order_acquire);
    if (!is_mem_png && exp_w > 0 && exp_h > 0 && orig_w >= exp_w && orig_h >= exp_h &&
        (orig_w > exp_w || orig_h > exp_h) &&
        orig_w == NextPow2U32(exp_w) && orig_h == NextPow2U32(exp_h)) {
        std::vector<uint8_t> src_rgba;
        uint32_t dec_w = 0, dec_h = 0;
        if (DecodePngFileViaWic(path_to_load, src_rgba, &dec_w, &dec_h) &&
            dec_w > 0 && dec_h > 0 && (dec_w % exp_w) == 0 && (dec_h % exp_h) == 0 &&
            (dec_w / exp_w) == (dec_h / exp_h)) {
            uint32_t scale    = dec_w / exp_w;
            uint32_t canvas_w = orig_w * scale;
            uint32_t canvas_h = orig_h * scale;
            std::vector<uint8_t> padded_rgba(static_cast<size_t>(canvas_w) * canvas_h * 4u, 0);
            for (uint32_t y = 0; y < dec_h; ++y) {
                std::memcpy(
                    &padded_rgba[static_cast<size_t>(y) * canvas_w * 4u],
                    &src_rgba[static_cast<size_t>(y) * dec_w * 4u],
                    static_cast<size_t>(dec_w) * 4u
                );
            }
            std::vector<uint8_t> padded_png;
            EncodeRgbaToPngBytes(padded_rgba.data(), canvas_w, canvas_h, padded_png);
            if (!padded_png.empty()) {
                png_bytes = std::move(padded_png);
                LogMsg("[ACZ_HDTex] Auto-padded sub-region PNG (%ux%u for %ux%u sprite) to power-of-two canvas %ux%u (logical %ux%u) for Slot #%u",
                       dec_w, dec_h, exp_w, exp_h, canvas_w, canvas_h, orig_w, orig_h, slot_idx);
            }
        }
    }

    if (gs_ctx != nullptr && g_InvalidateGsVramRange != nullptr) {
        g_InvalidateGsVramRange(gs_ctx, 0, 0x4000);
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

                g_Slots[slot_idx].loaded_tex  = hd_tex;
                g_Slots[slot_idx].load_failed = false;
                if (!g_HotReload && !g_Slots[slot_idx].mem_png.empty()) {
                    std::vector<uint8_t>().swap(g_Slots[slot_idx].mem_png);
                }
                g_SlotLoadedTex[slot_idx].store(hd_tex, std::memory_order_release);

                if (!is_mem_png) {
                    LogMsg("[ACZ_HDTex] Loaded HD PNG (%ux%u -> logical %ux%u) for Hash=%016llX (Slot #%u) -> tex=%p (%s)",
                           png_w, png_h,
                           *reinterpret_cast<uint32_t*>(hd_tex + 0x14),
                           *reinterpret_cast<uint32_t*>(hd_tex + 0x18),
                           static_cast<unsigned long long>(hash), slot_idx, hd_tex, path_to_load.c_str());
                }
            } else {
                g_Slots[slot_idx].load_failed = true;
                g_SlotLoadFailed[slot_idx].store(true, std::memory_order_release);
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
static uint8_t* __fastcall Hook_LookupOrCreateGsTexture(
    void* gs_ctx,
    uint64_t tex0_reg,
    uint8_t flag_r8
) {
    if (gs_ctx != nullptr && g_LastGsRenderCtx.load(std::memory_order_relaxed) != gs_ctx) {
        g_LastGsRenderCtx.store(gs_ctx, std::memory_order_relaxed);
    }

    uint8_t* gs_tex = g_OrigLookupOrCreateGsTexture(gs_ctx, tex0_reg, flag_r8);
    if (!gs_tex) {
        return nullptr;
    }

    uint32_t ext_id = *reinterpret_cast<uint32_t*>(gs_tex + 0x38);

    uint32_t tbp0 = static_cast<uint32_t>(tex0_reg & 0x3FFFull);
    uint32_t psm  = static_cast<uint32_t>((tex0_reg >> 20) & 0x3Full);
    uint32_t orig_w_pre = *reinterpret_cast<uint32_t*>(gs_tex + 0x14);
    uint32_t orig_h_pre = *reinterpret_cast<uint32_t*>(gs_tex + 0x18);

    // Dynamic SWG/TIM2 scratch VRAM slot check (e.g. TBP0 = 0x3000 in AircraftSelect.swa / Shop.swa)
    if (tbp0 == 0x3000u || (ext_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG) {
        uint32_t tracked_cid = g_LastTbpCustomId[tbp0 & 0x3FFFu].load(std::memory_order_acquire);
        if ((tracked_cid & 0xFFFF) == CUSTOM_EXTRATEX_TAG) {
            ext_id = tracked_cid;
        } else if (tbp0 == 0x3000u && tracked_cid == 0) {
            // An unreplaced TIM2 sprite was uploaded to scratch TBP0=0x3000; clear any stale tag unless VRAM matches a known hash
            ext_id = 0;
        }
        if ((ext_id & 0xFFFF) != CUSTOM_EXTRATEX_TAG && psm == 0x14u && orig_w_pre <= 64u && orig_h_pre <= 64u) {
            void* vram_ctx = g_LastGsVramCtx.load(std::memory_order_relaxed);
            const uint8_t* vram_base = SafeReadGsVramPtr(vram_ctx);
            uint32_t byte_off = tbp0 * 256u;
            if (vram_base != nullptr && byte_off + 2048u <= PS2_GS_VRAM_SIZE) {
                const uint8_t* vptr = vram_base + byte_off;
                uint16_t vslot = LockFreeLookupContentHash(SafeHashByteRange(vptr, 800u));
                if (vslot == 0) {
                    vslot = LockFreeLookupContentHash(SafeHashByteRange(vptr, 2048u));
                }
                if (vslot == 0) {
                    vslot = LockFreeLookupContentHash(SafeHashStridedByteRows(vptr, 40u, 20u, 32u));
                }
                if (vslot != 0) {
                    ext_id = (static_cast<uint32_t>(vslot) << 16) | CUSTOM_EXTRATEX_TAG;
                }
            }
        }
    }

    if ((ext_id & 0xFFFF) == CUSTOM_EXTRATEX_TAG) {
        uint16_t slot_idx = static_cast<uint16_t>(ext_id >> 16);
        if (slot_idx == 0 || slot_idx > MAX_CUSTOM_TEXTURES) {
            return gs_tex;
        }
        uint32_t orig_w   = *reinterpret_cast<uint32_t*>(gs_tex + 0x14);
        uint32_t orig_h   = *reinterpret_cast<uint32_t*>(gs_tex + 0x18);
        uint32_t orig_tbp = *reinterpret_cast<uint32_t*>(gs_tex + 0x20);
        uint32_t orig_cbp = *reinterpret_cast<uint32_t*>(gs_tex + 0x24);

        uint32_t exp_w = g_SlotExpectedW[slot_idx].load(std::memory_order_relaxed);
        uint32_t exp_h = g_SlotExpectedH[slot_idx].load(std::memory_order_relaxed);
        if (exp_w != 0 && exp_h != 0 && orig_w != 0 && orig_h != 0) {
            uint32_t pow2_w = NextPow2U32(exp_w);
            uint32_t pow2_h = NextPow2U32(exp_h);
            bool match_full = (orig_w == exp_w || orig_w == pow2_w) && (orig_h == exp_h || orig_h == pow2_h);
            bool match_half = g_SlotAllowHalfDim[slot_idx].load(std::memory_order_relaxed) &&
                              (orig_w * 2u == exp_w) && (orig_h * 2u == exp_h);
            if (!match_full && !match_half) {
                return gs_tex;
            }
        }

        uint8_t* hd_tex = nullptr;
        if (!g_HotReload) {
            hd_tex = g_SlotLoadedTex[slot_idx].load(std::memory_order_acquire);
            if (hd_tex == nullptr && g_SlotLoadFailed[slot_idx].load(std::memory_order_acquire)) {
                *reinterpret_cast<uint32_t*>(gs_tex + 0x38) = 0;
                return gs_tex;
            }
        }

        if (hd_tex == nullptr) {
            hd_tex = LoadReplacementPngForSlot(gs_ctx, slot_idx, orig_w, orig_h);
        }

        if (hd_tex != nullptr) {
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

static uint8_t* __fastcall Hook_LoadTextureFromArcPath(const char* const* arc_path_ptr) {
    char rel_path[260] = {};
    if (SafeReadCString(arc_path_ptr, rel_path, sizeof(rel_path))) {
        uint32_t ext_id = 0;
        uint16_t slot_idx = 0;
        if (TryMatchBuiltinExtratexName(rel_path, &ext_id)) {
            if (IsBuiltinHudFont000Id(ext_id)) {
                slot_idx = g_BuiltinExtratexSlot[0].load(std::memory_order_acquire);
            } else if (IsBuiltinHudFont001Id(ext_id)) {
                slot_idx = g_BuiltinExtratexSlot[1].load(std::memory_order_acquire);
            }
        }

        if (slot_idx != 0) {
            uint8_t* hd_tex = LoadReplacementPngForSlot(nullptr, slot_idx, 256u, 256u);
            if (hd_tex != nullptr) {
                LogMsg("[ACZ_HDTex] [ArcTexHook] Overrode modloader .arc texture '%s' with HD replacement Slot #%u -> tex=%p",
                       rel_path, slot_idx, reinterpret_cast<void*>(hd_tex));
                return hd_tex;
            }
        }
    }

    return g_OrigLoadTextureFromArcPath(arc_path_ptr);
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
    if (std::memcmp(target_bytes, expected_prologue, prologue_len) != 0) {
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
    std::memcpy(tramp, target_bytes, prologue_len);
    WriteAbsoluteJump64(tramp + prologue_len, reinterpret_cast<const void*>(target_addr + prologue_len));
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

// ----------------------------------------------------------------------------
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
    LogMsg("[ACZ_HDTex] Config: EnableLogging=%d, DumpAllGims=%d, DumpReplacedTextures=%d, DumpRawGimFiles=%d, HotReload=%d, AutoShareFontGlyphs=%d, DumpMasterFontAtlas=%d, DumpMergedFamilyAtlases=%d, DumpIndividualFontAtlases=%d",
           g_EnableLogging ? 1 : 0, g_DumpAllGims ? 1 : 0,
           g_DumpReplacedTextures ? 1 : 0, g_DumpRawGimFiles ? 1 : 0,
           g_HotReload ? 1 : 0, g_AutoShareFontGlyphs ? 1 : 0,
           g_DumpMasterFontAtlas ? 1 : 0, g_DumpMergedFamilyAtlases ? 1 : 0,
           g_DumpIndividualFontAtlases ? 1 : 0);

    g_InvalidateGsVramRange = reinterpret_cast<FnInvalidateGsVramRange>(
        g_ExeBase + AczRva(RVA_InvalidateGsVramRange)
    );
    g_CreateTextureFromPngMemory = reinterpret_cast<FnCreateTextureFromPngMemory>(
        g_ExeBase + AczRva(RVA_CreateTextureFromPngMemory)
    );

    ScanTexturesDirectory();
    PreloadExistingDumpedMasterFontAtlases();

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

    if (ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7 && ok8 && ok9) {
        LogMsg("[ACZ_HDTex] All 9 hooks installed! Ready to replace GIM, FHM, TIM2/SWAD, Dynamic GameFont, Master Atlas, and .arc extratex textures.");
    } else {
        LogMsg("[ACZ_HDTex] WARNING: One or more hooks failed to install.");
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