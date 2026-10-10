#pragma once
#include "runtime.hpp"
namespace fnv::audio {
void Initialize(const Config &config);
void FocusChanged(bool active);
void Shutdown();
bool BindEngine(HMODULE executable);
bool VerifyEngine(const BYTE *image, size_t length);
// Engine callback/state contract; no operating-system audio sessions or mixer.
void ApplyFocus(bool active, BYTE *pauseState, void(__cdecl *pause)(), void(__cdecl *resume)(),
                bool &ownedPause);
// Existing engine-owned IMediaControl transport, called on its native worker.
HRESULT ApplyTransportPause(void *control, bool stopStream);
HRESULT ApplyTransportRun(void *control, bool backgroundRadio);
} // namespace fnv::audio
