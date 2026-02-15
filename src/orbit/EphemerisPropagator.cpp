#include "EphemerisPropagator.h"

#include <algorithm>
#include <cmath>

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
