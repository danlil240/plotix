#include "plugin_api.hpp"

#include "plugin_ui_schema.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "adapters/data_source_registry.hpp"
#include "io/export_registry.hpp"
#include "math/data_transform.hpp"
#include "render/backend.hpp"
#include "render/series_type_registry.hpp"
#include "spectra/logger.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/overlay/overlay_registry.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace spectra
{

// ─── C ABI host functions ────────────────────────────────────────────────────

extern "C"
{
    int spectra_register_command(SpectraCommandRegistry registry, const SpectraCommandDesc* desc)
    {
        if (!registry || !desc || !desc->id || !desc->label)
            return -1;

        auto*   reg = static_cast<CommandRegistry*>(registry);
        Command cmd;
        cmd.id       = desc->id;
        cmd.label    = desc->label;
        cmd.category = desc->category ? desc->category : "Plugin";
        cmd.shortcut = desc->shortcut_hint ? desc->shortcut_hint : "";

        // Wrap the C callback in a std::function, capturing user_data
        auto cb = desc->callback;
        auto ud = desc->user_data;
        if (cb)
        {
            cmd.callback = [cb, ud]() { cb(ud); };
        }

        reg->register_command(std::move(cmd));
        return 0;
    }

    int spectra_unregister_command(SpectraCommandRegistry registry, const char* command_id)
    {
        if (!registry || !command_id)
            return -1;
        auto* reg = static_cast<CommandRegistry*>(registry);
        reg->unregister_command(command_id);
        return 0;
    }

    int spectra_execute_command(SpectraCommandRegistry registry, const char* command_id)
    {
        if (!registry || !command_id)
            return -1;
        auto* reg = static_cast<CommandRegistry*>(registry);
        return reg->execute(command_id) ? 0 : -1;
    }

    int spectra_bind_shortcut(SpectraShortcutManager manager,
                              const char*            shortcut_str,
                              const char*            command_id)
    {
        if (!manager || !shortcut_str || !command_id)
            return -1;
        auto*    mgr = static_cast<ShortcutManager*>(manager);
        Shortcut sc  = Shortcut::from_string(shortcut_str);
        if (!sc.valid())
            return -1;
        mgr->bind(sc, command_id);
        return 0;
    }

    int spectra_push_undo(SpectraUndoManager     manager,
                          const char*            description,
                          SpectraCommandCallback undo_fn,
                          void*                  undo_data,
                          SpectraCommandCallback redo_fn,
                          void*                  redo_data)
    {
        if (!manager || !description)
            return -1;
        auto* undo = static_cast<UndoManager*>(manager);

        UndoAction action;
        action.description = description;
        if (undo_fn)
        {
            action.undo_fn = [undo_fn, undo_data]() { undo_fn(undo_data); };
        }
        if (redo_fn)
        {
            action.redo_fn = [redo_fn, redo_data]() { redo_fn(redo_data); };
        }
        undo->push(std::move(action));
        return 0;
    }

    // ─── Transform registration (API v1.1) ───────────────────────────────────

    int spectra_register_transform(SpectraTransformRegistry registry,
                                   const char*              name,
                                   SpectraTransformCallback callback,
                                   void*                    user_data,
                                   const char*              description)
    {
        if (!registry || !name || !callback)
            return -1;

        auto* reg = static_cast<TransformRegistry*>(registry);
        auto  cb  = callback;
        auto  ud  = user_data;

        reg->register_transform(
            name,
            [cb, ud](float v) { return cb(v, ud); },
            description ? description : "");
        return 0;
    }

    int spectra_register_xy_transform(SpectraTransformRegistry   registry,
                                      const char*                name,
                                      SpectraXYTransformCallback callback,
                                      void*                      user_data,
                                      const char*                description)
    {
        if (!registry || !name || !callback)
            return -1;

        auto* reg = static_cast<TransformRegistry*>(registry);
        auto  cb  = callback;
        auto  ud  = user_data;

        reg->register_xy_transform(
            name,
            [cb, ud](std::span<const float> x_in,
                     std::span<const float> y_in,
                     std::vector<float>&    x_out,
                     std::vector<float>&    y_out)
            {
                size_t count     = x_in.size();
                size_t out_count = count;
                x_out.resize(count);
                y_out.resize(count);
                cb(x_in.data(), y_in.data(), count, x_out.data(), y_out.data(), &out_count, ud);
                x_out.resize(out_count);
                y_out.resize(out_count);
            },
            description ? description : "");
        return 0;
    }

    int spectra_unregister_transform(SpectraTransformRegistry registry, const char* name)
    {
        if (!registry || !name)
            return -1;

        auto* reg = static_cast<TransformRegistry*>(registry);
        reg->unregister_transform(name);
        return 0;
    }

    // ─── Overlay registration (API v1.2) ──────────────────────────────────────

    int spectra_register_overlay(SpectraOverlayRegistry registry,
                                 const char*            name,
                                 SpectraOverlayCallback callback,
                                 void*                  user_data)
    {
        if (!registry || !name || !callback)
            return -1;

        auto* reg = static_cast<OverlayRegistry*>(registry);
        auto  cb  = callback;
        auto  ud  = user_data;

        reg->register_overlay(name,
                              [cb, ud](const OverlayDrawContext& draw_ctx)
                              {
                                  SpectraOverlayContext c{};
                                  c.viewport_x   = draw_ctx.viewport_x;
                                  c.viewport_y   = draw_ctx.viewport_y;
                                  c.viewport_w   = draw_ctx.viewport_w;
                                  c.viewport_h   = draw_ctx.viewport_h;
                                  c.mouse_x      = draw_ctx.mouse_x;
                                  c.mouse_y      = draw_ctx.mouse_y;
                                  c.is_hovered   = draw_ctx.is_hovered ? 1 : 0;
                                  c.figure_id    = draw_ctx.figure_id;
                                  c.axes_index   = draw_ctx.axes_index;
                                  c.series_count = draw_ctx.series_count;
                                  c.reserved_    = draw_ctx.draw_list;
                                  cb(&c, ud);
                              });
        return 0;
    }

    int spectra_unregister_overlay(SpectraOverlayRegistry registry, const char* name)
    {
        if (!registry || !name)
            return -1;
        auto* reg = static_cast<OverlayRegistry*>(registry);
        reg->unregister_overlay(name);
        return 0;
    }

    // ─── Overlay drawing helpers (API v1.2) ──────────────────────────────────

    void spectra_overlay_draw_line(const SpectraOverlayContext* ctx,
                                   float                        x1,
                                   float                        y1,
                                   float                        x2,
                                   float                        y2,
                                   uint32_t                     color_rgba,
                                   float                        thickness)
    {
#ifdef SPECTRA_USE_IMGUI
        if (!ctx || !ctx->reserved_)
            return;
        auto* dl = static_cast<ImDrawList*>(ctx->reserved_);
        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color_rgba, thickness);
#else
        (void)ctx;
        (void)x1;
        (void)y1;
        (void)x2;
        (void)y2;
        (void)color_rgba;
        (void)thickness;
#endif
    }

    void spectra_overlay_draw_rect(const SpectraOverlayContext* ctx,
                                   float                        x,
                                   float                        y,
                                   float                        w,
                                   float                        h,
                                   uint32_t                     color_rgba,
                                   float                        thickness)
    {
#ifdef SPECTRA_USE_IMGUI
        if (!ctx || !ctx->reserved_)
            return;
        auto* dl = static_cast<ImDrawList*>(ctx->reserved_);
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color_rgba, 0.0f, 0, thickness);
#else
        (void)ctx;
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        (void)color_rgba;
        (void)thickness;
#endif
    }

    void spectra_overlay_draw_rect_filled(const SpectraOverlayContext* ctx,
                                          float                        x,
                                          float                        y,
                                          float                        w,
                                          float                        h,
                                          uint32_t                     color_rgba)
    {
#ifdef SPECTRA_USE_IMGUI
        if (!ctx || !ctx->reserved_)
            return;
        auto* dl = static_cast<ImDrawList*>(ctx->reserved_);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), color_rgba);
