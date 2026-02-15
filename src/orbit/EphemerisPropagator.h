#pragma once

#include "orbit/OrbitalElements.h"
#include "orbit/Propagator.h"

#include <array>
#include <chrono>
#include <memory>
#include <vector>

// One state sample in an ECI-like frame.
// Input units are kilometers and kilometers/second.
struct EphemerisSample
{
    std::chrono::system_clock::time_point t{};
    std::array<double, 3> positionKm{};
    std::array<double, 3> velocityKmPerS{};

    // Optional covariance data (upper triangle, row-major) in the same frame as position/velocity.
    // Layout: (0,0) (0,1) ... (0,5) (1,1) ... (5,5) => 21 values.
    bool hasCovarianceUpper = false;
    std::array<double, 21> covarianceUpper{};
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

    // If this ephemeris was created from a single epoch state (possibly with covariance),
    // the propagator may internally synthesize an SGP4 model. This allows drawing a full
    // orbit even when only one sample is provided.
    bool tryGetOrbitalPeriodSeconds(double& outPeriodSeconds) const;

    // True when the input looks like a set of epoch state estimates (each sample includes
    // a covariance upper triangle). In this mode we prefer SGP4-driven full-orbit sampling.
    bool isEpochStateSet() const;

    // True if at least one SGP4 model was successfully synthesized.
    bool hasSgp4() const { return static_cast<bool>(sgp4_) || !sgp4BySample_.empty(); }

    // Returns extracted orbital elements (if available) from a state vector or covariance sample.
    // Used for Kepler-based rendering when SGP4 synthesis fails.
    bool tryGetKeplerianElements(OrbitalElements& outElements) const;

    const std::vector<EphemerisSample>& samples() const { return samples_; }

private:
    static constexpr double kEarthRadiusKm = 6378.137;

    std::vector<EphemerisSample> samples_;

    // Extracted Keplerian elements from first sample (if extraction succeeded).
    // Used for full-orbit Kepler rendering when SGP4 synthesis fails.
    std::unique_ptr<OrbitalElements> keplerianElements_;

    // Optional internal SGP4 propagator synthesized from a single state vector.
    // Present only when samples_.size() == 1 and synthesis succeeds.
    std::unique_ptr<class Sgp4Propagator> sgp4_;

    // Optional per-sample SGP4 propagators synthesized from multiple epoch state estimates.
    // Present only when all samples include covariance and at least one synthesis succeeds.
    std::vector<std::unique_ptr<class Sgp4Propagator>> sgp4BySample_;

    static EciState toRenderState(const EphemerisSample& s);
    static EciState lerp(const EphemerisSample& a, const EphemerisSample& b, double alpha);
};
