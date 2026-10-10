#pragma once
#include "runtime.hpp"
namespace fnv::audio {
// Checkpoints contain identity and time only. COM interfaces are obtained and
// released on the engine thread that owns the transport; none cross threads.
HRESULT CheckpointPause(void *control, bool stop);
HRESULT CheckpointRun(void *control, bool suppress);
void ForgetCheckpoint(void *control);
} // namespace fnv::audio
