#include "window_state.hpp"
#include "physics.hpp"
#include "audio_focus.hpp"
#include <atomic>
#include <cstring>

namespace fnv::window {
namespace {
// Structure/locations documented by xNVSE GameSettings; no NVSE code is linked.
struct Setting {
    void *table;
    DWORD value;
    const char *name;
};
struct Node {
    Setting *setting;
    Node *next;
};
static_assert(sizeof(Setting) == 12 && sizeof(Node) == 8);
std::atomic<DWORD *> fullscreen{}, presentInterval{};
std::atomic<HWND> attachedWindow{};
std::atomic<WNDPROC> originalProcedure{};
LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Enforce();
    if (message == WM_ACTIVATEAPP)
        audio::FocusChanged(wparam != 0);
    if (message == WM_CLOSE || message == WM_DESTROY || (message == WM_ENDSESSION && wparam))
        audio::Shutdown();
    if (message == WM_ACTIVATEAPP || message == WM_ACTIVATE) {
        const bool active = message == WM_ACTIVATEAPP ? wparam != 0 : LOWORD(wparam) != WA_INACTIVE;
        Log("window focus message=0x%04X active=%d visible=%d iconic=%d", message, active,
            IsWindowVisible(window), IsIconic(window));
        // WM_ACTIVATE may transiently report active while the application is
        // losing focus. Restoring here can pull an exclusive window back over
        // the chosen application. Let Windows/the engine restore it on return.
    }
    const WNDPROC original = originalProcedure.load();
    const LRESULT result = original ? CallWindowProcW(original, window, message, wparam, lparam)
                                    : DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_ACTIVATEAPP && !wparam) {
        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        Log("window background: topmost removed without restoring or activating");
    }
    if (message == WM_NCDESTROY) {
        attachedWindow = nullptr;
        originalProcedure = nullptr;
    }
    return result;
}
} // namespace
DWORD *FindVerifiedFullscreen(BYTE *base, size_t size) {
    constexpr uintptr_t getter = 0x46E10, object = 0xDC77B4, valueGetter = 0x8D60;
    if (!base || size <= object + sizeof(Setting))
        return nullptr;
    __try {
        const BYTE prefix[] = {0x55, 0x8B, 0xEC, 0xB9};
        const BYTE tail[] = {0x8A, 0x00, 0x5D, 0xC3};
        const BYTE valueCode[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF8, 0x83, 0x7D,
                                  0xF8, 0x00, 0x74, 0x0B, 0x8B, 0x45, 0xF8, 0x83, 0xC0, 0x04};
        if (memcmp(base + getter, prefix, sizeof(prefix)) || base[getter + 8] != 0xE8 ||
            memcmp(base + getter + 13, tail, sizeof(tail)) ||
            memcmp(base + valueGetter, valueCode, sizeof(valueCode)))
            return nullptr;
        DWORD targetObject{};
        int32_t relative{};
        memcpy(&targetObject, base + getter + 4, 4);
        memcpy(&relative, base + getter + 9, 4);
        if (targetObject != reinterpret_cast<uintptr_t>(base + object) ||
            static_cast<int64_t>(getter + 13) + relative != valueGetter)
            return nullptr;
        auto *setting = reinterpret_cast<Setting *>(base + object);
        const uintptr_t name = reinterpret_cast<uintptr_t>(setting->name);
        if (name < reinterpret_cast<uintptr_t>(base) ||
            name >= reinterpret_cast<uintptr_t>(base) + size - 24 ||
            _stricmp(setting->name, "bFull Screen:Display") || setting->value > 1)
            return nullptr;
        return &setting->value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
DWORD *FindNumericSetting(void *collection, const char *expected) {
    if (!collection || !expected)
        return nullptr;
    __try {
        auto *node = reinterpret_cast<Node *>(static_cast<BYTE *>(collection) + 0x10C);
        for (size_t count = 0; node && count < 20000; ++count, node = node->next) {
            if (node->setting && node->setting->name && _stricmp(node->setting->name, expected) == 0)
                return &node->setting->value;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}
bool Bind(HMODULE executable) {
    if (!IsGameProcess())
        return false;
    if (fullscreen) {
        Enforce();
        return true;
    }
    auto *base = reinterpret_cast<BYTE *>(executable);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (!physics::Active() && !physics::VerifyImage(base, nt->OptionalHeader.SizeOfImage))
        return false;
    __try {
        const uintptr_t collections[] = {0xDF35A0, 0xDF96A0};
        for (const auto offset : collections) {
            void *collection = *reinterpret_cast<void **>(base + offset);
            if (!fullscreen)
                fullscreen = FindNumericSetting(collection, "bFull Screen:Display");
            if (!presentInterval)
                presentInterval = FindNumericSetting(collection, "iPresentInterval:Display");
        }
        if (!fullscreen) {
            fullscreen = FindVerifiedFullscreen(base, nt->OptionalHeader.SizeOfImage);
            if (fullscreen)
                Log("engine fullscreen bound through verified native getter (outside INI list)");
        }
        auto *value = fullscreen.load();
        if (value && *value > 1) {
            fullscreen = nullptr;
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fullscreen = nullptr;
        presentInterval = nullptr;
        return false;
    }
    Log("engine window settings bound fullscreen=%d interval=%d", fullscreen.load() != nullptr,
        presentInterval.load() != nullptr);
    Enforce();
    return fullscreen.load() != nullptr;
}
void Enforce() {
    __try {
        if (auto *value = fullscreen.load(); value && Settings().borderless) {
            const LONG old = InterlockedExchange(reinterpret_cast<volatile LONG *>(value), 0);
            if (old)
                Log("engine exclusive fullscreen disabled in memory; borderless presentation retained");
        }
        if (physics::Active() && !Settings().respectVSync)
            if (auto *value = presentInterval.load()) {
                const LONG old = InterlockedExchange(reinterpret_cast<volatile LONG *>(value), 0);
                if (old)
                    Log("engine presentation interval synchronized with verified uncapped timer");
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fullscreen = nullptr;
        presentInterval = nullptr;
    }
}
void HookWindow(HWND window) {
    if (!IsGameProcess() || (!Settings().borderless && !Settings().recoverExclusiveFocus) || !window ||
        attachedWindow)
        return;
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId())
        return;
    const WNDPROC previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (!previous || previous == &WindowProcedure)
        return;
    originalProcedure = previous;
    SetLastError(0);
    const LONG_PTR result =
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProcedure));
    if (!result && GetLastError()) {
        originalProcedure = nullptr;
        return;
    }
    attachedWindow = window;
    Log("native focus procedure installed before device creation");
}
} // namespace fnv::window
