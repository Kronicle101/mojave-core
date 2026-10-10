#include "simulation_clock.hpp"
#include <cmath>
#include <algorithm>
namespace fnv::physics {
SimulationStep SimulationClock::Consume(float seconds, float quantum, uint32_t maximumSteps) {
    if (!std::isfinite(seconds) || !std::isfinite(quantum) || seconds <= 0 || quantum <= 0 || !maximumSteps) {
        Reset();
        return {};
    }
    // Limit accumulated catch-up to the native batch plus one following batch.
    // A sustained stall cannot create an unbounded queue of physics work.
    remainder = std::min(remainder + double(seconds), double(quantum) * double(maximumSteps) * 2.0);
    // Original FNV bounds the native batch at three steps (one in its special
    // mode). Preserve that bound and carry remaining time to the next frame.
    const auto count =
        static_cast<uint32_t>(std::min(std::floor(remainder / double(quantum)), double(maximumSteps)));
    remainder -= double(count) * double(quantum);
    return {static_cast<float>(double(count) * double(quantum)), count};
}
void PublishSimulationStep(SimulationStage stage, SimulationStep value, float quantum) {
    *stage.delta = value.seconds;
    *stage.input = value.seconds;
    *stage.step = value.count ? quantum : 0;
    *stage.remainder = 0;
    *stage.count = value.count;
}
} // namespace fnv::physics
