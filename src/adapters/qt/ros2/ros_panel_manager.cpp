// RosPanelManager — Qt panel composition for ROS2/PX4 displays.
//
// Implementation note: The DisplayPlugin::draw_inspector_ui() method is
// framework-neutral (designed for ImGui). In the Qt frontend, we provide
// a Qt wrapper that hosts the plugin's inspector in a QWidget. The actual
// ImGui→Qt inspector bridge is a future task; for now, the inspector panel
// shows display metadata and status, and the per-display dock hosts the
// plugin's auxiliary UI.

#include "ros_panel_manager.hpp"

#include <QComboBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include "../../ros2/display/display_plugin.hpp"
#include "../../ros2/display/display_registry.hpp"

namespace spectra::adapters::qt
{

RosPanelManager::RosPanelManager(QMainWindow*           main_window,
                                 ros2::DisplayRegistry* registry,
                                 ros2::SceneManager*    scene_manager,
                                 QObject*               parent)
    : QObject(parent), main_window_(main_window), registry_(registry), scene_manager_(scene_manager)
{
    create_displays_list_panel();
    create_inspector_panel();
}

RosPanelManager::~RosPanelManager()
{
    for (auto& entry : displays_)
    {
        if (entry.plugin)
            entry.plugin->on_destroy();
    }
}

// ─── Display management ──────────────────────────────────────────────────────

std::string RosPanelManager::add_display(const std::string& type_id)
{
    if (!registry_)
        return {};

    auto plugin = registry_->create(type_id);
    if (!plugin)
        return {};

    std::string id = generate_id();

    RosDisplayEntry entry;
    entry.id      = id;
    entry.type_id = type_id;
    entry.plugin  = std::move(plugin);
    entry.enabled = true;
    entry.dock    = create_display_dock(entry.plugin.get(), id);

    if (main_window_ && entry.dock)
        main_window_->addDockWidget(Qt::RightDockWidgetArea, entry.dock);

    displays_.push_back(std::move(entry));
    refresh_display_list();

    return id;
}

void RosPanelManager::remove_display(const std::string& id)
{
    for (auto it = displays_.begin(); it != displays_.end(); ++it)
    {
        if (it->id == id)
        {
            if (it->plugin)
            {
                it->plugin->on_disable();
                it->plugin->on_destroy();
            }
            if (it->dock)
            {
                if (main_window_)
                    main_window_->removeDockWidget(it->dock);
                delete it->dock;
            }
            displays_.erase(it);
            refresh_display_list();
            return;
        }
    }
}

void RosPanelManager::set_display_enabled(const std::string& id, bool enabled)
{
    auto* entry = find_display(id);
    if (!entry)
        return;

    entry->enabled = enabled;
    if (entry->plugin)
    {
        entry->plugin->set_enabled(enabled);
        if (enabled)
        {
            // Context will be set by the caller via set_context()
            entry->plugin->on_enable({});
        }
        else
        {
            entry->plugin->on_disable();
        }
    }
    if (entry->dock)
        entry->dock->setVisible(enabled);
    refresh_display_list();
}

RosDisplayEntry* RosPanelManager::find_display(const std::string& id)
{
    for (auto& entry : displays_)
    {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

// ─── Context and update ──────────────────────────────────────────────────────

void RosPanelManager::set_context(const ros2::DisplayContext& context)
{
    // Store context pointer for future enable calls
    context_ = const_cast<ros2::DisplayContext*>(&context);

    for (auto& entry : displays_)
    {
        if (entry.enabled && entry.plugin)
            entry.plugin->on_enable(context);
    }
}

void RosPanelManager::update(float dt_sec)
{
    for (auto& entry : displays_)
    {
        if (entry.enabled && entry.plugin)
            entry.plugin->on_update(dt_sec);
    }
}

void RosPanelManager::submit_renderables()
{
    if (!scene_manager_)
        return;

    for (auto& entry : displays_)
    {
        if (entry.enabled && entry.plugin)
            entry.plugin->submit_renderables(*scene_manager_);
    }
}

// ─── Panel layout ────────────────────────────────────────────────────────────

void RosPanelManager::show_panels(bool visible)
{
    if (displays_list_dock_)
        displays_list_dock_->setVisible(visible);
    if (inspector_dock_)
        inspector_dock_->setVisible(visible);
}

std::string RosPanelManager::serialize_layout() const
{
    // Simple JSON serialization: [{id, type_id, enabled, config}, ...]
    std::string json = "[";
    for (size_t i = 0; i < displays_.size(); ++i)
    {
        const auto& entry = displays_[i];
        if (i > 0)
            json += ",";
        json += "{\"id\":\"" + entry.id + "\"";
        json += ",\"type_id\":\"" + entry.type_id + "\"";
        json += ",\"enabled\":" + std::string(entry.enabled ? "true" : "false");
        if (entry.plugin)
        {
            json += ",\"topic\":\"" + entry.plugin->topic() + "\"";
            json += ",\"config\":\"" + entry.plugin->serialize_config_blob() + "\"";
        }
        json += "}";
    }
    json += "]";
    return json;
}

bool RosPanelManager::deserialize_layout(const std::string& json)
{
    // Simple parsing: look for "type_id":"..." patterns
    // This is a minimal parser; a full JSON parser would be used in production.
    size_t pos = 0;
    while (pos < json.size())
    {
        size_t type_pos = json.find("\"type_id\":\"", pos);
        if (type_pos == std::string::npos)
            break;
        type_pos += 11;   // Skip past "type_id":"
        size_t type_end = json.find("\"", type_pos);
        if (type_end == std::string::npos)
            break;
        std::string type_id = json.substr(type_pos, type_end - type_pos);

        std::string id = add_display(type_id);
        if (!id.empty())
        {
            // Try to find topic
            size_t topic_pos = json.find("\"topic\":\"", type_end);
            if (topic_pos != std::string::npos && topic_pos < json.find("}", type_end))
            {
                topic_pos += 9;
                size_t topic_end = json.find("\"", topic_pos);
                if (topic_end != std::string::npos)
                {
                    std::string topic = json.substr(topic_pos, topic_end - topic_pos);
                    auto*       entry = find_display(id);
                    if (entry && entry->plugin)
                        entry->plugin->set_topic(topic);
                }
            }
        }

        pos = type_end;
    }
    refresh_display_list();
    return true;
}

// ─── Slots ───────────────────────────────────────────────────────────────────

void RosPanelManager::refresh_display_list()
{
    if (!displays_tree_)
        return;

    displays_tree_->clear();

    for (const auto& entry : displays_)
    {
        auto* item = new QTreeWidgetItem(displays_tree_);
        item->setText(0, QString::fromStdString(entry.id));
        item->setText(1, QString::fromStdString(entry.type_id));
        item->setText(2, entry.enabled ? "Enabled" : "Disabled");
        if (entry.plugin)
        {
            item->setText(3, QString::fromStdString(entry.plugin->topic()));
            auto    status = entry.plugin->status();
            QString status_str;
            switch (status)
            {
                case ros2::DisplayStatus::Ok:
                    status_str = "OK";
                    break;
                case ros2::DisplayStatus::Warn:
                    status_str = "Warn";
                    break;
                case ros2::DisplayStatus::Error:
                    status_str = "Error";
                    break;
                default:
                    status_str = "Disabled";
                    break;
            }
            item->setText(4, status_str);
        }
        item->setData(0, Qt::UserRole, QString::fromStdString(entry.id));
    }
}

void RosPanelManager::on_display_selected(QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/)
{
    if (!current)
    {
        selected_id_.clear();
        update_inspector();
        return;
    }

    selected_id_ = current->data(0, Qt::UserRole).toString().toStdString();
    update_inspector();
}

void RosPanelManager::on_add_display()
{
    if (!display_type_combo_)
        return;

    QString type_text = display_type_combo_->currentText();
    if (type_text.isEmpty())
        return;

    // Extract type_id from "Display Name (type_id)" format
    int start = type_text.indexOf('(');
    int end   = type_text.indexOf(')');
    if (start > 0 && end > start)
    {
        std::string type_id = type_text.mid(start + 1, end - start - 1).toStdString();
        add_display(type_id);
    }
}

void RosPanelManager::on_remove_display()
{
    if (selected_id_.empty())
        return;

    remove_display(selected_id_);
    selected_id_.clear();
    update_inspector();
}

// ─── Private implementation ──────────────────────────────────────────────────

void RosPanelManager::create_displays_list_panel()
{
    displays_list_dock_ = new QDockWidget("ROS Displays", main_window_);
    displays_list_dock_->setObjectName("RosDisplaysListDock");

    auto* content = new QWidget();
    auto* layout  = new QVBoxLayout(content);

    // Display type combo + add button
    auto* add_layout    = new QHBoxLayout();
    display_type_combo_ = new QComboBox();
    if (registry_)
    {
        auto types = registry_->list_types();
        for (const auto& info : types)
        {
            QString label = QString::fromStdString(info.display_name) + " ("
                            + QString::fromStdString(info.type_id) + ")";
            display_type_combo_->addItem(label);
        }
    }
    add_button_ = new QPushButton("Add");
    add_layout->addWidget(display_type_combo_);
    add_layout->addWidget(add_button_);
    layout->addLayout(add_layout);

    // Displays tree
    displays_tree_ = new QTreeWidget();
    displays_tree_->setColumnCount(5);
    displays_tree_->setHeaderLabels({"ID", "Type", "State", "Topic", "Status"});
    displays_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(displays_tree_);

    // Remove button
    remove_button_ = new QPushButton("Remove Display");
    layout->addWidget(remove_button_);

    displays_list_dock_->setWidget(content);

    if (main_window_)
        main_window_->addDockWidget(Qt::LeftDockWidgetArea, displays_list_dock_);

    // Connections
    connect(add_button_, &QPushButton::clicked, this, &RosPanelManager::on_add_display);
    connect(remove_button_, &QPushButton::clicked, this, &RosPanelManager::on_remove_display);
    connect(displays_tree_,
            &QTreeWidget::currentItemChanged,
            this,
            &RosPanelManager::on_display_selected);
}

void RosPanelManager::create_inspector_panel()
{
    inspector_dock_ = new QDockWidget("Display Inspector", main_window_);
    inspector_dock_->setObjectName("RosDisplayInspectorDock");

    inspector_stack_ = new QStackedWidget();

    // Empty page (no display selected)
    auto* empty_label = new QLabel("No display selected");
    empty_label->setAlignment(Qt::AlignCenter);
    inspector_stack_->addWidget(empty_label);

    inspector_dock_->setWidget(inspector_stack_);

    if (main_window_)
        main_window_->addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
}

QDockWidget* RosPanelManager::create_display_dock(ros2::DisplayPlugin* plugin,
                                                  const std::string&   id)
{
    if (!plugin)
        return nullptr;

    auto* dock = new QDockWidget(QString::fromStdString(plugin->display_name()), main_window_);
    dock->setObjectName(QString::fromStdString("RosDisplay_" + id));

    auto* content = new QWidget();
    auto* layout  = new QVBoxLayout(content);

    // Display info
    auto* info_label = new QLabel(QString("Type: %1\nTopic: %2")
                                      .arg(QString::fromStdString(plugin->type_id()))
                                      .arg(QString::fromStdString(plugin->topic())));
    layout->addWidget(info_label);

    // Placeholder for plugin's auxiliary UI
    // In the future, this will host either:
    //   - A Qt-native inspector widget generated from the plugin's properties
    //   - An ImGui render surface for plugins that use draw_inspector_ui()
    auto* placeholder = new QLabel("(Display render area — plugin auxiliary UI)");
    placeholder->setAlignment(Qt::AlignCenter);
    layout->addWidget(placeholder);

    dock->setWidget(content);
    return dock;
}

std::string RosPanelManager::generate_id()
{
    return "display_" + std::to_string(++id_counter_);
}

void RosPanelManager::update_inspector()
{
    if (!inspector_stack_)
        return;

    if (selected_id_.empty())
    {
        inspector_stack_->setCurrentIndex(0);   // Empty page
        return;
    }

    auto* entry = find_display(selected_id_);
    if (!entry || !entry->plugin)
    {
        inspector_stack_->setCurrentIndex(0);
        return;
    }

    // For now, show a simple info page.
    // In the future, this will host the plugin's inspector UI.
    auto* info_page = new QWidget();
    auto* layout    = new QVBoxLayout(info_page);

    layout->addWidget(new QLabel(QString("ID: %1").arg(QString::fromStdString(entry->id))));
    layout->addWidget(new QLabel(QString("Type: %1").arg(QString::fromStdString(entry->type_id))));
    layout->addWidget(
        new QLabel(QString("Topic: %1").arg(QString::fromStdString(entry->plugin->topic()))));
    layout->addWidget(new QLabel(QString("Enabled: %1").arg(entry->enabled ? "Yes" : "No")));

    auto    status = entry->plugin->status();
    QString status_str;
    switch (status)
    {
        case ros2::DisplayStatus::Ok:
            status_str = "OK";
            break;
        case ros2::DisplayStatus::Warn:
            status_str = "Warning";
            break;
        case ros2::DisplayStatus::Error:
            status_str = "Error";
            break;
        default:
            status_str = "Disabled";
            break;
    }
    layout->addWidget(new QLabel(QString("Status: %1").arg(status_str)));
    if (!entry->plugin->status_text().empty())
        layout->addWidget(new QLabel(
            QString("Status Text: %1").arg(QString::fromStdString(entry->plugin->status_text()))));

    layout->addStretch();

    // Replace the current inspector page
    int current_idx = inspector_stack_->currentIndex();
    if (current_idx > 0)
    {
        auto* old_widget = inspector_stack_->widget(current_idx);
        inspector_stack_->removeWidget(old_widget);
        delete old_widget;
    }

    inspector_stack_->addWidget(info_page);
    inspector_stack_->setCurrentWidget(info_page);
}

}   // namespace spectra::adapters::qt
