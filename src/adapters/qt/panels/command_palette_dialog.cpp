// command_palette_dialog.cpp — Qt-native command palette implementation.

#include "command_palette_dialog.hpp"

#include "../components/spectra_design_tokens.hpp"
#include "ui/commands/command_registry.hpp"

#include <spectra/logger.hpp>

#include <QBrush>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

namespace spectra::adapters::qt
{

QtCommandPaletteDialog::QtCommandPaletteDialog(CommandRegistry& registry, QWidget* parent)
    : QDialog(parent), registry_(registry), search_edit_(new QLineEdit(this)),
      results_list_(new QListWidget(this))
{
    setWindowTitle("Command Palette");
    setObjectName("command_palette_dialog");
    setModal(true);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(480);
    setMaximumWidth(560);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Search input
    search_edit_->setObjectName("palette_search");
    search_edit_->setPlaceholderText("Type a command...");
    search_edit_->setClearButtonEnabled(true);
    layout->addWidget(search_edit_);

    // Results list
    results_list_->setObjectName("palette_results");
    results_list_->setMinimumHeight(280);
    results_list_->setMaximumHeight(420);
    layout->addWidget(results_list_);

    connect(search_edit_, &QLineEdit::textChanged,
            this, &QtCommandPaletteDialog::on_search_changed);
    connect(results_list_, &QListWidget::itemActivated,
            this, &QtCommandPaletteDialog::on_item_activated);
    connect(results_list_, &QListWidget::currentItemChanged,
            this, &QtCommandPaletteDialog::on_item_selection_changed);
}

void QtCommandPaletteDialog::open_palette()
{
    search_edit_->clear();
    update_results("");
    search_edit_->setFocus();
    show();
    raise();
    activateWindow();
}

void QtCommandPaletteDialog::close_palette()
{
    search_edit_->clear();
    close();
}

void QtCommandPaletteDialog::on_search_changed(const QString& text)
{
    update_results(text.toStdString());
}

void QtCommandPaletteDialog::on_item_activated(QListWidgetItem* item)
{
    if (!item)
        return;
    execute_selected();
}

void QtCommandPaletteDialog::on_item_selection_changed()
{
    // Selection is driven by keyboard or mouse — no action needed here
}

void QtCommandPaletteDialog::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Escape:
            close_palette();
            return;

        case Qt::Key_Down:
            if (results_list_->count() > 0)
            {
                int row = results_list_->currentRow();
                row = std::min(row + 1, results_list_->count() - 1);
                results_list_->setCurrentRow(row);
            }
            return;

        case Qt::Key_Up:
            if (results_list_->count() > 0)
            {
                int row = results_list_->currentRow();
                row = std::max(row - 1, 0);
                results_list_->setCurrentRow(row);
            }
            return;

        case Qt::Key_Return:
        case Qt::Key_Enter:
            execute_selected();
            return;

        default:
            break;
    }
    QDialog::keyPressEvent(event);
}

void QtCommandPaletteDialog::update_results(const std::string& query)
{
    results_list_->clear();
    result_command_ids_.clear();

    std::vector<CommandSearchResult> results;

    if (query.empty())
    {
        // Show recent commands first, then all
        auto recent = registry_.recent_commands(5);
        auto all    = registry_.search("", 50);

        for (const auto* cmd : recent)
        {
            if (cmd)
            {
                CommandSearchResult r;
                r.command = cmd;
                r.score   = 1000;
                results.push_back(r);
            }
        }
        for (const auto& r : all)
        {
            bool is_recent = false;
            for (const auto* rc : recent)
            {
                if (rc && rc->id == r.command->id)
                {
                    is_recent = true;
                    break;
                }
            }
            if (!is_recent)
                results.push_back(r);
        }
    }
    else
    {
        results = registry_.search(query, 50);
    }

    const auto& colors = spectra_colors();

    std::string current_category;
    for (const auto& result : results)
    {
        if (!result.command)
            continue;

        // Category separator
        if (result.command->category != current_category)
        {
            current_category = result.command->category;
            auto* sep = new QListWidgetItem(results_list_);
            sep->setText(QString::fromStdString("── " + current_category + " ──"));
            sep->setFlags(sep->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
            QFont f = sep->font();
            f.setBold(true);
            f.setPointSize(f.pointSize() - 1);
            sep->setFont(f);
            sep->setBackground(QBrush(colors.input_surface));
            sep->setForeground(QBrush(colors.text_secondary));
        }

        // Command item
        QString label = QString::fromStdString(result.command->label);
        if (!result.command->shortcut.empty())
            label += "  [" + QString::fromStdString(result.command->shortcut) + "]";

        auto* item = new QListWidgetItem(label, results_list_);
        item->setToolTip(QString::fromStdString(result.command->id));
        if (!result.command->enabled)
        {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setForeground(QBrush(colors.text_muted));
        }

        result_command_ids_.push_back(result.command->id);
    }

    if (results_list_->count() > 0)
    {
        // Select first selectable item
        for (int i = 0; i < results_list_->count(); ++i)
        {
            if (results_list_->item(i)->flags() & Qt::ItemIsSelectable)
            {
                results_list_->setCurrentRow(i);
                break;
            }
        }
    }
}

void QtCommandPaletteDialog::execute_selected()
{
    int row = results_list_->currentRow();
    if (row < 0 || row >= static_cast<int>(result_command_ids_.size()))
        return;

    // Map list row to command id — need to account for category separators
    // result_command_ids_ only has entries for command items, not separators.
    // We need to count only selectable items up to current row.
    int cmd_index = 0;
    for (int i = 0; i <= row && i < results_list_->count(); ++i)
    {
        if (results_list_->item(i)->flags() & Qt::ItemIsSelectable)
        {
            if (i == row)
                break;
            ++cmd_index;
        }
    }

    if (cmd_index >= static_cast<int>(result_command_ids_.size()))
        return;

    std::string cmd_id = result_command_ids_[cmd_index];
    close_palette();
    registry_.execute(cmd_id);
}

}   // namespace spectra::adapters::qt