#else
        (void)ctx;
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        (void)color_rgba;
#endif
    }

    void spectra_overlay_draw_text(const SpectraOverlayContext* ctx,
                                   float                        x,
                                   float                        y,
                                   const char*                  text,
                                   uint32_t                     color_rgba)
    {
#ifdef SPECTRA_USE_IMGUI
        if (!ctx || !ctx->reserved_ || !text)
            return;
        auto* dl = static_cast<ImDrawList*>(ctx->reserved_);
        dl->AddText(ImVec2(x, y), color_rgba, text);
#else
        (void)ctx;
        (void)x;
        (void)y;
        (void)text;
        (void)color_rgba;
#endif
    }

    void spectra_overlay_draw_circle(const SpectraOverlayContext* ctx,
                                     float                        cx,
                                     float                        cy,
                                     float                        radius,
                                     uint32_t                     color_rgba,
                                     float                        thickness,
                                     int                          num_segments)
    {
#ifdef SPECTRA_USE_IMGUI
        if (!ctx || !ctx->reserved_)
            return;
        auto* dl = static_cast<ImDrawList*>(ctx->reserved_);
        dl->AddCircle(ImVec2(cx, cy), radius, color_rgba, num_segments, thickness);
#else
        (void)ctx;
        (void)cx;
        (void)cy;
        (void)radius;
        (void)color_rgba;
        (void)thickness;
        (void)num_segments;
#endif
    }

    void spectra_overlay_draw_circle_filled(const SpectraOverlayContext* ctx,
                                            float                        cx,
                                            float                        cy,
                                            float                        radius,
                                            uint32_t                     color_rgba,
                                            int                          num_segments)
    {
#ifdef SPECTRA_USE_IMGUI
        if (!ctx || !ctx->reserved_)
            return;
        auto* dl = static_cast<ImDrawList*>(ctx->reserved_);
        dl->AddCircleFilled(ImVec2(cx, cy), radius, color_rgba, num_segments);
#else
        (void)ctx;
        (void)cx;
        (void)cy;
        (void)radius;
        (void)color_rgba;
        (void)num_segments;
#endif
    }

    // ─── Data source registration (API v1.3) ─────────────────────────────────

    // ─── Export format registration (API v1.3) ────────────────────────────────

    int spectra_register_export_format(SpectraExportFormatRegistry registry,
                                       const char*                 name,
                                       const char*                 extension,
                                       SpectraExportCallback       callback,
                                       void*                       user_data)
    {
        if (!registry || !name || !extension || !callback)
            return -1;

        auto* reg = static_cast<ExportFormatRegistry*>(registry);
        auto  cb  = callback;
        auto  ud  = user_data;

        reg->register_format(name,
                             extension,
                             [cb, ud](const ExportContext& ctx) -> bool
                             {
                                 SpectraExportContext c{};
                                 c.figure_json     = ctx.figure_json;
                                 c.figure_json_len = ctx.figure_json_len;
                                 c.rgba_pixels     = ctx.rgba_pixels;
                                 c.pixel_width     = ctx.pixel_width;
                                 c.pixel_height    = ctx.pixel_height;
                                 c.output_path     = ctx.output_path;
                                 return cb(&c, ud) == 0;
                             });
        return 0;
    }

    int spectra_unregister_export_format(SpectraExportFormatRegistry registry, const char* name)
    {
        if (!registry || !name)
            return -1;
        auto* reg = static_cast<ExportFormatRegistry*>(registry);
        reg->unregister_format(name);
        return 0;
    }

    // Thin C-callback-driven DataSourceAdapter implementation.
    class CDataSourceAdapter : public DataSourceAdapter
    {
       public:
        explicit CDataSourceAdapter(const SpectraDataSourceDesc& desc) : desc_(desc) {}

        [[nodiscard]] const char* name() const override
        {
            if (desc_.name_fn)
                return desc_.name_fn(desc_.user_data);
            return desc_.name ? desc_.name : "Unknown";
        }

        void start() override
        {
            if (desc_.start_fn)
                desc_.start_fn(desc_.user_data);
        }

        void stop() override
        {
            if (desc_.stop_fn)
                desc_.stop_fn(desc_.user_data);
        }

        [[nodiscard]] bool is_running() const override
        {
            if (desc_.is_running_fn)
                return desc_.is_running_fn(desc_.user_data) != 0;
            return false;
        }

        std::vector<DataPoint> poll() override
        {
            std::vector<DataPoint> out;
            if (!desc_.poll_fn)
                return out;

            // Use a small stack buffer; plugins write up to max_points.
            constexpr size_t kBatch = 256;
            SpectraDataPoint buf[kBatch];
            size_t           n = desc_.poll_fn(buf, kBatch, desc_.user_data);
            if (n > kBatch)
                n = kBatch;

            out.reserve(n);
            for (size_t i = 0; i < n; ++i)
            {
                DataPoint dp;
                dp.series_label = buf[i].series_label ? buf[i].series_label : "";
                dp.timestamp    = buf[i].timestamp;
                dp.value        = buf[i].value;
                out.push_back(std::move(dp));
            }
            return out;
        }

        void build_ui() override
        {
            if (desc_.build_ui_fn)
                desc_.build_ui_fn(desc_.user_data);
        }

       private:
        SpectraDataSourceDesc desc_;
    };

    int spectra_register_data_source(SpectraDataSourceRegistry    registry,
                                     const SpectraDataSourceDesc* desc)
    {
        if (!registry || !desc)
            return -1;
        if (!desc->start_fn || !desc->stop_fn || !desc->is_running_fn || !desc->poll_fn)
            return -1;
        if (!desc->name && !desc->name_fn)
            return -1;

        auto* reg     = static_cast<DataSourceRegistry*>(registry);
        auto  adapter = std::make_unique<CDataSourceAdapter>(*desc);
        reg->register_source(std::move(adapter));
        return 0;
    }

    int spectra_unregister_data_source(SpectraDataSourceRegistry registry, const char* name)
    {
        if (!registry || !name)
            return -1;
        auto* reg = static_cast<DataSourceRegistry*>(registry);
        reg->unregister_source(name);
        return 0;
    }

    // ─── Custom series type registration (API v2.0) ──────────────────────────

    int spectra_register_series_type(void* registry, const SpectraSeriesTypeDesc* desc)
    {
        if (!registry || !desc || !desc->type_name)
            return -1;
        if (!desc->upload_fn || !desc->draw_fn || !desc->bounds_fn)
            return -1;

        auto* reg = static_cast<SeriesTypeRegistry*>(registry);

        // Build CustomPipelineDesc from the C ABI descriptor.
        CustomPipelineDesc pd;
        pd.vert_spirv           = desc->vert_spirv;
        pd.vert_spirv_size      = desc->vert_spirv_size;
        pd.frag_spirv           = desc->frag_spirv;
        pd.frag_spirv_size      = desc->frag_spirv_size;
        pd.topology             = desc->topology;
        pd.enable_depth_test    = (desc->flags & SPECTRA_SERIES_FLAG_3D) != 0;
        pd.enable_depth_write   = (desc->flags & SPECTRA_SERIES_FLAG_3D) != 0
                                  && (desc->flags & SPECTRA_SERIES_FLAG_TRANSPARENT) == 0;
        pd.enable_backface_cull = (desc->flags & SPECTRA_SERIES_FLAG_BACKFACE_CULL) != 0;
        pd.enable_blending      = true;

        // Convert vertex bindings/attributes via the pointer-compatible layout.
        static_assert(sizeof(CustomPipelineDesc::VertexBinding) == sizeof(SpectraVertexBinding));
        static_assert(sizeof(CustomPipelineDesc::VertexAttribute)
                      == sizeof(SpectraVertexAttribute));
        pd.vertex_bindings =
            reinterpret_cast<const CustomPipelineDesc::VertexBinding*>(desc->vertex_bindings);
        pd.vertex_binding_count = desc->vertex_binding_count;
        pd.vertex_attributes =
            reinterpret_cast<const CustomPipelineDesc::VertexAttribute*>(desc->vertex_attributes);
        pd.vertex_attribute_count = desc->vertex_attribute_count;

        // Capture C callbacks + user_data into std::function wrappers.
        auto upload_cb  = desc->upload_fn;
        auto draw_cb    = desc->draw_fn;
        auto bounds_cb  = desc->bounds_fn;
        auto cleanup_cb = desc->cleanup_fn;
        auto ud         = desc->user_data;

        auto upload_fn =
            [upload_cb,
             ud](Backend& backend, const void* data, void* gpu_state, size_t count) -> int
        {
            SpectraBackendHandle bh;
            bh.id = reinterpret_cast<uint64_t>(&backend);
            return upload_cb(bh, data, gpu_state, count, ud);
        };

        auto draw_fn = [draw_cb, ud](Backend&                   backend,
                                     PipelineHandle             pipeline,
                                     const void*                gpu_state,
                                     const float*               viewport_xywh,
                                     const SeriesPushConstants& pc) -> int
        {
            SpectraBackendHandle bh;
            bh.id = reinterpret_cast<uint64_t>(&backend);
            SpectraPipelineHandle ph;
            ph.id = pipeline.id;
            SpectraViewport vp;
            vp.x      = viewport_xywh[0];
            vp.y      = viewport_xywh[1];
            vp.width  = viewport_xywh[2];
            vp.height = viewport_xywh[3];

            // Copy push constants to C ABI layout.
            static_assert(sizeof(SpectraSeriesPushConst) == sizeof(SeriesPushConstants),
                          "Push constant layout mismatch");
            auto cpc = std::bit_cast<SpectraSeriesPushConst>(pc);

            return draw_cb(bh, ph, gpu_state, &vp, &cpc, ud);
        };

        CustomBoundsFn bounds_fn;
        if (bounds_cb)
        {
            bounds_fn = [bounds_cb, ud](const void* data, size_t count, float* bounds_out) -> int
            {
                SpectraRect rect;
                int         result = bounds_cb(data, count, &rect, ud);
                if (result == 0 && bounds_out)
                {
                    bounds_out[0] = rect.x_min;
                    bounds_out[1] = rect.x_max;
                    bounds_out[2] = rect.y_min;
                    bounds_out[3] = rect.y_max;
                }
                return result;
            };
        }

        CustomCleanupFn cleanup_fn;
        if (cleanup_cb)
        {
            cleanup_fn = [cleanup_cb, ud](Backend& backend, void* gpu_state)
            {
                SpectraBackendHandle bh;
                bh.id = reinterpret_cast<uint64_t>(&backend);
                cleanup_cb(bh, gpu_state, ud);
            };
        }

        reg->register_type(desc->type_name,
                           pd,
                           std::move(upload_fn),
                           std::move(draw_fn),
                           std::move(bounds_fn),
                           std::move(cleanup_fn));
        return 0;
    }

    int spectra_unregister_series_type(void* registry, const char* type_name)
    {
        if (!registry || !type_name)
            return -1;
        auto* reg = static_cast<SeriesTypeRegistry*>(registry);
        reg->unregister_type(type_name);
        return 0;
    }

    // ─── Backend C ABI helpers (API v2.0) ────────────────────────────────────

    SpectraBufferHandle spectra_backend_create_buffer(SpectraBackendHandle backend,
                                                      uint32_t             usage,
                                                      size_t               size_bytes)
    {
        auto* b = reinterpret_cast<Backend*>(backend.id);
        auto  u = static_cast<BufferUsage>(usage);
        auto  h = b->create_buffer(u, size_bytes);
        return SpectraBufferHandle{h.id};
    }

    void spectra_backend_destroy_buffer(SpectraBackendHandle backend, SpectraBufferHandle buffer)
    {
        auto* b = reinterpret_cast<Backend*>(backend.id);
        b->destroy_buffer(BufferHandle{buffer.id});
    }

    void spectra_backend_upload_buffer(SpectraBackendHandle backend,
                                       SpectraBufferHandle  buffer,
                                       const void*          data,
                                       size_t               size_bytes,
                                       size_t               offset)
    {
        auto* b = reinterpret_cast<Backend*>(backend.id);
        b->upload_buffer(BufferHandle{buffer.id}, data, size_bytes, offset);
    }

    void spectra_backend_bind_pipeline(SpectraBackendHandle backend, SpectraPipelineHandle pipeline)
    {
        auto* b = reinterpret_cast<Backend*>(backend.id);
        b->bind_pipeline(PipelineHandle{pipeline.id});
    }

    void spectra_backend_bind_buffer(SpectraBackendHandle backend,
                                     SpectraBufferHandle  buffer,
                                     uint32_t             binding)
    {
        auto* b = reinterpret_cast<Backend*>(backend.id);
        b->bind_buffer(BufferHandle{buffer.id}, binding);
    }

    void spectra_backend_bind_index_buffer(SpectraBackendHandle backend, SpectraBufferHandle buffer)
    {
        auto* b = reinterpret_cast<Backend*>(backend.id);
        b->bind_index_buffer(BufferHandle{buffer.id});
    }

    void spectra_backend_push_constants(SpectraBackendHandle          backend,
                                        const SpectraSeriesPushConst* pc)
    {
        if (!pc)
            return;
        auto*               b = reinterpret_cast<Backend*>(backend.id);
        SeriesPushConstants spc;
        static_assert(sizeof(SpectraSeriesPushConst) == sizeof(SeriesPushConstants),
                      "Push constant layout mismatch");
        spc = std::bit_cast<SeriesPushConstants>(*pc);
        b->push_constants(spc);
    }

    void spectra_backend_draw(SpectraBackendHandle backend,
                              uint32_t             vertex_count,
                              uint32_t             first_vertex)
    {
        auto* b = reinterpret_cast<Backend*>(backend.id);
        b->draw(vertex_count, first_vertex);
    }

    void spectra_backend_draw_instanced(SpectraBackendHandle backend,
                                        uint32_t             vertex_count,
                                        uint32_t             instance_count,
                                        uint32_t             first_vertex,
                                        uint32_t             first_instance)
    {
        auto* b = reinterpret_cast<Backend*>(backend.id);
        b->draw_instanced(vertex_count, instance_count, first_vertex, first_instance);
    }

    void spectra_backend_draw_indexed(SpectraBackendHandle backend,
                                      uint32_t             index_count,
                                      uint32_t             first_index,
                                      int32_t              vertex_offset)
    {
        auto* b = reinterpret_cast<Backend*>(backend.id);
        b->draw_indexed(index_count, first_index, vertex_offset);
    }

    // ─── Portable plugin UI schema C ABI (API v2.1) ──────────────────────────

    // Thread-local storage for the last schema ID returned by register_plugin_ui.
    // The C ABI returns a const char* that the caller must not free, so we
    // keep it in a thread-local std::string that stays valid until the next call.
    static thread_local std::string tl_last_schema_id;

    const char* spectra_register_plugin_ui(
        SpectraPluginUIRegistry              registry,
        const SpectraPluginUISchemaDesc*     schema_desc,
        SpectraPluginUIPropertyChangedFn     on_changed,
        SpectraPluginUIActionTriggeredFn     on_action,
        void*                                user_data)
    {
        if (!registry || !schema_desc || !schema_desc->plugin_name || !schema_desc->panel_title)
            return nullptr;

        auto* reg = static_cast<PluginUIRegistry*>(registry);

        PluginUISchema schema;
        schema.plugin_name = schema_desc->plugin_name;
        schema.panel_title = schema_desc->panel_title;
        schema.version     = 1;

        for (uint32_t i = 0; i < schema_desc->element_count; ++i)
        {
            const auto& src = schema_desc->elements[i];
            PluginUIElement elem;

            switch (src.type)
            {
                case SPECTRA_UI_ELEM_PROPERTY:
                {
                    elem.type = PluginUIElementType::Property;
                    elem.property.id    = src.prop_id ? src.prop_id : "";
                    elem.property.label = src.prop_label ? src.prop_label : "";
                    elem.property.type  = static_cast<PluginUIPropertyType>(src.prop_type);
                    elem.property.value = src.prop_value ? src.prop_value : "";
                    elem.property.min_value = src.prop_min ? src.prop_min : "";
                    elem.property.max_value = src.prop_max ? src.prop_max : "";
                    elem.property.default_value = src.prop_default ? src.prop_default : "";
                    elem.property.read_only = src.prop_read_only != 0;
                    elem.property.tooltip = src.prop_tooltip ? src.prop_tooltip : "";
                    for (uint32_t j = 0; j < src.enum_count && src.enum_options; ++j)
                    {
                        if (src.enum_options[j])
                            elem.property.enum_options.push_back(src.enum_options[j]);
                    }
                    break;
                }
                case SPECTRA_UI_ELEM_ACTION:
                {
                    elem.type = PluginUIElementType::Action;
                    elem.action.id       = src.action_id ? src.action_id : "";
                    elem.action.label    = src.action_label ? src.action_label : "";
                    elem.action.tooltip  = src.action_tooltip ? src.action_tooltip : "";
                    elem.action.enabled  = src.action_enabled != 0;
                    break;
                }
                case SPECTRA_UI_ELEM_LABEL:
                {
                    elem.type = PluginUIElementType::Label;
                    elem.label.text  = src.label_text ? src.label_text : "";
                    elem.label.style_hint = src.label_style ? src.label_style : "";
                    break;
                }
                case SPECTRA_UI_ELEM_SEPARATOR:
                {
                    elem.type = PluginUIElementType::Separator;
                    break;
                }
                case SPECTRA_UI_ELEM_GROUP:
                {
                    elem.type = PluginUIElementType::Group;
                    elem.group.title     = src.group_title ? src.group_title : "";
                    elem.group.collapsed = src.group_collapsed != 0;
                    break;
                }
                default:
                    continue;
            }

            schema.elements.push_back(std::move(elem));
        }

        PluginUICallbacks callbacks;
        if (on_changed)
        {
            callbacks.on_property_changed =
                [on_changed, user_data](const std::string& sid,
                                        const std::string& pid,
                                        const std::string& val) -> std::string
            {
                const char* result = on_changed(sid.c_str(), pid.c_str(),
                                                val.c_str(), user_data);
                return result ? std::string(result) : val;
            };
        }
        if (on_action)
        {
            callbacks.on_action_triggered =
                [on_action, user_data](const std::string& sid,
                                       const std::string& aid)
            {
                on_action(sid.c_str(), aid.c_str(), user_data);
            };
        }

        tl_last_schema_id = reg->register_schema(schema, std::move(callbacks));
        return tl_last_schema_id.c_str();
    }

    void spectra_unregister_plugin_ui(
        SpectraPluginUIRegistry registry, const char* plugin_name)
    {
        if (!registry || !plugin_name)
            return;
        auto* reg = static_cast<PluginUIRegistry*>(registry);
        reg->unregister_plugin(plugin_name);
    }

}   // extern "C"

