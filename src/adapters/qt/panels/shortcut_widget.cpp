// shortcut_widget.cpp — Qt shortcut editor panel implementation.

#include "shortcut_widget.hpp"

#include "ui/commands/shortcut_manager.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

QtShortcutWidget::QtShortcutWidget(ShortcutManager* shortcuts, QWidget* parent)
    : QDockWidget("Shortcuts", parent), shortcuts_(shortcuts)
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
    connect(rebind_btn_, &QPushButton::clicked,
            this, &QtShortcutWidget::on_rebind_clicked);
    connect(reset_btn_, &QPushButton::clicked,
            this, &QtShortcutWidget::on_reset_clicked);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, [this]() {
                rebind_btn_->setEnabled(table_->currentRow() >= 0);
            });

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
        auto* cmd_item = new QTableWidgetItem(QString::fromStdString(b.command_id));
        cmd_item->setFlags(cmd_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, cmd_item);

        auto* key_item = new QTableWidgetItem(QString::fromStdString(b.shortcut.to_string()));
        key_item->setFlags(key_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 1, key_item);

        ++row;
    }

    status_label_->setText(QString("%1 shortcut(s) bound").arg(bindings.size()));
}

void QtShortcutWidget::on_rebind_clicked()
{
    int row = table_->currentRow();
    if (row < 0 || !shortcuts_)
        return;

    QString cmd_id = table_->item(row, 0)->text();
    QString old_shortcut = table_->item(row, 1)->text();

    // TODO: Implement a key-capture dialog for rebinding.
    // For now, show a placeholder message.
    status_label_->setText(QString("Rebinding for '%1' — key capture dialog not yet implemented")
                               .arg(cmd_id));
    status_label_->setStyleSheet("color: orange;");
}

void QtShortcutWidget::on_reset_clicked()
{
    if (!shortcuts_)
        return;

    shortcuts_->clear();
    shortcuts_->register_defaults();
    refresh();
    status_label_->setText("Shortcuts reset to defaults");
    status_label_->setStyleSheet("color: green;");
}

}   // namespace spectra::adapters::qt
