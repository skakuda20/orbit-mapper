#pragma once

// Classical orbital elements (Keplerian)
// Units are intentionally "visualization friendly" for now.
// - semiMajorAxis: arbitrary units (e.g., Earth radii or scaled km)
// - angles are degrees
struct OrbitalElements
{
    double semiMajorAxis = 1.0;     // a
    double eccentricity = 0.0;      // e
    double inclinationDeg = 0.0;    // i
    double raanDeg = 0.0;           // Ω
    double argPeriapsisDeg = 0.0;   // ω
};
