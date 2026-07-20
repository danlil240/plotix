// settings_widget.cpp — Qt settings panel implementation.

#include "settings_widget.hpp"

#include "ui/settings/settings_store.hpp"
#include "ui/theme/theme.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

QtSettingsWidget::QtSettingsWidget(ui::settings::SettingsStore* store,
                                   ui::ThemeManager*           theme_mgr,
                                   QWidget*                    parent)
    : QDockWidget("Settings", parent), store_(store), theme_mgr_(theme_mgr)
{
    setObjectName("settings_panel");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget(this);
    setWidget(content);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    // ── Appearance group ──────────────────────────────────────────────
    auto* appearance_group = new QGroupBox("Appearance", content);
    auto* form = new QFormLayout(appearance_group);

    theme_combo_ = new QComboBox(appearance_group);
    theme_combo_->setObjectName("theme_combo");
    theme_combo_->addItems({"night", "dark", "light", "high_contrast"});
    form->addRow("Theme", theme_combo_);

    palette_combo_ = new QComboBox(appearance_group);
    palette_combo_->setObjectName("palette_combo");
    palette_combo_->addItems({"default", "colorblind", "tol_bright", "tol_muted",
                              "ibm", "wong", "viridis", "monochrome"});
    form->addRow("Data Palette", palette_combo_);

    layout->addWidget(appearance_group);

    // ── Panel visibility group ────────────────────────────────────────
    auto* panels_group = new QGroupBox("Panel Visibility", content);
    auto* panels_layout = new QVBoxLayout(panels_group);

    inspector_check_ = new QCheckBox("Inspector", panels_group);
    inspector_check_->setObjectName("inspector_visible_check");
    panels_layout->addWidget(inspector_check_);

    nav_rail_check_ = new QCheckBox("Navigation Rail", panels_group);
    nav_rail_check_->setObjectName("nav_rail_visible_check");
    panels_layout->addWidget(nav_rail_check_);

    timeline_check_ = new QCheckBox("Timeline", panels_group);
    timeline_check_->setObjectName("timeline_visible_check");
    panels_layout->addWidget(timeline_check_);

    layout->addWidget(panels_group);

    layout->addStretch();

    // ── Load current values from store ────────────────────────────────
    if (store_)
    {
        const auto& d = store_->data();

        int theme_idx = theme_combo_->findText(QString::fromStdString(d.default_theme));
        if (theme_idx >= 0)
            theme_combo_->setCurrentIndex(theme_idx);

        int pal_idx = palette_combo_->findText(QString::fromStdString(d.default_data_palette));
        if (pal_idx >= 0)
            palette_combo_->setCurrentIndex(pal_idx);

        inspector_check_->setChecked(d.inspector_visible);
        nav_rail_check_->setChecked(d.nav_rail_visible);
        timeline_check_->setChecked(d.timeline_visible);
    }

    // ── Connections ───────────────────────────────────────────────────
    connect(theme_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &QtSettingsWidget::on_theme_changed);
    connect(palette_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &QtSettingsWidget::on_palette_changed);
    connect(inspector_check_, &QCheckBox::toggled,
            this, &QtSettingsWidget::on_inspector_toggled);
    connect(nav_rail_check_, &QCheckBox::toggled,
            this, &QtSettingsWidget::on_nav_rail_toggled);
    connect(timeline_check_, &QCheckBox::toggled,
            this, &QtSettingsWidget::on_timeline_toggled);
}

void QtSettingsWidget::on_theme_changed(int index)
{
    if (!store_ || !theme_mgr_ || index < 0)
        return;

    QString theme = theme_combo_->itemText(index);
    auto& d       = store_->data_mut();
    d.default_theme = theme.toStdString();
    theme_mgr_->set_theme(d.default_theme);
    theme_mgr_->reset_glass_defaults();
    theme_mgr_->save_current_as_default();
    store_->notify_change();
    emit settings_changed();
}

void QtSettingsWidget::on_palette_changed(int index)
{
    if (!store_ || !theme_mgr_ || index < 0)
        return;

    QString pal = palette_combo_->itemText(index);
    auto& d     = store_->data_mut();
    d.default_data_palette = pal.toStdString();
    theme_mgr_->set_data_palette(d.default_data_palette);
    store_->notify_change();
    emit settings_changed();
}

void QtSettingsWidget::on_inspector_toggled(bool checked)
{
    if (!store_)
        return;
    store_->data_mut().inspector_visible = checked;
    store_->notify_change();
    emit settings_changed();
}

void QtSettingsWidget::on_nav_rail_toggled(bool checked)
{
    if (!store_)
        return;
    store_->data_mut().nav_rail_visible = checked;
    store_->notify_change();
    emit settings_changed();
}

void QtSettingsWidget::on_timeline_toggled(bool checked)
{
    if (!store_)
        return;
    store_->data_mut().timeline_visible = checked;
    store_->notify_change();
    emit settings_changed();
}

}   // namespace spectra::adapters::qt