// ─── PluginManager ───────────────────────────────────────────────────────────

PluginManager::~PluginManager()
{
    unload_all();
}

SpectraPluginContext PluginManager::make_context(uint32_t minor_version) const
{
    SpectraPluginContext ctx{};
    ctx.api_version_major      = SPECTRA_PLUGIN_API_VERSION_MAJOR;
    ctx.api_version_minor      = minor_version;
    ctx.command_registry       = static_cast<SpectraCommandRegistry>(registry_);
    ctx.shortcut_manager       = static_cast<SpectraShortcutManager>(shortcut_mgr_);
    ctx.undo_manager           = static_cast<SpectraUndoManager>(undo_mgr_);
    ctx.transform_registry     = static_cast<SpectraTransformRegistry>(transform_reg_);
    ctx.overlay_registry       = static_cast<SpectraOverlayRegistry>(overlay_reg_);
    ctx.export_format_registry = static_cast<SpectraExportFormatRegistry>(export_format_reg_);
    ctx.data_source_registry   = static_cast<SpectraDataSourceRegistry>(data_source_reg_);
    ctx.series_type_registry   = static_cast<SpectraSeriesTypeRegistry>(series_type_reg_);
    ctx.backend_handle         = static_cast<void*>(backend_);
    ctx.plugin_ui_registry     = static_cast<SpectraPluginUIRegistry>(plugin_ui_reg_);
    return ctx;
}

