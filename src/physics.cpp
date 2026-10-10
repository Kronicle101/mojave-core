// Source cleanup: 2026-10-10. Runtime logic unchanged.
// SPDX-License-Identifier: LGPL-2.1-only
// Timing/Havok integration adapted from NVTF TickFix by Carxt, lStewieAl and
// jazzisparis. See NOTICE.md and LICENSE. No NVSE code or runtime is linked.
#include "physics.hpp"
#include "simulation_clock.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace fnv::physics {
namespace {
constexpr uintptr_t TimerCall = 0x46F296;
constexpr uintptr_t OriginalGlobalUpdate = 0x371520;
constexpr uintptr_t TimerUpdate = 0x6A4EE0;
constexpr uintptr_t GlobalTimer = 0xDF6394;
constexpr uintptr_t HavokMaxTime = 0xE67B38;
constexpr uintptr_t SaveLoader = 0xDDDF38;
constexpr uintptr_t NewGameLoading = 0xDD8907;
constexpr uintptr_t PauseFade = 0xDDEA2D;
constexpr uintptr_t TickImport = 0xBDF060;
constexpr uintptr_t SchedulerCall = 0x46F2D8, OriginalScheduler = 0x866760;
constexpr uintptr_t PhysicsDelta = 0xDAFE64, PhysicsInput = 0xDAFE68, PhysicsStep = 0xE677AC;
constexpr uintptr_t PhysicsRemainder = 0xE677B0, PhysicsSteps = 0xE677B4, PhysicsMode = 0xE677B8,
                    TimeMultiplier = 0xDAC3A0;
BYTE *gameImage{};
LARGE_INTEGER counterFrequency{}, lastFrameCounter{}, tickOrigin{};
DWORD tickBase{};
float originalPhysicsStep = 0.016f;
std::atomic<bool> installed{false};
std::atomic<uint64_t> calls{}, loadingCalls{}, zeroFrames{};
std::atomic<DWORD> lastDeltaUS{};
std::atomic<float> lastHavok{};
SimulationClock simulationClock;
std::atomic<uint64_t> simulationTicks{}, idlePhysicsFrames{};
std::atomic<float> simulationRemainder{};
SRWLOCK installLock = SRWLOCK_INIT;
using UpdateFunction = void(__thiscall *)(Timer *, DWORD);
bool Loading();
void __cdecl SchedulePhysics(float seconds, bool special, bool force) {
    const float multiplier = *reinterpret_cast<float *>(gameImage + TimeMultiplier);
    const auto native = reinterpret_cast<void(__cdecl *)(float, bool, bool)>(gameImage + OriginalScheduler);
    *reinterpret_cast<float *>(gameImage + HavokMaxTime) = originalPhysicsStep;
    if (Loading() || *reinterpret_cast<DWORD *>(gameImage + PhysicsMode) != 0 || !std::isfinite(multiplier) ||
        multiplier <= 0 || !std::isfinite(seconds) || seconds <= 0) {
        simulationClock.Reset();
        simulationRemainder = 0;
        native(seconds, special, force);
        return;
    }
    const float quantum = originalPhysicsStep * multiplier;
    const auto step = simulationClock.Consume(seconds, quantum, special ? 1u : 3u);
    native(step.seconds, special, force);
    // The accumulator above owns the remainder. Publish one consistent native
    // physics interval before actors, drive-to-pose and the world update run.
    // In an idle render frame all physics consumers must see zero, preventing
    // controllers from applying another correction without a solver step.
    PublishSimulationStep({reinterpret_cast<float *>(gameImage + PhysicsDelta),
                           reinterpret_cast<float *>(gameImage + PhysicsInput),
                           reinterpret_cast<float *>(gameImage + PhysicsStep),
                           reinterpret_cast<float *>(gameImage + PhysicsRemainder),
                           reinterpret_cast<uint32_t *>(gameImage + PhysicsSteps)},
                          step, quantum);
    simulationTicks += step.count;
    if (!step.count)
        ++idlePhysicsFrames;
    simulationRemainder = static_cast<float>(simulationClock.Remainder());
}

DWORD WINAPI FineTick() {
    // Loading runs outside the normal TimeGlobal call. Restore Havok's original
    // maximum here too, rather than leaving a very small menu-frame value active.
    if (gameImage && Loading())
        *reinterpret_cast<float *>(gameImage + HavokMaxTime) = originalPhysicsStep;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const uint64_t elapsed = static_cast<uint64_t>(now.QuadPart - tickOrigin.QuadPart);
    const uint64_t hz = static_cast<uint64_t>(counterFrequency.QuadPart);
    // Split quotient/remainder to avoid overflow during long game sessions.
    return tickBase + static_cast<DWORD>((elapsed / hz) * 1000 + ((elapsed % hz) * 1000) / hz);
}
bool Loading() {
    __try {
        if (*reinterpret_cast<volatile bool *>(gameImage + NewGameLoading))
            return true;
        const auto *loader = *reinterpret_cast<BYTE **>(gameImage + SaveLoader);
        return loader && ((*reinterpret_cast<const DWORD *>(loader + 0x244) & 2) != 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}
void __fastcall UpdateGlobal(Timer *timer, void *) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double elapsed = lastFrameCounter.QuadPart ? double(now.QuadPart - lastFrameCounter.QuadPart) /
                                                           double(counterFrequency.QuadPart)
                                                     : 0.0;
    lastFrameCounter = now;
    const bool loading = Loading();
    const bool paused = *reinterpret_cast<const bool *>(gameImage + PauseFade) || timer->disabled != 0;
    if (elapsed <= 0 && !loading && !paused)
        ++zeroFrames;
    const float havok = AdvanceTimer(timer, FineTick(), elapsed, originalPhysicsStep, loading, paused,
                                     reinterpret_cast<UpdateFunction>(gameImage + TimerUpdate));
    *reinterpret_cast<float *>(gameImage + HavokMaxTime) = originalPhysicsStep;
    ++calls;
    if (loading)
        ++loadingCalls;
    lastDeltaUS = static_cast<DWORD>(std::clamp(elapsed * 1000000.0, 0.0, 4294967295.0));
    lastHavok = originalPhysicsStep;
    (void)havok; // AdvanceTimer preserves the render timer contract independently.
}
} // namespace
float AdvanceTimer(Timer *timer, DWORD tick, double elapsed, float originalHavok, bool loading, bool paused,
                   NativeTimerUpdate update) {
    const auto step = ComputeStep(elapsed, originalHavok, loading, paused);
    timer->clampMS = step.clampMS;
    if (elapsed <= 0 && !loading && !paused) {
        timer->deltaSeconds = 0;
        timer->realDeltaSeconds = 0;
        timer->sameFrame = true;
    } else {
        // Stock BSTimer only refreshes this flag in its integer-tick path.
        // Our positive fractional clamp must clear a previous zero-frame flag.
        if (!loading && !paused && step.clampMS > 0)
            timer->sameFrame = false;
        update(timer, tick);
    }
    // Havok applies the engine time multiplier itself at C6AF3A. Match its
    // unscaled input to the float actually produced by stock BSTimer.
    const float nativeDelta = timer->realDeltaSeconds;
    return loading || paused || elapsed <= 0 || !std::isfinite(nativeDelta) || nativeDelta <= 0
               ? originalHavok
               : std::min(nativeDelta, originalHavok);
}
bool VerifyImage(const BYTE *base, size_t length) {
    if (!base || length <= HavokMaxTime + sizeof(float))
        return false;
    const BYTE prologue[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34, 0x89, 0x4D, 0xEC, 0x8B, 0x45, 0xEC};
    if (memcmp(base + TimerUpdate, prologue, sizeof(prologue)))
        return false;
    // Verify both the caller's this pointer and its original callee, including
    // relocation. An existing patch or an encrypted/other build is rejected.
    if (base[TimerCall - 5] != 0xB9 || base[TimerCall] != 0xE8)
        return false;
    DWORD thisPointer{};
    int32_t displacement{};
    memcpy(&thisPointer, base + TimerCall - 4, sizeof(thisPointer));
    memcpy(&displacement, base + TimerCall + 1, sizeof(displacement));
    if (thisPointer != reinterpret_cast<uintptr_t>(base + GlobalTimer))
        return false;
    if (static_cast<int64_t>(TimerCall + 5) + displacement != OriginalGlobalUpdate)
        return false;
    float maxTime{};
    memcpy(&maxTime, base + HavokMaxTime, sizeof(maxTime));
    return std::isfinite(maxTime) && maxTime >= 0.001f && maxTime <= 0.1f;
}
bool VerifyScheduler(const BYTE *base, size_t length) {
    if (!base || length <= HavokMaxTime + 4)
        return false;
    const BYTE prefix[] = {0xD9, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x08, 0x80, 0x7C, 0x24, 0x14, 0};
    if (memcmp(base + OriginalScheduler, prefix, sizeof(prefix)) || base[OriginalScheduler + 12] != 0xD9 ||
        base[OriginalScheduler + 13] != 0x15)
        return false;
    DWORD input{}, delta{};
    memcpy(&input, base + OriginalScheduler + 14, 4);
    memcpy(&delta, base + OriginalScheduler + 0x2D, 4);
    int32_t relative{};
    memcpy(&relative, base + SchedulerCall + 1, 4);
    const BYTE pushFloat[] = {0x51, 0xD9, 0x1C, 0x24};
    return input == reinterpret_cast<uintptr_t>(base + PhysicsInput) &&
           delta == reinterpret_cast<uintptr_t>(base + PhysicsDelta) &&
           base[OriginalScheduler + 0x2B] == 0xD9 && base[OriginalScheduler + 0x2C] == 0x1D &&
           !memcmp(base + SchedulerCall - 4, pushFloat, 4) && base[SchedulerCall] == 0xE8 &&
           int64_t(SchedulerCall + 5) + relative == OriginalScheduler;
}
bool Install(HMODULE executable) {
    if (!Settings().highFPS)
        return false;
    AcquireSRWLockExclusive(&installLock);
    if (installed) {
        ReleaseSRWLockExclusive(&installLock);
        return true;
    }
    BYTE *base = reinterpret_cast<BYTE *>(executable);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        ReleaseSRWLockExclusive(&installLock);
        return false;
    }
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        !VerifyImage(base, nt->OptionalHeader.SizeOfImage) ||
        !VerifyScheduler(base, nt->OptionalHeader.SizeOfImage)) {
        Log("physics hook skipped: FNV timer signatures/data do not match 1.4.0.525");
        ReleaseSRWLockExclusive(&installLock);
        return false;
    }
    auto **tickSlot = reinterpret_cast<void **>(base + TickImport);
    void *oldTick =
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount"));
    if (*tickSlot != oldTick) {
        Log("physics hook skipped: GetTickCount already intercepted");
        ReleaseSRWLockExclusive(&installLock);
        return false;
    }
    DWORD codeProtection{}, schedulerProtection{}, tickProtection{};
    if (!VirtualProtect(base + TimerCall, 5, PAGE_EXECUTE_READWRITE, &codeProtection)) {
        ReleaseSRWLockExclusive(&installLock);
        return false;
    }
    if (!VirtualProtect(base + SchedulerCall, 5, PAGE_EXECUTE_READWRITE, &schedulerProtection)) {
        DWORD ignored{};
        VirtualProtect(base + TimerCall, 5, codeProtection, &ignored);
        ReleaseSRWLockExclusive(&installLock);
        return false;
    }
    if (!VirtualProtect(tickSlot, sizeof(void *), PAGE_READWRITE, &tickProtection)) {
        DWORD ignored{};
        VirtualProtect(base + TimerCall, 5, codeProtection, &ignored);
        VirtualProtect(base + SchedulerCall, 5, schedulerProtection, &ignored);
        ReleaseSRWLockExclusive(&installLock);
        return false;
    }
    gameImage = base;
    originalPhysicsStep = *reinterpret_cast<float *>(base + HavokMaxTime);
    QueryPerformanceFrequency(&counterFrequency);
    QueryPerformanceCounter(&tickOrigin);
    lastFrameCounter = {};
    tickBase = GetTickCount();
    const bool tickSet =
        InterlockedCompareExchangePointer(tickSlot, reinterpret_cast<void *>(&FineTick), oldTick) == oldTick;
    if (tickSet) {
        const int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&UpdateGlobal) -
                                                      reinterpret_cast<uintptr_t>(base + TimerCall + 5));
        memcpy(base + TimerCall + 1, &relative, sizeof(relative));
        FlushInstructionCache(GetCurrentProcess(), base + TimerCall, 5);
        const int32_t schedulerRelative =
            static_cast<int32_t>(reinterpret_cast<uintptr_t>(&SchedulePhysics) -
                                 reinterpret_cast<uintptr_t>(base + SchedulerCall + 5));
        memcpy(base + SchedulerCall + 1, &schedulerRelative, 4);
        FlushInstructionCache(GetCurrentProcess(), base + SchedulerCall, 5);
        installed = true;
    }
    DWORD ignored{};
    VirtualProtect(tickSlot, sizeof(void *), tickProtection, &ignored);
    VirtualProtect(base + TimerCall, 5, codeProtection, &ignored);
    VirtualProtect(base + SchedulerCall, 5, schedulerProtection, &ignored);
    Log("physics hook installed=%d; fractional render timer; native fixed physics quantum=%.9f with accumulator; no render FPS limiter",
        installed.load(), double(originalPhysicsStep));
    ReleaseSRWLockExclusive(&installLock);
    return installed;
}
bool Active() {
    return installed;
}
void Report() {
    if (installed) {
        const auto *timer = reinterpret_cast<const Timer *>(gameImage + GlobalTimer);
        Log("physics updates=%llu loadingUpdates=%llu zeroFrames=%llu latestFrameUS=%lu havokStep=%.9f clampMS=%.6f gameDelta=%.9f realDelta=%.9f nativeInput=%.9f nativeStep=%.9f nativeSteps=%lu paused=%u sameFrame=%u renderLimit=none",
            calls.load(), loadingCalls.load(), zeroFrames.load(), lastDeltaUS.load(),
            double(lastHavok.load()), double(timer->clampMS), double(timer->deltaSeconds),
            double(timer->realDeltaSeconds), double(*reinterpret_cast<float *>(gameImage + 0xDAFE68)),
            double(*reinterpret_cast<float *>(gameImage + 0xE677AC)),
            *reinterpret_cast<DWORD *>(gameImage + 0xE677B4), unsigned(timer->disabled),
            unsigned(timer->sameFrame));
        Log("physics scheduling ticks=%llu idleRenderFrames=%llu remainder=%.9f nativeMode=%lu",
            simulationTicks.load(), idlePhysicsFrames.load(), double(simulationRemainder.load()),
            *reinterpret_cast<DWORD *>(gameImage + PhysicsMode));
    }
}
} // namespace fnv::physics
