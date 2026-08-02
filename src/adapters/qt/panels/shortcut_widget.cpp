// shortcut_widget.cpp — Qt shortcut editor panel implementation.

#include "shortcut_widget.hpp"

#include "../qt_action_bridge.hpp"

#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

QtShortcutWidget::QtShortcutWidget(ShortcutManager* shortcuts,
                                   QtActionBridge*  action_bridge,
                                   QWidget*         parent)
    : QDockWidget("Shortcuts", parent), shortcuts_(shortcuts), action_bridge_(action_bridge)
{
    setObjectName("shortcut_panel");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget(this);
    setWidget(content);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ── Shortcut table ────────────────────────────────────────────────
    table_ = new QTableWidget(0, 2, content);
    table_->setObjectName("shortcut_table");
    table_->setHorizontalHeaderLabels({"Command", "Shortcut"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);

    // ── Status ────────────────────────────────────────────────────────
    status_label_ = new QLabel(content);
    status_label_->setStyleSheet("color: gray; padding: 4px;");
    layout->addWidget(status_label_);

    // ── Buttons ───────────────────────────────────────────────────────
    auto* btn_row = new QHBoxLayout();

    rebind_btn_ = new QPushButton("Rebind...", content);
    rebind_btn_->setObjectName("shortcut_rebind");
    rebind_btn_->setEnabled(false);
    btn_row->addWidget(rebind_btn_);

    reset_btn_ = new QPushButton("Reset to Defaults", content);
    reset_btn_->setObjectName("shortcut_reset");
    btn_row->addWidget(reset_btn_);

    layout->addLayout(btn_row);

    layout->addStretch();

    // ── Connections ───────────────────────────────────────────────────
    connect(rebind_btn_, &QPushButton::clicked, this, &QtShortcutWidget::on_rebind_clicked);
    connect(reset_btn_, &QPushButton::clicked, this, &QtShortcutWidget::on_reset_clicked);
    connect(table_,
            &QTableWidget::itemSelectionChanged,
            this,
            [this]() { rebind_btn_->setEnabled(table_->currentRow() >= 0); });

    refresh();
}

void QtShortcutWidget::refresh()
{
    if (!shortcuts_)
    {
        status_label_->setText("No shortcut manager");
        setEnabled(false);
        return;
    }
    setEnabled(true);

    auto bindings = shortcuts_->all_bindings();
    table_->setRowCount(static_cast<int>(bindings.size()));

    int row = 0;
    for (const auto& b : bindings)
    {
        if (shortcuts_->command_registry() && !shortcuts_->command_registry()->find(b.command_id))
            continue;
        auto* cmd_item = new QTableWidgetItem(QString::fromStdString(b.command_id));
        cmd_item->setFlags(cmd_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, cmd_item);

        auto* key_item = new QTableWidgetItem(QString::fromStdString(b.shortcut.to_string()));
        key_item->setFlags(key_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 1, key_item);

        ++row;
    }

    table_->setRowCount(row);

    status_label_->setText(QString("%1 shortcut(s) bound").arg(bindings.size()));
}

void QtShortcutWidget::on_rebind_clicked()
{
    int row = table_->currentRow();
    if (row < 0 || !shortcuts_)
        return;

    const QString command_id = table_->item(row, 0)->text();
    QDialog       dialog(this);
    dialog.setWindowTitle("Rebind Shortcut");
    auto* layout = new QFormLayout(&dialog);
    auto* edit   = new QKeySequenceEdit(QKeySequence(table_->item(row, 1)->text()), &dialog);
    edit->setObjectName("shortcut_key_capture");
    layout->addRow(command_id, edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    std::string error;
    if (!rebind_command(command_id.toStdString(), edit->keySequence(), &error))
    {
        status_label_->setText(QString::fromStdString(error));
        status_label_->setStyleSheet("color: #ef4444;");
    }
}

void QtShortcutWidget::on_reset_clicked()
{
    if (!shortcuts_)
        return;

    reset_to_defaults();
    status_label_->setText("Shortcuts reset to defaults");
    status_label_->setStyleSheet("color: green;");
}

bool QtShortcutWidget::rebind_command(const std::string&  command_id,
                                      const QKeySequence& sequence,
                                      std::string*        error)
{
    if (!shortcuts_ || command_id.empty())
        return false;
    const std::string text     = sequence.toString(QKeySequence::PortableText).toStdString();
    const Shortcut    shortcut = Shortcut::from_string(text);
    if (!shortcut.valid())
    {
        if (error)
            *error = "Choose a valid shortcut";
        return false;
    }

    const std::string displaced = shortcuts_->command_for_shortcut(shortcut);
    shortcuts_->unbind_command(command_id);
    shortcuts_->bind(shortcut, command_id);
    if (auto* registry = shortcuts_->command_registry())
    {
        registry->set_shortcut(command_id, text);
        if (!displaced.empty() && displaced != command_id)
            registry->set_shortcut(displaced, "");
    }
    if (action_bridge_)
        action_bridge_->sync_shortcuts(*shortcuts_);
    refresh();
    status_label_->setText(
        QString("%1 is now %2")
            .arg(QString::fromStdString(command_id), QString::fromStdString(text)));
    status_label_->setStyleSheet("color: #22c55e;");
    emit shortcuts_changed();
    return true;
}

void QtShortcutWidget::reset_to_defaults()
{
    if (!shortcuts_)
        return;
    shortcuts_->clear();
    shortcuts_->register_defaults();
    if (action_bridge_)
        action_bridge_->sync_shortcuts(*shortcuts_);
    refresh();
    emit shortcuts_changed();
}

}   // namespace spectra::adapters::qt
