#pragma once

#include <cstddef>
#include <functional>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#ifdef SPECTRA_USE_IMGUI
struct ImVec2;
#endif

namespace spectra
{

/**
 * TabBar - Widget for managing multiple figure tabs
 *
 * Provides tab switching, reordering, and add/close functionality.
 * Designed to work within the canvas area of the new layout system.
 */
class TabBar
{
   public:
    struct TabInfo
    {
        std::string title;
        bool        can_close   = true;
        bool        is_modified = false;   // For future: show dirty state

        TabInfo(const std::string& t, bool close = true, bool modified = false)
            : title(t), can_close(close), is_modified(modified)
        {
        }
    };

    using TabChangeCallback         = std::function<void(size_t new_index)>;
    using TabCloseCallback          = std::function<void(size_t index)>;
    using TabAddCallback            = std::function<void()>;
    using TabReorderCallback        = std::function<void(size_t old_index, size_t new_index)>;
    using TabDuplicateCallback      = std::function<void(size_t index)>;
    using TabCloseAllExceptCallback = std::function<void(size_t index)>;
    using TabCloseToRightCallback   = std::function<void(size_t index)>;
    using TabRenameCallback     = std::function<void(size_t index, const std::string& new_title)>;
    using TabSplitRightCallback = std::function<void(size_t index)>;
    using TabSplitDownCallback  = std::function<void(size_t index)>;
    using TabDetachCallback     = std::function<void(size_t index, float screen_x, float screen_y)>;
    using TabDragOutCallback    = std::function<void(size_t index, float mouse_x, float mouse_y)>;
    using TabDragUpdateCallback = std::function<void(size_t index, float mouse_x, float mouse_y)>;
    using TabDragEndCallback    = std::function<void(size_t index, float mouse_x, float mouse_y)>;
    using TabDragCancelCallback = std::function<void(size_t index)>;

    TabBar();
    ~TabBar() = default;

    // Disable copying
    TabBar(const TabBar&)            = delete;
    TabBar& operator=(const TabBar&) = delete;

    // Tab management
    size_t             add_tab(const std::string& title, bool can_close = true);
    void               remove_tab(size_t index);
    void               clear_tabs();
    void               set_tab_title(size_t index, const std::string& title);
    const std::string& get_tab_title(size_t index) const;

    // State queries
    size_t get_tab_count() const { return tabs_.size(); }
    size_t get_active_tab() const { return active_tab_; }
    void   set_active_tab(size_t index);
    bool   has_active_tab() const { return active_tab_ < tabs_.size(); }

    // Callbacks
    void set_tab_change_callback(TabChangeCallback callback) { on_tab_change_ = callback; }
    void set_tab_close_callback(TabCloseCallback callback) { on_tab_close_ = callback; }
    void set_tab_add_callback(TabAddCallback callback) { on_tab_add_ = callback; }
    void set_tab_reorder_callback(TabReorderCallback callback) { on_tab_reorder_ = callback; }
    void set_tab_duplicate_callback(TabDuplicateCallback callback) { on_tab_duplicate_ = callback; }
    void set_tab_close_all_except_callback(TabCloseAllExceptCallback callback)
    {
        on_tab_close_all_except_ = callback;
    }
    void set_tab_close_to_right_callback(TabCloseToRightCallback callback)
    {
        on_tab_close_to_right_ = callback;
    }
    void set_tab_rename_callback(TabRenameCallback callback) { on_tab_rename_ = callback; }
    void set_tab_split_right_callback(TabSplitRightCallback callback)
    {
        on_tab_split_right_ = callback;
    }
    void set_tab_split_down_callback(TabSplitDownCallback callback)
    {
        on_tab_split_down_ = callback;
    }
    void set_tab_detach_callback(TabDetachCallback callback) { on_tab_detach_ = callback; }
    void set_tab_drag_out_callback(TabDragOutCallback callback) { on_tab_drag_out_ = callback; }
    void set_tab_drag_update_callback(TabDragUpdateCallback callback)
    {
        on_tab_drag_update_ = callback;
    }
    void set_tab_drag_end_callback(TabDragEndCallback callback) { on_tab_drag_end_ = callback; }
    void set_tab_drag_cancel_callback(TabDragCancelCallback callback)
    {
        on_tab_drag_cancel_ = callback;
    }

