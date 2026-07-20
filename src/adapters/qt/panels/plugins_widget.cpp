// plugins_widget.cpp — Qt widget for plugin management.

#include "plugins_widget.hpp"

#include "ui/workspace/plugin_api.hpp"
#include "ui/workspace/plugin_ui_schema.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <spectra/logger.hpp>

namespace spectra::adapters::qt
{

QtPluginsWidget::QtPluginsWidget(PluginManager* mgr, PluginUIRegistry* ui_reg,
                                 QWidget* parent)
    : QDockWidget("Plugins", parent), mgr_(mgr), ui_reg_(ui_reg)
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
    auto* content = scroll ? scroll->widget() : nullptr;
    if (!content)
        return;

    delete content->layout();

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // ── Action buttons ──
    auto* btn_layout = new QHBoxLayout();

    auto* load_btn = new QPushButton("Load Plugin...", content);
    connect(load_btn, &QPushButton::clicked, this, &QtPluginsWidget::on_load_plugin);
    btn_layout->addWidget(load_btn);

    auto* scan_btn = new QPushButton("Scan Default", content);
    connect(scan_btn, &QPushButton::clicked, this, &QtPluginsWidget::on_scan_default);
    btn_layout->addWidget(scan_btn);

    layout->addLayout(btn_layout);

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

        auto* info = new QLabel(
            QString("Version: %1\nAuthor: %2\n%3")
                .arg(QString::fromStdString(plugin.version),
                     QString::fromStdString(plugin.author),
                     QString::fromStdString(plugin.description)),
            group);
        info->setWordWrap(true);
        form->addWidget(info);

        auto* controls = new QHBoxLayout();

        auto* enable_cb = new QCheckBox("Enabled", group);
        enable_cb->setChecked(plugin.enabled);
        QString plugin_name = QString::fromStdString(plugin.name);
        connect(enable_cb, &QCheckBox::toggled, group,
            [this, plugin_name](bool checked) {
                if (mgr_)
                    mgr_->set_plugin_enabled(plugin_name.toStdString(), checked);
            });
        controls->addWidget(enable_cb);

        auto* unload_btn = new QPushButton("Unload", group);
        connect(unload_btn, &QPushButton::clicked, group,
            [this, plugin_name]() {
                if (mgr_ && mgr_->unload_plugin(plugin_name.toStdString()))
                {
                    if (ui_reg_)
                        ui_reg_->unregister_plugin(plugin_name.toStdString());
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
    QString path = QFileDialog::getOpenFileName(
        this, "Load Plugin",
        QString::fromStdString(PluginManager::default_plugin_dir()),
#ifdef _WIN32
        "Plugins (*.dll)"
#elif defined(__APPLE__)
        "Plugins (*.dylib)"
#else
        "Plugins (*.so)"
#endif
    );

    if (path.isEmpty())
        return;

    if (mgr_->load_plugin(path.toStdString()))
    {
        SPECTRA_LOG_INFO("qt_plugins", "Loaded plugin: " + path.toStdString());
    }
    else
    {
        QMessageBox::warning(this, "Load Plugin", "Failed to load: " + path);
    }
    refresh();
}

void QtPluginsWidget::on_scan_dirs()
{
    // Scan default + custom dirs
    on_scan_default();
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
    refresh();
}

}   // namespace spectra::adapters::qt
