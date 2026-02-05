#pragma once

// Classical orbital elements (Keplerian)
// Units are intentionally "visualization friendly" for now.
// Recommended convention for this project:
// - Distances in Earth radii (Re): Earth sphere is radius 1.0 at the origin.
//   Example: 400 km altitude LEO -> a ≈ (Re + 400 km) / Re ≈ 1.063
// - angles are degrees
struct OrbitalElements
{
    double semiMajorAxis = 1.0;     // a
    double eccentricity = 0.0;      // e
    double inclinationDeg = 0.0;    // i
    double raanDeg = 0.0;           // Ω
    double argPeriapsisDeg = 0.0;   // ω
};
