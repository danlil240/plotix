#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cfloat>
    #include <cstdint>
    #include <functional>
    #include <memory>
    #include <spectra/fwd.hpp>
    #include <string>
    #include <unordered_map>
    #include <vector>

    #include "render/backend.hpp"
    #include "ui/data/csv_loader.hpp"

    #include "ui/docking/dock_system.hpp"
    #include "ui/input/input.hpp"
    #include "ui/overlay/custom_transform_dialog.hpp"
    #include "ui/overlay/plot_overlay_dialog.hpp"
    #include "ui/overlay/data_editor.hpp"
    #include "ui/overlay/data_interaction.hpp"
    #include "ui/overlay/inspector.hpp"
    #include "ui/layout/layout_manager.hpp"
    #include "ui/input/selection_context.hpp"

    #ifdef SPECTRA_USE_GLFW
struct GLFWwindow;
    #endif
    #ifdef SPECTRA_USE_SDL3
struct SDL_Window;
    #endif
struct ImFont;
struct ImFontAtlas;
struct ImGuiContext;
struct ImVec2;

namespace spectra::ui
{
struct ThemeColors;
class ThemeManager;
namespace shell
{
class SpectraAppShell;
class SpectraNavRail;
class StatusBar;
}   // namespace shell
namespace topics
{
class TopicsPanel;
}
namespace settings
{
class SettingsPanel;
}
}   // namespace spectra::ui

namespace spectra
{

class AnimationCurveEditor;
class AxisLinkManager;
class BoxZoomOverlay;
class CommandPalette;
class CommandRegistry;
class KnobManager;
class DataInteraction;
class DockSystem;
class ExportFormatRegistry;
class KeyframeInterpolator;
class ModeTransition;
class OverlayRegistry;
class PluginManager;
class Renderer;
class ShortcutManager;
class TabBar;
class TimelineEditor;
class UndoManager;
class VulkanBackend;
class WindowManager;

class ImGuiIntegration
{
    friend class ui::shell::SpectraAppShell;
    friend class ui::shell::SpectraNavRail;

   public:
    struct MenuItem
    {
        std::string           label;
        std::function<void()> callback;
        MenuItem(const std::string& l, std::function<void()> cb = nullptr) : label(l), callback(cb)
        {
        }
    };

    ImGuiIntegration() = default;
    ~ImGuiIntegration();

    ImGuiIntegration(const ImGuiIntegration&)            = delete;
    ImGuiIntegration& operator=(const ImGuiIntegration&) = delete;

    #ifdef SPECTRA_USE_GLFW
    bool init(VulkanBackend& backend, GLFWwindow* window, bool install_callbacks = true);
    #endif
    #ifdef SPECTRA_USE_SDL3
    bool init_sdl3(VulkanBackend& backend, SDL_Window* window);
    #endif
    bool is_initialized() const { return initialized_; }

    // Headless init: no GLFW window, no platform backend.
    // Input is fed manually via new_frame_headless().
    // Used by EmbedSurface for offscreen ImGui rendering.
    bool init_headless(VulkanBackend& backend, uint32_t width, uint32_t height);

    void shutdown();

    void new_frame();

    // Headless new_frame: manually set display size + input state.
    // Call instead of new_frame() when initialized with init_headless().
    struct HeadlessFrameInput
    {
        float display_w     = 800.0f;
        float display_h     = 600.0f;
        float dt            = 1.0f / 60.0f;
        float mouse_x       = -FLT_MAX;
        float mouse_y       = -FLT_MAX;
        bool  mouse_down[5] = {};
        float mouse_wheel   = 0.0f;
        float mouse_wheel_h = 0.0f;
        float dpi_scale     = 1.0f;
    };
    void new_frame_headless(const HeadlessFrameInput& input);

    void build_ui(Figure& figure, FigureViewModel* vm = nullptr);
    void build_empty_ui();   // Render menu bar only (no figure)
    void render(VulkanBackend& backend) const;

    // Render a tearoff preview card into a preview window.
    // If figure is provided, renders actual series data; otherwise a placeholder.
    void build_preview_ui(const std::string& title, Figure* figure = nullptr);

    void on_swapchain_recreated(VulkanBackend& backend);

    // Layout state can be queried by commands even when ImGui init failed.
    // Lazily materialize it so those command paths degrade safely instead of
    // dereferencing a null LayoutManager.
    LayoutManager& get_layout_manager()
    {
        if (!layout_manager_)
            layout_manager_ = std::make_unique<LayoutManager>();
        return *layout_manager_;
    }
    void update_layout(float window_width, float window_height, float dt = 0.0f);

    // True while inspector/nav layout, section fold, theme, or mode transitions run.
    // Used by the event-driven render loop to avoid sleeping through UI animations.
    bool has_active_chrome_animations() const;

