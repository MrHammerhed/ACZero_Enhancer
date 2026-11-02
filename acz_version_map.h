// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — Executable Version & RVA Mapper
// File: acz_version_map.h
// ============================================================================
// Supports both known PC port executables out-of-the-box:
//   1. v1.0.2.1 (Initial Release / AC8 Bundle)
//      - File Size     : 3,803,248 bytes (0x3A0870)
//      - TimeDateStamp : 0x6A9E70E0
//      - SizeOfImage   : 0x0385D000 (9 sections, no .bind)
//      - EntryPoint RVA: 0x0022DD38
//
//   2. v2.0.0.1 (Standalone Steam Release, AppID 5370600)
//      - File Size     : 4,046,008 bytes (0x3DBCB8)
//      - TimeDateStamp : 0x6AC61130
//      - SizeOfImage   : 0x0389B000 (10 sections, includes .bind SteamStub v3.1)
//      - EntryPoint RVA: 0x03861310 (.bind unpacker -> CRT 0x0022F638)
//
// Architectural Summary of 1.0.2.1 -> 2.0.0.1 RVA Shifts:
//   - .text [0x001000 .. 0x004C9F] : +0x0000 (identical)
//   - .text [0x004CA0 .. 0x004E7F] : +0x0010 (1 static atexit stub added at 0x004CA0)
//   - .text [0x004E80 .. 0x18AD1F] : +0x0040 (64-byte alignment shift across core game engine)
//   - .text [0x18AD20 .. 0x18B7FF] : Medal unlock function expanded by +0x0640 bytes for Steam Achievements
//   - .text [0x18B800 .. 0x18D7A3] : +0x0680
//   - .text [0x18D7A4 .. 0x18E51F] : +0x0685
//   - .text [0x18E520 .. 0x21E067] : +0x0680 (entire GS VRAM, GIM, TIM2, GameFont & D3D12 renderer)
//   - .text [0x21E068 .. 0x27AF5F] : +0x0687 .. +0x19D0 (WinRT VP9 codec check & Steam Achievement helpers)
//   - .rdata IAT ExitProcess       : 0x0027B2E8 -> 0x0027D2F8 (+0x2010)
//   - .rdata IAT PostQuitMessage   : 0x0027B6F0 -> 0x0027D6E8 (+0x1FF8)
//   - .rdata Float Constants       : +0x2238 (RIP-relative disp32 from +0x0040 .text sites shifts by +0x21F8)
//   - .rdata Code Cave             : 0x00308F00 -> 0x0030B500 (+0x2600)
//   - .data Initialized Globals    : +0x3000 (e.g. 0x0031CF18 -> 0x0031FF18)
//   - .data BSS / D3D12 Globals    : +0x3020 (e.g. 0x0033AC70 -> 0x0033DC90, 0x0033ACE8 -> 0x0033DD08)
// ============================================================================
#pragma once

#include <windows.h>
#include <cstdint>

enum class AczExeVersion : uint32_t {
    V1_0_2_1 = 0, // Initial Release (TimeDateStamp = 0x6A9E70E0, SizeOfImage = 0x0385D000)
    V2_0_0_1 = 1  // Standalone Steam Release (TimeDateStamp = 0x6AC61130, SizeOfImage = 0x0389B000)
};

inline AczExeVersion DetectAczExeVersion(uintptr_t exe_base) {
    if (exe_base == 0) return AczExeVersion::V1_0_2_1;
    __try {
        const uint16_t mz = *reinterpret_cast<const uint16_t*>(exe_base);
        if (mz == 0x5A4Du) {
            const uint32_t lfanew = *reinterpret_cast<const uint32_t*>(exe_base + 0x3Cu);
            if (lfanew >= 0x40u && lfanew < 0x1000u) {
                const uintptr_t nt = exe_base + lfanew;
                const uint32_t ts = *reinterpret_cast<const uint32_t*>(nt + 0x08u);
                const uint32_t img_size = *reinterpret_cast<const uint32_t*>(nt + 0x50u);
                if (ts == 0x6AC61130u || img_size >= 0x03890000u) {
                    return AczExeVersion::V2_0_0_1;
                }
                if (ts == 0x6A9E70E0u || img_size == 0x0385D000u) {
                    return AczExeVersion::V1_0_2_1;
                }
            }
        }
        // Fallback signature check on InitGimDescriptor (+0x203F40 in 2.0.0.1 vs +0x2038C0 in 1.0.2.1):
        const uint8_t* p2001 = reinterpret_cast<const uint8_t*>(exe_base + 0x00203F40u);
        if (p2001[0] == 0x40u && p2001[1] == 0x53u && p2001[2] == 0x48u && p2001[3] == 0x89u) {
            return AczExeVersion::V2_0_0_1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return AczExeVersion::V1_0_2_1;
}

inline const char* GetAczExeVersionString(AczExeVersion ver) {
    return (ver == AczExeVersion::V2_0_0_1)
        ? "2.0.0.1 (Standalone Steam)"
        : "1.0.2.1 (Initial Release)";
}

// Translates any baseline 1.0.2.1 RVA to the active executable's RVA:
inline uintptr_t AczResolveRva(uintptr_t rva_1021, AczExeVersion ver) {
    if (ver != AczExeVersion::V2_0_0_1) return rva_1021;

    // 1. .text code section (0x001000 .. 0x27AF5F in 1.0.2.1)
    if (rva_1021 < 0x004CA0u) return rva_1021;
    if (rva_1021 < 0x004E80u) return rva_1021 + 0x0010u;
    if (rva_1021 < 0x18AD20u) return rva_1021 + 0x0040u;
    if (rva_1021 < 0x18D7A4u) return rva_1021 + 0x0680u;
    if (rva_1021 < 0x18E520u) return rva_1021 + 0x0685u;
    if (rva_1021 < 0x21E068u) return rva_1021 + 0x0680u;
    if (rva_1021 < 0x27B000u) return rva_1021 + 0x1900u;

    // 2. .rdata IAT & constants (0x27B000 .. 0x308FFF in 1.0.2.1)
    if (rva_1021 == 0x0027B2E8u) return 0x0027D2F8u; // IAT ExitProcess
    if (rva_1021 == 0x0027B6F0u) return 0x0027D6E8u; // IAT PostQuitMessage
    if (rva_1021 >= 0x00308F00u && rva_1021 < 0x00309000u) {
        return rva_1021 + 0x2600u; // .rdata zero-padded float cave (0x308F00 -> 0x30B500)
    }
    if (rva_1021 < 0x00309000u) return rva_1021 + 0x2238u; // .rdata constants

    // 3. .data & BSS globals (0x309000+ in 1.0.2.1)
    if (rva_1021 < 0x00330000u) return rva_1021 + 0x3000u; // Initialized .data (e.g. 0x31CF18 -> 0x31FF18)
    return rva_1021 + 0x3020u; // BSS globals (e.g. 0x33AC70 -> 0x33DC90, 0x33ACE8 -> 0x33DD08, 0x33DA48 -> 0x340A68)
}
