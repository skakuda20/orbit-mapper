#include "EphemerisPropagator.h"

#include "orbit/OrbitalElements.h"
#include "orbit/Sgp4Propagator.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kEarthRadiusKm = 6378.137;
constexpr double kEarthMuKm3PerS2 = 398600.4418;

static double wrapDeg(double deg)
{
    double x = std::fmod(deg, 360.0);
    if (x < 0.0) {
        x += 360.0;
    }
    return x;
}

static int tleChecksum(const std::string& lineWithoutChecksum)
{
    int sum = 0;
    for (char c : lineWithoutChecksum) {
        if (c >= '0' && c <= '9') {
            sum += (c - '0');
        } else if (c == '-') {
            sum += 1;
        }
    }
    return sum % 10;
}

static std::string finalizeTleLine(std::string line)
{
    // libsgp4 expects exactly 69 chars per line (68 + checksum digit).
    if (line.size() < 68) {
        line.append(68 - line.size(), ' ');
    } else if (line.size() > 68) {
        line.resize(68);
    }
    const int cs = tleChecksum(line);
    line.push_back(static_cast<char>('0' + cs));
    return line;
}

static bool tleEpochFromTimePoint(std::chrono::system_clock::time_point tp, std::string& outEpoch)
{
    using namespace std::chrono;

    const auto secTp = time_point_cast<seconds>(tp);
    const auto us = duration_cast<microseconds>(tp - secTp).count();
    const std::time_t tt = system_clock::to_time_t(secTp);

    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif

    const int year = utc.tm_year + 1900;
    const int year2 = year % 100;
    const int doy = utc.tm_yday + 1;

    const double secOfDay =
        static_cast<double>(utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec) +
        static_cast<double>(us) / 1e6;
    const double dayFrac = secOfDay / 86400.0;
    const double dayWithFrac = static_cast<double>(doy) + dayFrac;

    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setw(2) << std::setfill('0') << year2;
    oss << std::setw(0) << std::setfill(' ');
    oss << std::setw(3) << std::setfill('0') << doy;
    oss << '.';
    oss << std::setw(8) << std::setfill('0') << std::right << std::setprecision(8) << (dayWithFrac - static_cast<double>(doy));

    // The above prints leading "0."; we need just the fractional digits after '.'
    // Replace "0." with nothing, keeping the digits.
    std::string frac = oss.str();
    // frac currently like "260450.12345678"? Actually: year2 + doy + '.' + (0.xxxxxxxx) formatted with width 8.
    // Simpler: rebuild explicitly.
    std::ostringstream oss2;
    oss2.setf(std::ios::fixed);
    oss2 << std::setw(2) << std::setfill('0') << year2;
    oss2 << std::setw(3) << std::setfill('0') << doy;
    oss2 << std::setw(0) << std::setfill(' ');
    oss2 << std::setprecision(8);
    // fractional part only
    double fracOnly = dayFrac;
    if (!(std::isfinite(fracOnly) && fracOnly >= 0.0 && fracOnly < 1.0)) {
        return false;
    }
    // Print as .dddddddd with exactly 8 digits.
    const long long fracScaled = static_cast<long long>(std::llround(fracOnly * 1e8));
    // Handle rounding rollover.
    long long fracScaledClamped = fracScaled;
    int doyAdj = 0;
    if (fracScaledClamped >= 100000000LL) {
        fracScaledClamped -= 100000000LL;
        doyAdj = 1;
    }

    const int doyFinal = doy + doyAdj;
    oss2.str("");
    oss2.clear();
    oss2 << std::setw(2) << std::setfill('0') << year2;
    oss2 << std::setw(3) << std::setfill('0') << doyFinal;
    oss2 << '.';
    oss2 << std::setw(8) << std::setfill('0') << fracScaledClamped;

    outEpoch = oss2.str();
    return outEpoch.size() == 14;
}