    bool wants_capture_mouse() const;
    bool wants_capture_keyboard() const;
    bool is_tab_interacting() const { return pane_tab_hovered_ || pane_tab_drag_.dragging; }
    bool is_menu_open() const { return !open_menu_label_.empty(); }

    // Reset pane-tab drag and dock split drag (does not close open menus).
    void cancel_tab_drag_state()
    {
        pane_tab_hovered_ = false;
        pane_tab_drag_    = PaneTabDragState{};
        if (dock_system_)
            dock_system_->cancel_drag();
    }

    // Reset transient UI capture state after MCP automation clicks (tab drag, open menus).
    void dismiss_automation_capture()
    {
        open_menu_label_.clear();
        cancel_tab_drag_state();
    }

    // Returns the FigureId being torn off (preview card active), or INVALID_FIGURE_ID if none.
    FigureId tearoff_figure() const
    {
        return (pane_tab_drag_.dragging && pane_tab_drag_.preview_active)
                   ? pane_tab_drag_.dragged_figure_index
                   : INVALID_FIGURE_ID;
    }

    // Interaction state getters
    bool     should_reset_view() const { return reset_view_; }
    void     clear_reset_view() { reset_view_ = false; }
    bool     should_reset_live_view() const { return reset_live_view_; }
    void     clear_reset_live_view() { reset_live_view_ = false; }
    ToolMode get_interaction_mode() const { return interaction_mode_; }
    bool     is_transform_dialog_open() const { return custom_transform_dialog_.is_open(); }
    bool     is_plot_overlay_dialog_open() const { return plot_overlay_dialog_.is_open(); }
    ui::PlotOverlayDialog&       plot_overlay_dialog() { return plot_overlay_dialog_; }
    const ui::PlotOverlayDialog& plot_overlay_dialog() const { return plot_overlay_dialog_; }
    bool                         is_theme_settings_visible() const { return show_theme_settings_; }

    // Status bar data setters (called by app loop with real data)
    // Double precision: x can sit at epoch scale (~1.7e9).
    void set_cursor_data(double x, double y, bool valid)
    {
        cursor_data_x_     = x;
        cursor_data_y_     = y;
        cursor_data_valid_ = valid;
    }
    void set_zoom_level(float zoom) { zoom_level_ = zoom; }
    void set_gpu_time(float ms) { gpu_time_ms_ = ms; }

    // Injected ThemeManager (not owned). Must be set before init().
    void              set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }
    ui::ThemeManager* theme_mgr() const { return theme_mgr_; }

    // Data interaction layer (owned externally by App)
    void             set_data_interaction(DataInteraction* di) { data_interaction_ = di; }
    DataInteraction* data_interaction() const { return data_interaction_; }

    // Box zoom overlay (Agent B Week 7, owned externally by App)
    void            set_box_zoom_overlay(BoxZoomOverlay* bzo) { box_zoom_overlay_ = bzo; }
    BoxZoomOverlay* box_zoom_overlay() const { return box_zoom_overlay_; }

    // Command palette & productivity (Agent F, owned externally by App)
    void             set_command_palette(CommandPalette* cp) { command_palette_ = cp; }
    void             set_command_registry(CommandRegistry* cr) { command_registry_ = cr; }
    void             set_shortcut_manager(ShortcutManager* sm) { shortcut_manager_ = sm; }
    void             set_undo_manager(UndoManager* um) { undo_manager_ = um; }
    CommandPalette*  command_palette() const { return command_palette_; }
    CommandRegistry* command_registry() const { return command_registry_; }
    ShortcutManager* shortcut_manager() const { return shortcut_manager_; }
    UndoManager*     undo_manager() const { return undo_manager_; }

    // Dock system (Agent A Week 9, owned externally by App)
    void        set_dock_system(DockSystem* ds) { dock_system_ = ds; }
    DockSystem* dock_system() const { return dock_system_; }

    // Axis link manager (Agent B Week 10, owned externally by App)
    void             set_axis_link_manager(AxisLinkManager* alm) { axis_link_mgr_ = alm; }
    AxisLinkManager* axis_link_manager() const { return axis_link_mgr_; }

    // Input handler (for right-click context menu hit-testing)
    void          set_input_handler(InputHandler* ih) { input_handler_ = ih; }
    InputHandler* input_handler() const { return input_handler_; }

    // Programmatically open the pane tab context menu (for QA agent testing)
    void open_tab_context_menu(FigureId fig_id)
    {
        pane_ctx_menu_fig_  = fig_id;
        pane_ctx_menu_open_ = true;
    }

    // Programmatically close the pane tab context menu
    void close_tab_context_menu()
    {
        pane_ctx_menu_fig_             = INVALID_FIGURE_ID;
        pane_ctx_menu_open_            = false;
        pane_ctx_menu_close_requested_ = true;
    }

