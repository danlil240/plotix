#pragma once

#include <string>

namespace spectra::adapters::ros2
{

class SceneManager;

class InspectorPanel
{
   public:
    void               set_title(const std::string& title) { title_ = title; }
    const std::string& title() const { return title_; }

    void draw(bool*              p_open,
              SceneManager&      scene,
              const std::string& fixed_frame   = {},
              size_t             display_count = 0);

   private:
    std::string title_{"Inspector"};
};

}   // namespace spectra::adapters::ros2