// Extract Keplerian orbital elements from an ECI state vector.
// Returns true if extraction succeeded (state is physically reasonable).
static bool extractOrbitalElements(
    const std::array<double, 3>& rKm,
    const std::array<double, 3>& vKmPerS,
    OrbitalElements& outElements)
{
    const double rx = rKm[0], ry = rKm[1], rz = rKm[2];
    const double vx = vKmPerS[0], vy = vKmPerS[1], vz = vKmPerS[2];

    const double r = std::sqrt(rx * rx + ry * ry + rz * rz);
    const double v2 = vx * vx + vy * vy + vz * vz;
    if (!(std::isfinite(r) && std::isfinite(v2) && r > kEarthRadiusKm && r < 1e6)) {
        return false;
    }

    // h = r x v
    const double hx = ry * vz - rz * vy;
    const double hy = rz * vx - rx * vz;
    const double hz = rx * vy - ry * vx;
    const double h = std::sqrt(hx * hx + hy * hy + hz * hz);
    if (!(std::isfinite(h) && h > 0.0)) {
        return false;
    }

    const double iRad = std::acos(std::clamp(hz / h, -1.0, 1.0));

    // n = k x h
    const double nx = -hy;
    const double ny = hx;
    const double n = std::sqrt(nx * nx + ny * ny);

    double raanRad = 0.0;
    if (n > 1e-12) {
        raanRad = std::atan2(ny, nx);
        if (raanRad < 0.0) {
            raanRad += kTwoPi;
        }
    }

    // evec = (v x h)/mu - r/|r|
    const double vxh_x = (vy * hz - vz * hy);
    const double vxh_y = (vz * hx - vx * hz);
    const double vxh_z = (vx * hy - vy * hx);
    const double ex = (vxh_x / kEarthMuKm3PerS2) - (rx / r);
    const double ey = (vxh_y / kEarthMuKm3PerS2) - (ry / r);
    const double ez = (vxh_z / kEarthMuKm3PerS2) - (rz / r);
    const double e = std::sqrt(ex * ex + ey * ey + ez * ez);

    if (!std::isfinite(e) || e < 0.0 || e > 0.999) {
        return false;
    }

    double argpRad = 0.0;
    double nuRad = 0.0;
    if (e > 1e-10 && n > 1e-12) {
        // Argument of perigee
        const double ndote = (nx * ex + ny * ey) / (n * e);
        argpRad = std::acos(std::clamp(ndote, -1.0, 1.0));
        if (ez < 0.0) {
            argpRad = kTwoPi - argpRad;
        }
        // True anomaly
        const double edotr = (ex * rx + ey * ry + ez * rz) / (e * r);
        nuRad = std::acos(std::clamp(edotr, -1.0, 1.0));
        const double rdotv = rx * vx + ry * vy + rz * vz;
        if (rdotv < 0.0) {
            nuRad = kTwoPi - nuRad;
        }
    } else {
        // Near-circular or equatorial: fall back to true longitude.
        const double px = rx / r;
        const double py = ry / r;
        nuRad = std::atan2(py, px);
        if (nuRad < 0.0) {
            nuRad += kTwoPi;
        }
        argpRad = 0.0;
    }

    // Semi-major axis via vis-viva
    const double a = 1.0 / (2.0 / r - v2 / kEarthMuKm3PerS2);
    if (!(std::isfinite(a) && a > kEarthRadiusKm && a < 1e6)) {
        return false;
    }

    // Mean anomaly from true anomaly
    double E = 0.0;
    {
        const double cosE = (e + std::cos(nuRad)) / (1.0 + e * std::cos(nuRad));
        const double sinE = (std::sqrt(1.0 - e * e) * std::sin(nuRad)) / (1.0 + e * std::cos(nuRad));
        E = std::atan2(sinE, cosE);
        if (E < 0.0) {
            E += kTwoPi;
        }
    }
    double M = E - e * std::sin(E);
    M = std::fmod(M, kTwoPi);
    if (M < 0.0) {
        M += kTwoPi;
    }

    outElements.semiMajorAxis = a / kEarthRadiusKm;
    outElements.eccentricity = e;
    outElements.inclinationDeg = wrapDeg(iRad * 180.0 / kPi);
    outElements.raanDeg = wrapDeg(raanRad * 180.0 / kPi);
    outElements.argPeriapsisDeg = wrapDeg(argpRad * 180.0 / kPi);
    outElements.meanAnomalyDeg = wrapDeg(M * 180.0 / kPi);
    return true;
}

