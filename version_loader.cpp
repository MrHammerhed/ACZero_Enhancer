// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — ACZero Enhancer Native ASI Loader
// File: version_loader.cpp (v0.1 — version.dll Proxy, .text Startup Hook & Chaining)
// Build: Visual Studio 2026 / 2022 x64 (Release) -> version.dll
// Usage: Place version.dll into "<GameDir>\version.dll" (next to acz.exe)
//        Loads all *.asi plugins from "<GameDir>\mods\enhancer\"
//        Writes unified log output to "<GameDir>\mods\enhancer\enhancer.log"
//        Optional chaining: if the original version.dll is renamed to
//        "version_orig.dll" (or "version_original.dll" / "modloader.dll")
//        next to acz.exe, it is automatically chained at startup.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#include <share.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <algorithm>
#include <string>
#include <vector>

#pragma intrinsic(_ReturnAddress)

// ----------------------------------------------------------------------------
// Constants & Add-on API (Compatible with ACZ Mod Loader 1.2 & ASI Plugins)
// ----------------------------------------------------------------------------
static constexpr const char* LOADER_VERSION_STR = "v0.1";

using FnModLoaderLog = void (__cdecl*)(const char* fmt, ...);

struct AczAddonApi {
    uint32_t       struct_size;  // +0x00 (0x30 = 48 bytes)
    uint32_t       api_version;  // +0x04 (1)
    HMODULE        addon_module; // +0x08
    HMODULE        exe_module;   // +0x10
    const wchar_t* game_dir;     // +0x18
    const wchar_t* mods_dir;     // +0x20
    FnModLoaderLog log_fn;       // +0x28 (writes to mods\enhancer\enhancer.log + newline)
};

using FnAczAddonInit  = void (__fastcall*)(const AczAddonApi* api);
using FnInitializeASI = void (__cdecl*)();

using PFN_GetSystemTimeAsFileTime = void  (WINAPI*)(LPFILETIME lpSystemTimeAsFileTime);
using PFN_GetModuleFileNameA      = DWORD (WINAPI*)(HMODULE hModule, LPSTR lpFilename, DWORD nSize);

// ----------------------------------------------------------------------------
// Global State
// ----------------------------------------------------------------------------
static HMODULE                     g_selfModule                  = nullptr;
static HMODULE                     g_exeModule                   = nullptr;
static HMODULE                     g_sysVersionModule            = nullptr;
static HMODULE                     g_chainedLoaderModule         = nullptr;

static uintptr_t                   g_textStart                   = 0;
static uintptr_t                   g_textEnd                     = 0;
static volatile LONG               g_gameCodeStarted             = 0;

static uintptr_t*                  g_slotGetSystemTimeAsFileTime = nullptr;
static PFN_GetSystemTimeAsFileTime g_origGetSystemTimeAsFileTime = nullptr;

static uintptr_t*                  g_slotGetModuleFileNameA      = nullptr;
static PFN_GetModuleFileNameA      g_origGetModuleFileNameA      = nullptr;

static wchar_t                     g_gameDir[MAX_PATH]           = {};
static wchar_t                     g_modsDir[MAX_PATH]           = {};
static wchar_t                     g_enhancerDir[MAX_PATH]       = {};
static wchar_t                     g_logFilePath[MAX_PATH]       = {};

static FILE*                       g_logFile                     = nullptr;
static CRITICAL_SECTION            g_logCs                       = {};
static bool                        g_logCsInitialized            = false;

// ----------------------------------------------------------------------------
// Unified Logger (mods\enhancer\enhancer.log)
// ----------------------------------------------------------------------------
static void __cdecl LoaderLogPrintf(const char* fmt, ...) {
    if (!fmt || !g_logFile) return;

    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';

    if (g_logCsInitialized) EnterCriticalSection(&g_logCs);
    if (g_logFile) {
        fprintf(g_logFile, "%s\n", buf);
        fflush(g_logFile);
    }
    if (g_logCsInitialized) LeaveCriticalSection(&g_logCs);
}

// ----------------------------------------------------------------------------
// System32\version.dll Export Resolver & 17 Proxy Exports
// ----------------------------------------------------------------------------
using PFN_VersionExport8 = uintptr_t (WINAPI*)(
    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4,
    uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8);

