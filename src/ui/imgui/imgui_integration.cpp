#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

    #include <format>

    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #ifdef SPECTRA_USE_GLFW
        #include <GLFW/glfw3.h>
        #include <cmath>
    #endif
    #ifdef SPECTRA_USE_SDL3
        #include <SDL3/SDL.h>
        #include <SDL3/SDL_vulkan.h>
    #endif

    // Compressed Inter font data
    #include "../../../third_party/inter_font.hpp"

    // Embedded Font Awesome 6 Free Solid icon font data
    #include "../../../third_party/fa_solid_900.hpp"

    // stb_image for decoding embedded PNG (implementation in stb_impl.cpp)
    #include "stb_image.h"

    // Embedded Spectra logo icon
    #include "spectra_icon_embedded.hpp"

    #include "ui/theme/glass_draw.hpp"
    #include "ui/ui_interaction_log.hpp"

    #include "../../../third_party/tinyfiledialogs.h"
    #include "../dialog_env_guard.hpp"
    #include "../native_dialog_policy.hpp"
    #include "../topics/topics_panel.hpp"
    #include "../settings/settings_panel.hpp"
    #include "ui/shell/spectra_app_shell.hpp"

    #ifndef M_PI
        #define M_PI 3.14159265358979323846
    #endif

static bool register_backend_texture(spectra::VulkanBackend& backend,
                                     spectra::TextureHandle  texture,
                                     uint64_t*               out_id)
{
    if (!texture)
        return false;

    VkSampler   sampler = VK_NULL_HANDLE;
    VkImageView view    = VK_NULL_HANDLE;
    if (!backend.texture_vulkan_handles(texture, &sampler, &view))
        return false;

    VkDescriptorSet ds =
        ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (out_id)
        *out_id = vk_rp_to_u64(ds);
    return true;
}

static bool load_embedded_texture(spectra::VulkanBackend& backend,
                                  const unsigned char*    bytes,
                                  unsigned int            size,
                                  spectra::TextureHandle* out_texture,
                                  uint64_t*               out_id,
                                  int*                    out_w,
                                  int*                    out_h,
                                  const char*             label)
{
    int            w        = 0;
    int            h        = 0;
    int            channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h, &channels, 4);
    if (!pixels)
    {
        SPECTRA_LOG_WARN("imgui", std::string("Failed to decode embedded ") + label + " PNG");
        return false;
    }

    spectra::TextureHandle texture =
        backend.create_texture(static_cast<uint32_t>(w), static_cast<uint32_t>(h), pixels);
    stbi_image_free(pixels);
    if (!texture)
    {
        SPECTRA_LOG_WARN("imgui", std::string("Failed to create ") + label + " Vulkan texture");
        return false;
    }

    uint64_t id = 0;
    if (!register_backend_texture(backend, texture, &id))
    {
        backend.destroy_texture(texture);
        SPECTRA_LOG_WARN("imgui",
                         std::string("Failed to register ") + label + " texture with ImGui");
        return false;
    }

    if (out_texture)
        *out_texture = texture;
    if (out_id)
        *out_id = id;
    if (out_w)
        *out_w = w;
    if (out_h)
        *out_h = h;
    return true;
}

namespace spectra
{
std::unique_ptr<ImGuiIntegration> make_imgui_integration()
{
    return std::make_unique<ImGuiIntegration>();
}

// ─── Lifecycle ──────────────────────────────────────────────────────────────

ImGuiIntegration::~ImGuiIntegration()
{
    shutdown();
}

const ui::ThemeColors& ImGuiIntegration::theme_colors() const
{
    if (theme_mgr_)
        return theme_mgr_->colors();
    return ui::theme();
}

    #ifdef SPECTRA_USE_GLFW
bool ImGuiIntegration::init(VulkanBackend& backend, GLFWwindow* window, bool install_callbacks)
{
    if (initialized_)
        return true;
    if (!window)
        return false;

    glfw_window_    = window;
    layout_manager_ = std::make_unique<LayoutManager>();

    IMGUI_CHECKVERSION();
    // In ImGui 1.92.x with RendererHasTextures the atlas must be owned by
    // the context (OwnerContext == ctx). Pass nullptr so CreateContext creates
    // its own atlas. The old shared-atlas workaround is not needed in 1.92.x.
    imgui_context_ = ImGui::CreateContext(nullptr);
    // CreateContext() restores the previous context if one exists (ImGui 1.90+).
    // We must explicitly switch to the new context so load_fonts() and
    // backend init operate on the correct context/atlas.
    ImGui::SetCurrentContext(imgui_context_);

    // Disable ImGui's automatic imgui.ini persistence for this window.  Spectra
    // handles layout/workspace persistence via WorkspaceData (JSON) rather than
    // imgui.ini, so letting ImGui write/read its own .ini file only causes
    // conflicts (especially in spectra-ros where docking state is managed by
    // RosSessionManager).
    ImGui::GetIO().IniFilename = nullptr;

    // Initialize theme system
    if (theme_mgr_)
        theme_mgr_->ensure_initialized();

    load_fonts();

    // Initialize icon font system (must be AFTER load_fonts so the atlas
    // contains merged FA6 glyphs when IconFont caches font pointers)
    ui::IconFont::instance().initialize();

    apply_modern_style();

    // Wire inspector fonts
    inspector_.set_fonts(font_body_, font_heading_, font_title_);
    data_editor_.set_fonts(font_body_, font_heading_, font_title_);

    ImGui_ImplGlfw_InitForVulkan(window, install_callbacks);

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance                     = backend.instance();
    ii.PhysicalDevice               = backend.physical_device();
    ii.Device                       = backend.device();
    ii.QueueFamily                  = backend.graphics_queue_family();
    ii.Queue                        = backend.graphics_queue();
    ii.DescriptorPool               = backend.descriptor_pool();
    ii.MinImageCount                = backend.min_image_count();
    ii.ImageCount                   = backend.image_count();
    ii.PipelineInfoMain.RenderPass  = backend.render_pass();
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);

    cached_render_pass_ = vk_rp_to_u64(ii.PipelineInfoMain.RenderPass);
    initialized_        = true;
    backend_            = &backend;

