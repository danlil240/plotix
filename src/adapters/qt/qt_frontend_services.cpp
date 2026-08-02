// qt_frontend_services.cpp — Qt frontend service implementations.

#include "qt_frontend_services.hpp"

#include <spectra/logger.hpp>

#include "ui/native_dialog_policy.hpp"

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QInputDialog>

#include <QString>
#include <QStringList>

#include <cstdlib>
#include <cctype>

namespace spectra::adapters::qt
{
namespace
{

std::string automation_key(const char* prefix, const std::string& title)
{
    std::string key(prefix);
    key.reserve(key.size() + title.size());
    for (const unsigned char c : title)
        key.push_back(std::isalnum(c) ? static_cast<char>(std::toupper(c)) : '_');
    return key;
}

}   // namespace

// ─── QtDialogService ──────────────────────────────────────────────────────────

std::optional<std::string> QtDialogService::file_dialog(FileType                       type,
                                                        const std::string&             title,
                                                        const std::string&             default_path,
                                                        const std::vector<FileFilter>& filters)
{
    // Automation must never block on a native modal dialog. An explicit path
    // provides a deterministic launched-process seam for artifact tests; with
    // no path the operation is treated exactly like user cancellation.
    if (!native_dialogs_enabled())
    {
        const std::string key = automation_key("SPECTRA_QT_DIALOG_", title);
        if (const char* scripted = std::getenv(key.c_str()); scripted && *scripted)
            return std::string(scripted);
        if (const char* scripted = std::getenv("SPECTRA_QT_DIALOG_PATH"); scripted && *scripted)
            return std::string(scripted);
        SPECTRA_LOG_INFO("qt_dialog", "File dialog suppressed (automation mode)");
        return std::nullopt;
    }

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
        result = QFileDialog::getOpenFileName(nullptr, qt_title, qt_path, filter_list.join(";;"));
    }
    else
    {
        result = QFileDialog::getSaveFileName(nullptr, qt_title, qt_path, filter_list.join(";;"));
    }

    if (result.isEmpty())
        return std::nullopt;
    return result.toStdString();
}

bool QtDialogService::message_box(const std::string& title,
                                  const std::string& message,
                                  bool               cancel_button)
{
    if (!native_dialogs_enabled())
        return false;

    QMessageBox::StandardButtons buttons =
        cancel_button ? QMessageBox::Ok | QMessageBox::Cancel : QMessageBox::Ok;

    QMessageBox::StandardButton rc = QMessageBox::question(nullptr,
                                                           QString::fromStdString(title),
                                                           QString::fromStdString(message),
                                                           buttons,
                                                           QMessageBox::Ok);

    return rc == QMessageBox::Ok;
}

std::optional<Color> QtDialogService::color_picker(const std::string& title, const Color& initial)
{
    if (!native_dialogs_enabled())
        return std::nullopt;

    QColor initial_qt(static_cast<int>(initial.r * 255.0f),
                      static_cast<int>(initial.g * 255.0f),
                      static_cast<int>(initial.b * 255.0f),
                      static_cast<int>(initial.a * 255.0f));

    QColor result = QColorDialog::getColor(initial_qt,
                                           nullptr,
                                           QString::fromStdString(title),
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

std::optional<double> QtDialogService::number_input(const std::string& title,
                                                    const std::string& label,
                                                    double             initial,
                                                    double             minimum,
                                                    double             maximum,
                                                    int                decimals)
{
    if (!native_dialogs_enabled())
    {
        const std::string key = automation_key("SPECTRA_QT_NUMBER_", title);
        const char*       raw = std::getenv(key.c_str());
        if (!raw || !*raw)
            raw = std::getenv("SPECTRA_QT_NUMBER_INPUT");
        if (!raw || !*raw)
            return std::nullopt;
        char*  end   = nullptr;
        double value = std::strtod(raw, &end);
        if (end == raw || *end != '\0' || value < minimum || value > maximum)
            return std::nullopt;
        return value;
    }

    bool         ok    = false;
    const double value = QInputDialog::getDouble(nullptr,
                                                 QString::fromStdString(title),
                                                 QString::fromStdString(label),
                                                 initial,
                                                 minimum,
                                                 maximum,
                                                 decimals,
                                                 &ok);
    return ok ? std::optional<double>(value) : std::nullopt;
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

FigureId QtWindowService::create_window(const std::string& title, uint32_t width, uint32_t height)
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
