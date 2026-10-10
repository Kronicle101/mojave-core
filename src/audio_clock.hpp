#pragma once
#include "runtime.hpp"
namespace fnv::audio::clock {
bool Verify(const BYTE *image, size_t length);
bool Bind(BYTE *image, size_t length);
void Unbind();
void FocusChanged(bool active);
// The native update must run while paused, before clearing the pause bit.
struct Counter {
    DWORD elapsed, lastTick, flags;
};
void Rebase(Counter *counter, void(__thiscall *update)(Counter *));
} // namespace fnv::audio::clock
