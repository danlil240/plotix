#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "display_plugin.hpp"

namespace spectra::adapters::ros2
{

struct DisplayTypeInfo
{
    std::string              type_id;
    std::string              display_name;
    std::string              icon;
    std::vector<std::string> compatible_types;
};

class DisplayRegistry
{
   public:
    template <typename T>
    bool register_display()
    {
        auto            probe = std::make_unique<T>();
        DisplayTypeInfo info{
            probe->type_id(),
            probe->display_name(),
            probe->icon(),
            probe->compatible_message_types(),
        };
        return register_factory(info, []() { return std::make_unique<T>(); });
    }

    bool                           register_factory(const DisplayTypeInfo&                          info,
                                                    std::function<std::unique_ptr<DisplayPlugin>()> factory);
    std::unique_ptr<DisplayPlugin> create(const std::string& type_id) const;
    std::vector<DisplayTypeInfo>   list_types() const;

   private:
    std::vector<DisplayTypeInfo>                                                     ordered_types_;
    std::unordered_map<std::string, std::function<std::unique_ptr<DisplayPlugin>()>> factories_;
};

}   // namespace spectra::adapters::ros2