    // Timeline editor (Agent G, owned externally by App)
    void            set_timeline_editor(TimelineEditor* te) { timeline_editor_ = te; }
    TimelineEditor* timeline_editor() const { return timeline_editor_; }

    // Keyframe interpolator (Agent G, owned externally by App)
    void set_keyframe_interpolator(KeyframeInterpolator* ki) { keyframe_interpolator_ = ki; }
    KeyframeInterpolator* keyframe_interpolator() const { return keyframe_interpolator_; }

    // Animation curve editor (Agent G, owned externally by App)
    void                  set_curve_editor(AnimationCurveEditor* ce) { curve_editor_ = ce; }
    AnimationCurveEditor* curve_editor() const { return curve_editor_; }

    // Series clipboard (owned externally by App)
    void set_series_clipboard(SeriesClipboard* sc)
    {
        series_clipboard_ = sc;
        inspector_.set_series_clipboard(sc);
        inspector_.set_defer_series_removal([this](AxesBase* owner, Series* s)
                                            { defer_series_removal(owner, s); });
    }
    SeriesClipboard* series_clipboard() const { return series_clipboard_; }

    // Mode transition (Agent 6 Week 11, owned externally by App)
    void            set_mode_transition(ModeTransition* mt) { mode_transition_ = mt; }
    ModeTransition* mode_transition() const { return mode_transition_; }

    // (Text renderer removed — plot text now rendered by Renderer::render_plot_text)

    // Knob manager (interactive parameter controls, owned externally by WindowUIContext)
    void         set_knob_manager(KnobManager* km) { knob_manager_ = km; }
    KnobManager* knob_manager() const { return knob_manager_; }

    // Plugin overlay registry (shared, not owned — owned by PluginManager)
    void             set_overlay_registry(OverlayRegistry* reg) { overlay_registry_ = reg; }
    OverlayRegistry* overlay_registry() const { return overlay_registry_; }

    // Plugin export format registry (shared, not owned — owned by PluginManager)
    void set_export_format_registry(ExportFormatRegistry* reg) { export_format_registry_ = reg; }
    ExportFormatRegistry* export_format_registry() const { return export_format_registry_; }

    // Plugin manager (shared, not owned — owned by AppRuntime)
    void           set_plugin_manager(PluginManager* mgr) { plugin_manager_ = mgr; }
    PluginManager* plugin_manager() const { return plugin_manager_; }

    // Plugins panel visibility
    bool is_plugins_panel_visible() const { return show_plugins_panel_; }
    void set_plugins_panel_visible(bool v) { show_plugins_panel_ = v; }

    // Topics panel (Phase 2 of plans/SPECTRA_TOPICS_PLAN.md).
    // The panel is owned externally (WindowUIContext); ImGuiIntegration only
    // calls draw() on it during the UI pass.  May be nullptr when no
    // transport is wired (e.g. in-process mode for now).
    void                     set_topics_panel(ui::topics::TopicsPanel* p) { topics_panel_ = p; }
    ui::topics::TopicsPanel* topics_panel() const { return topics_panel_; }

    // Settings panel — owned externally (WindowUIContext).
    void set_settings_panel(ui::settings::SettingsPanel* p) { settings_panel_ = p; }
    ui::settings::SettingsPanel* settings_panel() const { return settings_panel_; }

    // Tab bar (owned externally by App)
    void    set_tab_bar(TabBar* tb) { tab_bar_ = tb; }
    TabBar* tab_bar() const { return tab_bar_; }

    // Tab drag controller (owned externally by WindowUIContext)
    void set_tab_drag_controller(TabDragController* tdc) { tab_drag_controller_ = tdc; }
    TabDragController* tab_drag_controller() const { return tab_drag_controller_; }

    // Window identity (for dock highlight overlay)
    void set_window_id(uint32_t id) { window_id_ = id; }
    void set_window_manager(class WindowManager* wm) { window_manager_ = wm; }

    // Pane tab context menu callbacks (wired by App)
    using PaneTabCallback = std::function<void(FigureId figure_id)>;
    using PaneTabDetachCallback =
        std::function<void(FigureId figure_id, float screen_x, float screen_y)>;
    using PaneTabRenameCallback =
        std::function<void(FigureId figure_id, const std::string& new_title)>;

    // Returns the per-window ImGui context — used by automation to inject into
    // the correct context when multiple windows are active.
    ImGuiContext* imgui_context() const { return imgui_context_; }