    load_logo_textures(backend);

    return true;
}
    #endif   // SPECTRA_USE_GLFW

    #ifdef SPECTRA_USE_SDL3
bool ImGuiIntegration::init_sdl3(VulkanBackend& backend, SDL_Window* window)
{
    if (initialized_)
        return true;
    if (!window)
        return false;

    glfw_window_    = window;
    layout_manager_ = std::make_unique<LayoutManager>();

    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext(nullptr);
    ImGui::SetCurrentContext(imgui_context_);
    ImGui::GetIO().IniFilename = nullptr;

    if (theme_mgr_)
        theme_mgr_->ensure_initialized();

    load_fonts();
    ui::IconFont::instance().initialize();
    apply_modern_style();

    inspector_.set_fonts(font_body_, font_heading_, font_title_);
    data_editor_.set_fonts(font_body_, font_heading_, font_title_);

    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance                     = backend.instance();
    ii.PhysicalDevice               = backend.physical_device();
    ii.Device                       = backend.device();
    ii.QueueFamily                  = backend.graphics_queue_family();
    ii.Queue                        = backend.graphics_queue();
    ii.DescriptorPool               = backend.descriptor_pool();
    ii.MinImageCount                = backend.min_image_count();
    ii.ImageCount                   = backend.image_count();
    ii.PipelineInfoMain.RenderPass  = backend.render_pass();
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);

    cached_render_pass_ = vk_rp_to_u64(ii.PipelineInfoMain.RenderPass);
    initialized_        = true;
    backend_            = &backend;

    load_logo_textures(backend);

    return true;
}
    #endif   // SPECTRA_USE_SDL3

bool ImGuiIntegration::init_headless(VulkanBackend& backend, uint32_t width, uint32_t height)
{
    if (initialized_)
        return true;

    headless_       = true;
    layout_manager_ = std::make_unique<LayoutManager>();

    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext(nullptr);
    ImGui::SetCurrentContext(imgui_context_);

    // Configure IO for headless operation
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime   = 1.0f / 60.0f;
    io.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;
    // No ini file for headless
    io.IniFilename = nullptr;

    // Initialize theme system
    if (theme_mgr_)
        theme_mgr_->ensure_initialized();

    load_fonts();

    // Initialize icon font system (must be AFTER load_fonts so the atlas
    // contains merged FA6 glyphs when IconFont caches font pointers)
    ui::IconFont::instance().initialize();

    apply_modern_style();

    // Wire inspector fonts
    inspector_.set_fonts(font_body_, font_heading_, font_title_);
    data_editor_.set_fonts(font_body_, font_heading_, font_title_);

    // Skip ImGui_ImplGlfw_* — no GLFW window in headless mode.
    // Only init the Vulkan rendering backend.
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance       = backend.instance();
    ii.PhysicalDevice = backend.physical_device();
    ii.Device         = backend.device();
    ii.QueueFamily    = backend.graphics_queue_family();
    ii.Queue          = backend.graphics_queue();
    ii.DescriptorPool = backend.descriptor_pool();
    // ImGui Vulkan backend asserts MinImageCount >= 2. The headless backend
    // uses a single offscreen framebuffer (image_count=1), so clamp to 2.
    ii.MinImageCount                = std::max(backend.min_image_count(), 2u);
    ii.ImageCount                   = std::max(backend.image_count(), 2u);
    ii.PipelineInfoMain.RenderPass  = backend.render_pass();
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);

    cached_render_pass_ = vk_rp_to_u64(ii.PipelineInfoMain.RenderPass);
    initialized_        = true;
    backend_            = &backend;
    load_logo_textures(backend);
    return true;
}

void ImGuiIntegration::enable_docking()
{
    #ifdef IMGUI_HAS_DOCK
    if (imgui_context_)
    {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::SetCurrentContext(prev);
    }
    #endif
}

void ImGuiIntegration::enable_viewports()
{
    // Multi-viewport is incompatible with Spectra's overlay coordinate
    // system (all positions are window-relative, ViewportsEnable switches
    // them to screen-absolute).  Kept as a no-op for API stability.
}

void ImGuiIntegration::shutdown()
{
    if (!initialized_)
        return;

    // Switch to this integration's context before tearing down backends,
    // then restore the previous context so the caller is not left with a
    // dangling current context (fixes crash when closing secondary windows).
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    ImGuiContext* this_ctx = imgui_context_;
    if (this_ctx)
        ImGui::SetCurrentContext(this_ctx);

    destroy_logo_textures();
    ImGui_ImplVulkan_Shutdown();
    if (!headless_)
    {
    #ifdef SPECTRA_USE_GLFW
        ImGui_ImplGlfw_Shutdown();
    #elif defined(SPECTRA_USE_SDL3)
        ImGui_ImplSDL3_Shutdown();
    #endif
    }
    ui::IconFont::instance().release_context(this_ctx);
    ImGui::DestroyContext(this_ctx);
    imgui_context_ = nullptr;

    // Restore previous context (if it was a different context)
    if (prev_ctx && prev_ctx != this_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    else
        ImGui::SetCurrentContext(nullptr);

    layout_manager_.reset();
    backend_     = nullptr;
    initialized_ = false;
}

void ImGuiIntegration::on_swapchain_recreated(VulkanBackend& backend)
{
    if (!initialized_)
        return;

    ImGuiContext* previous_context = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(imgui_context_);

    ImGui_ImplVulkan_SetMinImageCount(backend.min_image_count());

    // Section 3F constraint 5: If the render pass handle changed (e.g. format
    // change on multi-monitor), ImGui holds a stale VkRenderPass.  Re-init the
    // Vulkan backend to pick up the new render pass.  This is a no-op in the
    // common case where recreate_swapchain reuses the render pass handle.
    VkRenderPass current_rp      = backend.render_pass();
    auto         current_rp_bits = vk_rp_to_u64(current_rp);
    if (current_rp_bits != cached_render_pass_ && current_rp != VK_NULL_HANDLE)
    {
        SPECTRA_LOG_WARN(
            "imgui",
            "Render pass changed after swapchain recreation — reinitializing ImGui Vulkan backend");
        ImGui_ImplVulkan_Shutdown();

        ImGui_ImplVulkan_InitInfo ii{};
        ii.Instance                     = backend.instance();
        ii.PhysicalDevice               = backend.physical_device();
        ii.Device                       = backend.device();
        ii.QueueFamily                  = backend.graphics_queue_family();
        ii.Queue                        = backend.graphics_queue();
        ii.DescriptorPool               = backend.descriptor_pool();
        ii.MinImageCount                = backend.min_image_count();
        ii.ImageCount                   = backend.image_count();
        ii.PipelineInfoMain.RenderPass  = current_rp;
        ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&ii);

        // Re-register the single shared logo texture after ImGui reinit
        // (corner is an alias — no separate remove/register needed)
        if (welcome_logo_texture_id_ != 0)
            ImGui_ImplVulkan_RemoveTexture(
                u64_to_handle<VkDescriptorSet>(welcome_logo_texture_id_));

        welcome_logo_texture_id_ = 0;
        corner_logo_texture_id_  = 0;
        register_backend_texture(backend, welcome_logo_texture_, &welcome_logo_texture_id_);
        corner_logo_texture_id_ = welcome_logo_texture_id_;

        cached_render_pass_ = current_rp_bits;
    }

    ImGui::SetCurrentContext(previous_context);
}

