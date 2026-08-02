#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "plugin_guard.hpp"
#include "plugin_manifest.hpp"

// Cross-platform symbol export macro for plugin entry points.
// Plugins must use SPECTRA_PLUGIN_API on spectra_plugin_init / spectra_plugin_shutdown.
#ifdef _WIN32
    #define SPECTRA_PLUGIN_API __declspec(dllexport)
#else
    #define SPECTRA_PLUGIN_API __attribute__((visibility("default")))
#endif

namespace spectra
{

class CommandRegistry;
class ShortcutManager;
class UndoManager;
class OverlayRegistry;
class ExportFormatRegistry;
class DataSourceRegistry;
class SeriesTypeRegistry;
class Backend;
class PluginUIRegistry;

// ─── Stable C ABI for plugins ────────────────────────────────────────────────
//
// Plugins are shared libraries (.so / .dll / .dylib) that export a single
// entry point: spectra_plugin_init(). The host calls this with a PluginContext
// that provides access to command registration, shortcuts, and undo.
//
// The C ABI ensures binary compatibility across compiler versions.

extern "C"
{
// Plugin API version — bump major on breaking changes.
#define SPECTRA_PLUGIN_API_VERSION_MAJOR 2
#define SPECTRA_PLUGIN_API_VERSION_MINOR 1

    // Opaque handles (pointers cast to void* for C ABI stability)
    typedef void* SpectraCommandRegistry;
    typedef void* SpectraShortcutManager;
    typedef void* SpectraUndoManager;
    typedef void* SpectraTransformRegistry;
    typedef void* SpectraOverlayRegistry;
    typedef void* SpectraExportFormatRegistry;
    typedef void* SpectraDataSourceRegistry;
    typedef void* SpectraSeriesTypeRegistry;

    // Forward-declared for use in SpectraPluginContext (full API in v2.1 section below)
    typedef void* SpectraPluginUIRegistry;

    // Plugin context passed to plugin_init.
    struct SpectraPluginContext
    {
        uint32_t               api_version_major;
        uint32_t               api_version_minor;
        SpectraCommandRegistry command_registry;
        SpectraShortcutManager shortcut_manager;
        SpectraUndoManager     undo_manager;

        // Added in API v1.1
        SpectraTransformRegistry transform_registry;   // nullptr for v1.0 plugins

        // Added in API v1.2
        SpectraOverlayRegistry overlay_registry;   // nullptr for v1.0/v1.1 plugins

        // Added in API v1.3
        SpectraExportFormatRegistry export_format_registry;   // nullptr for v1.0-v1.2 plugins
        SpectraDataSourceRegistry   data_source_registry;     // nullptr for v1.0-v1.2 plugins

        // Added in API v2.0
        SpectraSeriesTypeRegistry series_type_registry;   // nullptr for v1.x plugins
        void*                     backend_handle;         // opaque Backend* for C ABI wrappers

        // Added in API v2.1
        SpectraPluginUIRegistry   plugin_ui_registry;     // nullptr for v2.0 plugins
    };

    // Plugin info returned by plugin_init.
    struct SpectraPluginInfo
    {
        const char* name;                // Human-readable plugin name
        const char* version;             // Plugin version string
        const char* author;              // Author name
        const char* description;         // Short description
        uint32_t    api_version_major;   // API version the plugin was built against
        uint32_t    api_version_minor;
    };

    // C ABI functions for command registration
    typedef void (*SpectraCommandCallback)(void* user_data);

    struct SpectraCommandDesc
    {
        const char*            id;
        const char*            label;
        const char*            category;
        const char*            shortcut_hint;
        SpectraCommandCallback callback;
        void*                  user_data;
    };

    // ─── Transform callback signatures (added in API v1.1) ─────────────────────

    // Scalar transform: applied per Y element.
    typedef float (*SpectraTransformCallback)(float value, void* user_data);

    // XY transform: can change both axes and output length.
    typedef void (*SpectraXYTransformCallback)(const float* x_in,
                                               const float* y_in,
                                               size_t       count,
                                               float*       x_out,
                                               float*       y_out,
                                               size_t*      out_count,
                                               void*        user_data);

    // Plugin entry point signature.
    // Returns 0 on success, non-zero on failure.
    typedef int (*SpectraPluginInitFn)(const SpectraPluginContext* ctx,
                                       SpectraPluginInfo*          info_out);

    // Plugin cleanup signature (optional).
    typedef void (*SpectraPluginShutdownFn)(void);

    // ─── C ABI host functions (called by plugins) ────────────────────────────────

    // Register a command via C ABI.
    int spectra_register_command(SpectraCommandRegistry registry, const SpectraCommandDesc* desc);

    // Unregister a command via C ABI.
    int spectra_unregister_command(SpectraCommandRegistry registry, const char* command_id);

    // Execute a command via C ABI.
    int spectra_execute_command(SpectraCommandRegistry registry, const char* command_id);

    // Bind a shortcut via C ABI.
    int spectra_bind_shortcut(SpectraShortcutManager manager,
                              const char*            shortcut_str,
                              const char*            command_id);

    // Push an undo action via C ABI.
    int spectra_push_undo(SpectraUndoManager     manager,
                          const char*            description,
                          SpectraCommandCallback undo_fn,
                          void*                  undo_data,
                          SpectraCommandCallback redo_fn,
                          void*                  redo_data);

    // ─── Overlay context + drawing helpers (added in API v1.2) ─────────────────────

    // Context passed to overlay draw callbacks.
    struct SpectraOverlayContext
    {
        float    viewport_x;
        float    viewport_y;
        float    viewport_w;
        float    viewport_h;
        float    mouse_x;
        float    mouse_y;
        int      is_hovered;   // bool as int for C ABI
        uint32_t figure_id;
        int      axes_index;
        int      series_count;
        void*    reserved_;   // internal draw list — do not use from plugins
    };

    // Overlay draw callback signature.
    typedef void (*SpectraOverlayCallback)(const SpectraOverlayContext* ctx, void* user_data);

    // ─── Transform registration (added in API v1.1) ──────────────────────────────

    // Register a scalar transform (applied to each Y value independently).
    int spectra_register_transform(SpectraTransformRegistry registry,
                                   const char*              name,
                                   SpectraTransformCallback callback,
                                   void*                    user_data,
                                   const char*              description);

    // Register an XY transform (can change both X and Y arrays and their length).
    int spectra_register_xy_transform(SpectraTransformRegistry   registry,
                                      const char*                name,
                                      SpectraXYTransformCallback callback,
                                      void*                      user_data,
                                      const char*                description);

    // Unregister a scalar/XY transform previously registered by name.
    int spectra_unregister_transform(SpectraTransformRegistry registry, const char* name);

    // ─── Overlay registration (added in API v1.2) ────────────────────────────────

    // Register an overlay that will be drawn on every axes viewport.
    int spectra_register_overlay(SpectraOverlayRegistry registry,
                                 const char*            name,
                                 SpectraOverlayCallback callback,
                                 void*                  user_data);

    // Unregister a previously registered overlay.
    int spectra_unregister_overlay(SpectraOverlayRegistry registry, const char* name);

    // ─── Export format types + registration (added in API v1.3) ──────────────────

    // Export callback context — passed to the plugin's export function.
    struct SpectraExportContext
    {
        const char*    figure_json;       // JSON description of axes/series/labels/limits
        size_t         figure_json_len;   // Length of the JSON string
        const uint8_t* rgba_pixels;       // Raw RGBA pixel buffer (may be NULL)
        uint32_t       pixel_width;       // Pixel buffer width
        uint32_t       pixel_height;      // Pixel buffer height
        const char*    output_path;       // Destination file path
    };

    // Export callback signature.  Return 0 on success, non-zero on failure.
    typedef int (*SpectraExportCallback)(const SpectraExportContext* ctx, void* user_data);

    // Register a plugin-provided export format.
    int spectra_register_export_format(SpectraExportFormatRegistry registry,
                                       const char*                 name,
                                       const char*                 extension,
                                       SpectraExportCallback       callback,
                                       void*                       user_data);

    // Unregister a previously registered export format.
    int spectra_unregister_export_format(SpectraExportFormatRegistry registry, const char* name);

    // ─── Overlay drawing helpers (added in API v1.2) ─────────────────────────────
    // Plugins call these inside their overlay callback to draw shapes.
    // Using helpers avoids plugins linking ImGui directly.

    void spectra_overlay_draw_line(const SpectraOverlayContext* ctx,
                                   float                        x1,
                                   float                        y1,
                                   float                        x2,
                                   float                        y2,
                                   uint32_t                     color_rgba,
                                   float                        thickness);

    void spectra_overlay_draw_rect(const SpectraOverlayContext* ctx,
                                   float                        x,
                                   float                        y,
                                   float                        w,
                                   float                        h,
                                   uint32_t                     color_rgba,
                                   float                        thickness);

    void spectra_overlay_draw_rect_filled(const SpectraOverlayContext* ctx,
                                          float                        x,
                                          float                        y,
                                          float                        w,
                                          float                        h,
                                          uint32_t                     color_rgba);

    void spectra_overlay_draw_text(const SpectraOverlayContext* ctx,
                                   float                        x,
                                   float                        y,
                                   const char*                  text,
                                   uint32_t                     color_rgba);

    void spectra_overlay_draw_circle(const SpectraOverlayContext* ctx,
                                     float                        cx,
                                     float                        cy,
                                     float                        radius,
                                     uint32_t                     color_rgba,
                                     float                        thickness,
                                     int                          num_segments);

    void spectra_overlay_draw_circle_filled(const SpectraOverlayContext* ctx,
                                            float                        cx,
                                            float                        cy,
                                            float                        radius,
                                            uint32_t                     color_rgba,
                                            int                          num_segments);

    // ─── Data source adapter (added in API v1.3) ────────────────────────────────

    // A single data point returned by a data source poll callback.
    struct SpectraDataPoint
    {
        const char* series_label;   // Which series this point belongs to
        double      timestamp;      // Timestamp (seconds, source-defined epoch)
        float       value;          // Data value
    };

    // Data source callback signatures.
    typedef const char* (*SpectraDataSourceNameFn)(void* user_data);
    typedef void (*SpectraDataSourceStartFn)(void* user_data);
    typedef void (*SpectraDataSourceStopFn)(void* user_data);
    typedef int (*SpectraDataSourceIsRunningFn)(void* user_data);

    // Poll callback: write up to max_points into out_points, return actual count.
    typedef size_t (*SpectraDataSourcePollFn)(SpectraDataPoint* out_points,
                                              size_t            max_points,
                                              void*             user_data);

    // Optional UI callback — called inside the Data Sources panel.
    typedef void (*SpectraDataSourceBuildUIFn)(void* user_data);

    // Descriptor for registering a data source via C ABI.
    struct SpectraDataSourceDesc
    {
        const char*                  name;            // Human-readable name
        SpectraDataSourceNameFn      name_fn;         // Optional dynamic name (overrides name)
        SpectraDataSourceStartFn     start_fn;        // Required
        SpectraDataSourceStopFn      stop_fn;         // Required
        SpectraDataSourceIsRunningFn is_running_fn;   // Required
        SpectraDataSourcePollFn      poll_fn;         // Required
        SpectraDataSourceBuildUIFn   build_ui_fn;     // Optional (may be NULL)
        void*                        user_data;       // Passed to all callbacks
    };

    // Register a data source adapter that appears in the Data Sources panel.
    int spectra_register_data_source(SpectraDataSourceRegistry    registry,
                                     const SpectraDataSourceDesc* desc);

    // Unregister a data source by name.
    int spectra_unregister_data_source(SpectraDataSourceRegistry registry, const char* name);

    // ─── Custom series type plugin API (added in API v2.0) ──────────────────────

    // Opaque handles for custom series GPU interaction.
    typedef struct
    {
        uint64_t id;
    } SpectraBackendHandle;
    typedef struct
    {
        uint64_t id;
    } SpectraPipelineHandle;
    typedef struct
    {
        uint64_t id;
    } SpectraBufferHandle;

    // Vertex format values (match VkFormat).
    typedef enum SpectraVertexFormat
    {
        SPECTRA_FORMAT_R32_SFLOAT          = 100,
        SPECTRA_FORMAT_R32G32_SFLOAT       = 103,
        SPECTRA_FORMAT_R32G32B32_SFLOAT    = 106,
        SPECTRA_FORMAT_R32G32B32A32_SFLOAT = 109,
    } SpectraVertexFormat;

    typedef struct SpectraVertexBinding
    {
        uint32_t binding;
        uint32_t stride;
        uint32_t input_rate;   // 0 = per-vertex, 1 = per-instance
    } SpectraVertexBinding;

    typedef struct SpectraVertexAttribute
    {
        uint32_t location;
        uint32_t binding;
        uint32_t format;   // SpectraVertexFormat / VkFormat
        uint32_t offset;
    } SpectraVertexAttribute;

    typedef struct SpectraViewport
    {
        float x, y, width, height;
    } SpectraViewport;

    typedef struct SpectraRect
    {
        float x_min, x_max, y_min, y_max;
    } SpectraRect;

    // Stable C ABI mirror of SeriesPushConstants.
    typedef struct SpectraSeriesPushConst
    {
        float    color[4];
        float    line_width;
        float    point_size;
        float    data_offset_x;
        float    data_offset_y;
        uint32_t line_style;
        uint32_t marker_type;
        float    marker_size;
        float    opacity;
        float    dash_pattern[8];
        float    dash_total;
        int32_t  dash_count;
        float    _pad[2];
    } SpectraSeriesPushConst;

    // Series creation flags.
    typedef enum SpectraSeriesFlags
    {
        SPECTRA_SERIES_FLAG_NONE          = 0,
        SPECTRA_SERIES_FLAG_3D            = (1 << 0),
        SPECTRA_SERIES_FLAG_TRANSPARENT   = (1 << 1),
        SPECTRA_SERIES_FLAG_INDEXED       = (1 << 2),
        SPECTRA_SERIES_FLAG_INSTANCED     = (1 << 3),
        SPECTRA_SERIES_FLAG_BACKFACE_CULL = (1 << 4),
    } SpectraSeriesFlags;

    // Buffer usage for spectra_backend_create_buffer.
    typedef enum SpectraBufferUsage
    {
        SPECTRA_BUFFER_USAGE_VERTEX  = 0,
        SPECTRA_BUFFER_USAGE_INDEX   = 1,
        SPECTRA_BUFFER_USAGE_UNIFORM = 2,
        SPECTRA_BUFFER_USAGE_STORAGE = 3,
        SPECTRA_BUFFER_USAGE_STAGING = 4,
    } SpectraBufferUsage;

    // Custom series type descriptor.
    typedef struct SpectraSeriesTypeDesc
    {
        // Identity
        const char* type_name;
        uint32_t    flags;   // SpectraSeriesFlags bitmask

        // Pipeline shaders (pre-compiled SPIR-V)
        const uint8_t* vert_spirv;
        size_t         vert_spirv_size;
        const uint8_t* frag_spirv;
        size_t         frag_spirv_size;
        uint32_t       topology;   // VkPrimitiveTopology (0 = TRIANGLE_LIST)

        // Vertex input layout (NULL / 0 = SSBO-only)
        const SpectraVertexBinding*   vertex_bindings;
        uint32_t                      vertex_binding_count;
        const SpectraVertexAttribute* vertex_attributes;
        uint32_t                      vertex_attribute_count;

        // Upload callback — called when series data is dirty.
        int (*upload_fn)(SpectraBackendHandle backend,
                         const void*          series_data,
                         void*                gpu_state,
                         size_t               data_count,
                         void*                user_data);

        // Draw callback — called once per visible series per frame.
        int (*draw_fn)(SpectraBackendHandle          backend,
                       SpectraPipelineHandle         pipeline,
                       const void*                   gpu_state,
                       const SpectraViewport*        viewport,
                       const SpectraSeriesPushConst* push_constants,
                       void*                         user_data);

        // Bounds callback — compute bounding box for autoscale.
        int (*bounds_fn)(const void*  series_data,
                         size_t       data_count,
                         SpectraRect* bounds_out,
                         void*        user_data);

        // Optional cleanup callback — called when series is removed.
        void (*cleanup_fn)(SpectraBackendHandle backend, void* gpu_state, void* user_data);

        // Opaque pointer passed to all callbacks.
        void* user_data;
    } SpectraSeriesTypeDesc;

    // Register a custom series type.
    int spectra_register_series_type(void* registry, const SpectraSeriesTypeDesc* desc);

    // Unregister a custom series type by name.
    int spectra_unregister_series_type(void* registry, const char* type_name);

    // ─── Backend C ABI helpers (for use in upload_fn / draw_fn callbacks) ────

    SpectraBufferHandle spectra_backend_create_buffer(SpectraBackendHandle backend,
                                                      uint32_t             usage,
                                                      size_t               size_bytes);

    void spectra_backend_destroy_buffer(SpectraBackendHandle backend, SpectraBufferHandle buffer);

    void spectra_backend_upload_buffer(SpectraBackendHandle backend,
                                       SpectraBufferHandle  buffer,
                                       const void*          data,
                                       size_t               size_bytes,
                                       size_t               offset);

    void spectra_backend_bind_pipeline(SpectraBackendHandle  backend,
                                       SpectraPipelineHandle pipeline);

    void spectra_backend_bind_buffer(SpectraBackendHandle backend,
                                     SpectraBufferHandle  buffer,
                                     uint32_t             binding);

    void spectra_backend_bind_index_buffer(SpectraBackendHandle backend,
                                           SpectraBufferHandle  buffer);

    void spectra_backend_push_constants(SpectraBackendHandle          backend,
                                        const SpectraSeriesPushConst* pc);

    void spectra_backend_draw(SpectraBackendHandle backend,
                              uint32_t             vertex_count,
                              uint32_t             first_vertex);

    void spectra_backend_draw_instanced(SpectraBackendHandle backend,
                                        uint32_t             vertex_count,
                                        uint32_t             instance_count,
                                        uint32_t             first_vertex,
                                        uint32_t             first_instance);

    void spectra_backend_draw_indexed(SpectraBackendHandle backend,
                                      uint32_t             index_count,
                                      uint32_t             first_index,
                                      int32_t              vertex_offset);

    // ─── Portable plugin UI schema (added in API v2.1) ──────────────────────
    //
    // Plugins can register a versioned UI schema that both the Qt and ImGui
    // frontends can render without the plugin linking to either framework.
    //
    // The schema is a flat array of elements (properties, actions, labels,
    // separators, groups).  See plugin_ui_schema.hpp for the C++ types.

    // Property type values (match PluginUIPropertyType).
    typedef enum SpectraPluginUIPropertyType
    {
        SPECTRA_UI_PROP_BOOLEAN = 0,
        SPECTRA_UI_PROP_INTEGER = 1,
        SPECTRA_UI_PROP_FLOAT   = 2,
        SPECTRA_UI_PROP_STRING  = 3,
        SPECTRA_UI_PROP_ENUM    = 4,
        SPECTRA_UI_PROP_COLOR   = 5,
    } SpectraPluginUIPropertyType;

    // Element type values (match PluginUIElementType).
    typedef enum SpectraPluginUIElementType
    {
        SPECTRA_UI_ELEM_PROPERTY  = 0,
        SPECTRA_UI_ELEM_ACTION    = 1,
        SPECTRA_UI_ELEM_LABEL     = 2,
        SPECTRA_UI_ELEM_SEPARATOR = 3,
        SPECTRA_UI_ELEM_GROUP     = 4,
    } SpectraPluginUIElementType;

    // A single UI element descriptor (C ABI mirror of PluginUIElement).
    typedef struct SpectraPluginUIElementDesc
    {
        SpectraPluginUIElementType type;

        // Property fields (used when type == SPECTRA_UI_ELEM_PROPERTY)
        const char* prop_id;            // unique within schema
        const char* prop_label;
        SpectraPluginUIPropertyType prop_type;
        const char* prop_value;         // current value as string
        const char* prop_min;           // optional, may be NULL
        const char* prop_max;           // optional, may be NULL
        const char* prop_default;       // optional, may be NULL
        const char* const* enum_options; // array of strings, NULL-terminated if enum
        uint32_t    enum_count;
        int         prop_read_only;     // bool as int
        const char* prop_tooltip;       // optional, may be NULL

        // Action fields (used when type == SPECTRA_UI_ELEM_ACTION)
        const char* action_id;
        const char* action_label;
        const char* action_tooltip;     // optional, may be NULL
        int         action_enabled;     // bool as int

        // Label fields (used when type == SPECTRA_UI_ELEM_LABEL)
        const char* label_text;
        const char* label_style;        // optional, may be NULL

        // Group fields (used when type == SPECTRA_UI_ELEM_GROUP)
        const char* group_title;
        int         group_collapsed;    // bool as int
    } SpectraPluginUIElementDesc;

    // Schema descriptor passed to spectra_register_plugin_ui.
    typedef struct SpectraPluginUISchemaDesc
    {
        const char*                       plugin_name;
        const char*                       panel_title;
        const SpectraPluginUIElementDesc* elements;
        uint32_t                          element_count;
    } SpectraPluginUISchemaDesc;

    // Callback signatures for plugin UI interaction.
    typedef const char* (*SpectraPluginUIPropertyChangedFn)(
        const char* schema_id, const char* property_id,
        const char* new_value, void* user_data);
    typedef void (*SpectraPluginUIActionTriggeredFn)(
        const char* schema_id, const char* action_id, void* user_data);

    // Register a plugin UI schema.  Returns a schema ID string (owned by the
    // registry, valid until the schema is unregistered or the registry is
    // destroyed).  Returns NULL on failure.
    const char* spectra_register_plugin_ui(
        SpectraPluginUIRegistry              registry,
        const SpectraPluginUISchemaDesc*     schema,
        SpectraPluginUIPropertyChangedFn     on_changed,
        SpectraPluginUIActionTriggeredFn     on_action,
        void*                                user_data);

    // Unregister a plugin UI schema by plugin name.
    void spectra_unregister_plugin_ui(
        SpectraPluginUIRegistry registry, const char* plugin_name);

}   // extern "C"

// ─── C++ Plugin Manager ──────────────────────────────────────────────────────

// Represents a loaded plugin.
struct PluginEntry
{
    std::string       name;
    std::string       version;
    std::string       author;
    std::string       description;
    std::string       path;   // Path to the shared library
    bool              loaded  = false;
    bool              enabled = true;
    PluginDiagnostics diagnostics;              ///< Lifecycle diagnostics.
    uint32_t          api_version_minor  = 0;   // API minor version the plugin was built against
    void*             handle             = nullptr;   // dlopen/LoadLibrary handle
    SpectraPluginShutdownFn  shutdown_fn = nullptr;
    std::vector<std::string> registered_commands;   // Commands registered by this plugin
    std::vector<std::string> registered_transforms;   // Transforms registered by this plugin
    PluginManifest manifest;   // Parsed plugin.json manifest (may be empty if no manifest)
};

// Manages plugin lifecycle: discovery, loading, unloading.
// Thread-safe.
class PluginManager
{
   public:
    PluginManager() = default;
    ~PluginManager();