    void set_pane_tab_duplicate_cb(PaneTabCallback cb) { pane_tab_duplicate_cb_ = std::move(cb); }
    void set_pane_tab_close_cb(PaneTabCallback cb) { pane_tab_close_cb_ = std::move(cb); }
    void set_pane_tab_split_right_cb(PaneTabCallback cb)
    {
        pane_tab_split_right_cb_ = std::move(cb);
    }
    void set_pane_tab_split_down_cb(PaneTabCallback cb) { pane_tab_split_down_cb_ = std::move(cb); }
    void set_pane_tab_detach_cb(PaneTabDetachCallback cb) { pane_tab_detach_cb_ = std::move(cb); }
    void set_pane_tab_rename_cb(PaneTabRenameCallback cb) { pane_tab_rename_cb_ = std::move(cb); }

    // CSV data load callback: called when user confirms column selection.
    // Args: file path, x column data, y column data, x header, y header,
    //       optional z column data, optional z header, x column base offset.
    // x_offset is the double-precision base of the x column (non-zero for
    // datetime/epoch-timestamp columns); absolute x = x_offset + x[i].
    using CsvPlotCallback = std::function<void(const std::string&        path,
                                               const std::vector<float>& x,
                                               const std::vector<float>& y,
                                               const std::string&        x_label,
                                               const std::string&        y_label,
                                               const std::vector<float>* z,
                                               const std::string*        z_label,
                                               double                    x_offset)>;
    void set_csv_plot_callback(CsvPlotCallback cb) { csv_plot_cb_ = std::move(cb); }

    // Queue a CSV file for loading (e.g. from OS drag-and-drop).
    // The file is parsed and the column picker dialog is opened on the next build_ui() call.
    void request_csv_load(const std::string& path) { pending_csv_drop_path_ = path; }

    // Extra draw callback — called at the end of build_ui() within the active
    // ImGui frame.  Used by spectra-ros to inject ROS2 panels.
    using ExtraDrawCallback = std::function<void()>;
    void set_extra_draw_callback(ExtraDrawCallback cb) { extra_draw_cb_ = std::move(cb); }

    // Scene render callback — called by the render loop DURING the active
    // Vulkan render pass, right after figure content and before ImGui overlay.
    // Used by spectra-ros to issue GPU draw calls for the 3D scene viewport.
    using SceneRenderCallback = std::function<void(Renderer&)>;
    void set_scene_render_callback(SceneRenderCallback cb) { scene_render_cb_ = std::move(cb); }
    const SceneRenderCallback& scene_render_callback() const { return scene_render_cb_; }

    // Panel visibility toggles
    bool is_timeline_visible() const { return show_timeline_; }
    void set_timeline_visible(bool v) { show_timeline_ = v; }
    bool is_curve_editor_visible() const { return show_curve_editor_; }
    void set_curve_editor_visible(bool v) { show_curve_editor_ = v; }
    bool is_nav_rail_visible() const { return show_nav_rail_; }
    void set_nav_rail_visible(bool v);

    // Chrome visibility — set to false by adapters that provide their own UI
    // (e.g. spectra-ros supplies its own menu bar, status bar, and canvas).
    bool is_command_bar_visible() const { return command_bar_visible_; }
    void set_command_bar_visible(bool v) { command_bar_visible_ = v; }
    bool is_status_bar_visible() const { return status_bar_visible_; }
    void set_status_bar_visible(bool v) { status_bar_visible_ = v; }
    bool is_canvas_visible() const { return canvas_visible_; }
    void set_canvas_visible(bool v) { canvas_visible_ = v; }

    // Shell chrome (inspector toggle, pane tab headers, splitters, chrome backdrops).
    // Set to false by adapters that provide their own dock/tab chrome (e.g. Qt).
    bool is_shell_chrome_visible() const { return shell_chrome_visible_; }
    void set_shell_chrome_visible(bool v) { shell_chrome_visible_ = v; }
    void set_external_inspector_toggle_callbacks(std::function<void()> toggle,
                                                 std::function<bool()> is_open)
    {
        external_inspector_toggle_visible_ = static_cast<bool>(toggle);
        external_inspector_toggle_cb_      = std::move(toggle);
        external_inspector_open_cb_        = std::move(is_open);
    }

    // When true, Vulkan figure rendering (axes, series, gridlines) proceeds
    // even if the ImGui canvas overlay is hidden.  This lets adapter shells
    // suppress Spectra's canvas chrome while still rendering plot content.
    bool is_render_figure_enabled() const { return render_figure_; }
    void set_render_figure_enabled(bool v) { render_figure_ = v; }

    // Enable ImGui docking support.  Must be called BEFORE the first
    // NewFrame() to avoid an ImGui assertion.
    void enable_docking();

    // Enable ImGui multi-viewport support (tear-off OS windows).
    // Must be called BEFORE the first NewFrame().
    void enable_viewports();

    // Render secondary viewport windows (detached panels in their own OS windows).
    // Must be called AFTER the main render pass ends and command buffer is submitted.
    void render_viewports();

