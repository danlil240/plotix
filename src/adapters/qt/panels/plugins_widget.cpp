// plugins_widget.cpp — Qt widget for plugin management.

#include "plugins_widget.hpp"

#include "app/frontend_services.hpp"
#include "ui/workspace/plugin_api.hpp"
#include "ui/workspace/plugin_ui_schema.hpp"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <spectra/logger.hpp>

#include <algorithm>

namespace spectra::adapters::qt
{

QtPluginsWidget::QtPluginsWidget(PluginManager*    mgr,
                                 PluginUIRegistry* ui_reg,
                                 DialogService*    dialogs,
                                 QWidget*          parent)
    : QDockWidget("Plugins", parent), mgr_(mgr), ui_reg_(ui_reg), dialogs_(dialogs)
{
    setObjectName("plugins_dock");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    setWidget(scroll);

    refresh();
}

QtPluginsWidget::~QtPluginsWidget() = default;

void QtPluginsWidget::refresh()
{
    if (!mgr_)
        return;

    auto* scroll = static_cast<QScrollArea*>(widget());
    if (!scroll)
        return;
    QWidget* old_content = scroll->takeWidget();
    auto*    content     = new QWidget(scroll);
    scroll->setWidget(content);
    if (old_content)
        old_content->deleteLater();

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // ── Action buttons ──
    auto* btn_layout = new QHBoxLayout();

    auto* load_btn = new QPushButton("Load Plugin...", content);
    load_btn->setObjectName("plugin_load_button");
    connect(load_btn, &QPushButton::clicked, this, &QtPluginsWidget::on_load_plugin);
    btn_layout->addWidget(load_btn);

    auto* scan_btn = new QPushButton("Scan Plugin Dirs", content);
    scan_btn->setObjectName("plugin_scan_dirs_button");
    connect(scan_btn, &QPushButton::clicked, this, &QtPluginsWidget::on_scan_dirs);
    btn_layout->addWidget(scan_btn);

    auto* default_btn = new QPushButton("Rescan Default", content);
    default_btn->setObjectName("plugin_scan_default_button");
    connect(default_btn, &QPushButton::clicked, this, &QtPluginsWidget::on_scan_default);
    btn_layout->addWidget(default_btn);

    layout->addLayout(btn_layout);

    auto* dir_layout = new QHBoxLayout();
    auto* dir_edit   = new QLineEdit(content);
    dir_edit->setObjectName("plugin_scan_dir_input");
    dir_edit->setPlaceholderText("Custom plugin directory");
    connect(dir_edit, &QLineEdit::returnPressed, this, &QtPluginsWidget::on_add_scan_dir);
    dir_layout->addWidget(dir_edit);
    auto* add_dir_btn = new QPushButton("Add Dir", content);
    add_dir_btn->setObjectName("plugin_add_scan_dir_button");
    connect(add_dir_btn, &QPushButton::clicked, this, &QtPluginsWidget::on_add_scan_dir);
    dir_layout->addWidget(add_dir_btn);
    layout->addLayout(dir_layout);

    for (size_t index = 0; index < scan_dirs_.size(); ++index)
    {
        auto* row   = new QHBoxLayout();
        auto* label = new QLabel(QString::fromStdString(scan_dirs_[index]), content);
        label->setObjectName(QString("plugin_scan_dir_%1").arg(index));
        label->setWordWrap(true);
        row->addWidget(label, 1);
        auto* remove = new QPushButton("Remove", content);
        remove->setObjectName(QString("plugin_remove_scan_dir_%1").arg(index));
        connect(remove,
                &QPushButton::clicked,
                this,
                [this, index]()
                {
                    if (index < scan_dirs_.size())
                        scan_dirs_.erase(scan_dirs_.begin() + static_cast<std::ptrdiff_t>(index));
                    refresh();
                });
        row->addWidget(remove);
        layout->addLayout(row);
    }

    auto* status = new QLabel(QString::fromStdString(status_), content);
    status->setObjectName("plugin_status");
    status->setWordWrap(true);
    layout->addWidget(status);

    // ── Plugin list ──
    auto plugins = mgr_->plugins();
    if (plugins.empty())
    {
        auto* label = new QLabel("No plugins loaded.", content);
        label->setStyleSheet("color: gray; padding: 20px;");
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        layout->addStretch();
        return;
    }

    for (const auto& plugin : plugins)
    {
        auto* group = new QGroupBox(QString::fromStdString(plugin.name), content);
        auto* form = new QVBoxLayout(group);

        QStringList capabilities;
        for (const auto& capability : plugin.manifest.capabilities)
            capabilities.push_back(QString::fromStdString(capability));
        const QString capability_text =
            capabilities.empty() ? "(not declared)" : capabilities.join(", ");
        auto* info = new QLabel(
            QString("Version: %1\nAuthor: %2\n%3\nPath: %4\nAPI: v%5.%6\nCapabilities: "
                    "%7\nCalls: %8  Faults: %9  Init: %10 us%11")
                .arg(QString::fromStdString(plugin.version),
                     QString::fromStdString(plugin.author),
                     QString::fromStdString(plugin.description),
                     QString::fromStdString(plugin.path))
                .arg(SPECTRA_PLUGIN_API_VERSION_MAJOR)
                .arg(plugin.api_version_minor)
                .arg(capability_text)
                .arg(static_cast<qulonglong>(plugin.diagnostics.call_count))
                .arg(static_cast<qulonglong>(plugin.diagnostics.fault_count))
                .arg(static_cast<qulonglong>(plugin.diagnostics.init_time_us))
                .arg(plugin.diagnostics.quarantined ? "\nStatus: quarantined"
                     : plugin.diagnostics.last_fault_reason.empty()
                         ? "\nStatus: healthy"
                         : "\nLast fault: "
                               + QString::fromStdString(plugin.diagnostics.last_fault_reason)),
            group);
        info->setObjectName("plugin_info_" + QString::fromStdString(plugin.name));
        info->setWordWrap(true);
        form->addWidget(info);

        auto* controls = new QHBoxLayout();

        auto* enable_cb = new QCheckBox("Enabled", group);
        enable_cb->setObjectName("plugin_enabled_" + QString::fromStdString(plugin.name));
        enable_cb->setChecked(plugin.enabled);
        QString plugin_name = QString::fromStdString(plugin.name);
        connect(enable_cb, &QCheckBox::toggled, group,
            [this, plugin_name](bool checked) {
                if (mgr_)
                    mgr_->set_plugin_enabled(plugin_name.toStdString(), checked);
            });
        controls->addWidget(enable_cb);

        auto* unload_btn = new QPushButton("Unload", group);
        unload_btn->setObjectName("plugin_unload_" + QString::fromStdString(plugin.name));
        connect(unload_btn, &QPushButton::clicked, group,
            [this, plugin_name]() {
                if (mgr_ && mgr_->unload_plugin(plugin_name.toStdString()))
                {
                    if (ui_reg_)
                        ui_reg_->unregister_plugin(plugin_name.toStdString());
                    status_ = "Unloaded: " + plugin_name.toStdString();
                    refresh();
                }
            });
        controls->addWidget(unload_btn);

        form->addLayout(controls);
        layout->addWidget(group);
    }

    layout->addStretch();
}

void QtPluginsWidget::on_load_plugin()
{
    if (!mgr_ || !dialogs_)
        return;
    auto path = dialogs_->file_dialog(DialogService::FileType::Open,
                                      "Load Plugin",
                                      PluginManager::default_plugin_dir(),
#ifdef _WIN32
                                      {
                                          {
                                              "Plugins", "*.dll"
                                          }
                                      }
#elif defined(__APPLE__)
        {{"Plugins", "*.dylib"}}
#else
        {{"Plugins", "*.so"}}
#endif
    );

    if (!path)
        return;

    if (mgr_->load_plugin(*path))
    {
        SPECTRA_LOG_INFO("qt_plugins", "Loaded plugin: " + *path);
        status_ = "Loaded: " + *path;
    }
    else
    {
        status_ = "Failed to load: " + *path;
        dialogs_->message_box("Load Plugin", "Failed to load: " + *path);
    }
    refresh();
}

void QtPluginsWidget::on_scan_dirs()
{
    if (!mgr_)
        return;
    int                      loaded = 0;
    int                      failed = 0;
    std::vector<std::string> dirs{PluginManager::default_plugin_dir()};
    for (const auto& dir : scan_dirs_)
        if (std::ranges::find(dirs, dir) == dirs.end())
            dirs.push_back(dir);
    for (const auto& dir : dirs)
        for (const auto& path : mgr_->discover(dir))
            mgr_->load_plugin(path) ? ++loaded : ++failed;
    status_ = "Scan complete: loaded " + std::to_string(loaded) + ", failed/skipped "
              + std::to_string(failed);
    refresh();
}

void QtPluginsWidget::on_add_scan_dir()
{
    auto* edit = findChild<QLineEdit*>("plugin_scan_dir_input");
    if (!edit)
        return;
    const std::string dir = edit->text().trimmed().toStdString();
    if (dir.empty())
        return;
    if (std::ranges::find(scan_dirs_, dir) == scan_dirs_.end())
        scan_dirs_.push_back(dir);
    refresh();
}

void QtPluginsWidget::on_scan_default()
{
    if (!mgr_)
        return;

    int loaded = 0;
    int failed = 0;

    auto discovered = mgr_->discover(PluginManager::default_plugin_dir());
    for (const auto& path : discovered)
    {
        if (mgr_->load_plugin(path))
            ++loaded;
        else
            ++failed;
    }

    SPECTRA_LOG_INFO("qt_plugins",
                     "Default scan: loaded " + std::to_string(loaded) +
                     ", failed/skipped " + std::to_string(failed));
    status_ = "Default scan: loaded " + std::to_string(loaded) + ", failed/skipped "
              + std::to_string(failed);
    refresh();
}

}   // namespace spectra::adapters::qt