// Convert ECI state vector (km, km/s) to a synthetic TLE (best-effort).
// Note: SGP4 is designed for mean elements; this uses osculating elements derived
// from the state and sets drag terms to zero. This is intended for visualization.
static bool buildSyntheticTleFromEciState(
    std::chrono::system_clock::time_point epoch,
    const std::array<double, 3>& rKm,
    const std::array<double, 3>& vKmPerS,
    std::string& outLine1,
    std::string& outLine2)
{
    const double rx = rKm[0], ry = rKm[1], rz = rKm[2];
    const double vx = vKmPerS[0], vy = vKmPerS[1], vz = vKmPerS[2];

    const double r = std::sqrt(rx * rx + ry * ry + rz * rz);
    const double v2 = vx * vx + vy * vy + vz * vz;
    if (!(std::isfinite(r) && std::isfinite(v2) && r > 0.0)) {
        return false;
    }

    // h = r x v
    const double hx = ry * vz - rz * vy;
    const double hy = rz * vx - rx * vz;
    const double hz = rx * vy - ry * vx;
    const double h = std::sqrt(hx * hx + hy * hy + hz * hz);
    if (!(std::isfinite(h) && h > 0.0)) {
        return false;
    }

    const double iRad = std::acos(std::clamp(hz / h, -1.0, 1.0));

    // n = k x h
    const double nx = -hy;
    const double ny = hx;
    const double n = std::sqrt(nx * nx + ny * ny);

    double raanRad = 0.0;
    if (n > 1e-12) {
        raanRad = std::atan2(ny, nx);
        if (raanRad < 0.0) {
            raanRad += kTwoPi;
        }
    }

    // evec = (v x h)/mu - r/|r|
    const double vxh_x = (vy * hz - vz * hy);
    const double vxh_y = (vz * hx - vx * hz);
    const double vxh_z = (vx * hy - vy * hx);
    const double ex = (vxh_x / kEarthMuKm3PerS2) - (rx / r);
    const double ey = (vxh_y / kEarthMuKm3PerS2) - (ry / r);
    const double ez = (vxh_z / kEarthMuKm3PerS2) - (rz / r);
    const double e = std::sqrt(ex * ex + ey * ey + ez * ez);

    if (!std::isfinite(e) || e < 0.0 || e >= 0.999) {
        // SGP4 implementation rejects e>=0.999.
        return false;
    }

    double argpRad = 0.0;
    double nuRad = 0.0;
    if (e > 1e-10 && n > 1e-12) {
        // Argument of perigee
        const double ndote = (nx * ex + ny * ey) / (n * e);
        argpRad = std::acos(std::clamp(ndote, -1.0, 1.0));
        if (ez < 0.0) {
            argpRad = kTwoPi - argpRad;
        }
        // True anomaly
        const double edotr = (ex * rx + ey * ry + ez * rz) / (e * r);
        nuRad = std::acos(std::clamp(edotr, -1.0, 1.0));
        const double rdotv = rx * vx + ry * vy + rz * vz;
        if (rdotv < 0.0) {
            nuRad = kTwoPi - nuRad;
        }
    } else {
        // Near-circular or equatorial: fall back to true longitude.
        // Compute angle from node vector (or x-axis if equatorial).
        const double px = rx / r;
        const double py = ry / r;
        // Project into equatorial plane for angle.
        nuRad = std::atan2(py, px);
        if (nuRad < 0.0) {
            nuRad += kTwoPi;
        }
        argpRad = 0.0;
    }

    // Semi-major axis via vis-viva
    const double a = 1.0 / (2.0 / r - v2 / kEarthMuKm3PerS2);
    if (!(std::isfinite(a) && a > 0.0)) {
        return false;
    }

    const double nRadPerS = std::sqrt(kEarthMuKm3PerS2 / (a * a * a));
    const double meanMotionRevPerDay = nRadPerS * 86400.0 / kTwoPi;
    if (!(std::isfinite(meanMotionRevPerDay) && meanMotionRevPerDay > 0.0)) {
        return false;
    }

    // Mean anomaly from true anomaly
    double E = 0.0;
    {
        const double cosE = (e + std::cos(nuRad)) / (1.0 + e * std::cos(nuRad));
        const double sinE = (std::sqrt(1.0 - e * e) * std::sin(nuRad)) / (1.0 + e * std::cos(nuRad));
        E = std::atan2(sinE, cosE);
        if (E < 0.0) {
            E += kTwoPi;
        }
    }
    double M = E - e * std::sin(E);
    M = std::fmod(M, kTwoPi);
    if (M < 0.0) {
        M += kTwoPi;
    }

    const double incDeg = wrapDeg(iRad * 180.0 / kPi);
    const double raanDeg = wrapDeg(raanRad * 180.0 / kPi);
    const double argpDeg = wrapDeg(argpRad * 180.0 / kPi);
    const double meanAnomDeg = wrapDeg(M * 180.0 / kPi);

    std::string epochStr;
    if (!tleEpochFromTimePoint(epoch, epochStr)) {
        return false;
    }

    // Format eccentricity as 7 digits with implied decimal point.
    long long ecc7 = static_cast<long long>(std::llround(e * 1e7));
    if (ecc7 < 0) {
        ecc7 = 0;
    }
    if (ecc7 > 9999999) {
        ecc7 = 9999999;
    }

    std::ostringstream l1;
    l1 << "1 "
       << "00001"  // sat number
       << "U "
       << "00000A   "
       << epochStr
       << "  .00000000  00000-0  00000-0 0  999";

    std::ostringstream l2;
    l2.setf(std::ios::fixed);
    l2 << "2 "
       << "00001";

    l2 << ' ' << std::setw(8) << std::setfill(' ') << std::right << std::setprecision(4) << incDeg;
    l2 << ' ' << std::setw(8) << std::setfill(' ') << std::right << std::setprecision(4) << raanDeg;
    l2 << ' ' << std::setw(7) << std::setfill('0') << std::right << ecc7;
    l2 << ' ' << std::setw(8) << std::setfill(' ') << std::right << std::setprecision(4) << argpDeg;
    l2 << ' ' << std::setw(8) << std::setfill(' ') << std::right << std::setprecision(4) << meanAnomDeg;
    l2 << ' ' << std::setw(11) << std::setfill(' ') << std::right << std::setprecision(8) << meanMotionRevPerDay;
    l2 << std::setw(5) << std::setfill(' ') << std::right << 1;

    outLine1 = finalizeTleLine(l1.str());
    outLine2 = finalizeTleLine(l2.str());
    return outLine1.size() == 69 && outLine2.size() == 69;
}
} // namespace

