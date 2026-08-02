#pragma once

#include <optional>

#include <spectra/math3d.hpp>

namespace spectra::adapters::ros2
{

class MeasureTool
{
   public:
    bool active() const { return active_; }
    void set_active(bool v)
    {
        active_ = v;
        if (!active_)
            reset();
    }

    void reset()
    {
        click_count_ = 0;
        has_result_  = false;
    }

    bool   has_result() const { return has_result_; }
    double distance() const { return distance_; }

    // Call on left click in viewport with a world-space point on the ground plane.
    void click_point(const spectra::vec3& world);

   private:
    bool          active_{false};
    int           click_count_{0};
    spectra::vec3 first_{};
    spectra::vec3 second_{};
    double        distance_{0.0};
    bool          has_result_{false};
};

}   // namespace spectra::adapters::ros2
