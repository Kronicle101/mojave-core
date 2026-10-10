#pragma once
#include <cstdint>
namespace fnv::physics {
struct SimulationStep {
    float seconds;
    uint32_t count;
};
struct SimulationStage {
    float *delta;
    float *input;
    float *step;
    float *remainder;
    uint32_t *count;
};
void PublishSimulationStep(SimulationStage stage, SimulationStep value, float quantum);
class SimulationClock {
    double remainder{};

  public:
    SimulationStep Consume(float seconds, float quantum, uint32_t maximumSteps = 3);
    void Reset() {
        remainder = 0;
    }
    double Remainder() const {
        return remainder;
    }
};
} // namespace fnv::physics