bool PluginManager::load_plugin(const std::string& path)
{
    std::lock_guard lock(mutex_);

    // Check if already loaded
    for (const auto& p : plugins_)
    {
        if (p.path == path && p.loaded)
            return false;
    }

    // Manifest pre-check before opening the shared library
    PluginManifest manifest;
    std::string    manifest_path = find_plugin_manifest_path(path);
    if (!manifest_path.empty())
    {
        manifest = load_plugin_manifest(manifest_path);
        if (manifest.is_valid())
        {
            if (!manifest.is_api_compatible(SPECTRA_PLUGIN_API_VERSION_MAJOR,
                                            SPECTRA_PLUGIN_API_VERSION_MINOR))
            {
                SPECTRA_LOG_WARN(
                    "plugin",
                    "Plugin manifest '{}' requests API v{} but host provides v{}.{} — skipping "
                    "load",
                    manifest.name,
                    manifest.api_version,
                    SPECTRA_PLUGIN_API_VERSION_MAJOR,
                    SPECTRA_PLUGIN_API_VERSION_MINOR);
                return false;
            }
            SPECTRA_LOG_INFO("plugin",
                             "Loading plugin '{}' v{} by {} (API v{})",
                             manifest.name,
                             manifest.version,
                             manifest.author.empty() ? "unknown" : manifest.author,
                             manifest.api_version);
        }
    }

    void*                   handle      = nullptr;
    SpectraPluginInitFn     init_fn     = nullptr;
    SpectraPluginShutdownFn shutdown_fn = nullptr;

#ifdef _WIN32
    handle = LoadLibraryA(path.c_str());
    if (!handle)
        return false;
    init_fn = reinterpret_cast<SpectraPluginInitFn>(
        GetProcAddress(static_cast<HMODULE>(handle), "spectra_plugin_init"));
    shutdown_fn = reinterpret_cast<SpectraPluginShutdownFn>(
        GetProcAddress(static_cast<HMODULE>(handle), "spectra_plugin_shutdown"));
#else
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return false;
    init_fn = reinterpret_cast<SpectraPluginInitFn>(dlsym(handle, "spectra_plugin_init"));
    shutdown_fn =
        reinterpret_cast<SpectraPluginShutdownFn>(dlsym(handle, "spectra_plugin_shutdown"));
#endif

    if (!init_fn)
    {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }

    SpectraPluginContext ctx = make_context(SPECTRA_PLUGIN_API_VERSION_MINOR);
    SpectraPluginInfo    info{};
    std::vector<std::string> registered_transforms;
    if (transform_reg_)
        transform_reg_->begin_registration_capture(&registered_transforms);
    auto t0     = std::chrono::steady_clock::now();
    int  result = init_fn(&ctx, &info);
    if (transform_reg_)
        transform_reg_->end_registration_capture();
    auto                 t1     = std::chrono::steady_clock::now();
    size_t               init_us =
        static_cast<size_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    const auto unregister_new_transforms = [&]()
    {
        if (transform_reg_)
            for (const auto& name : registered_transforms)
                transform_reg_->unregister_transform(name);
    };
    if (result != 0)
    {
        unregister_new_transforms();
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }

    // Version compatibility check
    if (info.api_version_major != SPECTRA_PLUGIN_API_VERSION_MAJOR)
    {
        unregister_new_transforms();
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }

    // Minor version negotiation
    uint32_t plugin_minor = info.api_version_minor;
    if (plugin_minor > SPECTRA_PLUGIN_API_VERSION_MINOR)
    {
        SPECTRA_LOG_WARN("plugin",
                         "Plugin '{}' requests API v{}.{} but host only supports v{}.{}. "
                         "Some features may not be available.",
                         info.name ? info.name : "Unknown",
                         info.api_version_major,
                         plugin_minor,
                         SPECTRA_PLUGIN_API_VERSION_MAJOR,
                         SPECTRA_PLUGIN_API_VERSION_MINOR);
    }

    PluginEntry entry;
    entry.name                     = info.name ? info.name : "Unknown";
    entry.version                  = info.version ? info.version : "0.0.0";
    entry.author                   = info.author ? info.author : "";
    entry.description              = info.description ? info.description : "";
    entry.path                     = path;
    entry.loaded                   = true;
    entry.enabled                  = true;
    entry.api_version_minor        = plugin_minor;
    entry.handle                   = handle;
    entry.shutdown_fn              = shutdown_fn;
    entry.registered_transforms    = registered_transforms;
    entry.diagnostics.init_time_us = init_us;
    entry.manifest                 = std::move(manifest);

    if (transform_reg_)
        for (const auto& transform_name : entry.registered_transforms)
            transform_reg_->set_transform_source(transform_name, entry.name);

    plugins_.push_back(std::move(entry));
    return true;
}