void ImGuiIntegration::update_layout(float window_width, float window_height, float dt)
{
    if (layout_manager_)
    {
        layout_manager_->update(window_width, window_height, dt);
    }
}

bool ImGuiIntegration::has_active_chrome_animations() const
{
    if (layout_manager_ && layout_manager_->is_animating())
        return true;
    if (ui::widgets::any_section_animations_active())
        return true;
    if (theme_mgr_ && theme_mgr_->is_transitioning())
        return true;
    if (mode_transition_ && mode_transition_->is_active())
        return true;
    return false;
}

void ImGuiIntegration::set_nav_rail_visible(bool v)
{
    show_nav_rail_ = v;
    if (layout_manager_)
        layout_manager_->set_nav_rail_visible(v);
}

void ImGuiIntegration::new_frame()
{
    if (!initialized_)
        return;
    ImGui_ImplVulkan_NewFrame();
    #ifdef SPECTRA_USE_GLFW
    ImGui_ImplGlfw_NewFrame();
    #elif defined(SPECTRA_USE_SDL3)
    ImGui_ImplSDL3_NewFrame();
    #endif
    ImGui::NewFrame();

    // Update layout with current window size and delta time
    ImGuiIO& io = ImGui::GetIO();
    update_layout(io.DisplaySize.x, io.DisplaySize.y, io.DeltaTime);
}

void ImGuiIntegration::new_frame_headless(const HeadlessFrameInput& input)
{
    if (!initialized_)
        return;

    ImGui::SetCurrentContext(imgui_context_);

    // Manually populate IO — replaces ImGui_ImplGlfw_NewFrame()
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(input.display_w, input.display_h);
    io.DeltaTime   = (input.dt > 0.0f) ? input.dt : (1.0f / 60.0f);

    // Mouse state
    io.MousePos = ImVec2(input.mouse_x, input.mouse_y);
    for (int i = 0; i < 5; ++i)
        io.MouseDown[i] = input.mouse_down[i];
    io.MouseWheel  = input.mouse_wheel;
    io.MouseWheelH = input.mouse_wheel_h;

    // Display scaling
    io.DisplayFramebufferScale = ImVec2(input.dpi_scale, input.dpi_scale);

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    // Update layout with current display size and delta time
    update_layout(input.display_w, input.display_h, io.DeltaTime);
}