static FARPROC ResolveSystemVersionExport(const char* funcName) {
    HMODULE hSysVer = g_sysVersionModule;
    if (!hSysVer) {
        wchar_t sysDir[MAX_PATH] = {};
        UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
        if (len == 0 || len >= MAX_PATH - 16) {
            return nullptr;
        }
        wcscat_s(sysDir, MAX_PATH, L"\\version.dll");
        hSysVer = LoadLibraryW(sysDir);
        if (!hSysVer) {
            return nullptr;
        }
        g_sysVersionModule = hSysVer;
    }
    return GetProcAddress(hSysVer, funcName);
}

#define DEFINE_VERSION_PROXY_EXPORT(funcName)                                      \
    __pragma(comment(linker, "/EXPORT:" #funcName "=Proxy_" #funcName))            \
    extern "C" uintptr_t WINAPI Proxy_##funcName(                                  \
        uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4,                    \
        uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8)                    \
    {                                                                              \
        static PFN_VersionExport8 s_proc = nullptr;                                \
        PFN_VersionExport8 proc = s_proc;                                          \
        if (!proc) {                                                               \
            proc = reinterpret_cast<PFN_VersionExport8>(                           \
                ResolveSystemVersionExport(#funcName));                            \
            s_proc = proc;                                                         \
            if (!proc) {                                                           \
                SetLastError(ERROR_PROC_NOT_FOUND);                                \
                return 0;                                                          \
            }                                                                      \
        }                                                                          \
        return proc(a1, a2, a3, a4, a5, a6, a7, a8);                               \
    }

DEFINE_VERSION_PROXY_EXPORT(GetFileVersionInfoA)
DEFINE_VERSION_PROXY_EXPORT(GetFileVersionInfoByHandle)
DEFINE_VERSION_PROXY_EXPORT(GetFileVersionInfoExA)
DEFINE_VERSION_PROXY_EXPORT(GetFileVersionInfoExW)
DEFINE_VERSION_PROXY_EXPORT(GetFileVersionInfoSizeA)
DEFINE_VERSION_PROXY_EXPORT(GetFileVersionInfoSizeExA)
DEFINE_VERSION_PROXY_EXPORT(GetFileVersionInfoSizeExW)
DEFINE_VERSION_PROXY_EXPORT(GetFileVersionInfoSizeW)
DEFINE_VERSION_PROXY_EXPORT(GetFileVersionInfoW)
DEFINE_VERSION_PROXY_EXPORT(VerFindFileA)
DEFINE_VERSION_PROXY_EXPORT(VerFindFileW)
DEFINE_VERSION_PROXY_EXPORT(VerInstallFileA)
DEFINE_VERSION_PROXY_EXPORT(VerInstallFileW)
DEFINE_VERSION_PROXY_EXPORT(VerLanguageNameA)
DEFINE_VERSION_PROXY_EXPORT(VerLanguageNameW)
DEFINE_VERSION_PROXY_EXPORT(VerQueryValueA)
DEFINE_VERSION_PROXY_EXPORT(VerQueryValueW)

// ----------------------------------------------------------------------------
// Safe Plugin Initialization Wrapper
// ----------------------------------------------------------------------------
static bool SafeInvokeAddonEntry(
    const wchar_t* fileName,
    HMODULE hAddon,
    FnAczAddonInit fnAddonInit,
    FnInitializeASI fnInitAsi,
    const AczAddonApi* api,
    DWORD* outExceptionCode)
{
    __try {
        if (fnAddonInit) {
            fnAddonInit(api);
        } else if (fnInitAsi) {
            fnInitAsi();
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (outExceptionCode) *outExceptionCode = GetExceptionCode();
        return false;
    }
}

static void LoadSingleAsiPlugin(const wchar_t* fileName) {
    if (!fileName || !fileName[0]) return;

    wchar_t fullPath[MAX_PATH] = {};
    swprintf_s(fullPath, MAX_PATH, L"%ls\\%ls", g_enhancerDir, fileName);

    if (GetFileAttributesW(fullPath) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    HMODULE hAddon = LoadLibraryW(fullPath);
    if (!hAddon) {
        LoaderLogPrintf("[Loader] ERROR: Could not load ASI plugin %ls (Win32 error %lu)",
                        fileName, GetLastError());
        return;
    }

    auto fnAddonInit = reinterpret_cast<FnAczAddonInit>(GetProcAddress(hAddon, "AczAddonInit"));
    auto fnInitAsi   = reinterpret_cast<FnInitializeASI>(GetProcAddress(hAddon, "InitializeASI"));

    AczAddonApi api = {};
    api.struct_size  = sizeof(AczAddonApi);
    api.api_version  = 1;
    api.addon_module = hAddon;
    api.exe_module   = g_exeModule;
    api.game_dir     = g_gameDir;
    api.mods_dir     = g_modsDir;
    api.log_fn       = &LoaderLogPrintf;

    DWORD excCode = 0;
    if (!SafeInvokeAddonEntry(fileName, hAddon, fnAddonInit, fnInitAsi, &api, &excCode)) {
        LoaderLogPrintf("[Loader] ERROR: ASI plugin %ls crashed during startup (exception 0x%08lX)",
                        fileName, excCode);
        return;
    }

    LoaderLogPrintf("[Loader] ASI plugin loaded: mods\\enhancer\\%ls", fileName);
}

static void LoadAllAsiPlugins() {
    wchar_t searchPattern[MAX_PATH] = {};
    swprintf_s(searchPattern, MAX_PATH, L"%ls\\*.asi", g_enhancerDir);

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(searchPattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LoaderLogPrintf("[Loader] No *.asi plugins found in mods\\enhancer\\");
        return;
    }

    std::vector<std::wstring> foundFiles;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            foundFiles.emplace_back(fd.cFileName);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    // Priority order: memdump.asi first (crash handler & diagnostics),
    // then enhancer.asi (widescreen/input/60fps), then acz_hdtex.asi (HD textures & auto-installer),
    // followed by any other *.asi files in alphabetical order.
    static const wchar_t* kPriorityOrder[] = {
        L"memdump.asi",
        L"enhancer.asi",
        L"acz_hdtex.asi"
    };

    std::vector<bool> loaded(foundFiles.size(), false);

    for (const wchar_t* prioName : kPriorityOrder) {
        for (size_t i = 0; i < foundFiles.size(); ++i) {
            if (!loaded[i] && _wcsicmp(foundFiles[i].c_str(), prioName) == 0) {
                loaded[i] = true;
                LoadSingleAsiPlugin(foundFiles[i].c_str());
            }
        }
    }

    std::vector<std::wstring> remaining;
    for (size_t i = 0; i < foundFiles.size(); ++i) {
        if (!loaded[i]) {
            remaining.push_back(foundFiles[i]);
        }
    }
    std::sort(remaining.begin(), remaining.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });

    for (const std::wstring& name : remaining) {
        LoadSingleAsiPlugin(name.c_str());
    }
}

// ----------------------------------------------------------------------------
// One-Shot Post-SteamStub Game Code Startup Trigger (.text Range Check)
// Replicates the exact mechanism from ACE COMBAT ZERO mod loader 1.2:
// While SteamStub (.bind) is unpacking acz.exe, calls to kernel32 IAT have
// a return address outside acz.exe's .text section and pass straight through.
// The instant execution enters acz.exe's .text section, we restore the IAT
// slots and load all ASI plugins before WinMain or D3D12 initialization.
// ----------------------------------------------------------------------------
static void RestoreStartupIatSlot(uintptr_t* pSlot, uintptr_t hookFn, uintptr_t origFn) {
    if (!pSlot || !origFn) return;
    if (*pSlot == hookFn) {
        DWORD oldProt = 0;
        if (VirtualProtect(pSlot, sizeof(uintptr_t), PAGE_READWRITE, &oldProt)) {
            _InterlockedExchange64(reinterpret_cast<volatile LONG64*>(pSlot), static_cast<LONG64>(origFn));
            VirtualProtect(pSlot, sizeof(uintptr_t), oldProt, &oldProt);
        }
    }
}

static void WINAPI Hook_GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
static DWORD WINAPI Hook_GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);

__declspec(noinline) static void OnPotentialGameCodeStart(uintptr_t callerRetAddr) {
    if (callerRetAddr < g_textStart || callerRetAddr >= g_textEnd) {
        return;
    }

    if (InterlockedCompareExchange(&g_gameCodeStarted, 1, 0) != 0) {
        return;
    }

    RestoreStartupIatSlot(
        g_slotGetSystemTimeAsFileTime,
        reinterpret_cast<uintptr_t>(&Hook_GetSystemTimeAsFileTime),
        reinterpret_cast<uintptr_t>(g_origGetSystemTimeAsFileTime));

    RestoreStartupIatSlot(
        g_slotGetModuleFileNameA,
        reinterpret_cast<uintptr_t>(&Hook_GetModuleFileNameA),
        reinterpret_cast<uintptr_t>(g_origGetModuleFileNameA));

    LoaderLogPrintf("[Loader] Game code started (caller RVA +0x%llX)",
                    static_cast<unsigned long long>(callerRetAddr - reinterpret_cast<uintptr_t>(g_exeModule)));

    LoadAllAsiPlugins();
}

static void WINAPI Hook_GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
    OnPotentialGameCodeStart(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    g_origGetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}

static DWORD WINAPI Hook_GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    OnPotentialGameCodeStart(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    return g_origGetModuleFileNameA(hModule, lpFilename, nSize);
}

// ----------------------------------------------------------------------------
// PE Section & Import Table Helpers
// ----------------------------------------------------------------------------
static bool FindExeTextSection(HMODULE hExe, uintptr_t* outStart, uintptr_t* outEnd) {
    if (!hExe || !outStart || !outEnd) return false;
    auto base = reinterpret_cast<const uint8_t*>(hExe);
    auto dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    auto sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (std::memcmp(sec->Name, ".text", 6) == 0) {
            *outStart = reinterpret_cast<uintptr_t>(base) + sec->VirtualAddress;
            *outEnd   = *outStart + sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

static uintptr_t* FindKernel32IatSlot(HMODULE hExe, const char* targetFuncName) {
    if (!hExe || !targetFuncName) return nullptr;
    auto base = reinterpret_cast<uint8_t*>(hExe);
    auto dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const IMAGE_DATA_DIRECTORY& impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (impDir.VirtualAddress == 0 || impDir.Size == 0) return nullptr;

    auto impDesc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + impDir.VirtualAddress);
    for (; impDesc->Name != 0; ++impDesc) {
        const char* dllName = reinterpret_cast<const char*>(base + impDesc->Name);
        if (_stricmp(dllName, "kernel32.dll") != 0) {
            continue;
        }

        if (impDesc->OriginalFirstThunk == 0 || impDesc->FirstThunk == 0) {
            continue;
        }

        auto origThunk = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + impDesc->OriginalFirstThunk);
        auto iatThunk  = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + impDesc->FirstThunk);

        for (; origThunk->u1.AddressOfData != 0; ++origThunk, ++iatThunk) {
            if (IMAGE_SNAP_BY_ORDINAL64(origThunk->u1.Ordinal)) {
                continue;
            }
            auto importByName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + origThunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(importByName->Name), targetFuncName) == 0) {
                return reinterpret_cast<uintptr_t*>(&iatThunk->u1.Function);
            }
        }
    }
    return nullptr;
}

