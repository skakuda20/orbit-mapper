#include "Sgp4Propagator.h"

#include <cmath>
#include <cstring>

#if !defined(ORBIT_MAPPER_SGP4_STUB) || (ORBIT_MAPPER_SGP4_STUB == 0)
#include "SGP4.h"
#endif

namespace {
// Rendering convention: Earth sphere radius == 1.0.
// Convert SGP4 km outputs to Earth radii.
constexpr double kEarthRadiusKm = 6378.137;
constexpr double kRadToDeg = 57.295779513082320876798154814105;

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
    elsetrec satrec{};
    double epochJd = 0.0;
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
    // twoline2rv mutates the input C strings to normalize formatting.
    char l1[130] = {0};
    char l2[130] = {0};
    std::strncpy(l1, line1.c_str(), sizeof(l1) - 1);
    std::strncpy(l2, line2.c_str(), sizeof(l2) - 1);

    double startmfe = 0.0;
    double stopmfe = 0.0;
    double deltamin = 0.0;

    auto* writable = const_cast<Context*>(ctx_.get());
    SGP4Funcs::twoline2rv(
        l1,
        l2,
        /*typerun=*/'c',
        /*typeinput=*/'e',
        /*opsmode=*/'i',
        /*whichconst=*/wgs84,
        startmfe,
        stopmfe,
        deltamin,
        writable->satrec);

    writable->epochJd = writable->satrec.jdsatepoch + writable->satrec.jdsatepochF;
#endif
}

EciState Sgp4Propagator::propagate(std::chrono::system_clock::time_point t) const
{
#if defined(ORBIT_MAPPER_SGP4_STUB) && (ORBIT_MAPPER_SGP4_STUB != 0)
    // Stub output: circular orbit in XY plane.
    EciState state;
    const double r = 3.0;
    state.position = {r, 0.0, 0.0};
    state.velocity = {0.0, 0.0, 0.0};
    return state;
#else
    EciState state;
    if (!ctx_) {
        return state;
    }

    // sgp4() mutates satrec during the call, so use a local copy.
    elsetrec sat = ctx_->satrec;

    const double jd = toJulianDate(t);
    const double tsinceMin = (jd - ctx_->epochJd) * 1440.0;

    double rKm[3] = {0.0, 0.0, 0.0};
    double vKmps[3] = {0.0, 0.0, 0.0};
    const bool ok = SGP4Funcs::sgp4(sat, tsinceMin, rKm, vKmps);
    if (!ok) {
        return state;
    }

    state.position = {rKm[0] / kEarthRadiusKm, rKm[1] / kEarthRadiusKm, rKm[2] / kEarthRadiusKm};
    state.velocity = {vKmps[0] / kEarthRadiusKm, vKmps[1] / kEarthRadiusKm, vKmps[2] / kEarthRadiusKm};
    return state;
#endif
}

bool Sgp4Propagator::tryGetMeanElements(OrbitalElements& outElements) const
{
#if defined(ORBIT_MAPPER_SGP4_STUB) && (ORBIT_MAPPER_SGP4_STUB != 0)
    (void)outElements;
    return false;
#else
    if (!ctx_) {
        return false;
    }

    // Vallado SGP4 stores angles in radians and a in Earth radii (er).
    // These are mean elements from the TLE, which is what we want for drawing a matching orbit.
    outElements.semiMajorAxis = ctx_->satrec.a;
    outElements.eccentricity = ctx_->satrec.ecco;
    outElements.inclinationDeg = ctx_->satrec.inclo * kRadToDeg;
    outElements.raanDeg = ctx_->satrec.nodeo * kRadToDeg;
    outElements.argPeriapsisDeg = ctx_->satrec.argpo * kRadToDeg;
    outElements.meanAnomalyDeg = ctx_->satrec.mo * kRadToDeg;
    return true;
#endif
}
