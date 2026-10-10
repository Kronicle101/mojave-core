#include "runtime.hpp"
#include "io_cache.hpp"
#include "physics.hpp"
#include "device_hooks.hpp"
#include "audio_focus.hpp"
#include <psapi.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

namespace fnv {
namespace {
Config settings;
std::wstring logPath;
SRWLOCK logLock = SRWLOCK_INIT;
bool gameProcess = false;
bool graphicsHost = false;
constexpr wchar_t GameExecutable[] = L"FalloutNV.exe";
constexpr wchar_t ConfigFile[] = L"FNVNativeStabilizer.ini";
constexpr wchar_t LogFile[] = L"FNVNativeStabilizer.log";
constexpr char ProductVersion[] = "FNV Native Stabilizer 0.4.4";
DWORD Bounded(LPCWSTR section, LPCWSTR key, DWORD fallback, DWORD lo, DWORD hi, LPCWSTR file) {
    return std::clamp<DWORD>(GetPrivateProfileIntW(section, key, fallback, file), lo, hi);
}
DWORD WINAPI Monitor(void *) {
    for (;;) {
        Sleep(settings.telemetrySeconds * 1000);
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (!GlobalMemoryStatusEx(&memory))
            continue;
        if (UnderPressure(memory, settings))
            io::Trim();
        PROCESS_MEMORY_COUNTERS_EX process{};
        process.cb = sizeof(process);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&process),
                             sizeof(process));
        const auto stats = io::Snapshot();
        physics::Report();
        graphics::Report();
        Log("memory privateMiB=%llu workingMiB=%llu availableMiB=%llu virtualFreeMiB=%llu load=%lu cacheKiB=%lu handles=%lu hits=%llu misses=%llu diskBytes=%llu servedBytes=%llu evictions=%llu",
            static_cast<uint64_t>(process.PrivateUsage) >> 20,
            static_cast<uint64_t>(process.WorkingSetSize) >> 20, memory.ullAvailPhys >> 20,
            memory.ullAvailVirtual >> 20, memory.dwMemoryLoad, stats.allocatedBytes >> 10,
            stats.trackedHandles, stats.hits, stats.misses, stats.diskBytes, stats.servedBytes,
            stats.evictions);
    }
}
} // namespace
const Config &Settings() {
    return settings;
}
bool IsGameProcess() {
    return gameProcess;
}
bool IsGraphicsHost() {
    return graphicsHost;
}
bool UnderPressure(const MEMORYSTATUSEX &m, const Config &c) {
    return m.ullAvailPhys < (uint64_t(c.minAvailableMiB) << 20) || m.dwMemoryLoad >= c.maxMemoryLoad ||
           m.ullAvailVirtual < (uint64_t(c.minVirtualMiB) << 20);
}
void Log(const char *format, ...) {
    if (!settings.log || logPath.empty())
        return;
    const DWORD error = GetLastError();
    char line[2048];
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const int prefix = sprintf_s(line, "%04u-%02u-%02u %02u:%02u:%02u ", now.wYear, now.wMonth, now.wDay,
                                 now.wHour, now.wMinute, now.wSecond);
    va_list args;
    va_start(args, format);
    vsnprintf_s(line + prefix, sizeof(line) - prefix, _TRUNCATE, format, args);
    va_end(args);
    strcat_s(line, "\r\n");
    AcquireSRWLockExclusive(&logLock);
    HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&logLock);
    SetLastError(error);
}
void Initialize(HMODULE self) {
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path)));
    std::wstring directory(path);
    directory.resize(directory.find_last_of(L"\\/") + 1);
    const auto ini = directory + ConfigFile;
    logPath = directory + LogFile;
    auto flag = [&](LPCWSTR section, LPCWSTR key, bool fallback) {
        return GetPrivateProfileIntW(section, key, fallback ? 1 : 0, ini.c_str()) != 0;
    };
    settings.dx12 = flag(L"Graphics", L"EnableDX12", false);
    settings.borderless = flag(L"Graphics", L"Borderless", false);
    settings.respectVSync = flag(L"Graphics", L"RespectVSync", true);
    settings.recoverExclusiveFocus = flag(L"Graphics", L"RecoverExclusiveFocus", true);
    settings.pauseEngineAudio = flag(L"Audio", L"PauseEngineOnBackground", true);
    settings.processLFH = flag(L"Memory", L"EnableProcessLFH", true);
    settings.cache = flag(L"IO", L"EnableCache", true);
    settings.log = flag(L"Diagnostics", L"Log", true);
    settings.highFPS = flag(L"Physics", L"EnableHighFPS", true);
    settings.cacheMiB = Bounded(L"IO", L"CacheMiB", 8, 1, 64, ini.c_str());
    settings.readAheadKiB = Bounded(L"IO", L"ReadAheadKiB", 0, 0, 256, ini.c_str());
    settings.minAvailableMiB = Bounded(L"IO", L"MinAvailableMiB", 1024, 256, 8192, ini.c_str());
    settings.maxMemoryLoad = Bounded(L"IO", L"MaxMemoryLoadPercent", 85, 50, 99, ini.c_str());
    settings.minVirtualMiB = Bounded(L"IO", L"MinVirtualMiB", 256, 64, 1024, ini.c_str());
    settings.telemetrySeconds = Bounded(L"Diagnostics", L"TelemetrySeconds", 10, 2, 300, ini.c_str());
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    const wchar_t *name = wcsrchr(path, L'\\');
    gameProcess = _wcsicmp(name ? name + 1 : path, GameExecutable) == 0;
    graphicsHost = gameProcess || _wcsicmp(name ? name + 1 : path, L"graphics_smoke.exe") == 0;
    Log("%s x86; game=%d DX12=%d borderless=%d cacheMiB=%lu readAheadKiB=%lu", ProductVersion, gameProcess,
        settings.dx12, settings.borderless, settings.cacheMiB, settings.readAheadKiB);
    Log("presentation policy=%s respectVSync=%d exclusiveRecovery=%d",
        settings.borderless ? "borderless" : "launcher native mode", settings.respectVSync,
        settings.recoverExclusiveFocus);
    if (!gameProcess)
        return;
    // This DLL remains alive while import pointers and its monitor are active.
    HMODULE pinned{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&Initialize), &pinned);
    if (settings.processLFH) {
        ULONG mode = 2;
        Log("process LFH result=%d (engine custom allocators unaffected)",
            HeapSetInformation(GetProcessHeap(), HeapCompatibilityInformation, &mode, sizeof(mode)));
    }
    io::Configure(settings);
    if (settings.cache)
        Log("BSA import cache installed=%d", io::InstallImports(GetModuleHandleW(nullptr)));
    audio::Initialize(settings);
    HANDLE thread = CreateThread(nullptr, 0, Monitor, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    else
        Log("monitor unavailable error=%lu", GetLastError());
}
void ApplyBorderless(HWND window, UINT &width, UINT &height) {
    if (!settings.borderless || !window)
        return;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    const UINT screenWidth = static_cast<UINT>(monitor.rcMonitor.right - monitor.rcMonitor.left);
    const UINT screenHeight = static_cast<UINT>(monitor.rcMonitor.bottom - monitor.rcMonitor.top);
    if (!width)
        width = screenWidth;
    if (!height)
        height = screenHeight;
    SetWindowLongW(window, GWL_STYLE,
                   (GetWindowLongW(window, GWL_STYLE) & ~(WS_OVERLAPPEDWINDOW | WS_CHILD)) | WS_POPUP);
    SetWindowLongW(window, GWL_EXSTYLE,
                   GetWindowLongW(window, GWL_EXSTYLE) &
                       ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_TOPMOST));
    SetWindowPos(window, HWND_NOTOPMOST,
                 monitor.rcMonitor.left + (static_cast<int>(screenWidth) - static_cast<int>(width)) / 2,
                 monitor.rcMonitor.top + (static_cast<int>(screenHeight) - static_cast<int>(height)) / 2,
                 static_cast<int>(width), static_cast<int>(height), SWP_FRAMECHANGED | SWP_NOACTIVATE);
    Log("borderless applied %ux%u", width, height);
}
} // namespace fnv