static bool InstallSingleKernel32IatHook(
    HMODULE hExe,
    HMODULE hKernel32,
    const char* funcName,
    uintptr_t hookFn,
    uintptr_t** outSlot,
    uintptr_t* outOrigFn)
{
    uintptr_t* pSlot = FindKernel32IatSlot(hExe, funcName);
    if (!pSlot) {
        *outSlot = nullptr;
        return false;
    }

    uintptr_t currentTarget = *pSlot;
    HMODULE hOwner = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(currentTarget),
            &hOwner))
    {
        currentTarget = reinterpret_cast<uintptr_t>(GetProcAddress(hKernel32, funcName));
    }

    if (!currentTarget) {
        *outSlot = nullptr;
        return false;
    }

    *outSlot   = pSlot;
    *outOrigFn = currentTarget;

    DWORD oldProt = 0;
    if (!VirtualProtect(pSlot, sizeof(uintptr_t), PAGE_READWRITE, &oldProt)) {
        return false;
    }
    _InterlockedExchange64(reinterpret_cast<volatile LONG64*>(pSlot), static_cast<LONG64>(hookFn));
    VirtualProtect(pSlot, sizeof(uintptr_t), oldProt, &oldProt);
    return true;
}

// ----------------------------------------------------------------------------
// Optional Chaining of Renamed Original Loader (version_orig.dll, etc.)
// ----------------------------------------------------------------------------
static void LoadChainedOriginalLoaderIfPresent() {
    static const wchar_t* kCandidateNames[] = {
        L"version_orig.dll",
        L"version_original.dll",
        L"modloader.dll"
    };

    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(g_selfModule, selfPath, MAX_PATH);

    for (const wchar_t* candidate : kCandidateNames) {
        wchar_t candidatePath[MAX_PATH] = {};
        swprintf_s(candidatePath, MAX_PATH, L"%ls\\%ls", g_gameDir, candidate);

        if (_wcsicmp(candidatePath, selfPath) == 0) {
            continue;
        }
        if (GetFileAttributesW(candidatePath) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }

        HMODULE hChained = LoadLibraryW(candidatePath);
        if (hChained && hChained != g_selfModule) {
            g_chainedLoaderModule = hChained;
            LoaderLogPrintf("[Loader] Chained external loader: %ls (base=%p)",
                            candidate, reinterpret_cast<void*>(hChained));
            return;
        } else if (!hChained) {
            LoaderLogPrintf("[Loader] WARNING: Failed to load chained loader %ls (Win32 error %lu)",
                            candidate, GetLastError());
        }
    }
}