    // Series selection from canvas click (updates inspector context, toggles on re-click)
    void select_series(Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx);

    // Select series without toggle behavior (for right-click context menu)
    void select_series_no_toggle(Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx);

    // Add/toggle a series in multi-selection (shift-click)
    void toggle_series_in_selection(Figure*   fig,
                                    Axes*     ax,
                                    AxesBase* ab,
                                    int       ax_idx,
                                    Series*   s,
                                    int       s_idx);

    // Deselect any currently selected series (canvas click on empty area)
    void deselect_series();

    // Select multiple series from rectangle drag (replaces current selection)
    void select_series_in_rect(const std::vector<DataInteraction::RectSelectedEntry>& entries);

    // Switch inspector to Series section (for programmatic series cycling)
    void set_inspector_section_series() { active_section_ = Section::Series; }

    // Get/set selection context for programmatic series cycling
    ui::SelectionContext&       selection_context() { return selection_ctx_; }
    const ui::SelectionContext& selection_context() const { return selection_ctx_; }

    // Invalidate cached figure/axes/series pointers when a figure is destroyed,
    // preventing dangling pointer dereference in inspector rendering.
    void clear_figure_cache(Figure* fig)
    {
        if (selection_ctx_.figure == fig)
            selection_ctx_.clear();
        if (inspector_.context().figure == fig)
            inspector_.set_context({});
    }

    // Called when a series is about to be destroyed.  Clears any selection
    // context entries that reference the doomed series so the inspector
    // never dereferences a dangling pointer.
    void notify_series_removed(const Series* s)
    {
        if (selection_ctx_.series == s)
            selection_ctx_.clear();
        else
        {
            auto& sv = selection_ctx_.selected_series;
            for (auto it = sv.begin(); it != sv.end(); ++it)
            {
                if (it->series == s)
                {
                    sv.erase(it);
                    if (sv.empty())
                        selection_ctx_.clear();
                    break;
                }
            }
        }
    }

    // ── Deferred series removal ──────────────────────────────────────
    // Series deletion must be deferred so the user's on_frame callback
    // (which may hold raw Series& references) runs before the series is
    // destroyed.  Commands push entries here; WindowRuntime flushes them
    // after drive_figure_anim.
    struct PendingSeriesRemoval
    {
        AxesBase* owner;
        Series*   series;
    };

    void defer_series_removal(AxesBase* owner, Series* series)
    {
        pending_series_removals_.push_back({owner, series});
    }

    // Execute all pending removals.  Called by WindowRuntime::update()
    // after the user's on_frame callback has finished.
    void flush_deferred_series_removals()
    {
        if (pending_series_removals_.empty())
            return;
        for (auto& r : pending_series_removals_)
        {
            if (!r.owner || !r.series)
                continue;
            auto& svec = r.owner->series_mut();
            for (size_t i = 0; i < svec.size(); ++i)
            {
                if (svec[i].get() == r.series)
                {
                    r.owner->remove_series(i);
                    break;
                }
            }
        }
        pending_series_removals_.clear();
    }

    bool has_pending_series_removals() const { return !pending_series_removals_.empty(); }

    // Shared shell framework (Phase 3 core unification).
    void populate_status_bar(ui::shell::StatusBar& bar);
    void render_menubar_menu(const char* label, const std::vector<MenuItem>& items);

    ui::shell::SpectraAppShell* app_shell() const { return app_shell_; }
    void set_app_shell(ui::shell::SpectraAppShell* shell) { app_shell_ = shell; }

    // Called by SpectraCanvasHost — core canvas window + glass frame + scrollbar.
    void draw_canvas_content(Figure& figure);

    bool icon_label_button_rail(const char* icon_codepoint,
                                const char* label,
                                bool        active,
                                ImFont*     icon_font,
                                ImFont*     label_font,
                                float       width,
                                float       scale = 1.0f);

    ImFont* icon_font() const { return font_icon_; }
    ImFont* heading_font() const { return font_heading_; }
    ImFont* menubar_font() const { return font_menubar_; }
    ImFont* title_font() const { return font_title_; }
    ImFont* body_font() const { return font_body_; }

    // Vision command-bar window frame (styled backdrop). Pair with end_command_bar().
    bool begin_command_bar();
    void end_command_bar();

   private:
    void apply_modern_style();
    void load_fonts();

    void draw_chrome_backdrops();
    void draw_command_bar();
    void draw_tab_bar();
    void draw_nav_rail();
    void draw_canvas(Figure& figure);
    void draw_inspector(Figure& figure);
    void draw_inspector_toggle();
    void draw_status_bar();
    void draw_split_view_splitters();
    void draw_pane_tab_headers();
    void draw_axes_context_menu(Figure& figure);
    void draw_axis_link_indicators(Figure& figure);
    void draw_timeline_panel();
    void draw_curve_editor_panel();
    void draw_plugins_panel();
    void draw_theme_settings();
    void draw_knobs_panel();
    void draw_csv_dialog();
    void draw_custom_transform_dialog();
    void draw_plot_overlay_dialog();

