// plugin_ui_schema.cpp — Portable plugin UI schema registry implementation.

#include "plugin_ui_schema.hpp"

#include <atomic>
#include <sstream>

namespace spectra
{

// ─── PluginUIRegistry ────────────────────────────────────────────────────────

static std::atomic<uint64_t> s_schema_counter{1};

static std::string generate_schema_id()
{
    std::ostringstream oss;
    oss << "plugin_ui_" << s_schema_counter.fetch_add(1);
    return oss.str();
}

std::string PluginUIRegistry::register_schema(const PluginUISchema& schema,
                                               PluginUICallbacks     callbacks)
{
    std::lock_guard lock(mutex_);

    // If a schema with the same plugin_name exists, replace it and keep the same ID
    for (auto& entry : entries_)
    {
        if (entry.schema.plugin_name == schema.plugin_name)
        {
            entry.schema    = schema;
            entry.callbacks = std::move(callbacks);
            if (change_listener_)
                change_listener_();
            return entry.id;
        }
    }

    Entry entry;
    entry.id        = generate_schema_id();
    entry.schema    = schema;
    entry.callbacks = std::move(callbacks);
    entries_.push_back(std::move(entry));

    if (change_listener_)
        change_listener_();

    return entries_.back().id;
}

void PluginUIRegistry::unregister_schema(const std::string& schema_id)
{
    std::lock_guard lock(mutex_);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&](const Entry& e) { return e.id == schema_id; }),
        entries_.end());
    if (change_listener_)
        change_listener_();
}

void PluginUIRegistry::unregister_plugin(const std::string& plugin_name)
{
    std::lock_guard lock(mutex_);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&](const Entry& e) { return e.schema.plugin_name == plugin_name; }),
        entries_.end());
    if (change_listener_)
        change_listener_();
}

std::vector<PluginUISchema> PluginUIRegistry::schemas() const
{
    std::lock_guard lock(mutex_);
    std::vector<PluginUISchema> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_)
        result.push_back(entry.schema);
    return result;
}

const PluginUISchema* PluginUIRegistry::find_schema(const std::string& schema_id) const
{
    std::lock_guard lock(mutex_);
    for (const auto& entry : entries_)
    {
        if (entry.id == schema_id)
            return &entry.schema;
    }
    return nullptr;
}

std::string PluginUIRegistry::set_property_value(const std::string& schema_id,
                                                  const std::string& property_id,
                                                  const std::string& new_value)
{
    std::lock_guard lock(mutex_);
    for (auto& entry : entries_)
    {
        if (entry.id != schema_id)
            continue;
        // Find the property in the schema
        for (auto& elem : entry.schema.elements)
        {
            if (elem.type == PluginUIElementType::Property &&
                elem.property.id == property_id)
            {
                if (entry.callbacks.on_property_changed)
                {
                    std::string actual =
                        entry.callbacks.on_property_changed(schema_id, property_id, new_value);
                    if (!actual.empty())
                    {
                        elem.property.value = actual;
                        return actual;
                    }
                }
                elem.property.value = new_value;
                return new_value;
            }
        }
    }
    return new_value;
}

void PluginUIRegistry::trigger_action(const std::string& schema_id,
                                      const std::string& action_id)
{
    // Callbacks should be called outside the lock to avoid deadlocks.
    // Copy the callback under the lock, then call it outside.
    PluginUICallbacks cbs;
    {
        std::lock_guard lock(mutex_);
        for (auto& entry : entries_)
        {
            if (entry.id != schema_id)
                continue;
            for (const auto& elem : entry.schema.elements)
            {
                if (elem.type == PluginUIElementType::Action &&
                    elem.action.id == action_id)
                {
                    cbs = entry.callbacks;
                    break;
                }
            }
        }
    }
    if (cbs.on_action_triggered)
        cbs.on_action_triggered(schema_id, action_id);
}

void PluginUIRegistry::set_change_listener(std::function<void()> listener)
{
    std::lock_guard lock(mutex_);
    change_listener_ = std::move(listener);
}

void PluginUIRegistry::clear()
{
    std::lock_guard lock(mutex_);
    entries_.clear();
    if (change_listener_)
        change_listener_();
}

}   // namespace spectra
