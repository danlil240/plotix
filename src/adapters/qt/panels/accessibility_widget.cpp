// accessibility_widget.cpp — Qt accessibility panel implementation.

#include "accessibility_widget.hpp"

#include "ui/accessibility/sonification.hpp"
#include "app/frontend_services.hpp"
#include "ui/data/html_table_export.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

QtAccessibilityWidget::QtAccessibilityWidget(FigureRegistry* registry,
                                             DialogService*  dialogs,
                                             QWidget*        parent)
    : QDockWidget("Accessibility", parent), registry_(registry), dialogs_(dialogs)
{
    setObjectName("accessibility_panel");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget(this);
    setWidget(content);

    build_ui();
}

void QtAccessibilityWidget::build_ui()
{
    auto* content = widget();
    auto* layout  = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ── Sonification group ────────────────────────────────────────────
    auto* sono_group = new QGroupBox("Sonification", content);
    sono_group->setObjectName("accessibility_sonification_group");
    auto* sono_layout = new QVBoxLayout(sono_group);

    // Axes selector
    auto* axes_row = new QHBoxLayout();
    axes_row->addWidget(new QLabel("Axes:", sono_group));
    axes_combo_ = new QComboBox(sono_group);
    axes_combo_->setObjectName("accessibility_axes_combo");
    axes_row->addWidget(axes_combo_);
    sono_layout->addLayout(axes_row);

    // Duration
    auto* dur_row = new QHBoxLayout();
    dur_row->addWidget(new QLabel("Duration (s):", sono_group));
    duration_spin_ = new QDoubleSpinBox(sono_group);
    duration_spin_->setObjectName("accessibility_duration");
    duration_spin_->setRange(0.1, 60.0);
    duration_spin_->setDecimals(2);
    duration_spin_->setValue(3.0);
    dur_row->addWidget(duration_spin_);
    sono_layout->addLayout(dur_row);

    // Frequency range
    auto* freq_row = new QHBoxLayout();
    freq_row->addWidget(new QLabel("Freq Lo (Hz):", sono_group));
    freq_lo_spin_ = new QDoubleSpinBox(sono_group);
    freq_lo_spin_->setObjectName("accessibility_freq_lo");
    freq_lo_spin_->setRange(20.0, 20000.0);
    freq_lo_spin_->setDecimals(1);
    freq_lo_spin_->setValue(220.0);
    freq_row->addWidget(freq_lo_spin_);

    freq_row->addWidget(new QLabel("Hi (Hz):", sono_group));
    freq_hi_spin_ = new QDoubleSpinBox(sono_group);
    freq_hi_spin_->setObjectName("accessibility_freq_hi");
    freq_hi_spin_->setRange(20.0, 20000.0);
    freq_hi_spin_->setDecimals(1);
    freq_hi_spin_->setValue(880.0);
    freq_row->addWidget(freq_hi_spin_);
    sono_layout->addLayout(freq_row);

    // Amplitude
    auto* amp_row = new QHBoxLayout();
    amp_row->addWidget(new QLabel("Amplitude:", sono_group));
    amplitude_spin_ = new QDoubleSpinBox(sono_group);
    amplitude_spin_->setObjectName("accessibility_amplitude");
    amplitude_spin_->setRange(0.0, 1.0);
    amplitude_spin_->setDecimals(2);
    amplitude_spin_->setSingleStep(0.05);
    amplitude_spin_->setValue(0.5);
    amp_row->addWidget(amplitude_spin_);
    sono_layout->addLayout(amp_row);

    // Sonify button
    sonify_btn_ = new QPushButton("Export WAV...", sono_group);
    sonify_btn_->setObjectName("accessibility_sonify_btn");
    sono_layout->addWidget(sonify_btn_);

    sonify_status_ = new QLabel("Ready", sono_group);
    sonify_status_->setStyleSheet("color: gray;");
    sono_layout->addWidget(sonify_status_);

    layout->addWidget(sono_group);

    // ── HTML table export group ───────────────────────────────────────
    auto* html_group = new QGroupBox("Data Table Export", content);
    html_group->setObjectName("accessibility_html_group");
    auto* html_layout = new QVBoxLayout(html_group);

    auto* html_desc = new QLabel(
        "Export an accessible HTML table representation\n"
        "of the active figure for screen readers.", html_group);
    html_desc->setStyleSheet("color: gray;");
    html_layout->addWidget(html_desc);

    html_btn_ = new QPushButton("Export HTML Table...", html_group);
    html_btn_->setObjectName("accessibility_html_btn");
    html_layout->addWidget(html_btn_);

    html_status_ = new QLabel("Ready", html_group);
    html_status_->setStyleSheet("color: gray;");
    html_layout->addWidget(html_status_);

    layout->addWidget(html_group);

    layout->addStretch();

    // ── Connections ───────────────────────────────────────────────────
    connect(sonify_btn_, &QPushButton::clicked,
            this, &QtAccessibilityWidget::on_sonify_clicked);
    connect(html_btn_, &QPushButton::clicked,
            this, &QtAccessibilityWidget::on_export_html_clicked);
}

