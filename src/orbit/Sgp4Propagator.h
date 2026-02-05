#pragma once

#include "orbit/Propagator.h"

#include <string>

// Wrapper around an SGP4 implementation.
// For now this builds as a stub unless you wire in an SGP4 library in CMake.
class Sgp4Propagator final : public Propagator
{
public:
    // Standard SGP4 input format: Two-Line Element set.
    Sgp4Propagator(std::string line1, std::string line2);

    EciState propagate(std::chrono::system_clock::time_point t) const override;

private:
    std::string line1_;
    std::string line2_;
};