bool PluginManager::unload_plugin(const std::string& name)
{
    std::lock_guard lock(mutex_);

    for (auto it = plugins_.begin(); it != plugins_.end(); ++it)
    {
        if (it->name == name && it->loaded)
        {
            // Unregister commands
            if (registry_)
            {
                for (const auto& cmd_id : it->registered_commands)
                {
                    registry_->unregister_command(cmd_id);
                }
            }

            if (transform_reg_)
                for (const auto& transform_name : it->registered_transforms)
                    transform_reg_->unregister_transform(transform_name);

            // Call shutdown
            if (it->shutdown_fn)
            {
                it->shutdown_fn();
            }

            // Close library
            if (it->handle)
            {
#ifdef _WIN32
                FreeLibrary(static_cast<HMODULE>(it->handle));
#else
                dlclose(it->handle);
#endif
            }

            plugins_.erase(it);
            return true;
        }
    }
    return false;
}

void PluginManager::unload_all()
{
    std::lock_guard lock(mutex_);

    for (auto& p : plugins_)
    {
        if (!p.loaded)
            continue;

        if (registry_)
        {
            for (const auto& cmd_id : p.registered_commands)
            {
                registry_->unregister_command(cmd_id);
            }
        }

        if (transform_reg_)
            for (const auto& transform_name : p.registered_transforms)
                transform_reg_->unregister_transform(transform_name);

        if (p.shutdown_fn)
        {
            p.shutdown_fn();
        }

        if (p.handle)
        {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(p.handle));
#else
            dlclose(p.handle);
#endif
        }
        p.loaded = false;
    }
    plugins_.clear();
}

