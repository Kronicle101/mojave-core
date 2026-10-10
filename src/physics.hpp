// Source cleanup: 2026-10-10. Runtime logic unchanged.
// SPDX-License-Identifier: LGPL-2.1-only
#pragma once
#include "runtime.hpp"
#include <cstddef>

namespace fnv::physics {
// FNV 1.4.0.525 BSTimer layout, documented by NVTF.
struct Timer {
    BYTE disabled;
    BYTE padding[3];
    float clampMS;
    float remainderMS;
    float deltaSeconds;
    float realDeltaSeconds;
    DWORD lastTime;
    DWORD firstTime;
    bool changeMultiplierSlowly;
    bool sameFrame;
    BYTE tail[2];
};
static_assert(sizeof(Timer) == 0x20);
static_assert(offsetof(Timer, clampMS) == 4);
struct Step {
    float clampMS, havokSeconds;
};
Step ComputeStep(double elapsedSeconds, float originalHavokSeconds, bool loading, bool paused);
using NativeTimerUpdate = void(__thiscall *)(Timer *, DWORD);
float AdvanceTimer(Timer *timer, DWORD tick, double elapsed, float originalHavok, bool loading, bool paused,
                   NativeTimerUpdate update);
bool VerifyScheduler(const BYTE *image, size_t length);
bool VerifyImage(const BYTE *base, size_t length);
bool Install(HMODULE executable);
bool Active();
void Report();
} // namespace fnv::physics
