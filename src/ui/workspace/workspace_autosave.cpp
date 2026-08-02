#include "workspace_autosave.hpp"

#include <filesystem>
#include <fstream>

#include "spectra/logger.hpp"

namespace spectra
{

WorkspaceAutosave::WorkspaceAutosave()
{
    autosave_path_   = default_autosave_path();
    last_save_time_  = Clock::now();
    last_dirty_time_ = Clock::now();
}

void WorkspaceAutosave::set_interval(Duration interval)
{
    interval_ = interval;
}

WorkspaceAutosave::Duration WorkspaceAutosave::interval() const
{
    return interval_;
}

void WorkspaceAutosave::set_debounce(Duration debounce)
{
    debounce_ = debounce;
}

WorkspaceAutosave::Duration WorkspaceAutosave::debounce() const
{
    return debounce_;
}

void WorkspaceAutosave::set_autosave_path(const std::string& path)
{
    autosave_path_ = path;
}

const std::string& WorkspaceAutosave::autosave_path() const
{
    return autosave_path_;
}

void WorkspaceAutosave::set_serialize_fn(std::function<std::string()> fn)
{
    serialize_fn_ = std::move(fn);
}

void WorkspaceAutosave::mark_dirty()
{
    dirty_           = true;
    last_dirty_time_ = Clock::now();
}

void WorkspaceAutosave::tick()
{
    if (!dirty_ || !serialize_fn_)
        return;

    auto now = Clock::now();

    // Check debounce: enough time since last mark_dirty?
    if (now - last_dirty_time_ < debounce_)
        return;

    // Check interval: enough time since last save?
    if (ever_saved_ && now - last_save_time_ < interval_)
        return;

    save_now();
}

bool WorkspaceAutosave::save_now()
{
    if (!serialize_fn_)
        return false;

    if (autosave_path_.empty())
    {
        SPECTRA_LOG_WARN("workspace", "Autosave path not set, skipping autosave");
        return false;
    }

    // Ensure directory exists
    std::filesystem::path p(autosave_path_);
    if (p.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec)
        {
            SPECTRA_LOG_WARN("workspace",
                             "Could not create autosave directory: {}",
                             p.parent_path().string());
            return false;
        }
    }

    std::string json;
    try
    {
        json = serialize_fn_();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("workspace", "Autosave serialize failed: {}", e.what());
        return false;
    }

    // Write to a temp file first, then rename for atomicity
    std::string tmp_path = autosave_path_ + ".tmp";
    {
        std::ofstream f(tmp_path, std::ios::trunc);
        if (!f.is_open())
        {
            SPECTRA_LOG_WARN("workspace", "Could not open autosave temp file: {}", tmp_path);
            return false;
        }
        f << json;
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, autosave_path_, ec);
    if (ec)
    {
        SPECTRA_LOG_WARN("workspace", "Could not rename autosave temp file: {}", ec.message());
        return false;
    }

    dirty_          = false;
    ever_saved_     = true;
    last_save_time_ = Clock::now();
    SPECTRA_LOG_DEBUG("workspace", "Autosaved workspace to {}", autosave_path_);
    return true;
}

bool WorkspaceAutosave::has_unsaved_changes() const
{
    return dirty_;
}

WorkspaceAutosave::Duration WorkspaceAutosave::time_since_last_save() const
{
    if (!ever_saved_)
        return Duration::max();
    return Clock::now() - last_save_time_;
}

std::string WorkspaceAutosave::last_saved_path() const
{
    if (!ever_saved_)
        return {};
    return autosave_path_;
}

bool WorkspaceAutosave::has_autosave() const
{
    if (autosave_path_.empty())
        return false;
    return std::filesystem::exists(autosave_path_);
}

bool WorkspaceAutosave::clear_autosave()
{
    if (autosave_path_.empty())
        return true;
    std::error_code ec;
    std::filesystem::remove(autosave_path_, ec);
    if (ec)
    {
        SPECTRA_LOG_WARN("workspace", "Could not clear autosave: {}", ec.message());
        return false;
    }
    return !std::filesystem::exists(autosave_path_);
}

bool WorkspaceAutosave::autosave_is_newer_than(const std::string& reference_path) const
{
    if (!has_autosave())
        return false;

    std::error_code ec;
    auto            autosave_time = std::filesystem::last_write_time(autosave_path_, ec);
    if (ec)
        return false;

    auto reference_time = std::filesystem::last_write_time(reference_path, ec);
    if (ec)
        return true;   // Reference doesn't exist, autosave is "newer"

    return autosave_time > reference_time;
}

std::string WorkspaceAutosave::default_autosave_path()
{
    const char* home = std::getenv("HOME");
    if (!home)
        home = ".";
    return std::string(home) + "/.config/spectra/autosave.spectra";
}

}   // namespace spectra
