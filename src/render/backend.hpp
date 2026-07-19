#pragma once

#include <cstdint>
#include <spectra/color.hpp>
#include <spectra/series.hpp>

namespace spectra
{

class FrameProfiler;

/// Pipeline creation descriptor for plugin-provided custom series types.
/// Uses raw SPIR-V bytecodes and Vulkan-compatible enum values.
struct CustomPipelineDesc
{
    const uint8_t* vert_spirv      = nullptr;
    size_t         vert_spirv_size = 0;
    const uint8_t* frag_spirv      = nullptr;
    size_t         frag_spirv_size = 0;
    uint32_t       topology        = 0;   // VkPrimitiveTopology value

    struct VertexBinding
    {
        uint32_t binding;
        uint32_t stride;
        uint32_t input_rate;   // 0 = per-vertex, 1 = per-instance
    };
    struct VertexAttribute
    {
        uint32_t location;
        uint32_t binding;
        uint32_t format;   // VkFormat value
        uint32_t offset;
    };
    const VertexBinding*   vertex_bindings        = nullptr;
    uint32_t               vertex_binding_count   = 0;
    const VertexAttribute* vertex_attributes      = nullptr;
    uint32_t               vertex_attribute_count = 0;

    bool enable_depth_test    = false;
    bool enable_depth_write   = false;
    bool enable_backface_cull = false;
    bool enable_blending      = true;
};

enum class BufferUsage
{
    Vertex,
    Index,
    Uniform,
    Storage,
    Staging,
};

enum class PipelineType
{
    Line,
    Scatter,
    ScatterColormap,
    Grid,
    Heatmap,
    // 3D pipeline types
    Line3D,
    Scatter3D,
    Mesh3D,
    Surface3D,
    Grid3D,
    GridOverlay3D,   // Same as Grid3D but no depth test — for grid lines rendered after series
    // Wireframe 3D pipeline variants (line topology with vertex buffer)
    SurfaceWireframe3D,
    SurfaceWireframe3D_Transparent,
    // Transparent 3D pipeline variants (depth test ON, depth write OFF)
    Line3D_Transparent,
    Scatter3D_Transparent,
    Mesh3D_Transparent,
    Surface3D_Transparent,
    // Screen-space filled overlay (triangle list, vec2, same shaders as Grid)
    Overlay,
    // Statistical series fill (triangle list, vec2+float alpha, per-vertex gradient)
    StatFill,
    // 3D filled overlay (triangle list, vec3, depth test ON, grid3d shaders)
    Arrow3D,
    // Text rendering pipeline (screen-space textured quads)
    Text,
    // Text with depth test (for 3D labels occluded by geometry)
    TextDepth,
    // 3D marker primitives (cubes, spheres, cylinders — used by ROS display plugins)
    Marker3D,
    Marker3D_Transparent,
    // Point cloud rendering (per-point color from SSBO)
    PointCloud,
    PointCloud_Transparent,
    // 3D textured quad (camera image billboard in scene)
    Image3D,
};

struct BufferHandle
{
    uint64_t id = 0;
    explicit operator bool() const { return id != 0; }
};

struct PipelineHandle
{
    uint64_t id = 0;
    explicit operator bool() const { return id != 0; }
};

struct TextureHandle
{
    uint64_t id = 0;
    explicit operator bool() const { return id != 0; }
};

struct FrameUBO
{
    float projection[16]{};   // mat4 — orthographic (2D) or perspective/ortho (3D)
    float view[16]{};         // mat4 — identity (2D) or camera view matrix (3D)
    float model[16]{};        // mat4 — identity (2D) or per-series transform (3D)
    float viewport_width  = 0.0f;
    float viewport_height = 0.0f;
    float time            = 0.0f;
    float _pad0           = 0.0f;
    // 3D-specific fields (std140 aligned)
    float camera_pos[3]{};   // Eye position (for lighting)
    float near_plane = 0.01f;
    float light_dir[3]{};   // Directional light (Phase 3)
    float far_plane = 1000.0f;
};

struct SeriesPushConstants
{
    float color[4]{};
    float line_width    = 2.0f;
    float point_size    = 4.0f;
    float data_offset_x = 0.0f;
    float data_offset_y = 0.0f;
    // Plot style fields (line dash pattern + marker shape)
    uint32_t line_style  = 1;   // 0=None, 1=Solid, 2=Dashed, 3=Dotted, 4=DashDot, 5=DashDotDot
    uint32_t marker_type = 0;   // 0=None, 1=Point, 2=Circle, ... (matches MarkerStyle enum)
    float    marker_size = 6.0f;
    float    opacity     = 1.0f;
    // Dash pattern (up to 4 on/off pairs)
    float dash_pattern[8]{};
    float dash_total = 0.0f;
    int   dash_count = 0;
    float _pad2[2]{};   // alignment padding
};

class Backend
{
   public:
    virtual ~Backend() = default;

