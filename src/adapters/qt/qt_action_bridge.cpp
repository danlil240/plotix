// qt_action_bridge.cpp — Creates QActions from CommandRegistry entries.

#include "qt_action_bridge.hpp"

#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"

#include <spectra/logger.hpp>

#include <QString>

#include <algorithm>

namespace spectra::adapters::qt
{

QtActionBridge::QtActionBridge(CommandRegistry& registry, QObject* parent)
    : QObject(parent), registry_(registry)
{
}

void QtActionBridge::rebuild()
{
    actions_.clear();

    auto all = registry_.all_commands();
    for (const auto* cmd : all)
    {
        if (!cmd)
            continue;

        auto action = std::make_unique<QAction>(QString::fromStdString(cmd->label), this);
        action->setObjectName(QString::fromStdString("action_" + cmd->id));
        action->setEnabled(cmd->enabled);

        if (!cmd->shortcut.empty())
        {
            auto seq = shortcut_to_qt(cmd->shortcut);
            if (!seq.isEmpty())
                action->setShortcut(seq);
        }

        // Capture command_id by value for the lambda.
        std::string id = cmd->id;
        connect(action.get(), &QAction::triggered, this, [this, id]() { registry_.execute(id); });

        actions_[cmd->id] = std::move(action);
    }

    SPECTRA_LOG_INFO("qt_action_bridge",
                     "Rebuilt " + std::to_string(actions_.size()) + " QActions");
}

QAction* QtActionBridge::action_for(const std::string& command_id) const
{
    auto it = actions_.find(command_id);
    return it != actions_.end() ? it->second.get() : nullptr;
}

std::vector<QtActionBridge::CategoryActions> QtActionBridge::actions_by_category() const
{
    std::vector<CategoryActions>            result;
    std::unordered_map<std::string, size_t> cat_index;

    for (const auto& [id, action] : actions_)
    {
        const auto* cmd = registry_.find(id);
        if (!cmd)
            continue;

        auto [it, inserted] = cat_index.try_emplace(cmd->category, result.size());
        if (inserted)
        {
            result.push_back({cmd->category, {}});
        }
        result[it->second].actions.push_back(action.get());
    }

    for (auto& category : result)
    {
        std::sort(category.actions.begin(),
                  category.actions.end(),
                  [](const QAction* lhs, const QAction* rhs)
                  { return lhs->objectName() < rhs->objectName(); });
    }
    std::sort(result.begin(),
              result.end(),
              [](const CategoryActions& lhs, const CategoryActions& rhs)
              { return lhs.category < rhs.category; });

    return result;
}

std::vector<std::string> QtActionBridge::categories() const
{
    return registry_.categories();
}

void QtActionBridge::refresh()
{
    for (auto& [id, action] : actions_)
    {
        const auto* cmd = registry_.find(id);
        if (!cmd)
            continue;

        action->setText(QString::fromStdString(cmd->label));
        action->setEnabled(cmd->enabled);
    }
}

void QtActionBridge::sync_shortcuts(const ShortcutManager& shortcuts)
{
    std::unordered_map<std::string, QList<QKeySequence>> sequences;
    for (const auto& binding : shortcuts.all_bindings())
    {
        const QKeySequence sequence = shortcut_to_qt(binding.shortcut.to_string());
        if (!sequence.isEmpty())
            sequences[binding.command_id].push_back(sequence);
    }

    for (auto& [id, action] : actions_)
    {
        const auto it = sequences.find(id);
        action->setShortcuts(it == sequences.end() ? QList<QKeySequence>{} : it->second);
        registry_.set_shortcut(id,
                               it == sequences.end() || it->second.empty()
                                   ? std::string{}
                                   : it->second.front().toString().toStdString());
    }
}

QKeySequence QtActionBridge::shortcut_to_qt(const std::string& shortcut)
{
    // Spectra shortcuts use "+" as separator, same as QKeySequence.
    // Map common modifier names to Qt equivalents.
    // QKeySequence accepts "Ctrl+R", "Shift+Tab", etc. directly.
    return QKeySequence(QString::fromStdString(shortcut));
}

}   // namespace spectra::adapters::qt