void ImGuiIntegration::build_ui(Figure& figure, FigureViewModel* vm)
{
    if (!initialized_)
    {
        SPECTRA_LOG_WARN("ui", "build_ui called but ImGui is not initialized");
        return;
    }

    current_figure_ = &figure;
    inspector_.set_figure_view_model(vm);

    if (app_shell_)
        app_shell_->set_current_figure(&figure);

    if (app_shell_)
        app_shell_->ensure_initialized();

    float dt = ImGui::GetIO().DeltaTime;
    if (theme_mgr_)
        theme_mgr_->update(dt);
    ui::widgets::update_section_animations(dt);

    // Sync panel_open_ from layout manager so external toggles (commands, undo)
    // that only call set_inspector_visible() also open the panel content.
    if (layout_manager_)
        panel_open_ = layout_manager_->is_inspector_visible();

    // Fade tracks the slide animation so open/close feel cohesive.
    if (layout_manager_)
        panel_anim_ = layout_manager_->inspector_open_amount();

    // Update bottom panel height so canvas shrinks when timeline is open
    if (layout_manager_)
    {
        float target_h = (show_timeline_ && timeline_editor_) ? 200.0f : 0.0f;
        float cur_h    = layout_manager_->bottom_panel_height();
        float new_h    = cur_h + (target_h - cur_h) * std::min(1.0f, 12.0f * dt);
        if (std::abs(new_h - target_h) < 0.5f)
            new_h = target_h;
        layout_manager_->set_bottom_panel_height(new_h);
    }

    // Opaque chrome backdrops cover the plot clear color in UI zones during
    // live resize when layout briefly lags the swapchain extent.
    if (shell_chrome_visible_)
        draw_chrome_backdrops();

    // Draw all zones using layout manager.
    // Each draw call is gated so adapter shells (e.g. spectra-ros) can suppress
    // Spectra's own chrome and replace it with their own menu/status/canvas.
    if (command_bar_visible_)
        draw_command_bar();
    if (show_nav_rail_)
        draw_nav_rail();
    if (canvas_visible_)
    {
        draw_canvas(figure);
        draw_plot_overlays(figure);
        draw_axis_link_indicators(figure);
        draw_axes_context_menu(figure);
        draw_topic_drop_target(figure);
    }
    if (canvas_visible_)
    {
        if (app_shell_ && shell_chrome_visible_)
        {
            app_shell_->draw_registered_panels();
        }
        else if (shell_chrome_visible_ && layout_manager_->inspector_animated_width() >= 1.0f)
        {
            draw_inspector(figure);
        }
    }
    if (canvas_visible_ && (shell_chrome_visible_ || external_inspector_toggle_visible_))
    {
        draw_inspector_toggle();
    }
    if (status_bar_visible_)
        draw_status_bar();
    if (canvas_visible_ && shell_chrome_visible_)
    {
        draw_pane_tab_headers();   // Must run before splitters so pane_tab_hovered_ is set
        draw_split_view_splitters();
    }

    // Draw timeline / curve editor via shell panel registry when available.
    if (!app_shell_)
    {
        if (show_timeline_ && timeline_editor_)
            draw_timeline_panel();

        if (show_curve_editor_ && curve_editor_)
            draw_curve_editor_panel();
    }

    // Draw deferred tooltip (command bar) on top of everything
    if (deferred_tooltip_)
    {
        // Clamp tooltip position to stay within window bounds
        {
            ImVec2 mouse   = ImGui::GetIO().MousePos;
            ImVec2 display = ImGui::GetIO().DisplaySize;
            float  margin  = 8.0f;
            float  est_tip_w =
                ImGui::CalcTextSize(deferred_tooltip_).x + ui::tokens::SPACE_3 * 2.0f + 4.0f;
            float est_tip_h =
                ImGui::GetTextLineHeight() + (ui::tokens::SPACE_2 + 1.0f) * 2.0f + 6.0f;

            // Default: tooltip floats above the cursor (anchored by its bottom).
            // If there isn't room above (e.g. command-bar controls at the top of
            // the screen), drop it below the cursor so it never clips off-screen.
            bool  place_below = (mouse.y - est_tip_h - 4.0f) < margin;
            float pivot_y     = place_below ? 0.0f : 1.0f;
            float tip_y       = place_below ? (mouse.y + 24.0f) : (mouse.y - 4.0f);
            tip_y             = std::clamp(tip_y, margin, display.y - margin);

            // If tooltip would clip right edge, anchor from right side
            if (mouse.x + est_tip_w * 0.5f > display.x - margin)
            {
                float tip_x = display.x - margin;
                ImGui::SetNextWindowPos(ImVec2(tip_x, tip_y),
                                        ImGuiCond_Always,
                                        ImVec2(1.0f, pivot_y));
            }
            else if (mouse.x - est_tip_w * 0.5f < margin)
            {
                float tip_x = margin;
                ImGui::SetNextWindowPos(ImVec2(tip_x, tip_y),
                                        ImGuiCond_Always,
                                        ImVec2(0.0f, pivot_y));
            }
            else
            {
                ImGui::SetNextWindowPos(ImVec2(mouse.x, tip_y),
                                        ImGuiCond_Always,
                                        ImVec2(0.5f, pivot_y));
            }
        }
        ImGui::BeginTooltip();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(ui::tokens::SPACE_3 + 2.0f, ui::tokens::SPACE_2 + 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
        // Transparent popup bg — frosted glass card is drawn manually below.
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(theme_colors().text_primary.r,
                                     theme_colors().text_primary.g,
                                     theme_colors().text_primary.b,
                                     theme_colors().text_primary.a));
        {
            ImDrawList* tdl = ImGui::GetWindowDrawList();
            ImVec2      tp0 = ImGui::GetWindowPos();
            ImVec2      tsz = ImGui::GetWindowSize();
            ImVec2      tp1 = ImVec2(tp0.x + tsz.x, tp0.y + tsz.y);
            tdl->PushClipRectFullScreen();
            ui::glass_draw::draw_glass_card(tdl,
                                            tp0,
                                            tp1,
                                            ui::tokens::RADIUS_MD,
                                            theme_colors(),
                                            0.78f,
                                            theme_colors().glow_intensity);
            tdl->PopClipRect();
        }
        ImGui::TextUnformatted(deferred_tooltip_);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
        ImGui::EndTooltip();
        deferred_tooltip_ = nullptr;   // Clear for next frame
    }

    // Draw data interaction overlays (tooltip, crosshair, markers) into a regular
    // ImGui window so they render behind menus/popups in z-order.
    ImDrawList* overlay_dl = nullptr;
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGuiWindowFlags overlay_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##overlay_host", nullptr, overlay_flags))
            overlay_dl = ImGui::GetWindowDrawList();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
    if (data_interaction_)
    {
        ImGuiIO& io = ImGui::GetIO();
        data_interaction_->draw_overlays(io.DisplaySize.x, io.DisplaySize.y, &figure, overlay_dl);

        // In split mode, draw_overlays only draws legends for the active figure.
        // Draw legends for all other split pane figures as well.
        if (dock_system_ && dock_system_->is_split() && get_figure_ptr_)
        {
            auto panes = dock_system_->split_view().all_panes();
            for (auto* pane : panes)
            {
                if (!pane)
                    continue;
                for (auto fig_id : pane->figure_indices())
                {
                    Figure* fig = get_figure_ptr_(fig_id);
                    if (fig && fig != &figure)
                    {
                        data_interaction_->draw_legend_for_figure(*fig);
                    }
                }
            }
        }
    }

    // Draw box zoom overlay (Agent B Week 7) — on top of data overlays
    if (box_zoom_overlay_)
    {
        box_zoom_overlay_->update(dt);
        ImGuiIO& io = ImGui::GetIO();
        box_zoom_overlay_->draw(io.DisplaySize.x, io.DisplaySize.y);
    }

    // Draw select rectangle overlay (Select tool rubber-band)
    if (input_handler_ && input_handler_->is_select_rect_active())
    {
        const auto& sr = input_handler_->select_rect();
        auto        x0 = static_cast<float>(std::min(sr.x0, sr.x1));
        auto        y0 = static_cast<float>(std::min(sr.y0, sr.y1));
        auto        x1 = static_cast<float>(std::max(sr.x0, sr.x1));
        auto        y1 = static_cast<float>(std::max(sr.y0, sr.y1));

        ImDrawList* fg     = ImGui::GetForegroundDrawList();
        const auto& colors = theme_colors();
        ImU32       fill   = IM_COL32(static_cast<uint8_t>(colors.selection_fill.r * 255),
                              static_cast<uint8_t>(colors.selection_fill.g * 255),
                              static_cast<uint8_t>(colors.selection_fill.b * 255),
                              40);
        ImU32       border = IM_COL32(static_cast<uint8_t>(colors.selection_border.r * 255),
                                static_cast<uint8_t>(colors.selection_border.g * 255),
                                static_cast<uint8_t>(colors.selection_border.b * 255),
                                200);
        fg->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill);
        fg->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), border, 0.0f, 0, 1.0f);
    }

    // Draw measure overlay (Measure tool mode)
    // Crosshair is handled by data_interaction (auto-enabled when entering Measure mode)
    if (input_handler_ && input_handler_->tool_mode() == ToolMode::Measure)
    {
        Axes* ax = input_handler_->measure_axes();
        if (!ax)
            ax = input_handler_->active_axes();
        bool dragging   = input_handler_->is_measure_dragging();
        bool has_result = input_handler_->has_measure_result();
        if (ax && (dragging || has_result))
        {
            const auto& vp   = ax->viewport();
            auto        xlim = ax->x_limits();
            auto        ylim = ax->y_limits();

            auto data_to_screen = [&](double dx_, double dy_, float& scr_x, float& scr_y)
            {
                double xr = xlim.max - xlim.min;
                double yr = ylim.max - ylim.min;
                if (xr == 0.0)
                    xr = 1.0;
                if (yr == 0.0)
                    yr = 1.0;
                scr_x = static_cast<float>(vp.x + (dx_ - xlim.min) / xr * vp.w);
                scr_y = static_cast<float>(vp.y + (1.0 - (dy_ - ylim.min) / yr) * vp.h);
            };

            double sx = input_handler_->measure_start_data_x();
            double sy = input_handler_->measure_start_data_y();
            double ex = input_handler_->measure_end_data_x();
            double ey = input_handler_->measure_end_data_y();

            auto  mdx  = static_cast<float>(ex - sx);
            auto  mdy  = static_cast<float>(ey - sy);
            float dist = std::sqrt(mdx * mdx + mdy * mdy);
            if (dist > 1e-6f)
            {
                auto* dl       = ImGui::GetForegroundDrawList();
                auto  accent   = theme_colors().accent;
                ImU32 line_col = IM_COL32(uint8_t(accent.r * 255),
                                          uint8_t(accent.g * 255),
                                          uint8_t(accent.b * 255),
                                          220);
                ImU32 dot_col  = IM_COL32(uint8_t(accent.r * 255),
                                         uint8_t(accent.g * 255),
                                         uint8_t(accent.b * 255),
                                         255);
                ImU32 bg_col   = IM_COL32(uint8_t(theme_colors().bg_elevated.r * 255),
                                        uint8_t(theme_colors().bg_elevated.g * 255),
                                        uint8_t(theme_colors().bg_elevated.b * 255),
                                        230);

                float scr_sx = NAN;
                float scr_sy = NAN;
                float scr_ex = NAN;
                float scr_ey = NAN;
                data_to_screen(sx, sy, scr_sx, scr_sy);
                data_to_screen(ex, ey, scr_ex, scr_ey);

                // Measurement line
                dl->AddLine(ImVec2(scr_sx, scr_sy), ImVec2(scr_ex, scr_ey), line_col, 2.0f);

                // Endpoint dots
                dl->AddCircleFilled(ImVec2(scr_sx, scr_sy), 4.0f, dot_col);
                dl->AddCircleFilled(ImVec2(scr_ex, scr_ey), 4.0f, dot_col);

                // Distance label at midpoint
                float             mid_x = (scr_sx + scr_ex) * 0.5f;
                float             mid_y = (scr_sy + scr_ey) * 0.5f;
                const std::string label =
                    std::format("dX: {:.4f}  dY: {:.4f}  dist: {:.4f}", mdx, mdy, dist);
                ImVec2 tsz = ImGui::CalcTextSize(label.c_str());
                float  pad = 6.0f;
                dl->AddRectFilled(ImVec2(mid_x - tsz.x * 0.5f - pad, mid_y - tsz.y - pad * 2),
                                  ImVec2(mid_x + tsz.x * 0.5f + pad, mid_y - pad * 0.5f),
                                  bg_col,
                                  4.0f);
                dl->AddText(ImVec2(mid_x - tsz.x * 0.5f, mid_y - tsz.y - pad),
                            IM_COL32(255, 255, 255, 240),
                            label.c_str());
            }
        }
    }

    // Handle deferred CSV open request (from welcome screen button)
    if (pending_open_csv_)
    {
        pending_open_csv_ = false;
        if (native_dialogs_enabled())
        {
            DialogEnvGuard env_guard;
            char const*    filters[3] = {"*.csv", "*.tsv", "*.txt"};
            const char*    home_env   = std::getenv("HOME");
            std::string    home_dir   = home_env ? std::string(home_env) + "/" : "/";
            char const*    result     = tinyfd_openFileDialog("Open CSV File",
                                                       home_dir.c_str(),
                                                       3,
                                                       filters,
                                                       "CSV files",
                                                       0);
            if (result)
            {
                csv_file_path_   = result;
                csv_data_        = parse_csv(csv_file_path_);
                csv_data_loaded_ = csv_data_.error.empty();
                csv_error_       = csv_data_.error;
                csv_col_x_       = 0;
                csv_selected_y_.clear();
                for (size_t c = 1; c < csv_data_.num_cols; ++c)
                    csv_selected_y_.push_back(static_cast<int>(c));
                if (csv_selected_y_.empty() && csv_data_.num_cols > 0)
                    csv_selected_y_.push_back(0);
                csv_col_z_ = -1;
                if (csv_data_loaded_)
                    csv_dialog_open_ = true;
            }
        }
    }

    // Handle CSV file dropped via OS drag-and-drop
    if (!pending_csv_drop_path_.empty())
    {
        std::string path = std::move(pending_csv_drop_path_);
        pending_csv_drop_path_.clear();
        csv_file_path_   = path;
        csv_data_        = parse_csv(csv_file_path_);
        csv_data_loaded_ = csv_data_.error.empty();
        csv_error_       = csv_data_.error;
        csv_col_x_       = 0;
        csv_selected_y_.clear();
        for (size_t c = 1; c < csv_data_.num_cols; ++c)
            csv_selected_y_.push_back(static_cast<int>(c));
        if (csv_selected_y_.empty() && csv_data_.num_cols > 0)
            csv_selected_y_.push_back(0);
        csv_col_z_ = -1;
        if (csv_data_loaded_)
            csv_dialog_open_ = true;
    }

    // Draw CSV load dialog if open
    if (csv_dialog_open_)
    {
        draw_csv_dialog();
    }

    // Draw custom transform dialog if open
    if (custom_transform_dialog_.is_open())
    {
        draw_custom_transform_dialog();
    }

    if (plot_overlay_dialog_.is_open())
    {
        draw_plot_overlay_dialog();
    }

    // Draw plugin / topics panels (legacy path when shell registry is unavailable).
    if (!app_shell_)
    {
        if (show_plugins_panel_)
            draw_plugins_panel();

        if (topics_panel_)
            topics_panel_->draw();
    }

    // Draw Settings panel.
    if (settings_panel_)
    {
        settings_panel_->draw();
    }

    // Draw theme settings window if open
    if (show_theme_settings_)
    {
        draw_theme_settings();
    }

    // ── ROS2 Adapter error modal ──────────────────────────────────────────────
    // Shown when the tools.ros2_adapter command encounters a launch failure.
    // The flag is set from the command callback; cleared here after the user
    // dismisses the modal.
    #ifdef SPECTRA_USE_ROS2
    if (ros2_adapter_has_error())
    {
        ImGui::OpenPopup("ROS2 Adapter Error##ros2err");
    }
    if (ImGui::BeginPopupModal("ROS2 Adapter Error##ros2err",
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        const auto& colors = theme_colors();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_primary.r,
                                     colors.text_primary.g,
                                     colors.text_primary.b,
                                     colors.text_primary.a));

        ImGui::TextUnformatted("Could not launch spectra-ros:");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     colors.text_secondary.a));
        ImGui::TextWrapped("%s", ros2_adapter_pending_error().c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextUnformatted("Make sure a ROS2 workspace is sourced and\n"
                               "spectra-ros is on your PATH.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float btn_w = 80.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - btn_w) * 0.5f + ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("OK", ImVec2(btn_w, 0)))
        {
            ros2_adapter_clear_error();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    #endif   // SPECTRA_USE_ROS2

    // Draw directional dock highlight overlay when another window is dragging a tab over this one
    if (window_manager_ && window_id_ != 0 && window_manager_->drag_target_window() == window_id_)
    {
        auto&       theme     = theme_colors();
        auto        drop_info = window_manager_->cross_window_drop_info();
        ImDrawList* dl        = ImGui::GetForegroundDrawList();

        if (drop_info.zone >= 1 && drop_info.zone <= 5)
        {
            // Draw highlight rect for the active drop zone
            ImU32 highlight_color  = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                             static_cast<int>(theme.accent.g * 255),
                                             static_cast<int>(theme.accent.b * 255),
                                             40);
            ImU32 highlight_border = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                              static_cast<int>(theme.accent.g * 255),
                                              static_cast<int>(theme.accent.b * 255),
                                              160);

            float hx = drop_info.hx;
            float hy = drop_info.hy;
            float hw = drop_info.hw;
            float hh = drop_info.hh;

            dl->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + hw, hy + hh), highlight_color, 4.0f);
            dl->AddRect(ImVec2(hx, hy), ImVec2(hx + hw, hy + hh), highlight_border, 4.0f, 0, 2.0f);

            // Draw a label indicating the action
            const char* label = nullptr;
            switch (drop_info.zone)
            {
                case 1:
                    label = "Split Left";
                    break;
                case 2:
                    label = "Split Right";
                    break;
                case 3:
                    label = "Split Up";
                    break;
                case 4:
                    label = "Split Down";
                    break;
                case 5:
                    label = "Add Tab";
                    break;
                default:
                    break;
            }
            if (label)
            {
                ImVec2 lsz = ImGui::CalcTextSize(label);
                float  lx  = hx + (hw - lsz.x) * 0.5f;
                float  ly  = hy + (hh - lsz.y) * 0.5f;
                float  pad = 10.0f;
                dl->AddRectFilled(ImVec2(lx - pad, ly - pad),
                                  ImVec2(lx + lsz.x + pad, ly + lsz.y + pad),
                                  IM_COL32(30, 30, 30, 200),
                                  6.0f);
                dl->AddText(ImVec2(lx, ly),
                            IM_COL32(static_cast<int>(theme.accent.r * 255),
                                     static_cast<int>(theme.accent.g * 255),
                                     static_cast<int>(theme.accent.b * 255),
                                     220),
                            label);
            }
        }
    }

    // Extra draw callback (used by spectra-ros to inject ROS2 panels)
    if (extra_draw_cb_)
        extra_draw_cb_();

    // Draw knobs panel last (above all other windows, user-moveable)
    if (knob_manager_ && !knob_manager_->empty())
    {
        draw_knobs_panel();
    }

    // Draw command palette overlay (Agent F) — must be last to render on top
    if (command_palette_)
    {
        ImGuiIO& io = ImGui::GetIO();
        command_palette_->draw(io.DisplaySize.x, io.DisplaySize.y);
    }

    ui::log_imgui_frame_activations();
}

