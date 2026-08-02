#pragma once

#include <chrono>
#include <functional>
#include <string>

namespace spectra
{

struct WorkspaceData;

/// Auto-save mechanism for workspace state.
///
/// Periodically serializes workspace state to a well-known path
/// (~/.config/spectra/autosave.spectra). On startup, the host can
/// check for an autosave newer than the last clean save and offer
/// recovery.
///
/// Usage:
///   WorkspaceAutosave autosave;
///   autosave.set_interval(std::chrono::seconds(60));
///   autosave.set_serialize_fn([&]() -> std::string { return serialize(...); });
///   autosave.mark_dirty();   // call when workspace changes
///   autosave.tick();         // call from main loop
class WorkspaceAutosave
{
   public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration  = Clock::duration;

    WorkspaceAutosave();
    ~WorkspaceAutosave() = default;

    WorkspaceAutosave(const WorkspaceAutosave&)            = delete;
    WorkspaceAutosave& operator=(const WorkspaceAutosave&) = delete;

    // ── Configuration ──

    /// Set the interval between auto-saves (default: 60 seconds).
    void                   set_interval(Duration interval);
    [[nodiscard]] Duration interval() const;

    /// Set the debounce delay after mark_dirty() before saving (default: 5 seconds).
    /// Prevents saving on every single keystroke.
    void                   set_debounce(Duration debounce);
    [[nodiscard]] Duration debounce() const;

    /// Set the output path for auto-saves.
    /// Default: ~/.config/spectra/autosave.spectra
    void                             set_autosave_path(const std::string& path);
    [[nodiscard]] const std::string& autosave_path() const;

    /// Set the function that produces the JSON to save.
    /// Called when it's time to write the autosave.
    void set_serialize_fn(std::function<std::string()> fn);

    // ── Lifecycle ──

    /// Mark the workspace as modified (resets the debounce timer).
    void mark_dirty();

    /// Call from the main loop. Saves if:
    ///   - dirty_ is set
    ///   - At least debounce_ has passed since last mark_dirty()
    ///   - At least interval_ has passed since last save
    void tick();

    /// Force an immediate save regardless of timers.
    bool save_now();

    // ── Status ──

    /// True if there are unsaved changes.
    [[nodiscard]] bool has_unsaved_changes() const;

    /// Time since the last successful autosave (or very large if never saved).
    [[nodiscard]] Duration time_since_last_save() const;

    /// Path to the autosave file (empty if never saved or path not set).
    [[nodiscard]] std::string last_saved_path() const;

    /// Check if an autosave file exists at the configured path.
    [[nodiscard]] bool has_autosave() const;

    /// Remove the autosave at this instance's configured path. Returns true
    /// when no recovery file remains (including when none existed).
    bool clear_autosave();

    /// Check if the autosave is newer than a reference file (for recovery detection).
    /// Returns false if autosave doesn't exist.
    [[nodiscard]] bool autosave_is_newer_than(const std::string& reference_path) const;

    /// Static: return the default autosave path.
    static std::string default_autosave_path();

   private:
    Duration  interval_{std::chrono::seconds(60)};
    Duration  debounce_{std::chrono::seconds(5)};
    TimePoint last_save_time_{};
    TimePoint last_dirty_time_{};
    bool      dirty_      = false;
    bool      ever_saved_ = false;

    std::string                  autosave_path_;
    std::function<std::string()> serialize_fn_;
};

}   // namespace spectra