    PluginManager(const PluginManager&)            = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // Set the host services that plugins can access.
    void set_command_registry(CommandRegistry* reg) { registry_ = reg; }
    void set_shortcut_manager(ShortcutManager* mgr) { shortcut_mgr_ = mgr; }
    void set_undo_manager(UndoManager* undo) { undo_mgr_ = undo; }
    void set_transform_registry(class TransformRegistry* reg) { transform_reg_ = reg; }
    void set_overlay_registry(OverlayRegistry* reg) { overlay_reg_ = reg; }
    void set_export_format_registry(ExportFormatRegistry* reg) { export_format_reg_ = reg; }
    void set_data_source_registry(DataSourceRegistry* reg) { data_source_reg_ = reg; }
    void set_series_type_registry(SeriesTypeRegistry* reg) { series_type_reg_ = reg; }
    void set_backend(Backend* backend) { backend_ = backend; }
    void set_plugin_ui_registry(PluginUIRegistry* reg) { plugin_ui_reg_ = reg; }

    // Load a plugin from a shared library path.
    // Returns true on success.
    bool load_plugin(const std::string& path);

    // Unload a plugin by name. Calls shutdown, unregisters commands.
    bool unload_plugin(const std::string& name);

    // Unload all plugins.
    void unload_all();

    // Get list of loaded plugins.
    std::vector<PluginEntry> plugins() const;