void ImGuiIntegration::build_empty_ui()
{
    if (!initialized_)
        return;

    current_figure_ = nullptr;

    float dt = ImGui::GetIO().DeltaTime;
    if (theme_mgr_)
        theme_mgr_->update(dt);

    // Draw command bar (menu) so user can create figures / load CSV.
    // Suppressed when an adapter shell provides its own menu (e.g. spectra-ros).
    if (command_bar_visible_)
        draw_command_bar();

    // Fill the rest with the background color
    auto&       theme = theme_colors();
    ImDrawList* bg    = ImGui::GetBackgroundDrawList();
    ImGuiIO&    io    = ImGui::GetIO();
    bg->AddRectFilled(ImVec2(0, 0),
                      ImVec2(io.DisplaySize.x, io.DisplaySize.y),
                      IM_COL32(static_cast<int>(theme.bg_primary.r * 255),
                               static_cast<int>(theme.bg_primary.g * 255),
                               static_cast<int>(theme.bg_primary.b * 255),
                               255));

    // Draw welcome screen with centered logo
    draw_welcome_screen(io.DisplaySize.x, io.DisplaySize.y, dt);

    // Draw CSV dialog if open (user may have opened it from the menu)
    if (csv_dialog_open_)
        draw_csv_dialog();

    // Plugins panel can be opened while no figure exists.
    if (show_plugins_panel_)
        draw_plugins_panel();

    // Topics panel can be opened while no figure exists.
    if (topics_panel_)
        topics_panel_->draw();

    // Settings panel can be opened while no figure exists.
    if (settings_panel_)
        settings_panel_->draw();

    // Welcome-state drop target: allows dragging a topic to auto-create a new figure.
    draw_topic_drop_target_welcome();
}

