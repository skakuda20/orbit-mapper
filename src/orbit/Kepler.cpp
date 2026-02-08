#include "Kepler.h"

#include <cmath>

namespace {
constexpr double kPi = 3.141592653589793238462643383279502884;

static double degToRad(double deg)
{
    return deg * (kPi / 180.0);
}
}

namespace Kepler {

std::array<double, 3> positionEciFromElements(const OrbitalElements& elements, double nu)
{
    const double a = elements.semiMajorAxis;
    const double e = elements.eccentricity;

    const double i = degToRad(elements.inclinationDeg);
    const double raan = degToRad(elements.raanDeg);
    const double argp = degToRad(elements.argPeriapsisDeg);

    // Perifocal (PQW) coordinates
    const double p = a * (1.0 - e * e);
    const double r = p / (1.0 + e * std::cos(nu));

    const double x_p = r * std::cos(nu);
    const double y_p = r * std::sin(nu);
    const double z_p = 0.0;

    const double cosO = std::cos(raan);
    const double sinO = std::sin(raan);
    const double cosi = std::cos(i);
    const double sini = std::sin(i);
    const double cosw = std::cos(argp);
    const double sinw = std::sin(argp);

    // Rotation matrix: R = R_z(Ω) * R_x(i) * R_z(ω)
    // Row-major form for direct computation
    const double r11 = cosO * cosw - sinO * sinw * cosi;
    const double r12 = -cosO * sinw - sinO * cosw * cosi;
    const double r13 = sinO * sini;
    
    const double r21 = sinO * cosw + cosO * sinw * cosi;
    const double r22 = -sinO * sinw + cosO * cosw * cosi;
    const double r23 = -cosO * sini;
    
    const double r31 = sinw * sini;
    const double r32 = cosw * sini;
    const double r33 = cosi;

    const double x = r11 * x_p + r12 * y_p + r13 * z_p;
    const double y = r21 * x_p + r22 * y_p + r23 * z_p;
    const double z = r31 * x_p + r32 * y_p + r33 * z_p;

    // Rotate 90 degrees around X-axis: (x,y,z) -> (x,-z,y)
    // This moves equatorial orbits from X-Y plane to X-Z plane
    return {x, -z, y};
}

} // namespace Kepler
