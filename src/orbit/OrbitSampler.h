#pragma once

#include "orbit/OrbitalElements.h"

#include <vector>

namespace OrbitSampler {

// Returns xyz float triplets for a GL_LINE_STRIP.
std::vector<float> sampleOrbitPolyline(const OrbitalElements& elements, int segments);

} // namespace OrbitSampler