    // Lifecycle
    virtual bool init(bool headless) = 0;
    virtual void shutdown()          = 0;
    virtual void wait_idle()         = 0;

    // Surface / swapchain (windowed mode)
    virtual bool create_surface(void* native_window)                 = 0;
    virtual bool create_swapchain(uint32_t width, uint32_t height)   = 0;
    virtual bool recreate_swapchain(uint32_t width, uint32_t height) = 0;

    // Offscreen framebuffer (headless mode)
    virtual bool create_offscreen_framebuffer(uint32_t width, uint32_t height) = 0;

    // Pipeline management
    virtual PipelineHandle create_pipeline(PipelineType type) = 0;

    // Custom pipeline from plugin-provided SPIR-V and config.
    // Default implementation returns an invalid handle (no-op for backends that
    // do not support custom pipelines).
    virtual PipelineHandle create_custom_pipeline(const CustomPipelineDesc& desc)
    {
        (void)desc;
        return PipelineHandle{};
    }
    virtual void destroy_pipeline(PipelineHandle handle) { (void)handle; }

    // Buffer management
    virtual BufferHandle create_buffer(BufferUsage usage, size_t size_bytes) = 0;
    virtual void         destroy_buffer(BufferHandle handle)                 = 0;
    virtual void         upload_buffer(BufferHandle handle,
                                       const void*  data,
                                       size_t       size_bytes,
                                       size_t       offset = 0)                    = 0;

    // Texture management
    virtual TextureHandle create_texture(uint32_t       width,
                                         uint32_t       height,
                                         const uint8_t* rgba_data) = 0;
    virtual void          destroy_texture(TextureHandle handle)    = 0;

    // Frame rendering
    virtual bool begin_frame(FrameProfiler* profiler = nullptr) = 0;
    virtual void end_frame(FrameProfiler* profiler = nullptr)   = 0;

    // Render pass
    virtual void begin_render_pass(const Color& clear_color = colors::white) = 0;
    virtual void end_render_pass()                                           = 0;

    // Drawing
    virtual void bind_pipeline(PipelineHandle handle)                               = 0;
    virtual void bind_buffer(BufferHandle handle, uint32_t binding)                 = 0;
    virtual void bind_index_buffer(BufferHandle handle)                             = 0;
    virtual void bind_texture(TextureHandle handle, uint32_t binding)               = 0;
    virtual void push_constants(const SeriesPushConstants& pc)                      = 0;
    virtual void set_viewport(float x, float y, float width, float height)          = 0;
    virtual void set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;
    virtual void set_line_width(float width)                                        = 0;
    virtual void draw(uint32_t vertex_count, uint32_t first_vertex = 0)             = 0;
    virtual void draw_instanced(uint32_t vertex_count,
                                uint32_t instance_count,
                                uint32_t first_vertex   = 0,
                                uint32_t first_instance = 0)                        = 0;
    virtual void draw_indexed(uint32_t index_count,
                              uint32_t first_index   = 0,
                              int32_t  vertex_offset = 0)                            = 0;

    // Readback (for offscreen/export)
    virtual bool readback_framebuffer(uint8_t* out_rgba, uint32_t width, uint32_t height) = 0;

    // MSAA configuration (must be set before creating swapchain/offscreen framebuffer)
    virtual void     set_msaa_samples(uint32_t samples) { msaa_samples_ = samples; }
    virtual uint32_t msaa_samples() const { return msaa_samples_; }

    // Queries
    virtual uint32_t swapchain_width() const  = 0;
    virtual uint32_t swapchain_height() const = 0;

    // Clip-space Y convention.  Vulkan clip Y points down (true),
    // WebGPU/OpenGL clip Y points up (false).  Used by the renderer
    // to build correct orthographic projection matrices.
    virtual bool clip_y_down() const { return true; }

    // Returns the current in-flight frame slot index [0, max_frames_in_flight).
    // Used by renderers to ring-buffer per-frame GPU resources (vertex buffers,
    // UBOs) so that frame N+1's CPU upload doesn't overwrite data that frame N's
    // GPU commands still reference.
    virtual uint32_t current_flight_frame() const { return 0; }
    virtual uint32_t max_frames_in_flight() const { return 1; }

   protected:
    uint32_t msaa_samples_ = 1;   // 1 = no MSAA, 4 = MSAA 4x
};

}   // namespace spectra
