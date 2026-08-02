#include "resources/ros_package_resolver.hpp"

#include <filesystem>

#ifdef SPECTRA_USE_ROS2
    #include <ament_index_cpp/get_package_share_directory.hpp>
#endif

namespace spectra::adapters::ros2
{

PackageResolveResult resolve_ros_resource_uri(const std::string& uri)
{
    PackageResolveResult result;
    if (uri.empty())
    {
        result.error = "Empty URI";
        return result;
    }

    if (uri.rfind("file://", 0) == 0)
    {
        result.path = uri.substr(7);
        result.ok   = std::filesystem::exists(result.path);
        if (!result.ok)
            result.error = "File not found: " + result.path;
        return result;
    }

    if (uri.rfind("package://", 0) != 0)
    {
        result.path = uri;
        result.ok   = std::filesystem::exists(result.path);
        if (!result.ok)
            result.error = "Path not found: " + uri;
        return result;
    }

#ifdef SPECTRA_USE_ROS2
    const std::string rest  = uri.substr(10);
    const auto        slash = rest.find('/');
    if (slash == std::string::npos)
    {
        result.error = "Invalid package URI (missing path): " + uri;
        return result;
    }

    const std::string package = rest.substr(0, slash);
    const std::string rel     = rest.substr(slash + 1);
    try
    {
        const std::string share = ament_index_cpp::get_package_share_directory(package);
        result.path             = (std::filesystem::path(share) / rel).string();
        result.ok               = std::filesystem::exists(result.path);
        if (!result.ok)
            result.error = "Resolved path missing: " + result.path;
    }
    catch (const std::exception& ex)
    {
        result.error = std::string("Package lookup failed: ") + ex.what();
    }
#else
    result.error = "ROS2 not available";
#endif
    return result;
}

}   // namespace spectra::adapters::ros2
