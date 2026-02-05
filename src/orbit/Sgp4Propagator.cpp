#include "Sgp4Propagator.h"

#include <cmath>

Sgp4Propagator::Sgp4Propagator(std::string line1, std::string line2)
    : line1_(std::move(line1))
    , line2_(std::move(line2))
{
}

EciState Sgp4Propagator::propagate(std::chrono::system_clock::time_point /*t*/) const
{
#if defined(ORBIT_MAPPER_SGP4_STUB)
    // Stub output: circular orbit in XY plane.
    // Replace this with a real SGP4 call once you hook a library in.
    EciState state;
    const double r = 3.0;
    state.position = {r, 0.0, 0.0};
    state.velocity = {0.0, 0.0, 0.0};
    return state;
#else
    // TODO: Implement using chosen SGP4 library.
    EciState state;
    return state;
#endif
}
