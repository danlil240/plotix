// export_widget.cpp — Qt export panel implementation.

#include "export_widget.hpp"

#include "app/frontend_services.hpp"
#include "io/export_registry.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

QtExportWidget::QtExportWidget(ExportFormatRegistry* formats,
                               FigureRegistry*       registry,
                               DialogService*        dialog_service,
                               QWidget*              parent)
    : QDockWidget("Export", parent), formats_(formats), registry_(registry), dialogs_(dialog_service)
{
    setObjectName("export_panel");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget(this);
    setWidget(content);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    // ── Format selection ──────────────────────────────────────────────
    auto* format_group = new QFormLayout();

    format_combo_ = new QComboBox(content);
    format_combo_->setObjectName("export_format");
    format_combo_->addItem("PNG Image (built-in)", "png_builtin");
    format_group->addRow("Format", format_combo_);

    layout->addLayout(format_group);

    // ── Resolution ────────────────────────────────────────────────────
    auto* res_group = new QFormLayout();

    width_spin_ = new QSpinBox(content);
    width_spin_->setObjectName("export_width");
    width_spin_->setRange(64, 16384);
    width_spin_->setValue(1920);
    width_spin_->setSuffix(" px");
    res_group->addRow("Width", width_spin_);

    height_spin_ = new QSpinBox(content);
    height_spin_->setObjectName("export_height");
    height_spin_->setRange(64, 16384);
    height_spin_->setValue(1080);
    height_spin_->setSuffix(" px");
    res_group->addRow("Height", height_spin_);

    layout->addLayout(res_group);

    // ── Output path ───────────────────────────────────────────────────
    auto* path_layout = new QHBoxLayout();
    path_edit_ = new QLineEdit(content);
    path_edit_->setObjectName("export_path");
    path_edit_->setPlaceholderText("Select output file...");
    path_layout->addWidget(path_edit_);

    browse_btn_ = new QPushButton("Browse...", content);
    browse_btn_->setObjectName("export_browse");
    path_layout->addWidget(browse_btn_);

    layout->addLayout(path_layout);

    // ── Export button ─────────────────────────────────────────────────
    export_btn_ = new QPushButton("Export", content);
    export_btn_->setObjectName("export_button");
    layout->addWidget(export_btn_);

    // ── Status ────────────────────────────────────────────────────────
    status_label_ = new QLabel("No figure selected", content);
    status_label_->setStyleSheet("color: gray; padding: 4px;");
    layout->addWidget(status_label_);

    layout->addStretch();

    // ── Connections ───────────────────────────────────────────────────
    connect(browse_btn_, &QPushButton::clicked,
            this, &QtExportWidget::on_browse_clicked);
    connect(export_btn_, &QPushButton::clicked,
            this, &QtExportWidget::on_export_clicked);

    refresh_formats();
}

void QtExportWidget::set_active_figure(FigureId id)
{
    active_id_ = id;
    if (id == INVALID_FIGURE_ID)
    {
        status_label_->setText("No figure selected");
        status_label_->setStyleSheet("color: gray;");
        export_btn_->setEnabled(false);
        return;
    }

    if (registry_)
    {
        Figure* fig = registry_->get(id);
        if (fig)
        {
            width_spin_->setValue(static_cast<int>(fig->width()));
            height_spin_->setValue(static_cast<int>(fig->height()));
            status_label_->setText(QString("Figure %1 ready").arg(id));
            status_label_->setStyleSheet("color: gray;");
            export_btn_->setEnabled(true);
            return;
        }
    }

    status_label_->setText("Figure not found");
    status_label_->setStyleSheet("color: orange;");
    export_btn_->setEnabled(false);
}

void QtExportWidget::refresh_formats()
{
    if (!formats_)
        return;

    // Keep the built-in PNG entry, add plugin formats
    int current = format_combo_->currentIndex();
    while (format_combo_->count() > 1)
        format_combo_->removeItem(1);

    auto available = formats_->available_formats();
    for (const auto& fmt : available)
    {
        QString label = QString("%1 (*.%2)")
                            .arg(QString::fromStdString(fmt.name))
                            .arg(QString::fromStdString(fmt.extension));
        format_combo_->addItem(label, QString::fromStdString(fmt.name));
    }

    if (current >= 0 && current < format_combo_->count())
        format_combo_->setCurrentIndex(current);
}

void QtExportWidget::on_browse_clicked()
{
    if (!dialogs_)
        return;

    QString current_format = format_combo_->currentData().toString();
    std::string filter;
    std::string default_ext = "png";

    if (current_format == "png_builtin")
    {
        filter = "PNG Image;;*.png";
        default_ext = "png";
    }
    else
    {
        default_ext = current_format.toStdString();
        filter = current_format.toStdString();
    }

    std::vector<DialogService::FileFilter> filters;
    filters.push_back({current_format.toStdString(), "*." + default_ext});

    auto result = dialogs_->file_dialog(
        DialogService::FileType::Save,
        "Export Figure",
        "",
        filters);

    if (result)
        path_edit_->setText(QString::fromStdString(*result));
}

void QtExportWidget::on_export_clicked()
{
    if (active_id_ == INVALID_FIGURE_ID || !registry_)
    {
        status_label_->setText("No figure selected");
        status_label_->setStyleSheet("color: red;");
        return;
    }

    Figure* fig = registry_->get(active_id_);
    if (!fig)
    {
        status_label_->setText("Figure not found");
        status_label_->setStyleSheet("color: red;");
        return;
    }

    QString path = path_edit_->text().trimmed();
    if (path.isEmpty())
    {
        status_label_->setText("No output path specified");
        status_label_->setStyleSheet("color: red;");
        return;
    }

    QString format_key = format_combo_->currentData().toString();
    uint32_t w = static_cast<uint32_t>(width_spin_->value());
    uint32_t h = static_cast<uint32_t>(height_spin_->value());

    if (format_key == "png_builtin")
    {
        fig->save_png(path.toStdString(), w, h);
        status_label_->setText(QString("Exported: %1").arg(path));
        status_label_->setStyleSheet("color: green;");
        SPECTRA_LOG_INFO("qt_export", "Exported figure " + std::to_string(active_id_) + " to " + path.toStdString());
    }
    else if (formats_)
    {
        // Plugin export format
        // TODO: When readback is available from the Qt canvas, pass RGBA pixels
        // to ExportFormatRegistry::export_figure(). For now, use the figure JSON
        // path with null pixels (data-only export).
        std::string fmt_name = format_key.toStdString();
        bool ok = formats_->export_figure(fmt_name, "", nullptr, w, h, path.toStdString());
        if (ok)
        {
            status_label_->setText(QString("Exported: %1").arg(path));
            status_label_->setStyleSheet("color: green;");
        }
        else
        {
            status_label_->setText("Export failed");
            status_label_->setStyleSheet("color: red;");
        }
    }
}

}   // namespace spectra::adapters::qt