// ----------------------------------------------------------------------------
// Loader Initialization (Called on DLL_PROCESS_ATTACH)
// ----------------------------------------------------------------------------
static void InitializeVersionLoader() {
    g_exeModule = GetModuleHandleW(nullptr);
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!g_exeModule || !hKernel32) {
        return;
    }

    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return;
    }

    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (!lastSlash || _wcsicmp(lastSlash + 1, L"acz.exe") != 0) {
        return;
    }

    size_t dirLen = static_cast<size_t>(lastSlash - exePath);
    wcsncpy_s(g_gameDir, MAX_PATH, exePath, dirLen);
    swprintf_s(g_modsDir, MAX_PATH, L"%ls\\mods", g_gameDir);
    swprintf_s(g_enhancerDir, MAX_PATH, L"%ls\\enhancer", g_modsDir);
    swprintf_s(g_logFilePath, MAX_PATH, L"%ls\\enhancer.log", g_enhancerDir);

    CreateDirectoryW(g_modsDir, nullptr);
    CreateDirectoryW(g_enhancerDir, nullptr);

    InitializeCriticalSection(&g_logCs);
    g_logCsInitialized = true;

    g_logFile = _wfsopen(g_logFilePath, L"w", _SH_DENYNO);

    SYSTEMTIME st = {};
    GetLocalTime(&st);

    char exePathUtf8[MAX_PATH * 3] = {};
    if (!WideCharToMultiByte(CP_UTF8, 0, exePath, -1, exePathUtf8, sizeof(exePathUtf8), nullptr, nullptr)) {
        strcpy_s(exePathUtf8, "acz.exe");
    }

    LoaderLogPrintf("[Loader] ================================================================");
    LoaderLogPrintf("[Loader]  ACZero Enhancer ASI Loader (version.dll %s), %04u-%02u-%02u %02u:%02u:%02u",
                    LOADER_VERSION_STR,
                    st.wYear, st.wMonth, st.wDay,
                    st.wHour, st.wMinute, st.wSecond);
    LoaderLogPrintf("[Loader]  Game: %s", exePathUtf8);
    LoaderLogPrintf("[Loader] ================================================================");

    if (!FindExeTextSection(g_exeModule, &g_textStart, &g_textEnd)) {
        LoaderLogPrintf("[Loader] ERROR: Could not locate .text section in acz.exe; ASI plugins will NOT be loaded.");
        return;
    }

    int hooksInstalled = 0;
    if (InstallSingleKernel32IatHook(
            g_exeModule,
            hKernel32,
            "GetSystemTimeAsFileTime",
            reinterpret_cast<uintptr_t>(&Hook_GetSystemTimeAsFileTime),
            &g_slotGetSystemTimeAsFileTime,
            reinterpret_cast<uintptr_t*>(&g_origGetSystemTimeAsFileTime)))
    {
        ++hooksInstalled;
    }

    if (InstallSingleKernel32IatHook(
            g_exeModule,
            hKernel32,
            "GetModuleFileNameA",
            reinterpret_cast<uintptr_t>(&Hook_GetModuleFileNameA),
            &g_slotGetModuleFileNameA,
            reinterpret_cast<uintptr_t*>(&g_origGetModuleFileNameA)))
    {
        ++hooksInstalled;
    }

    if (hooksInstalled == 0) {
        LoaderLogPrintf("[Loader] ERROR: Could not hook acz.exe startup IAT; ASI plugins will NOT be loaded.");
        return;
    }

    LoaderLogPrintf("[Loader] Waiting for game code to start (.text: 0x%llX..0x%llX)",
                    static_cast<unsigned long long>(g_textStart),
                    static_cast<unsigned long long>(g_textEnd));

    // Chain renamed original version.dll (e.g., version_orig.dll) if present next to acz.exe
    LoadChainedOriginalLoaderIfPresent();
}

// ----------------------------------------------------------------------------
// DLL Entry Point
// ----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_selfModule = hModule;
        DisableThreadLibraryCalls(hModule);
        InitializeVersionLoader();
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        if (g_logFile) {
            LoaderLogPrintf("[Loader] Shutting down.");
            fclose(g_logFile);
            g_logFile = nullptr;
        }
        if (g_logCsInitialized) {
            DeleteCriticalSection(&g_logCs);
            g_logCsInitialized = false;
        }
    }
    return TRUE;
}
