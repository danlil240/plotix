// qt_frontend_services.cpp — Qt frontend service implementations.

#include "qt_frontend_services.hpp"

#include <spectra/logger.hpp>

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>

#include <QString>
#include <QStringList>

namespace spectra::adapters::qt
{

// ─── QtDialogService ──────────────────────────────────────────────────────────

std::optional<std::string>
QtDialogService::file_dialog(FileType                       type,
                              const std::string&             title,
                              const std::string&             default_path,
                              const std::vector<FileFilter>& filters)
{
    QString qt_title = QString::fromStdString(title);
    QString qt_path  = QString::fromStdString(default_path);

    // Build filter string: "PNG Image (*.png);;All Files (*)"
    QStringList filter_list;
    for (const auto& f : filters)
    {
        filter_list << QString::fromStdString(f.name + " (" + f.pattern + ")");
    }
    if (filter_list.isEmpty())
        filter_list << "All Files (*)";

    QString result;
    if (type == FileType::Open)
    {
        result = QFileDialog::getOpenFileName(
            nullptr, qt_title, qt_path, filter_list.join(";;"));
    }
    else
    {
        result = QFileDialog::getSaveFileName(
            nullptr, qt_title, qt_path, filter_list.join(";;"));
    }

    if (result.isEmpty())
        return std::nullopt;
    return result.toStdString();
}

bool QtDialogService::message_box(const std::string& title,
                                   const std::string& message,
                                   bool               cancel_button)
{
    QMessageBox::StandardButtons buttons =
        cancel_button ? QMessageBox::Ok | QMessageBox::Cancel : QMessageBox::Ok;

    QMessageBox::StandardButton rc =
        QMessageBox::question(nullptr,
                               QString::fromStdString(title),
                               QString::fromStdString(message),
                               buttons,
                               QMessageBox::Ok);

    return rc == QMessageBox::Ok;
}

std::optional<Color>
QtDialogService::color_picker(const std::string& title, const Color& initial)
{
    QColor initial_qt(
        static_cast<int>(initial.r * 255.0f),
        static_cast<int>(initial.g * 255.0f),
        static_cast<int>(initial.b * 255.0f),
        static_cast<int>(initial.a * 255.0f));

    QColor result = QColorDialog::getColor(
        initial_qt, nullptr, QString::fromStdString(title),
        QColorDialog::ShowAlphaChannel);

    if (!result.isValid())
        return std::nullopt;

    Color c;
    c.r = result.redF();
    c.g = result.greenF();
    c.b = result.blueF();
    c.a = result.alphaF();
    return c;
}

// ─── QtClipboardService ───────────────────────────────────────────────────────

void QtClipboardService::copy_text(const std::string& text)
{
    QApplication::clipboard()->setText(QString::fromStdString(text));
}

void QtClipboardService::copy_image(const std::vector<uint8_t>& png_data)
{
    QPixmap pixmap;
    if (pixmap.loadFromData(png_data.data(), static_cast<uint32_t>(png_data.size()), "PNG"))
    {
        QApplication::clipboard()->setPixmap(pixmap);
    }
    else
    {
        SPECTRA_LOG_WARN("qt_clipboard", "Failed to load PNG data for clipboard copy");
    }
}

std::string QtClipboardService::paste_text()
{
    const QClipboard* clip = QApplication::clipboard();
    const QMimeData*  mime = clip->mimeData();
    if (mime && mime->hasText())
        return mime->text().toStdString();
    return {};
}

// ─── QtRedrawRequest ──────────────────────────────────────────────────────────

void QtRedrawRequest::request_redraw()
{
    if (callback_)
        callback_();
}

void QtRedrawRequest::request_redraw(FigureId figure_id)
{
    if (figure_callback_)
        figure_callback_(figure_id);
    else if (callback_)
        callback_();
}

// ─── QtWindowService ──────────────────────────────────────────────────────────

FigureId QtWindowService::create_window(const std::string& title,
                                         uint32_t           width,
                                         uint32_t           height)
{
    if (create_fn_)
        return create_fn_(title, width, height);
    return INVALID_FIGURE_ID;
}

void QtWindowService::close_window(FigureId figure_id)
{
    if (close_fn_)
        close_fn_(figure_id);
}

void QtWindowService::focus_window(FigureId figure_id)
{
    if (focus_fn_)
        focus_fn_(figure_id);
}

size_t QtWindowService::window_count() const
{
    if (count_fn_)
        return count_fn_();
    return 0;
}

}   // namespace spectra::adapters::qt
