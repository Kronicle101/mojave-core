#include "audio_clock.hpp"
#include <atomic>
#include <cstring>
namespace fnv::audio::clock {
namespace {
constexpr uintptr_t Root = 0xDF6D98, RootTable = 0xCA39D8, Pause = 0x6D8700, Resume = 0x6D8740,
                    Update = 0x6EA100, TickImport = 0xBDF060;
constexpr uintptr_t UpdateSites[] = {0x6DDC17};
constexpr uintptr_t ResumeSites[] = {0x450E5D, 0x46B3FF};
BYTE *gameImage{};
SRWLOCK clockLock = SRWLOCK_INIT;
std::atomic<bool> background{false};
bool ownsPause{};
void *pausedRoot{};
using Native = void(__thiscall *)(void *);
struct Patch {
    BYTE *site;
    BYTE original[5];
};
Patch patches[4]{};
size_t patched{};
Counter *Time(void *root) {
    return reinterpret_cast<Counter *>(static_cast<BYTE *>(root) + 0x14);
}
void *GetRoot() {
    void *root = *reinterpret_cast<void **>(gameImage + Root);
    if (!root || *static_cast<void **>(root) != gameImage + RootTable)
        return nullptr;
    return root;
}
bool Call(const BYTE *base, uintptr_t site, uintptr_t target) {
    int32_t displacement{};
    memcpy(&displacement, base + site + 1, 4);
    return base[site] == 0xE8 && static_cast<int64_t>(site + 5) + displacement == target;
}
bool Absolute(const BYTE *base, uintptr_t offset, uintptr_t target) {
    DWORD address{};
    memcpy(&address, base + offset, 4);
    return address == reinterpret_cast<uintptr_t>(base + target);
}
void __fastcall CounterUpdate(Counter *counter, void *) {
    AcquireSRWLockExclusive(&clockLock);
    if (background) {
        // Exactly the side effect of the verified native update while paused.
        // Leave elapsed and pause flags untouched; refresh the native epoch.
        const auto tick =
            reinterpret_cast<DWORD(WINAPI *)()>(*reinterpret_cast<void **>(gameImage + TickImport));
        counter->lastTick = tick();
    } else
        reinterpret_cast<void(__thiscall *)(Counter *)>(gameImage + Update)(counter);
    ReleaseSRWLockExclusive(&clockLock);
}
void __fastcall RootResume(void *root, void *) {
    AcquireSRWLockExclusive(&clockLock);
    const bool resume = !background.load();
    if (resume) {
        Counter *counter = Time(root);
        if (counter->flags & 1)
            Rebase(counter, reinterpret_cast<void(__thiscall *)(Counter *)>(gameImage + Update));
    }
    ReleaseSRWLockExclusive(&clockLock);
    // The engine callback can itself process audio updates. Do not invoke it
    // while holding our nonrecursive counter lock.
    if (resume)
        reinterpret_cast<Native>(gameImage + Resume)(root);
}
} // namespace
void Rebase(Counter *counter, void(__thiscall *update)(Counter *)) {
    if (!counter || !update)
        return;
    const DWORD flags = counter->flags;
    counter->flags |= 1;
    update(counter);
    counter->flags = flags;
}
bool Verify(const BYTE *base, size_t length) {
    if (!base || length <= Root + 4)
        return false;
    const BYTE pause[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xF8, 0x8B, 0x45, 0xF8, 0x83, 0x78,
                          0x30, 0,    0x74, 0x08, 0x8B, 0x4D, 0xF8, 0x8B, 0x51, 0x30, 0xFF, 0xD2, 0x8B, 0x45,
                          0xF8, 0x83, 0xC0, 0x14, 0x89, 0x45, 0xFC, 0x8B, 0x4D, 0xFC, 0x8B, 0x51, 0x08, 0x83,
                          0xCA, 0x01, 0x8B, 0x45, 0xFC, 0x89, 0x50, 0x08, 0x8B, 0xE5, 0x5D, 0xC3};
    BYTE resume[sizeof(pause)]{};
    memcpy(resume, pause, sizeof(pause));
    resume[14] = 0x34;
    resume[23] = 0x34;
    resume[42] = 0xE2;
    resume[43] = 0xFE;
    const BYTE update[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF4, 0xFF, 0x15, 0,    0,
                           0,    0,    0x89, 0x45, 0xFC, 0x8B, 0x45, 0xF4, 0x8B, 0x4D, 0xFC, 0x2B, 0x48,
                           0x04, 0x89, 0x4D, 0xF8, 0x8B, 0x55, 0xF4, 0x8B, 0x42, 0x08, 0x83, 0xE0, 0x01,
                           0xF7, 0xD8, 0x1B, 0xC0, 0xF7, 0xD8, 0x0F, 0xB6, 0xC8, 0x85, 0xC9, 0x75, 0x0D,
                           0x8B, 0x55, 0xF4, 0x8B, 0x02, 0x03, 0x45, 0xF8, 0x8B, 0x4D, 0xF4, 0x89, 0x01,
                           0x8B, 0x55, 0xF4, 0x8B, 0x45, 0xFC, 0x89, 0x42, 0x04, 0x8B, 0xE5, 0x5D, 0xC3};
    constexpr size_t operand = 11;
    if (memcmp(base + Pause, pause, sizeof(pause)) || memcmp(base + Resume, resume, sizeof(resume)) ||
        memcmp(base + Update, update, operand) || !Absolute(base, Update + operand, TickImport) ||
        memcmp(base + Update + operand + 4, update + operand + 4, sizeof(update) - operand - 4))
        return false;
    for (uintptr_t site : UpdateSites)
        if (!Call(base, site, Update))
            return false;
    for (uintptr_t site : ResumeSites)
        if (!Call(base, site, Resume))
            return false;
    return true;
}
bool Bind(BYTE *base, size_t length) {
    if (gameImage)
        return true;
    if (!Verify(base, length))
        return false;
    uintptr_t sites[4]{};
    const void *targets[4]{};
    size_t count{};
    for (uintptr_t site : UpdateSites) {
        sites[count] = site;
        targets[count++] = reinterpret_cast<void *>(&CounterUpdate);
    }
    for (uintptr_t site : ResumeSites) {
        sites[count] = site;
        targets[count++] = reinterpret_cast<void *>(&RootResume);
    }
    DWORD protections[4]{};
    size_t ready{};
    for (; ready < count; ++ready)
        if (!VirtualProtect(base + sites[ready], 5, PAGE_EXECUTE_READWRITE, &protections[ready]))
            break;
    if (ready == count) {
        gameImage = base;
        for (size_t i = 0; i < count; ++i) {
            patches[i].site = base + sites[i];
            memcpy(patches[i].original, patches[i].site, 5);
            BYTE call[5] = {0xE8};
            const DWORD relative = static_cast<DWORD>(reinterpret_cast<uintptr_t>(targets[i]) -
                                                      reinterpret_cast<uintptr_t>(patches[i].site + 5));
            memcpy(call + 1, &relative, 4);
            memcpy(patches[i].site, call, 5);
            FlushInstructionCache(GetCurrentProcess(), patches[i].site, 5);
        }
        patched = count;
    }
    const bool result = ready == count;
    while (ready) {
        --ready;
        DWORD ignored{};
        VirtualProtect(base + sites[ready], 5, protections[ready], &ignored);
    }
    return result;
}
void Unbind() {
    for (size_t i = 0; i < patched; ++i) {
        DWORD protection{};
        if (VirtualProtect(patches[i].site, 5, PAGE_EXECUTE_READWRITE, &protection)) {
            memcpy(patches[i].site, patches[i].original, 5);
            FlushInstructionCache(GetCurrentProcess(), patches[i].site, 5);
            DWORD ignored{};
            VirtualProtect(patches[i].site, 5, protection, &ignored);
        }
    }
    patched = 0;
    gameImage = nullptr;
}
void FocusChanged(bool active) {
    if (!gameImage)
        return;
    AcquireSRWLockExclusive(&clockLock);
    void *root = GetRoot();
    bool pauseNative{}, resumeNative{};
    if (!active && !background) {
        if (root) {
            Counter *counter = Time(root);
            reinterpret_cast<void(__thiscall *)(Counter *)>(gameImage + Update)(counter);
            ownsPause = (counter->flags & 1) == 0;
            pausedRoot = root;
            pauseNative = ownsPause;
            Log("audio engine clock paused elapsed=%lu owned=%d", counter->elapsed, ownsPause);
        }
        background = true;
    } else if (active && background) {
        if (root && root == pausedRoot) {
            Counter *counter = Time(root);
            Rebase(counter, reinterpret_cast<void(__thiscall *)(Counter *)>(gameImage + Update));
            resumeNative = ownsPause;
            Log("audio engine clock rebased elapsed=%lu owned=%d", counter->elapsed, ownsPause);
        }
        background = false;
        ownsPause = false;
        pausedRoot = nullptr;
    }
    ReleaseSRWLockExclusive(&clockLock);
    if (pauseNative)
        reinterpret_cast<Native>(gameImage + Pause)(root);
    if (resumeNative)
        reinterpret_cast<Native>(gameImage + Resume)(root);
}
} // namespace fnv::audio::clock
