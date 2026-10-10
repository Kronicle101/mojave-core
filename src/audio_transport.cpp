#include "audio_transport.hpp"
#include "audio_focus.hpp"
#include <dshow.h>
#include <cmath>
namespace fnv::audio {
namespace {
struct Checkpoint {
    void *identity;
    double seconds;
    bool valid;
};
thread_local Checkpoint checkpoints[4]{};
// IMediaPosition is also used by the stock music path of both engines.
constexpr GUID MediaPositionId{0x56A868B2, 0x0AD4, 0x11CE, {0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};
Checkpoint *Find(void *control, bool create) {
    Checkpoint *vacant{};
    for (auto &entry : checkpoints) {
        if (entry.identity == control)
            return &entry;
        if (!entry.identity && !vacant)
            vacant = &entry;
    }
    if (create && vacant) {
        *vacant = {control, 0, false};
        return vacant;
    }
    return nullptr;
}
HRESULT Position(void *control, double &seconds, bool write) {
    IMediaPosition *position{};
    HRESULT result = static_cast<IUnknown *>(control)->QueryInterface(MediaPositionId,
                                                                      reinterpret_cast<void **>(&position));
    if (FAILED(result))
        return result;
    result = write ? position->put_CurrentPosition(seconds) : position->get_CurrentPosition(&seconds);
    position->Release();
    return result;
}
} // namespace
HRESULT CheckpointPause(void *control, bool stop) {
    if (!control)
        return E_POINTER;
    Checkpoint *entry = Find(control, true);
    if (entry && !entry->valid) {
        double seconds{};
        if (SUCCEEDED(Position(control, seconds, false)) && std::isfinite(seconds) && seconds >= 0) {
            entry->seconds = seconds;
            entry->valid = true;
            Log("music checkpoint saved seconds=%.6f thread=%lu", seconds, GetCurrentThreadId());
        }
    }
    const HRESULT result = ApplyTransportPause(control, stop);
    if (FAILED(result))
        ForgetCheckpoint(control);
    return result;
}
HRESULT CheckpointRun(void *control, bool suppress) {
    if (!control)
        return E_POINTER;
    if (suppress)
        return S_OK;
    if (Checkpoint *entry = Find(control, false)) {
        if (entry->valid) {
            double seconds = entry->seconds;
            const HRESULT result = Position(control, seconds, true);
            Log("music checkpoint restored seconds=%.6f hr=0x%08lX thread=%lu", seconds, result,
                GetCurrentThreadId());
            // A failed seek must not leave the game permanently silent.
        }
        *entry = {};
    }
    return ApplyTransportRun(control, false);
}
void ForgetCheckpoint(void *control) {
    if (Checkpoint *entry = Find(control, false))
        *entry = {};
}
} // namespace fnv::audio