EphemerisPropagator::EphemerisPropagator(std::vector<EphemerisSample> samples)
    : samples_(std::move(samples))
{
    samples_.erase(
        std::remove_if(samples_.begin(), samples_.end(), [](const EphemerisSample& s) {
            return s.t == std::chrono::system_clock::time_point{};
        }),
        samples_.end());

    std::sort(samples_.begin(), samples_.end(), [](const EphemerisSample& a, const EphemerisSample& b) {
        return a.t < b.t;
    });

    // Always try to extract Keplerian elements from the first sample.
    // This provides a fallback for full-orbit rendering when SGP4 synthesis fails.
    if (!samples_.empty()) {
        auto el = std::make_unique<OrbitalElements>();
        if (extractOrbitalElements(samples_[0].positionKm, samples_[0].velocityKmPerS, *el)) {
            keplerianElements_ = std::move(el);
        }
    }

    // If only one epoch state is provided, try to synthesize an SGP4 model
    // so we can still propagate a full orbit for visualization.
    if (samples_.size() == 1) {
        std::string l1;
        std::string l2;
        if (buildSyntheticTleFromEciState(samples_[0].t, samples_[0].positionKm, samples_[0].velocityKmPerS, l1, l2)) {
            try {
                sgp4_ = std::make_unique<Sgp4Propagator>(l1, l2);
            } catch (...) {
                sgp4_.reset();
            }
        }
    } else if (!samples_.empty()) {
        // Multi-sample input: if any sample includes covariance, treat this as a set of
        // epoch state estimates and attempt per-sample SGP4 synthesis. Even if the
        // covariance isn't used yet, its presence is a strong signal of this format.
        const bool anyHaveCov = std::any_of(samples_.begin(), samples_.end(), [](const EphemerisSample& s) {
            return s.hasCovarianceUpper;
        });

        if (anyHaveCov) {
            sgp4BySample_.resize(samples_.size());
            bool anyOk = false;
            for (size_t k = 0; k < samples_.size(); ++k) {
                if (!samples_[k].hasCovarianceUpper) {
                    continue;
                }

                std::string l1;
                std::string l2;
                if (!buildSyntheticTleFromEciState(samples_[k].t, samples_[k].positionKm, samples_[k].velocityKmPerS, l1, l2)) {
                    continue;
                }
                try {
                    sgp4BySample_[k] = std::make_unique<Sgp4Propagator>(l1, l2);
                    anyOk = true;
                } catch (...) {
                    sgp4BySample_[k].reset();
                }
            }
            if (!anyOk) {
                sgp4BySample_.clear();
            }
        }
    }
}

