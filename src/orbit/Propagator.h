#pragma once

#include <array>
#include <chrono>

struct EciState
{
    std::array<double, 3> position{}; // same units as chosen for visualization (e.g. km)
    std::array<double, 3> velocity{}; // units per second
};

class Propagator
{
public:
    virtual ~Propagator() = default;

    // Propagate to an absolute time.
    virtual EciState propagate(std::chrono::system_clock::time_point t) const = 0;
};