    // Rendering
    void draw(const Rect& bounds, bool menus_open = false);

    // Interaction state
    bool is_tab_hovered(size_t index) const;
    bool is_close_button_hovered(size_t index) const;
    void cancel_drag();

    // Modified indicator
    void set_tab_modified(size_t index, bool modified);
    bool is_tab_modified(size_t index) const;

   private:
    std::vector<TabInfo> tabs_;
    size_t               active_tab_ = 0;

    // Interaction state
    size_t hovered_tab_      = SIZE_MAX;
    size_t hovered_close_    = SIZE_MAX;
    bool   is_dragging_      = false;
    size_t dragged_tab_      = SIZE_MAX;
    float  drag_offset_x_    = 0.0f;
    float  drag_start_y_     = 0.0f;
    bool   is_dock_dragging_ = false;   // Tab has been dragged out of bar → dock mode

    // Callbacks
    TabChangeCallback         on_tab_change_;
    TabCloseCallback          on_tab_close_;
    TabAddCallback            on_tab_add_;
    TabReorderCallback        on_tab_reorder_;
    TabDuplicateCallback      on_tab_duplicate_;
    TabCloseAllExceptCallback on_tab_close_all_except_;
    TabCloseToRightCallback   on_tab_close_to_right_;
    TabRenameCallback         on_tab_rename_;
    TabSplitRightCallback     on_tab_split_right_;
    TabSplitDownCallback      on_tab_split_down_;
    TabDetachCallback         on_tab_detach_;
    TabDragOutCallback        on_tab_drag_out_;
    TabDragUpdateCallback     on_tab_drag_update_;
    TabDragEndCallback        on_tab_drag_end_;
    TabDragCancelCallback     on_tab_drag_cancel_;

    // Layout constants
    static constexpr float TAB_HEIGHT            = 38.0f;
    static constexpr float TAB_MIN_WIDTH         = 104.0f;
    static constexpr float TAB_MAX_WIDTH         = 220.0f;
    static constexpr float TAB_PADDING           = 22.0f;
    static constexpr float CLOSE_BUTTON_SIZE     = 14.0f;
    static constexpr float ADD_BUTTON_WIDTH      = 34.0f;
    static constexpr float TAB_RADIUS            = 8.0f;
    static constexpr float SELECTED_HEIGHT_EXTRA = 3.0f;
    static constexpr float CLOSE_PAD_RIGHT       = 10.0f;
    static constexpr float UNDERLINE_HEIGHT      = 2.5f;

    // Context menu state
    size_t context_menu_tab_   = SIZE_MAX;
    bool   context_menu_open_  = false;
    bool   renaming_tab_       = false;
    size_t rename_tab_index_   = SIZE_MAX;
    char   rename_buffer_[256] = {};

    // Internal helpers
    void handle_input(const Rect& bounds);
    void draw_tabs(const Rect& bounds, bool menus_open = false);
    void draw_add_button(const Rect& bounds);
    void draw_context_menu();

    struct TabLayout
    {
        Rect bounds;
        Rect close_bounds;
        bool is_visible;
        bool is_clipped;
    };

    std::vector<TabLayout> compute_tab_layouts(const Rect& bounds) const;
#ifdef SPECTRA_USE_IMGUI
    size_t get_tab_at_position(const ImVec2& pos, const std::vector<TabLayout>& layouts) const;
    size_t get_close_button_at_position(const ImVec2&                 pos,
                                        const std::vector<TabLayout>& layouts) const;
#endif

    void start_drag(size_t tab_index, float mouse_x);
    void update_drag(float mouse_x);
    void end_drag();

    // Scrolling (for when tabs overflow)
    float scroll_offset_ = 0.0f;
    bool  needs_scroll_buttons(const Rect& bounds) const;
    void  draw_scroll_buttons(const Rect& bounds);
    void  scroll_to_tab(size_t index);
};

}   // namespace spectra
