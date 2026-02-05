#pragma once

#include "orbit/OrbitalElements.h"

#include <array>

namespace Kepler {

// Returns ECI-like position vector in the same units as semiMajorAxis.
// Input true anomaly is in radians.
std::array<double, 3> positionEciFromElements(const OrbitalElements& elements, double trueAnomalyRad);

} // namespace Kepler
