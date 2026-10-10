// Source cleanup: 2026-10-10. Runtime logic unchanged.
// SPDX-License-Identifier: LGPL-2.1-only
#include "physics.hpp"
#include <algorithm>
#include <cmath>
namespace fnv::physics {
Step ComputeStep(double seconds, float originalHavokSeconds, bool loading, bool paused) {
    if (!std::isfinite(originalHavokSeconds) || originalHavokSeconds <= 0)
        originalHavokSeconds = 0.016f;
    const double duration = std::isfinite(seconds) && seconds > 0 ? std::min(seconds, 0.050) : 0.0;
    if (loading || duration == 0)
        return {0, originalHavokSeconds};
    const float clamp = static_cast<float>(duration * 1000.0);
    // The native world can execute this interval directly. A lower bound would
    // make physics advance farther than the game on sub-bound frames.
    const float havok =
        paused ? originalHavokSeconds : static_cast<float>(std::min(duration, double(originalHavokSeconds)));
    return {clamp, havok};
}
} // namespace fnv::physics