// ─── Welcome screen logo texture ────────────────────────────────────────────

void ImGuiIntegration::load_logo_textures(VulkanBackend& backend)
{
    if (logo_loaded_)
        return;
    logo_loaded_ = true;   // Don't retry on failure

    const bool welcome_ok = load_embedded_texture(backend,
                                                  SpectraIcon_png_data,
                                                  SpectraIcon_png_size,
                                                  &welcome_logo_texture_,
                                                  &welcome_logo_texture_id_,
                                                  &welcome_logo_width_,
                                                  &welcome_logo_height_,
                                                  "welcome logo");

    // Reuse the same icon for the corner mark (the round dark icon reads well at 24px)
    corner_logo_texture_    = welcome_logo_texture_;
    corner_logo_texture_id_ = welcome_logo_texture_id_;
    corner_logo_width_      = welcome_logo_width_;
    corner_logo_height_     = welcome_logo_height_;
    const bool corner_ok    = welcome_ok;

    if (welcome_ok)
        SPECTRA_LOG_DEBUG("imgui",
                          "Welcome logo texture loaded: {}x{}",
                          welcome_logo_width_,
                          welcome_logo_height_);
    if (corner_ok)
        SPECTRA_LOG_DEBUG("imgui",
                          "Corner logo: reusing welcome texture {}x{}",
                          corner_logo_width_,
                          corner_logo_height_);
}

