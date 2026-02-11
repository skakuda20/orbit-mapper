#pragma once

#include "orbit/OrbitalElements.h"
#include "orbit/Propagator.h"

#include <memory>
#include <string>

// Wrapper around an SGP4 implementation.
class Sgp4Propagator final : public Propagator
{
public:
    // Standard SGP4 input format: Two-Line Element set.
    Sgp4Propagator(std::string line1, std::string line2);

    EciState propagate(std::chrono::system_clock::time_point t) const override;

    // Returns the TLE mean elements (best-effort) in the app's rendering convention.
    // Useful for drawing an orbit polyline that matches the SGP4-propagated marker.
    bool tryGetMeanElements(OrbitalElements& outElements) const;

    // Returns orbital period in seconds (best-effort).
    bool tryGetOrbitalPeriodSeconds(double& outPeriodSeconds) const;

private:
    struct Context;
    std::shared_ptr<const Context> ctx_;
};
