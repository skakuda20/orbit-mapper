#pragma once

#include "orbit/Propagator.h"

#include <array>
#include <chrono>
#include <vector>

// One state sample in an ECI-like frame.
// Input units are kilometers and kilometers/second.
struct EphemerisSample
{
    std::chrono::system_clock::time_point t{};
    std::array<double, 3> positionKm{};
    std::array<double, 3> velocityKmPerS{};
};

// Simple ephemeris-driven propagator.
// - Linearly interpolates between samples by time.
// - Converts km -> Earth radii for visualization.
// - Applies the project's ECI->render axis remap: (x,y,z) -> (x,z,-y).
class EphemerisPropagator final : public Propagator
{
public:
    explicit EphemerisPropagator(std::vector<EphemerisSample> samples);

    EciState propagate(std::chrono::system_clock::time_point t) const override;

    const std::vector<EphemerisSample>& samples() const { return samples_; }

private:
    static constexpr double kEarthRadiusKm = 6378.137;

    std::vector<EphemerisSample> samples_;

    static EciState toRenderState(const EphemerisSample& s);
    static EciState lerp(const EphemerisSample& a, const EphemerisSample& b, double alpha);
};