    void draw_plot_overlays(Figure& figure);
    void draw_topic_drop_target(Figure& figure);
    void draw_topic_drop_target_welcome();
    void draw_toolbar_button(const char*                  icon,
                             const std::function<void()>& callback,
                             const char*                  tooltip,
                             bool                         is_active  = false,
                             float                        icon_scale = 1.0f);
    void draw_menubar_menu(const char* label, const std::vector<MenuItem>& items);

    bool     initialized_        = false;
    bool     headless_           = false;   // True when initialized via init_headless()
    uint64_t cached_render_pass_ = 0;       // Opaque VkRenderPass handle for change detection
    std::unique_ptr<LayoutManager> layout_manager_;

    // Inspector system (Agent C)
    ui::Inspector        inspector_;
    ui::SelectionContext selection_ctx_;

    // Data editor (tabular series data view + inline editing)
    ui::DataEditor data_editor_;

    // Custom transform dialog (user-defined formula transforms)
    ui::CustomTransformDialog custom_transform_dialog_;
    ui::PlotOverlayDialog     plot_overlay_dialog_;

    // Deferred series removal queue (flushed after on_frame callback)
    std::vector<PendingSeriesRemoval> pending_series_removals_;

    // Panel state
    bool panel_open_    = false;
    bool show_nav_rail_ = true;   // Nav rail toolbar visibility

    // Adapter chrome suppression flags (all default true — safe for normal builds)
    bool command_bar_visible_  = true;    // Spectra command bar / menu
    bool command_bar_began_    = false;   // ImGui::Begin succeeded for begin_command_bar()
    bool status_bar_visible_   = true;    // Spectra status bar
    bool canvas_visible_       = true;    // Plot canvas, overlays, splitters, tab headers
    bool shell_chrome_visible_ = true;    // Inspector toggle, pane tabs, splitters, backdrops
    bool render_figure_        = true;    // Vulkan figure rendering (independent of canvas UI)
    bool external_inspector_toggle_visible_ = false;
    std::function<void()> external_inspector_toggle_cb_;
    std::function<bool()> external_inspector_open_cb_;

    enum class Section
    {
        Figure,
        Series,
        Axes,
        DataEditor
    };
    Section active_section_ = Section::Figure;

    float panel_anim_ = 0.0f;

    // Per-window ImGui context and font atlas (owned).
    // Each window gets its own context+atlas so secondary window init
    // mid-frame doesn't touch the primary's locked shared atlas.
    ImGuiContext*                imgui_context_ = nullptr;
    std::unique_ptr<ImFontAtlas> owned_font_atlas_;

    // Fonts at different sizes
    ImFont* font_body_    = nullptr;   // 16px — body text, controls
    ImFont* font_heading_ = nullptr;   // 12.5px — section headers (uppercase)
    ImFont* font_icon_    = nullptr;   // 20px — icon bar symbols
    ImFont* font_title_   = nullptr;   // 18px — panel title
    ImFont* font_menubar_ = nullptr;   // 15px — menubar items

    // Interaction state
    bool     reset_view_       = false;
    bool     reset_live_view_  = false;
    ToolMode interaction_mode_ = ToolMode::Pan;

    // Status bar data
    double cursor_data_x_     = 0.0;
    double cursor_data_y_     = 0.0;
    bool   cursor_data_valid_ = false;
    float  zoom_level_        = 1.0f;
    float  gpu_time_ms_       = 0.0f;

    // Injected ThemeManager (not owned)
    ui::ThemeManager* theme_mgr_ = nullptr;

    // Contextual theme accessor — uses the injected per-window ThemeManager
    // instead of the global ui::theme() singleton, enabling per-window theming.
    const ui::ThemeColors& theme_colors() const;

    // Data interaction layer (not owned)
    DataInteraction* data_interaction_ = nullptr;

    // Box zoom overlay (Agent B Week 7, not owned)
    BoxZoomOverlay* box_zoom_overlay_ = nullptr;

    // Command palette & productivity (Agent F, not owned)
    CommandPalette*  command_palette_  = nullptr;
    CommandRegistry* command_registry_ = nullptr;
    ShortcutManager* shortcut_manager_ = nullptr;
    UndoManager*     undo_manager_     = nullptr;

    // Dock system (Agent A Week 9, not owned)
    DockSystem* dock_system_ = nullptr;

    // Axis link manager (Agent B Week 10, not owned)
    AxisLinkManager* axis_link_mgr_ = nullptr;

    // Input handler (not owned)
    InputHandler* input_handler_ = nullptr;