void ImGuiIntegration::destroy_logo_textures()
{
    // Corner is an alias of welcome — clear it first without destroying
    corner_logo_texture_id_ = 0;
    corner_logo_texture_    = {};
    corner_logo_width_      = 0;
    corner_logo_height_     = 0;

    if (welcome_logo_texture_id_ != 0)
    {
        ImGui_ImplVulkan_RemoveTexture(u64_to_handle<VkDescriptorSet>(welcome_logo_texture_id_));
        welcome_logo_texture_id_ = 0;
    }

    if (backend_ && welcome_logo_texture_)
    {
        backend_->destroy_texture(welcome_logo_texture_);
        welcome_logo_texture_ = {};
    }

    welcome_logo_width_  = 0;
    welcome_logo_height_ = 0;
}

void ImGuiIntegration::draw_welcome_screen(float display_w, float display_h, float dt)
{
    (void)dt;
    const auto& colors = theme_colors();
    ImDrawList* fg     = ImGui::GetBackgroundDrawList();

    // Menu bar height offset
    float menu_h    = ImGui::GetFrameHeight() + 2.0f;
    float content_h = display_h - menu_h;
    float cx        = display_w * 0.5f;

    // Vertical center of the content area (below menu bar)
    float center_y = menu_h + content_h * 0.45f;

    // ── Logo image ──
    if (welcome_logo_texture_id_)
    {
        float logo_draw_sz = std::clamp(content_h * 0.28f, 180.0f, 220.0f);
        float lx           = cx - logo_draw_sz * 0.5f;
        float ly           = center_y - logo_draw_sz * 0.5f;
        fg->AddCircleFilled(ImVec2(cx, center_y),
                            logo_draw_sz * 0.62f,
                            IM_COL32(static_cast<int>(colors.accent.r * 255),
                                     static_cast<int>(colors.accent.g * 255),
                                     static_cast<int>(colors.accent.b * 255),
                                     16),
                            96);
        fg->AddImage(imgui_texture_id_from_u64(welcome_logo_texture_id_),
                     ImVec2(lx, ly),
                     ImVec2(lx + logo_draw_sz, ly + logo_draw_sz));
    }
    else
    {
        // Fallback: draw chart-line icon with accent glow
        float icon_cy = center_y;
        for (int i = 0; i < 3; ++i)
        {
            float r = 42.0f + static_cast<float>(i) * 10.0f;
            float a = 0.10f - static_cast<float>(i) * 0.03f;
            fg->AddCircleFilled(ImVec2(cx, icon_cy),
                                r,
                                IM_COL32(static_cast<int>(colors.accent.r * 255),
                                         static_cast<int>(colors.accent.g * 255),
                                         static_cast<int>(colors.accent.b * 255),
                                         static_cast<int>(a * 255)),
                                64);
        }
        ImFont* ifont = ui::icon_font(ui::tokens::ICON_XL);
        if (ifont)
        {
            const char* icon = ui::icon_str(ui::Icon::ChartLine);
            ImVec2      sz   = ifont->CalcTextSizeA(ui::tokens::ICON_XL, FLT_MAX, 0.0f, icon);
            fg->AddText(ifont,
                        ui::tokens::ICON_XL,
                        ImVec2(cx - sz.x * 0.5f, icon_cy - sz.y * 0.5f),
                        IM_COL32(static_cast<int>(colors.accent.r * 255),
                                 static_cast<int>(colors.accent.g * 255),
                                 static_cast<int>(colors.accent.b * 255),
                                 255),
                        icon);
        }
    }

    // ── "Spectra" title ──
    {
        const char* title     = "Spectra";
        ImFont*     font      = font_title_ ? font_title_ : ImGui::GetFont();
        float       font_size = font_title_ ? font_title_->LegacySize : 26.0f;
        ImVec2      sz        = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, title);
        fg->AddText(font,
                    font_size,
                    ImVec2(cx - sz.x * 0.5f, center_y + 155.0f),
                    IM_COL32(static_cast<int>(colors.text_primary.r * 255),
                             static_cast<int>(colors.text_primary.g * 255),
                             static_cast<int>(colors.text_primary.b * 255),
                             255),
                    title);
    }

    // ── Subtitle ──
    {
        const char* sub  = "GPU-Accelerated Scientific Visualization";
        ImFont*     font = ImGui::GetFont();
        ImVec2      sz   = font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, sub);
        fg->AddText(font,
                    12.0f,
                    ImVec2(cx - sz.x * 0.5f, center_y + 187.0f),
                    IM_COL32(static_cast<int>(colors.text_secondary.r * 255),
                             static_cast<int>(colors.text_secondary.g * 255),
                             static_cast<int>(colors.text_secondary.b * 255),
                             220),
                    sub);
    }

    // ── Keyboard hint ──
    {
        const char* hint   = "Press Ctrl+T to create a new figure";
        ImFont*     font   = ImGui::GetFont();
        ImVec2      sz     = font->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, hint);
        float       hint_y = display_h - 52.0f;
        fg->AddText(font,
                    12.0f,
                    ImVec2(cx - sz.x * 0.5f, hint_y),
                    IM_COL32(static_cast<int>(colors.text_tertiary.r * 255),
                             static_cast<int>(colors.text_tertiary.g * 255),
                             static_cast<int>(colors.text_tertiary.b * 255),
                             220),
                    hint);
    }

    // ── Version ──
    {
    #ifdef SPECTRA_VERSION_STRING
        const char* ver = "v" SPECTRA_VERSION_STRING;
    #else
        const char* ver = "v0.1.2";
    #endif
        ImFont* font = ImGui::GetFont();
        ImVec2  sz   = font->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, ver);
        fg->AddText(font,
                    11.0f,
                    ImVec2(cx - sz.x * 0.5f, display_h - 30.0f),
                    IM_COL32(static_cast<int>(colors.text_tertiary.r * 255),
                             static_cast<int>(colors.text_tertiary.g * 255),
                             static_cast<int>(colors.text_tertiary.b * 255),
                             220),
                    ver);
    }
}

