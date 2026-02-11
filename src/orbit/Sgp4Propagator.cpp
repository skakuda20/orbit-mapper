#include "Sgp4Propagator.h"

#include <cmath>
#include <memory>

#if !defined(ORBIT_MAPPER_SGP4_STUB) || (ORBIT_MAPPER_SGP4_STUB == 0)
#include "SGP4.h"
#include "Tle.h"
#include "DateTime.h"
#endif

namespace {
// Rendering convention: Earth sphere radius == 1.0.
// Convert SGP4 km outputs to Earth radii.
constexpr double kEarthRadiusKm = 6378.137;
constexpr double kRadToDeg = 57.295779513082320876798154814105;
constexpr double kEarthMuKm3PerS2 = 398600.4418;  // Earth's gravitational parameter

static double unixSecondsToJulianDate(double unixSeconds)
{
    // Unix epoch (1970-01-01 00:00:00 UTC) = JD 2440587.5
    return 2440587.5 + (unixSeconds / 86400.0);
}

static double toJulianDate(std::chrono::system_clock::time_point tp)
{
    const auto sinceEpoch = tp.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(sinceEpoch).count();
    return unixSecondsToJulianDate(seconds);
}
} // namespace

struct Sgp4Propagator::Context
{
#if !defined(ORBIT_MAPPER_SGP4_STUB) || (ORBIT_MAPPER_SGP4_STUB == 0)
    std::unique_ptr<libsgp4::SGP4> sgp4;
    std::unique_ptr<libsgp4::Tle> tle;
#else
    std::string line1;
    std::string line2;
#endif
};

Sgp4Propagator::Sgp4Propagator(std::string line1, std::string line2)
    : ctx_(std::make_shared<Context>())
{
#if defined(ORBIT_MAPPER_SGP4_STUB) && (ORBIT_MAPPER_SGP4_STUB != 0)
    // Keep TLE around in stub mode (useful for debugging).
    const_cast<Context*>(ctx_.get())->line1 = std::move(line1);
    const_cast<Context*>(ctx_.get())->line2 = std::move(line2);
#else
    try {
        auto* writable = const_cast<Context*>(ctx_.get());
        writable->tle = std::make_unique<libsgp4::Tle>(line1, line2);
        writable->sgp4 = std::make_unique<libsgp4::SGP4>(*writable->tle);
    } catch (...) {
        // If TLE parsing fails, silently leave ctx_ in a safe state (sgp4/tle remain nullptr)
    }
#endif
}

EciState Sgp4Propagator::propagate(std::chrono::system_clock::time_point t) const
{
#if defined(ORBIT_MAPPER_SGP4_STUB) && (ORBIT_MAPPER_SGP4_STUB != 0)
    // Stub output: circular orbit in XY plane.
    EciState state;
    const double r = 3.0;
    // Render convention: (x,y,z) -> (x,z,-y)
    state.position = {r, 0.0, -0.0};
    state.velocity = {0.0, 0.0, -0.0};
    return state;
#else
    EciState state;
    if (!ctx_ || !ctx_->sgp4) {
        return state;
    }

    try {
        // Convert system clock to libsgp4 DateTime
        const auto jd = toJulianDate(t);
        const auto days = static_cast<int>(jd);
        const auto fraction = jd - days;
        libsgp4::DateTime dt(days, fraction);

        // Propagate to get ECI position
        const auto eci = ctx_->sgp4->FindPosition(dt);
        const auto pos = eci.Position();
        const auto vel = eci.Velocity();

        // Render convention: +Y is up.
        // Remap SGP4 ECI (x,y,z) to render coords (x,z,-y) to match Kepler/OrbitSampler.
        state.position = {pos.x / kEarthRadiusKm, pos.z / kEarthRadiusKm, -pos.y / kEarthRadiusKm};
        state.velocity = {vel.x / kEarthRadiusKm, vel.z / kEarthRadiusKm, -vel.y / kEarthRadiusKm};
        return state;
    } catch (...) {
        // Silently return zero state on any propagation error
        return state;
    }
#endif
}

bool Sgp4Propagator::tryGetMeanElements(OrbitalElements& outElements) const
{
#if defined(ORBIT_MAPPER_SGP4_STUB) && (ORBIT_MAPPER_SGP4_STUB != 0)
    (void)outElements;
    return false;
#else
    if (!ctx_ || !ctx_->tle) {
        return false;
    }

    try {
        // libsgp4::Tle stores elements with accessors.
        // Mean motion is in radians per minute; convert to semi-major axis.
        const double n = ctx_->tle->MeanMotion() * (2.0 * 3.141592653589793238462643383279502884) / 86400.0;  // Convert to rad/s
        const double a = std::cbrt(kEarthMuKm3PerS2 / (n * n));  // Semi-major axis in km
        
        outElements.semiMajorAxis = a / kEarthRadiusKm;
        outElements.eccentricity = ctx_->tle->Eccentricity();
        outElements.inclinationDeg = ctx_->tle->Inclination(true);  // true = in degrees
        outElements.raanDeg = ctx_->tle->RightAscendingNode(true);  // true = in degrees
        outElements.argPeriapsisDeg = ctx_->tle->ArgumentPerigee(true);  // true = in degrees
        outElements.meanAnomalyDeg = ctx_->tle->MeanAnomaly(true);  // true = in degrees
        return true;
    } catch (...) {
        return false;
    }
#endif
}