    // Timeline & animation (not owned)
    TimelineEditor*       timeline_editor_        = nullptr;
    KeyframeInterpolator* keyframe_interpolator_  = nullptr;
    AnimationCurveEditor* curve_editor_           = nullptr;
    bool                  show_timeline_          = false;
    bool                  show_curve_editor_      = false;
    bool                  curve_editor_needs_fit_ = true;

    // Series clipboard (not owned)
    SeriesClipboard* series_clipboard_ = nullptr;

    // Mode transition (not owned)
    ModeTransition* mode_transition_ = nullptr;

    // (text_renderer_ removed — plot text now rendered by Renderer::render_plot_text)

    // Knob manager (not owned)
    KnobManager* knob_manager_ = nullptr;

    // Plugin overlay registry (not owned)
    OverlayRegistry* overlay_registry_ = nullptr;

    // Plugin export format registry (not owned)
    ExportFormatRegistry* export_format_registry_ = nullptr;

    // Plugin manager (not owned)
    PluginManager* plugin_manager_ = nullptr;

    // Plugins panel state
    bool                     show_plugins_panel_ = false;
    std::vector<std::string> plugin_scan_dirs_;
    char                     plugin_scan_dir_buf_[512] = {};
    std::string              plugin_panel_status_;

    // Topics panel (Phase 2 of plans/SPECTRA_TOPICS_PLAN.md) — not owned.
    ui::topics::TopicsPanel* topics_panel_ = nullptr;

    // Settings panel — not owned.
    ui::settings::SettingsPanel* settings_panel_ = nullptr;

    // Tab bar (not owned)
    TabBar* tab_bar_ = nullptr;

    // Tab drag controller (not owned)
    TabDragController* tab_drag_controller_ = nullptr;

    // Window identity for dock highlight
    uint32_t             window_id_      = 0;
    class WindowManager* window_manager_ = nullptr;

    // GLFW window pointer (not owned) — stored for direct mouse queries
    void* glfw_window_ = nullptr;

    // Pane tab context menu callbacks
    PaneTabCallback       pane_tab_duplicate_cb_;
    PaneTabCallback       pane_tab_close_cb_;
    PaneTabCallback       pane_tab_split_right_cb_;
    PaneTabCallback       pane_tab_split_down_cb_;
    PaneTabDetachCallback pane_tab_detach_cb_;
    PaneTabRenameCallback pane_tab_rename_cb_;

    // Pane tab context menu state
    FigureId pane_ctx_menu_fig_             = INVALID_FIGURE_ID;   // Figure id of right-clicked tab
    bool     pane_ctx_menu_open_            = false;
    bool     pane_ctx_menu_close_requested_ = false;

    // Pane tab rename state
    bool     pane_tab_renaming_        = false;
    FigureId pane_tab_rename_fig_      = INVALID_FIGURE_ID;
    char     pane_tab_rename_buf_[256] = {};

    // Current figure pointer (set each frame in build_ui for menu callbacks)
    Figure* current_figure_ = nullptr;

    // Axes context menu state (right-click on canvas)
    AxesBase* context_menu_axes_         = nullptr;   // Which axes was right-clicked (2D or 3D)
    bool      context_menu_armed_        = false;
    AxesBase* context_menu_pressed_axes_ = nullptr;
    float     context_menu_press_x_      = 0.0f;
    float     context_menu_press_y_      = 0.0f;
    // bool context_menu_open_ = false;  // Currently unused

    // Per-pane tab drag state
    struct PaneTabDragState
    {
        bool     dragging             = false;
        uint32_t source_pane_id       = 0;
        FigureId dragged_figure_index = INVALID_FIGURE_ID;
        float    drag_start_x         = 0.0f;
        float    drag_start_y         = 0.0f;
        bool     cross_pane           = false;   // Dragged outside source pane header
        bool dock_dragging = false;   // Dragged far enough vertically → dock system handles split

        // Tearoff preview card animation
        float preview_scale   = 0.0f;   // 0→1 animated scale of the preview card
        float preview_opacity = 0.0f;   // 0→1 animated opacity
        float preview_shadow  = 0.0f;   // 0→1 animated shadow intensity
        float source_tab_x    = 0.0f;   // Source tab position (animation origin)
        float source_tab_y    = 0.0f;
        float source_tab_w    = 0.0f;
        float source_tab_h    = 0.0f;
        bool  preview_active  = false;   // True once drag exceeds vertical threshold
    };
    PaneTabDragState pane_tab_drag_;
    bool             pane_tab_hovered_ = false;   // True when mouse is over a pane tab header