void ImGuiIntegration::render(VulkanBackend& backend) const
{
    if (!initialized_)
        return;
    ImGui::Render();
    auto* dd = ImGui::GetDrawData();
    if (dd)
        ImGui_ImplVulkan_RenderDrawData(dd, backend.current_command_buffer());
}

void ImGuiIntegration::render_viewports()
{
    // No-op — see enable_viewports().
}

bool ImGuiIntegration::wants_capture_mouse() const
{
    if (!initialized_)
        return false;

    bool wants_capture      = ImGui::GetIO().WantCaptureMouse;
    bool any_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    bool any_item_hovered   = ImGui::IsAnyItemHovered();
    bool any_item_active    = ImGui::IsAnyItemActive();

    // If an ImGui item is actively being interacted with (e.g. dragging a slider),
    // always capture — regardless of cursor position.
    if (any_item_active)
        return true;

    if (layout_manager_)
    {
        Rect       canvas    = layout_manager_->canvas_rect();
        ImVec2     mouse     = ImGui::GetIO().MousePos;
        const bool in_canvas = mouse.x >= canvas.x && mouse.x < canvas.x + canvas.w
                               && mouse.y >= canvas.y && mouse.y < canvas.y + canvas.h;

        // Outside the plot canvas: never forward pointer events to InputHandler.
        // Chrome windows (command bar, nav rail, status bar, inspector) may not set
        // WantCaptureMouse on empty padding, but they still own the pointer.
        if (!in_canvas)
            return true;

        // Inside canvas: pass through unless an ImGui item/window is hovered
        // (floating panels, inspector toggle, pane tab headers, etc.).
        return any_item_hovered || any_window_hovered;
    }

    return wants_capture && (any_window_hovered || any_item_hovered);
}
bool ImGuiIntegration::wants_capture_keyboard() const
{
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

// ─── Fonts ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::load_fonts()
{
    ImGuiIO& io = ImGui::GetIO();

    // Font Awesome 6 Free Solid — only the codepoints we actually use.
    // Two coalesced ranges covering our Icon enum values:
    //   U+E473–U+E522  (supplemental: chart-simple, magnifying-glass-chart)
    //   U+F002–U+F698  (main FA6 block — covers all our icons)
    static const ImWchar icon_ranges[] = {0xE473, 0xE522, 0xF002, 0xF698, 0};

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;   // we own the static data
    cfg.OversampleH          = 4;       // sharper horizontal sub-pixel positioning
    cfg.OversampleV          = 2;       // sharper vertical edges
    cfg.PixelSnapH           = true;    // snap glyphs to whole pixels for crispness

    // Helper lambda: add Inter font at a given size, then merge FA6 icons
    auto add_font_pair = [&](float size) -> ImFont*
    {
        cfg.SizePixels = 0;
        ImFont* font   = io.Fonts->AddFontFromMemoryCompressedTTF(InterFont_compressed_data,
                                                                InterFont_compressed_size,
                                                                size,
                                                                &cfg);

        ImFontConfig icon_cfg;
        icon_cfg.FontDataOwnedByAtlas = false;
        icon_cfg.MergeMode            = true;
        icon_cfg.GlyphMinAdvanceX     = 0.0f;   // natural glyph widths
        icon_cfg.PixelSnapH           = true;

        io.Fonts->AddFontFromMemoryTTF((void*)fa_solid_900_data,
                                       fa_solid_900_size,
                                       size,
                                       &icon_cfg,
                                       icon_ranges);
        return font;
    };

    // Body font (16px) + FA6 icon merge
    font_body_ = add_font_pair(16.0f);

    // Heading font (12.5px) + FA6 icon merge
    font_heading_ = add_font_pair(11.5f);

    // Icon font (20px) — primary icon font with Inter merged in
    font_icon_ = add_font_pair(20.0f);

    // Title font (18px) + FA6 icon merge
    font_title_ = add_font_pair(20.0f);

    // Menubar font (15px) + FA6 icon merge
    font_menubar_ = add_font_pair(14.0f);

    io.FontDefault = font_body_;
}

// ─── Style ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::apply_modern_style()
{
    // Apply theme colors through ThemeManager
    if (theme_mgr_)
        theme_mgr_->apply_to_imgui();
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
