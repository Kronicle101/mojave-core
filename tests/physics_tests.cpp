#include "physics.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
    const int rates[] = {30, 60, 120, 144, 165, 240, 360, 496, 1000, 10000};
    for (const int fps : rates) {
        double seconds = 0;
        for (int frame = 0; frame < fps * 120; ++frame) {
            const auto step = fnv::physics::ComputeStep(1.0 / fps, 0.016f, false, false);
            // Contract observed in the real BSTimer: positive clamp in ms is
            // converted to seconds, including sub-millisecond fractions.
            seconds += double(step.clampMS) * 0.001;
            CHECK(step.clampMS > 0 && step.havokSeconds > 0 && step.havokSeconds <= 0.016001f);
            if (fps >= 120) CHECK(std::abs(double(step.havokSeconds) - 1.0 / fps) < 1e-8);
            CHECK(double(step.havokSeconds) <= 1.0/fps + 1e-9);
        }
        CHECK(std::abs(seconds - 120.0) < 0.00002);
        std::printf("PASS timer contract %d FPS: simulated %.9f s for 120 s; no minimum frame-time floor\n", fps, seconds);
    }
    auto step = fnv::physics::ComputeStep(2.0, 0.016f, false, false);
    CHECK(step.clampMS == 50.0f && step.havokSeconds == 0.016f);
    step = fnv::physics::ComputeStep(1.0 / 144, 0.016f, true, false);
    CHECK(step.clampMS == 0 && step.havokSeconds == 0.016f);
    step = fnv::physics::ComputeStep(1.0 / 144, 0.016f, false, true);
    CHECK(step.clampMS > 0 && step.havokSeconds == 0.016f);
    for (double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        step = fnv::physics::ComputeStep(invalid, 0.016f, false, false);
        CHECK(step.clampMS == 0 && step.havokSeconds == 0.016f);
    }
    constexpr size_t imageSize = 0xE68000;
    auto* image = static_cast<BYTE*>(VirtualAlloc(nullptr, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(image);
    const BYTE signature[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34, 0x89, 0x4D, 0xEC, 0x8B, 0x45, 0xEC};
    memcpy(image + 0x6A4EE0, signature, sizeof(signature));
    image[0x46F291] = 0xB9;
    const DWORD thisPointer = reinterpret_cast<DWORD>(image + 0xDF6394);
    memcpy(image + 0x46F292, &thisPointer, sizeof(thisPointer));
    image[0x46F296] = 0xE8;
    const int32_t call = static_cast<int32_t>(0x371520 - 0x46F29B);
    memcpy(image + 0x46F297, &call, sizeof(call));
    const float maxTime = 0.016f;
    memcpy(image + 0xE67B38, &maxTime, sizeof(maxTime));
    CHECK(fnv::physics::VerifyImage(image, imageSize));
    CHECK(!fnv::physics::VerifyImage(image, 4096));
    image[0x46F297] ^= 1;
    CHECK(!fnv::physics::VerifyImage(image, imageSize));
    image[0x46F297] ^= 1;
    image[0x6A4EE0] = 0xE9;
    CHECK(!fnv::physics::VerifyImage(image, imageSize));
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    std::puts("PASS loading, pause, stalls, invalid deltas and engine signature/conflict guards");
    return 0;
}