EciState EphemerisPropagator::toRenderState(const EphemerisSample& s)
{
    // ECI -> render: (x,y,z) -> (x,z,-y)
    const double xRe = s.positionKm[0] / kEarthRadiusKm;
    const double yRe = s.positionKm[2] / kEarthRadiusKm;
    const double zRe = -s.positionKm[1] / kEarthRadiusKm;

    const double vxRe = s.velocityKmPerS[0] / kEarthRadiusKm;
    const double vyRe = s.velocityKmPerS[2] / kEarthRadiusKm;
    const double vzRe = -s.velocityKmPerS[1] / kEarthRadiusKm;

    EciState st;
    st.position = {xRe, yRe, zRe};
    st.velocity = {vxRe, vyRe, vzRe};
    return st;
}

EciState EphemerisPropagator::lerp(const EphemerisSample& a, const EphemerisSample& b, double alpha)
{
    alpha = std::clamp(alpha, 0.0, 1.0);

    EphemerisSample s;
    s.t = a.t;
    for (int i = 0; i < 3; ++i) {
        s.positionKm[i] = a.positionKm[i] + alpha * (b.positionKm[i] - a.positionKm[i]);
        s.velocityKmPerS[i] = a.velocityKmPerS[i] + alpha * (b.velocityKmPerS[i] - a.velocityKmPerS[i]);
    }
    return toRenderState(s);
}

EciState EphemerisPropagator::propagate(std::chrono::system_clock::time_point t) const
{
    if (samples_.empty()) {
        return {};
    }

    if (sgp4_) {
        return sgp4_->propagate(t);
    }

    if (!sgp4BySample_.empty() && sgp4BySample_.size() == samples_.size()) {
        auto it = std::lower_bound(
            samples_.begin(),
            samples_.end(),
            t,
            [](const EphemerisSample& s, const std::chrono::system_clock::time_point& tp) { return s.t < tp; });

        size_t idx = 0;
        if (it == samples_.begin()) {
            idx = 0;
        } else if (it == samples_.end()) {
            idx = samples_.size() - 1;
        } else {
            const size_t bIdx = static_cast<size_t>(it - samples_.begin());
            const size_t aIdx = bIdx - 1;
            const auto da = (t >= samples_[aIdx].t) ? (t - samples_[aIdx].t) : (samples_[aIdx].t - t);
            const auto db = (t >= samples_[bIdx].t) ? (t - samples_[bIdx].t) : (samples_[bIdx].t - t);
            idx = (da <= db) ? aIdx : bIdx;
        }

        if (idx < sgp4BySample_.size() && sgp4BySample_[idx]) {
            return sgp4BySample_[idx]->propagate(t);
        }
    }

    if (samples_.size() == 1) {
        return toRenderState(samples_.front());
    }

    if (t <= samples_.front().t) {
        return toRenderState(samples_.front());
    }

    if (t >= samples_.back().t) {
        return toRenderState(samples_.back());
    }

    auto it = std::lower_bound(
        samples_.begin(),
        samples_.end(),
        t,
        [](const EphemerisSample& s, const std::chrono::system_clock::time_point& tp) { return s.t < tp; });

    if (it == samples_.begin()) {
        return toRenderState(*it);
    }

    const auto& b = *it;
    const auto& a = *(it - 1);

    const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(b.t - a.t).count();
    if (dt <= 0.0) {
        return toRenderState(a);
    }

    const double u = std::chrono::duration_cast<std::chrono::duration<double>>(t - a.t).count() / dt;
    return lerp(a, b, u);
}

bool EphemerisPropagator::tryGetOrbitalPeriodSeconds(double& outPeriodSeconds) const
{
    if (sgp4_) {
        return sgp4_->tryGetOrbitalPeriodSeconds(outPeriodSeconds);
    }
    for (const auto& p : sgp4BySample_) {
        if (p && p->tryGetOrbitalPeriodSeconds(outPeriodSeconds)) {
            return true;
        }
    }
    return false;
}

bool EphemerisPropagator::isEpochStateSet() const
{
    if (samples_.empty()) {
        return false;
    }

    if (sgp4_ || !sgp4BySample_.empty()) {
        return true;
    }

    return std::any_of(samples_.begin(), samples_.end(), [](const EphemerisSample& s) {
        return s.hasCovarianceUpper;
    });
}

bool EphemerisPropagator::tryGetKeplerianElements(OrbitalElements& outElements) const
{
    if (keplerianElements_) {
        outElements = *keplerianElements_;
        return true;
    }
    return false;
}