std::vector<PluginEntry> PluginManager::plugins() const
{
    std::lock_guard lock(mutex_);
    return plugins_;
}

const PluginEntry* PluginManager::find_plugin(const std::string& name) const
{
    std::lock_guard lock(mutex_);
    for (const auto& p : plugins_)
    {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

const PluginDiagnostics* PluginManager::diagnostics(const std::string& name) const
{
    std::lock_guard lock(mutex_);
    for (const auto& p : plugins_)
    {
        if (p.name == name)
            return &p.diagnostics;
    }
    return nullptr;
}

size_t PluginManager::plugin_count() const
{
    std::lock_guard lock(mutex_);
    return plugins_.size();
}

void PluginManager::set_plugin_enabled(const std::string& name, bool enabled)
{
    std::lock_guard lock(mutex_);
    for (auto& p : plugins_)
    {
        if (p.name == name)
        {
            p.enabled = enabled;
            // Enable/disable all commands registered by this plugin
            if (registry_)
            {
                for (const auto& cmd_id : p.registered_commands)
                {
                    registry_->set_enabled(cmd_id, enabled);
                }
            }
            break;
        }
    }
}

std::vector<std::string> PluginManager::discover(const std::string& directory) const
{
    std::vector<std::string> paths;
    try
    {
        if (!std::filesystem::exists(directory))
            return paths;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file())
                continue;
            auto ext = entry.path().extension().string();
#ifdef _WIN32
            if (ext == ".dll")
            {
#elif defined(__APPLE__)
            if (ext == ".dylib")
            {
#else
            if (ext == ".so")
            {
#endif
                paths.push_back(entry.path().string());
            }
        }
    }
    catch (...)
    {
        // Ignore filesystem errors
    }
    return paths;
}

std::string PluginManager::default_plugin_dir()
{
    const char* home = std::getenv("HOME");
    if (!home)
        home = std::getenv("USERPROFILE");
    if (!home)
        return "plugins";

    std::filesystem::path dir = std::filesystem::path(home) / ".config" / "spectra" / "plugins";
    return dir.string();
}

// ─── Serialization ───────────────────────────────────────────────────────────

static std::string escape_json(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

std::string PluginManager::serialize_state() const
{
    std::lock_guard lock(mutex_);
    std::string     out = "{\n  \"plugins\": [\n";
    for (size_t i = 0; i < plugins_.size(); ++i)
    {
        const auto& p = plugins_[i];
        out += R"(    {"name": ")" + escape_json(p.name) + R"(", "path": ")" + escape_json(p.path)
               + R"(", "enabled": )" + (p.enabled ? "true" : "false") + "}";
        if (i + 1 < plugins_.size())
            out += ",";
        out += "\n";
    }
    out += "  ]\n}\n";
    return out;
}

