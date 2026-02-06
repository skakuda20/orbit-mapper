#include "OrbitSampler.h"

#include "orbit/Kepler.h"

#include <cmath>

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

namespace OrbitSampler {

std::vector<float> sampleOrbitPolyline(const OrbitalElements& elements, int segments)
{
    if (segments < 8) {
        segments = 8;
    }

    std::vector<float> out;
    out.reserve(static_cast<size_t>(segments + 1) * 3);


    // Mean anomaly at epoch (deg) to radians
    const double meanAnomaly0 = elements.meanAnomalyDeg * (kTwoPi / 360.0);

    for (int s = 0; s <= segments; ++s) {
        const double t = static_cast<double>(s) / static_cast<double>(segments);
        // Mean anomaly for this segment
        const double M = std::fmod(meanAnomaly0 + t * kTwoPi, kTwoPi);
        // Convert mean anomaly to true anomaly (nu)
        double E = M;
        // Solve Kepler's equation: M = E - e*sin(E) for E (Eccentric anomaly)
        for (int iter = 0; iter < 8; ++iter) {
            E = M + elements.eccentricity * std::sin(E);
        }
        const double nu = 2.0 * std::atan2(
            std::sqrt(1 + elements.eccentricity) * std::sin(E / 2.0),
            std::sqrt(1 - elements.eccentricity) * std::cos(E / 2.0));
        const auto pos = Kepler::positionEciFromElements(elements, nu);
        out.push_back(static_cast<float>(pos[0]));
        out.push_back(static_cast<float>(pos[1]));
        out.push_back(static_cast<float>(pos[2]));
    }

    return out;
}

} // namespace OrbitSampler
