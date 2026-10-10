#pragma once
#include <windows.h>
#include <cstdint>

namespace fnv {
struct Config {
    bool dx12 = false;
    bool borderless = false;
    bool respectVSync = true;
    bool recoverExclusiveFocus = true;
    bool pauseEngineAudio = true;
    bool processLFH = true;
    bool cache = true;
    bool log = true;
    bool highFPS = true;
    bool reservedPlatformFlag = false;
    DWORD cacheMiB = 8;
    DWORD readAheadKiB = 0;
    DWORD minAvailableMiB = 1024;
    DWORD maxMemoryLoad = 85;
    DWORD minVirtualMiB = 256;
    DWORD telemetrySeconds = 10;
};
const Config &Settings();
void Initialize(HMODULE self);
void Log(const char *format, ...);
bool IsGameProcess();
bool IsGraphicsHost();
bool UnderPressure(const MEMORYSTATUSEX &status, const Config &config);
void ApplyBorderless(HWND window, UINT &width, UINT &height);
} // namespace fnv