bool PluginManager::deserialize_state(const std::string& json)
{
    struct StateEntry
    {
        std::string name;
        std::string path;
        bool        enabled = true;
    };

    auto read_json_string = [&](size_t from_colon) -> std::pair<std::string, size_t>
    {
        auto q1 = json.find('"', from_colon);
        if (q1 == std::string::npos)
            return {"", std::string::npos};
        auto q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos)
            return {"", std::string::npos};
        return {json.substr(q1 + 1, q2 - q1 - 1), q2 + 1};
    };

    std::vector<StateEntry> entries;
    size_t                  pos = 0;
    while ((pos = json.find("\"name\"", pos)) != std::string::npos)
    {
        StateEntry e;

        auto name_colon = json.find(':', pos);
        if (name_colon == std::string::npos)
            break;
        auto [name, after_name] = read_json_string(name_colon + 1);
        if (after_name == std::string::npos)
            break;
        e.name = name;

        auto path_key = json.find("\"path\"", after_name);
        if (path_key == std::string::npos)
            break;
        auto path_colon = json.find(':', path_key);
        if (path_colon == std::string::npos)
            break;
        auto [path, after_path] = read_json_string(path_colon + 1);
        if (after_path == std::string::npos)
            break;
        e.path = path;

        auto enabled_key = json.find("\"enabled\"", after_path);
        if (enabled_key != std::string::npos)
        {
            auto enabled_colon = json.find(':', enabled_key);
            if (enabled_colon != std::string::npos)
            {
                auto rest = json.substr(enabled_colon + 1, 10);
                e.enabled = rest.find("true") != std::string::npos;
            }
        }

        if (!e.path.empty())
            entries.push_back(std::move(e));

        pos = after_path;
    }

    for (const auto& e : entries)
    {
        if (!e.path.empty())
            load_plugin(e.path);

        std::string plugin_name = e.name;
        if (plugin_name.empty())
        {
            auto loaded = plugins();
            for (const auto& p : loaded)
            {
                if (p.path == e.path)
                {
                    plugin_name = p.name;
                    break;
                }
            }
        }

        if (!plugin_name.empty())
            set_plugin_enabled(plugin_name, e.enabled);
    }

    return true;
}

}   // namespace spectra