void QtAccessibilityWidget::set_active_figure(FigureId id)
{
    active_id_ = id;
    refresh_axes_list();
}

void QtAccessibilityWidget::refresh_axes_list()
{
    axes_combo_->clear();

    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
    {
        sonify_btn_->setEnabled(false);
        html_btn_->setEnabled(false);
        return;
    }

    Figure* fig = registry_->get(active_id_);
    if (!fig)
    {
        sonify_btn_->setEnabled(false);
        html_btn_->setEnabled(false);
        return;
    }

    sonify_btn_->setEnabled(true);
    html_btn_->setEnabled(true);

    const auto& axes_list = fig->axes();
    for (size_t i = 0; i < axes_list.size(); ++i)
    {
        QString label = QString("Axes %1").arg(i + 1);
        if (axes_list[i])
        {
            std::string title = axes_list[i]->title();
            if (!title.empty())
                label += QString(" — %1").arg(QString::fromStdString(title));
        }
        axes_combo_->addItem(label, static_cast<int>(i));
    }

    if (axes_combo_->count() == 0)
    {
        axes_combo_->addItem("(no axes)", -1);
        sonify_btn_->setEnabled(false);
    }
}

void QtAccessibilityWidget::on_sonify_clicked()
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
        return;

    Figure* fig = registry_->get(active_id_);
    if (!fig)
        return;

    int axes_idx = axes_combo_->currentData().toInt();
    if (axes_idx < 0)
        return;

    Axes* axes = fig->get_axes(static_cast<size_t>(axes_idx));
    if (!axes)
        return;

    if (!dialogs_)
        return;
    auto path = dialogs_->file_dialog(DialogService::FileType::Save,
                                      "Export Sonification WAV",
                                      "spectra_sonify.wav",
                                      {{"WAV Audio", "*.wav"}});
    if (!path)
        return;

    SonificationParams params;
    params.duration_sec = static_cast<float>(duration_spin_->value());
    params.freq_lo_hz   = static_cast<float>(freq_lo_spin_->value());
    params.freq_hi_hz   = static_cast<float>(freq_hi_spin_->value());
    params.amplitude    = static_cast<float>(amplitude_spin_->value());

    const QString qpath = QString::fromStdString(*path);
    if (sonify_axes_to_wav(*axes, *path, params))
    {
        sonify_status_->setText("WAV exported: " + qpath);
        sonify_status_->setStyleSheet("color: green;");
        SPECTRA_LOG_INFO("qt_accessibility", "Sonification WAV exported to '{}'", *path);
    }
    else
    {
        sonify_status_->setText("Failed — no compatible series?");
        sonify_status_->setStyleSheet("color: red;");
        SPECTRA_LOG_WARN("qt_accessibility", "Sonification failed for figure {}", active_id_);
    }
}

void QtAccessibilityWidget::on_export_html_clicked()
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
        return;

    Figure* fig = registry_->get(active_id_);
    if (!fig)
        return;

    if (!dialogs_)
        return;
    auto path = dialogs_->file_dialog(DialogService::FileType::Save,
                                      "Export HTML Table",
                                      "spectra_data.html",
                                      {{"HTML Document", "*.html"}});
    if (!path)
        return;

    const QString qpath = QString::fromStdString(*path);
    if (figure_to_html_table_file(*fig, *path))
    {
        html_status_->setText("HTML exported: " + qpath);
        html_status_->setStyleSheet("color: green;");
        SPECTRA_LOG_INFO("qt_accessibility", "HTML table exported to '{}'", *path);
    }
    else
    {
        html_status_->setText("Failed to write HTML");
        html_status_->setStyleSheet("color: red;");
        SPECTRA_LOG_WARN("qt_accessibility", "HTML table export failed for figure {}", active_id_);
    }
}

}   // namespace spectra::adapters::qt
