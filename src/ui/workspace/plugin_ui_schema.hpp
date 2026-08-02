#pragma once

// Portable plugin UI schema — a framework-neutral, versioned UI-description
// API that plugins can use to declare properties, actions, enums, validation,
// and status.  Both the Qt and ImGui frontends can render this schema without
// plugins linking to either UI framework.
//
// This is the "portable plugin UI schema" deliverable from Phase 6 of the
// Qt 6 Application Migration Plan (section 16, item 3).
//
// Design:
//   - Plugins register a PluginUISchema via the C ABI or C++ API.
//   - Each schema contains an ordered list of elements (properties, actions,
//     labels, separators, groups).
//   - Properties have typed values with optional min/max/enum validation.
//   - Actions are buttons that trigger a plugin callback.
//   - The registry notifies the frontend when schemas change.
//   - Frontends render the schema using their own widgets.
//   - Property changes flow back through the registry to the plugin.

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace spectra
{

// ─── Element types ───────────────────────────────────────────────────────────

enum class PluginUIElementType : uint8_t
{
    Property,    // editable value (bool, int, float, string, enum, color)
    Action,      // button that triggers a callback
    Label,       // read-only text
    Separator,   // visual separator line
    Group,       // grouping header with optional collapsible children
};

enum class PluginUIPropertyType : uint8_t
{
    Boolean,
    Integer,
    Float,
    String,
    Enum,    // value is an index into enum_options
    Color,   // value is "#RRGGBB" or "#RRGGBBAA"
};

// A single property descriptor within a plugin UI schema.
struct PluginUIProperty
{
    std::string          id;      // unique within the schema
    std::string          label;   // display label
    PluginUIPropertyType type = PluginUIPropertyType::String;

    // Current value serialized as a string:
    //   Boolean: "true"/"false"
    //   Integer: decimal string
    //   Float:   decimal string
    //   String:  raw text
    //   Enum:    index as decimal string (0-based)
    //   Color:   "#RRGGBB" or "#RRGGBBAA"
    std::string value;

    // Optional validation constraints (empty = not constrained)
    std::string min_value;
    std::string max_value;
    std::string default_value;

    // Enum options (only used when type == Enum)
    std::vector<std::string> enum_options;

    bool read_only = false;

    // Optional tooltip / description
    std::string tooltip;
};

// A single action descriptor within a plugin UI schema.
struct PluginUIAction
{
    std::string id;        // unique within the schema
    std::string label;     // button text
    std::string tooltip;   // optional
    bool        enabled = true;
};

// A label element (read-only text, e.g. status or description).
struct PluginUILabel
{
    std::string text;
    std::string style_hint;   // "heading", "status", "warning", "error", or empty
};

// A group element (collapsible section header).
struct PluginUIGroup
{
    std::string title;
    bool        collapsed = false;
};

// A single ordered element in the schema.
struct PluginUIElement
{
    PluginUIElementType type = PluginUIElementType::Label;

    // Only the field matching `type` is meaningful.
    PluginUIProperty property;
    PluginUIAction   action;
    PluginUILabel    label;
    PluginUIGroup    group;

    // For groups: child element indices (into the schema's elements vector).
    // For non-groups: empty.
    std::vector<size_t> children;
};

// A complete UI schema for a plugin panel.
struct PluginUISchema
{
    std::string                  plugin_name;   // plugin that registered this schema
    std::string                  panel_title;   // title shown in the dock/panel
    std::vector<PluginUIElement> elements;

    // Schema version (for future evolution)
    uint32_t version = 1;
};

struct RegisteredPluginUISchema
{
    std::string    id;
    PluginUISchema schema;
};

// ─── Registry ────────────────────────────────────────────────────────────────

// Callbacks from the plugin UI registry to the plugin.
// These are set by the plugin when registering a schema.
struct PluginUICallbacks
{
    // Called when a property value changes.  The plugin should update its
    // internal state and return the new serialized value (may differ from
    // the requested value if the plugin clamps or rejects it).
    std::function<std::string(const std::string& schema_id,
                              const std::string& property_id,
                              const std::string& new_value)>
        on_property_changed;

    // Called when an action is triggered.
    std::function<void(const std::string& schema_id, const std::string& action_id)>
        on_action_triggered;
};

// Manages plugin UI schemas.  Thread-safe.
// Frontends query schemas() to build their widgets, and call
// set_property_value() / trigger_action() to relay user interaction
// back to the plugin.
class PluginUIRegistry
{
   public:
    PluginUIRegistry()  = default;
    ~PluginUIRegistry() = default;

    PluginUIRegistry(const PluginUIRegistry&)            = delete;
    PluginUIRegistry& operator=(const PluginUIRegistry&) = delete;

    // Register a UI schema.  Returns a schema ID (unique within the registry).
    // If a schema with the same plugin_name already exists, it is replaced.
    std::string register_schema(const PluginUISchema& schema, PluginUICallbacks callbacks = {});

    // Unregister a schema by ID.
    void unregister_schema(const std::string& schema_id);

    // Unregister all schemas from a given plugin.
    void unregister_plugin(const std::string& plugin_name);

    // Get all registered schemas.
    std::vector<PluginUISchema> schemas() const;

    // Get schemas together with the stable IDs required for interaction callbacks.
    std::vector<RegisteredPluginUISchema> registered_schemas() const;

    // Get a schema by ID.  Returns nullptr if not found.
    const PluginUISchema* find_schema(const std::string& schema_id) const;

    // Set a property value (called by the frontend when the user edits a property).
    // Returns the actual value set (may be clamped by the plugin).
    std::string set_property_value(const std::string& schema_id,
                                   const std::string& property_id,
                                   const std::string& new_value);

    // Trigger an action (called by the frontend when the user clicks a button).
    void trigger_action(const std::string& schema_id, const std::string& action_id);

    // Set a listener that is called when schemas are added/removed/updated.
    // The listener is called from the registry's mutex, so it should not
    // call back into the registry.
    void set_change_listener(std::function<void()> listener);

    // Clear all schemas.
    void clear();

   private:
    struct Entry
    {
        std::string       id;
        PluginUISchema    schema;
        PluginUICallbacks callbacks;
    };

    mutable std::mutex    mutex_;
    std::vector<Entry>    entries_;
    std::function<void()> change_listener_;
};

}   // namespace spectra
