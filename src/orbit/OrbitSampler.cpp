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

    for (int s = 0; s <= segments; ++s) {
        const double t = static_cast<double>(s) / static_cast<double>(segments);
        const double nu = t * kTwoPi;
        const auto pos = Kepler::positionEciFromElements(elements, nu);
        out.push_back(static_cast<float>(pos[0]));
        out.push_back(static_cast<float>(pos[1]));
        out.push_back(static_cast<float>(pos[2]));
    }

    return out;
}

} // namespace OrbitSampler
