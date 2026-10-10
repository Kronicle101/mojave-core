#include "audio_focus.hpp"
#include "physics.hpp"
#include "audio_clock.hpp"
#include "audio_transport.hpp"
#include <atomic>
#include <cstring>
#include <unknwn.h>

namespace fnv::audio {
namespace {
constexpr uintptr_t PauseMusic = 0x430640;
constexpr uintptr_t ResumeMusic = 0x430660;
constexpr uintptr_t MusicLock = 0x430470;
constexpr uintptr_t MusicUnlock = 0x430490;
constexpr uintptr_t MusicPauseState = 0xDDD312;
constexpr uintptr_t MusicCriticalSection = 0xDDD3D8;
constexpr uintptr_t WorkerPauseCall = 0x43193B;
constexpr uintptr_t WorkerRunCall = 0x431960;
constexpr uintptr_t WorkerLoopRunCall = 0x431A66;
constexpr uintptr_t WorkerReleaseCall = 0x431CDB;
BYTE *image{};
std::atomic<bool> bound{false};
std::atomic<bool> background{false};
bool enabled = true;
bool pausedByUs = false;
using Transport = HRESULT(__stdcall *)(void *);
using StateQuery = HRESULT(__stdcall *)(void *, LONG, LONG *);
bool BackgroundRadio() {
    return background.load() && image && image[MusicPauseState + 1] != 0;
}
LONG GraphState(void *control) {
    LONG state = -1;
    if (control) {
        auto **table = *reinterpret_cast<void ***>(control);
        if (FAILED(reinterpret_cast<StateQuery>(table[10])(control, 0, &state)))
            state = -1;
    }
    return state;
}
HRESULT __stdcall WorkerPause(void *control) {
    const bool radio = BackgroundRadio();
    const LONG before = radio ? GraphState(control) : -1;
    const HRESULT result = background ? CheckpointPause(control, radio) : ApplyTransportPause(control, false);
    if (radio && before != 0)
        Log("radio native worker Stop hr=0x%08lX state=%ld thread=%lu", result, GraphState(control),
            GetCurrentThreadId());
    return result;
}
HRESULT __stdcall WorkerRun(void *control) {
    const bool radio = BackgroundRadio();
    const LONG before = !radio ? GraphState(control) : -1;
    const HRESULT result = CheckpointRun(control, background.load());
    if (!radio && before == 0)
        Log("music native worker Run hr=0x%08lX state=%ld thread=%lu", result, GraphState(control),
            GetCurrentThreadId());
    return result;
}
ULONG __stdcall WorkerRelease(void *control) {
    ForgetCheckpoint(control);
    return static_cast<IUnknown *>(control)->Release();
}
bool PatchWorker(BYTE *base) {
    const uintptr_t sites[] = {WorkerPauseCall, WorkerRunCall, WorkerLoopRunCall, WorkerReleaseCall};
    const void *targets[] = {reinterpret_cast<void *>(&WorkerPause), reinterpret_cast<void *>(&WorkerRun),
                             reinterpret_cast<void *>(&WorkerRun), reinterpret_cast<void *>(&WorkerRelease)};
    DWORD previous[4]{};
    size_t ready = 0;
    for (; ready < 4; ++ready) {
        if (!VirtualProtect(base + sites[ready], 5, PAGE_EXECUTE_READWRITE, &previous[ready]))
            break;
    }
    if (ready == 4) {
        for (size_t index = 0; index < 4; ++index) {
            BYTE patch[5] = {0xE8};
            const DWORD relative = static_cast<DWORD>(reinterpret_cast<uintptr_t>(targets[index]) -
                                                      reinterpret_cast<uintptr_t>(base + sites[index] + 5));
            memcpy(patch + 1, &relative, 4);
            memcpy(base + sites[index], patch, 5);
            FlushInstructionCache(GetCurrentProcess(), base + sites[index], 5);
        }
    }
    const bool success = ready == 4;
    while (ready) {
        --ready;
        DWORD ignored{};
        VirtualProtect(base + sites[ready], 5, previous[ready], &ignored);
    }
    return success;
}
bool MatchesCall(const BYTE *base, uintptr_t call, uintptr_t target) {
    int32_t relative{};
    if (base[call] != 0xE8)
        return false;
    memcpy(&relative, base + call + 1, sizeof(relative));
    return static_cast<int64_t>(call + 5) + relative == target;
}
bool PauseBody(const BYTE *base, uintptr_t function, BYTE value) {
    const BYTE entry[] = {0x55, 0x8B, 0xEC};
    DWORD state{};
    memcpy(&state, base + function + 10, sizeof(state));
    return !memcmp(base + function, entry, sizeof(entry)) && MatchesCall(base, function + 3, MusicLock) &&
           base[function + 8] == 0xC6 && base[function + 9] == 0x05 &&
           state == reinterpret_cast<uintptr_t>(base + MusicPauseState) && base[function + 14] == value &&
           MatchesCall(base, function + 15, MusicUnlock) && base[function + 20] == 0x5D &&
           base[function + 21] == 0xC3;
}
} // namespace
bool VerifyEngine(const BYTE *base, size_t length) {
    if (!base || length <= MusicCriticalSection + 32)
        return false;
    if (!PauseBody(base, PauseMusic, 1) || !PauseBody(base, ResumeMusic, 0))
        return false;
    // Whole instructions: existing stdcall argument is already pushed by FNV.
    const BYTE pauseSite[] = {0x52, 0x8B, 0x41, 0x20, 0xFF, 0xD0, 0xEB, 0x23};
    const BYTE runSite[] = {0x50, 0x8B, 0x4A, 0x1C, 0xFF, 0xD1};
    const BYTE loopSite[] = {0x52, 0x8B, 0x41, 0x1C, 0xFF, 0xD0};
    const BYTE releaseSite[] = {0x51, 0x8B, 0x50, 0x08, 0xFF, 0xD2};
    if (memcmp(base + WorkerPauseCall - 1, pauseSite, sizeof(pauseSite)) ||
        memcmp(base + WorkerRunCall - 1, runSite, sizeof(runSite)) ||
        memcmp(base + WorkerLoopRunCall - 1, loopSite, sizeof(loopSite)) ||
        memcmp(base + WorkerReleaseCall - 1, releaseSite, sizeof(releaseSite)))
        return false;
    DWORD lockObject{}, unlockObject{};
    memcpy(&lockObject, base + MusicLock + 6, 4);
    memcpy(&unlockObject, base + MusicUnlock + 4, 4);
    const BYTE lockPrefix[] = {0x55, 0x8B, 0xEC, 0x6A, 0x00, 0xB9};
    const BYTE unlockPrefix[] = {0x55, 0x8B, 0xEC, 0xB9};
    return !memcmp(base + MusicLock, lockPrefix, sizeof(lockPrefix)) &&
           !memcmp(base + MusicUnlock, unlockPrefix, sizeof(unlockPrefix)) &&
           lockObject == reinterpret_cast<uintptr_t>(base + MusicCriticalSection) &&
           unlockObject == lockObject && MatchesCall(base, MusicLock + 10, 0x538A0) &&
           MatchesCall(base, MusicUnlock + 8, 0x538C0) && base[MusicPauseState] <= 1 &&
           base[MusicPauseState + 1] <= 1;
}
void Initialize(const Config &config) {
    enabled = config.pauseEngineAudio;
}
bool BindEngine(HMODULE executable) {
    if (!enabled || !IsGameProcess())
        return false;
    if (bound)
        return true;
    auto *base = reinterpret_cast<BYTE *>(executable);
    const auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if ((!physics::Active() && !physics::VerifyImage(base, nt->OptionalHeader.SizeOfImage)) ||
        !VerifyEngine(base, nt->OptionalHeader.SizeOfImage) ||
        !clock::Verify(base, nt->OptionalHeader.SizeOfImage)) {
        Log("engine audio pause skipped: native music/lock signatures do not match");
        return false;
    }
    image = base;
    if (!clock::Bind(base, nt->OptionalHeader.SizeOfImage)) {
        image = nullptr;
        Log("engine audio skipped: clock hooks could not be patched");
        return false;
    }
    if (!PatchWorker(base)) {
        clock::Unbind();
        image = nullptr;
        Log("engine audio skipped: worker call sites could not be patched");
        return false;
    }
    bound = true;
    Log("engine audio bound: native clock pause/rebase + worker cursor checkpoints + radio Stop/Run; no session mute");
    return true;
}
void FocusChanged(bool active) {
    if (!enabled || !bound)
        return;
    background = !active;
    clock::FocusChanged(active);
    using Function = void(__cdecl *)();
    const bool before = pausedByUs;
    ApplyFocus(active, image + MusicPauseState, reinterpret_cast<Function>(image + PauseMusic),
               reinterpret_cast<Function>(image + ResumeMusic), pausedByUs);
    if (pausedByUs != before)
        Log("engine music %s via native routine pauseState=%u", active ? "resumed" : "paused",
            image[MusicPauseState]);
}
void ApplyFocus(bool active, BYTE *state, void(__cdecl *pause)(), void(__cdecl *resume)(), bool &owned) {
    if (!state || !pause || !resume)
        return;
    if (!active && !owned && !*state) {
        pause();
        owned = *state != 0;
    } else if (active && owned) {
        resume();
        owned = false;
    }
}
HRESULT ApplyTransportPause(void *control, bool stopStream) {
    if (!control)
        return E_POINTER;
    if (stopStream && GraphState(control) == 0)
        return S_OK;
    auto **table = *reinterpret_cast<void ***>(control);
    return reinterpret_cast<Transport>(table[stopStream ? 9 : 8])(control);
}
HRESULT ApplyTransportRun(void *control, bool backgroundRadio) {
    if (!control)
        return E_POINTER;
    if (backgroundRadio)
        return S_OK; // Also cover the native end-of-track loop path.
    auto **table = *reinterpret_cast<void ***>(control);
    return reinterpret_cast<Transport>(table[7])(control);
}
void Shutdown() {
    FocusChanged(true);
}
} // namespace fnv::audio