    // Get a plugin by name.
    [[nodiscard]] const PluginEntry* find_plugin(const std::string& name) const;

    // Get diagnostics for a plugin by name. Returns nullptr if not found.
    [[nodiscard]] const PluginDiagnostics* diagnostics(const std::string& name) const;

    // Number of loaded plugins.
    size_t plugin_count() const;

    // Enable/disable a plugin (disabled plugins' commands are disabled).
    void set_plugin_enabled(const std::string& name, bool enabled);

    // Discover plugins in a directory (scans for .so/.dll/.dylib files).
    std::vector<std::string> discover(const std::string& directory) const;

    // Default plugin directory (~/.config/spectra/plugins/).
    static std::string default_plugin_dir();

    // Serialize plugin state (enabled/disabled) to JSON.
    std::string serialize_state() const;

    // Deserialize plugin state from JSON.
    bool deserialize_state(const std::string& json);

   private:
    CommandRegistry*         registry_          = nullptr;
    ShortcutManager*         shortcut_mgr_      = nullptr;
    UndoManager*             undo_mgr_          = nullptr;
    TransformRegistry*       transform_reg_     = nullptr;
    OverlayRegistry*         overlay_reg_       = nullptr;
    ExportFormatRegistry*    export_format_reg_ = nullptr;
    DataSourceRegistry*      data_source_reg_   = nullptr;
    SeriesTypeRegistry*      series_type_reg_   = nullptr;
    PluginUIRegistry*        plugin_ui_reg_     = nullptr;
    Backend*                 backend_           = nullptr;
    mutable std::mutex       mutex_;
    std::vector<PluginEntry> plugins_;

    // Build a plugin context gated to the given minor version.
    // Fields added after that version are set to nullptr.
    SpectraPluginContext make_context(uint32_t minor_version) const;
};

}   // namespace spectra
