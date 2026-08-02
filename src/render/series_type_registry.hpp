#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "backend.hpp"

namespace spectra
{

// Callback signatures for custom series type plugins (C++ wrappers).
using CustomUploadFn = std::function<
    int(Backend& backend, const void* series_data, void* gpu_state, size_t data_count)>;
using CustomDrawFn    = std::function<int(Backend&                   backend,
                                       PipelineHandle             pipeline,
                                       const void*                gpu_state,
                                       const float*               viewport_xywh,
                                       const SeriesPushConstants& push_constants)>;
using CustomBoundsFn  = std::function<int(const void* series_data,
                                         size_t      data_count,
                                         float*      bounds_out)>;   // [xmin, xmax, ymin, ymax]
using CustomCleanupFn = std::function<void(Backend& backend, void* gpu_state)>;

/// An entry in the series type registry (internal storage).
struct SeriesTypeEntry
{
    std::string        type_name;
    CustomPipelineDesc pipeline_desc;
    PipelineHandle     pipeline;   // Set by create_pipelines()
    CustomUploadFn     upload_fn;
    CustomDrawFn       draw_fn;
    CustomBoundsFn     bounds_fn;
    CustomCleanupFn    cleanup_fn;   // May be empty

    // Persistent copy of SPIR-V bytecode (the registration pointer may not outlive the plugin init)
    std::vector<uint8_t> vert_spirv_storage;
    std::vector<uint8_t> frag_spirv_storage;

    bool faulted = false;   // Set true on crash/exception to skip future invocations
};

/// Registry for plugin-defined custom series types.
/// Thread-safe for registration; pipeline creation happens on the render thread.
class SeriesTypeRegistry
{
   public:
    SeriesTypeRegistry()  = default;
    ~SeriesTypeRegistry() = default;

    SeriesTypeRegistry(const SeriesTypeRegistry&)            = delete;
    SeriesTypeRegistry& operator=(const SeriesTypeRegistry&) = delete;

    /// Register a custom series type.  Stores the descriptor and callbacks.
    /// The pipeline is not created until create_pipelines() is called.
    void register_type(const std::string&        type_name,
                       const CustomPipelineDesc& pipeline_desc,
                       CustomUploadFn            upload_fn,
                       CustomDrawFn              draw_fn,
                       CustomBoundsFn            bounds_fn,
                       CustomCleanupFn           cleanup_fn = {});

    /// Remove a custom series type by name.
    void unregister_type(const std::string& name);

    /// Look up an entry by name.  Returns nullptr if not found.
    const SeriesTypeEntry* find(const std::string& name) const;

    /// Mutable lookup (needed by Renderer for pipeline handle assignment).
    SeriesTypeEntry* find_mut(const std::string& name);

    /// Create GPU pipelines for all registered custom types.
    /// Called from Renderer::init() and on swapchain recreation.
    void create_pipelines(Backend& backend);

    /// Destroy all custom pipelines.  Called on shutdown or swapchain recreation.
    void destroy_pipelines(Backend& backend);

    /// List all registered type names.
    std::vector<std::string> type_names() const;

    /// Number of registered custom types.
    size_t count() const;

   private:
    mutable std::mutex           mutex_;
    std::vector<SeriesTypeEntry> types_;
};

}   // namespace spectra