    // Animated tab positions: keyed by (pane_id, figure_index) so each
    // pane gets independent animation state (prevents shaking after splits)
    struct PaneTabAnim
    {
        float current_x      = 0.0f;
        float target_x       = 0.0f;
        float opacity        = 1.0f;
        float target_opacity = 1.0f;
    };
    struct PaneTabAnimKey
    {
        uint32_t pane_id;
        FigureId fig_idx;
        bool     operator==(const PaneTabAnimKey& o) const
        {
            return pane_id == o.pane_id && fig_idx == o.fig_idx;
        }
    };
    struct PaneTabAnimKeyHash
    {
        size_t operator()(const PaneTabAnimKey& k) const
        {
            return std::hash<uint64_t>()((static_cast<uint64_t>(k.pane_id) << 32)
                                         | (k.fig_idx & 0xFFFFFFFF));
        }
    };
    std::unordered_map<PaneTabAnimKey, PaneTabAnim, PaneTabAnimKeyHash> pane_tab_anims_;

    // Tab insertion gap animation state (for drag-between-tabs)
    struct InsertionGap
    {
        uint32_t target_pane_id = 0;   // Pane where the gap is shown
        size_t   insert_after_idx =
            SIZE_MAX;               // Insert after this tab index (SIZE_MAX = before first)
        float current_gap = 0.0f;   // Animated gap width
        float target_gap  = 0.0f;   // Target gap width
    };
    InsertionGap insertion_gap_;

    // Non-split drag-to-split state
    struct TabDragSplitState
    {
        bool     active          = false;   // True when a main tab bar tab is being dock-dragged
        DropZone suggested_zone  = DropZone::None;
        float    overlay_opacity = 0.0f;   // Animated overlay opacity
    };
    TabDragSplitState tab_drag_split_;

    // Figure title lookup callback (set by App)
    std::function<std::string(FigureId)> get_figure_title_;

   public:
    void set_figure_title_callback(std::function<std::string(FigureId)> cb)
    {
        get_figure_title_ = std::move(cb);
    }

    // Figure pointer resolver (set by WindowManager, used for split-mode legend drawing)
    std::function<Figure*(FigureId)> get_figure_ptr_;

   public:
    void set_figure_ptr_callback(std::function<Figure*(FigureId)> cb)
    {
        get_figure_ptr_ = std::move(cb);
    }

   private:
    // Theme settings window state
    bool show_theme_settings_ = false;

    // CSV plot callback (set by App)
    CsvPlotCallback csv_plot_cb_;

    // Extra draw callback (set by spectra-ros or other adapters)
    ExtraDrawCallback           extra_draw_cb_;
    ui::shell::SpectraAppShell* app_shell_ = nullptr;

    // Scene render callback (set by spectra-ros for GPU 3D viewport)
    SceneRenderCallback scene_render_cb_;

    // CSV column picker dialog state (file selected via native OS dialog)
    bool             pending_open_csv_ = false;   // Set by welcome screen, handled in draw()
    std::string      pending_csv_drop_path_;      // Set by OS drag-and-drop, handled in draw()
    bool             csv_dialog_open_ = false;
    std::string      csv_file_path_;
    CsvData          csv_data_;
    bool             csv_data_loaded_ = false;
    int              csv_col_x_       = 0;
    std::vector<int> csv_selected_y_;   // selected Y column indices
    int              csv_col_z_ = -1;   // -1 = no Z column (2D plot)
    std::string      csv_error_;

    // Menubar hover-switch state: tracks which menu popup is currently open
    // so hovering another menu button auto-opens it
    std::string open_menu_label_;   // label of currently open menu ("" = none)

    // Deferred tooltip rendering to ensure tooltips appear above all UI elements
    const char* deferred_tooltip_ = nullptr;

    // Knobs panel screen rect (updated each frame) — used to suppress tab bar
    // foreground rendering when the mouse is over the knobs panel.
    struct
    {
        float x = 0, y = 0, w = 0, h = 0;
    } knobs_panel_rect_;

    // Branded textures used by the empty-state welcome screen and top-left app mark
    VulkanBackend* backend_ = nullptr;

    uint64_t welcome_logo_texture_id_ = 0;   // ImTextureID / VkDescriptorSet bits
    uint64_t corner_logo_texture_id_  = 0;   // ImTextureID / VkDescriptorSet bits
    int      welcome_logo_width_      = 0;
    int      welcome_logo_height_     = 0;
    int      corner_logo_width_       = 0;
    int      corner_logo_height_      = 0;
    bool     logo_loaded_             = false;

    TextureHandle welcome_logo_texture_{};
    TextureHandle corner_logo_texture_{};

    void load_logo_textures(VulkanBackend& backend);
    void destroy_logo_textures();
    void draw_welcome_screen(float display_w, float display_h, float dt);
};

// Helper factory to construct an ImGuiIntegration instance from translation units
// that only have forward declarations for ImGui internals.
std::unique_ptr<ImGuiIntegration> make_imgui_integration();

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
