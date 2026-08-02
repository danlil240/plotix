#include "command_registry.hpp"

#include <algorithm>
#include <cctype>

#include "ui/native_dialog_policy.hpp"
#include "ui/ui_interaction_log.hpp"

namespace spectra
{

namespace
{

bool is_automation_side_effect_command(const std::string& id)
{
    static constexpr const char* kBlocked[] = {
        "help.show",
        "accessibility.sonify_series",
        "data.export_html_table",
    };
    for (const char* blocked : kBlocked)
    {
        if (id == blocked)
            return true;
    }
    return false;
}

}   // namespace

// ─── Registration ────────────────────────────────────────────────────────────

void CommandRegistry::register_command(Command cmd)
{
    std::lock_guard lock(mutex_);
    commands_[cmd.id] = std::move(cmd);
}

void CommandRegistry::register_command(const std::string&    id,
                                       const std::string&    label,
                                       std::function<void()> callback,
                                       const std::string&    shortcut,
                                       const std::string&    category,
                                       uint16_t              icon)
{
    Command cmd;
    cmd.id       = id;
    cmd.label    = label;
    cmd.callback = std::move(callback);
    cmd.shortcut = shortcut;
    cmd.category = category;
    cmd.icon     = icon;
    register_command(std::move(cmd));
}

void CommandRegistry::unregister_command(const std::string& id)
{
    std::lock_guard lock(mutex_);
    commands_.erase(id);
}

// ─── Execution ───────────────────────────────────────────────────────────────

bool CommandRegistry::execute(const std::string& id)
{
    std::function<void()> cb;
    {
        std::lock_guard lock(mutex_);
        auto            it = commands_.find(id);
        if (it == commands_.end() || !it->second.callback)
        {
            ui::log_ui_action("command", id, "miss", "not_found");
            return false;
        }
        if (!it->second.enabled)
        {
            ui::log_ui_action("command", id, "miss", "disabled");
            return false;
        }
        cb = it->second.callback;
    }
    if (!native_dialogs_enabled() && is_automation_side_effect_command(id))
    {
        ui::log_ui_action("command", id, "skipped", "automation");
        return true;
    }
    // Execute outside the lock to avoid deadlocks
    cb();
    ui::log_ui_action("command", id, "ok");
    record_execution(id);
    return true;
}

// ─── Search ──────────────────────────────────────────────────────────────────

int CommandRegistry::fuzzy_score(const std::string& query, const std::string& text)
{
    if (query.empty())
        return 1;
    if (text.empty())
        return 0;

    // Case-insensitive fuzzy matching
    // Scoring:
    //   - Exact substring match: high score
    //   - Prefix match: higher score
    //   - Character-by-character fuzzy: lower score
    //   - Consecutive matches: bonus
    //   - Word boundary matches: bonus

    std::string q_lower;
    std::string t_lower;
    q_lower.reserve(query.size());
    t_lower.reserve(text.size());
    for (char c : query)
        q_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char c : text)
        t_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Exact substring match
    auto pos = t_lower.find(q_lower);
    if (pos != std::string::npos)
    {
        int score = 100;
        if (pos == 0)
            score += 50;   // Prefix bonus
        if (q_lower.size() == t_lower.size())
            score += 25;   // Exact match bonus
        return score;
    }

    // Fuzzy character matching
    size_t qi                = 0;
    int    score             = 0;
    bool   prev_matched      = false;
    int    consecutive_bonus = 0;

    for (size_t ti = 0; ti < t_lower.size() && qi < q_lower.size(); ++ti)
    {
        if (t_lower[ti] == q_lower[qi])
        {
            score += 10;

            // Consecutive match bonus
            if (prev_matched)
            {
                consecutive_bonus += 5;
                score += consecutive_bonus;
            }
            else
            {
                consecutive_bonus = 0;
            }

            // Word boundary bonus (after space, underscore, dot, or uppercase transition)
            if (ti == 0 || text[ti - 1] == ' ' || text[ti - 1] == '_' || text[ti - 1] == '.'
                || (std::islower(static_cast<unsigned char>(text[ti - 1]))
                    && std::isupper(static_cast<unsigned char>(text[ti]))))
            {
                score += 15;
            }

            prev_matched = true;
            ++qi;
        }
        else
        {
            prev_matched      = false;
            consecutive_bonus = 0;
        }
    }

    // All query chars must match
    if (qi < q_lower.size())
        return 0;

    return score;
}

std::vector<CommandSearchResult> CommandRegistry::search(const std::string& query,
                                                         size_t             max_results) const
{
    std::lock_guard                  lock(mutex_);
    std::vector<CommandSearchResult> results;
    results.reserve(commands_.size());

    for (const auto& [id, cmd] : commands_)
    {
        // Score against label, id, and category
        int label_score = fuzzy_score(query, cmd.label);
        int id_score    = fuzzy_score(query, cmd.id);
        int cat_score   = fuzzy_score(query, cmd.category) / 2;   // Category match worth less

        int best = std::max({label_score, id_score, cat_score});
        if (best > 0)
        {
            results.push_back({&cmd, best});
        }
    }

    // Sort by score descending, then by category+label for stability
    std::sort(results.begin(),
              results.end(),
              [](const CommandSearchResult& a, const CommandSearchResult& b)
              {
                  if (a.score != b.score)
                      return a.score > b.score;
                  if (a.command->category != b.command->category)
                      return a.command->category < b.command->category;
                  return a.command->label < b.command->label;
              });

    if (results.size() > max_results)
    {
        results.resize(max_results);
    }
    return results;
}

// ─── Queries ─────────────────────────────────────────────────────────────────

const Command* CommandRegistry::find(const std::string& id) const
{
    std::lock_guard lock(mutex_);
    auto            it = commands_.find(id);
    return it != commands_.end() ? &it->second : nullptr;
}

std::vector<const Command*> CommandRegistry::all_commands() const
{
    std::lock_guard             lock(mutex_);
    std::vector<const Command*> result;
    result.reserve(commands_.size());
    for (const auto& [id, cmd] : commands_)
    {
        result.push_back(&cmd);
    }
    std::sort(result.begin(),
              result.end(),
              [](const Command* a, const Command* b)
              {
                  if (a->category != b->category)
                      return a->category < b->category;
                  return a->label < b->label;
              });
    return result;
}

std::vector<const Command*> CommandRegistry::commands_in_category(const std::string& category) const
{
    std::lock_guard             lock(mutex_);
    std::vector<const Command*> result;
    for (const auto& [id, cmd] : commands_)
    {
        if (cmd.category == category)
        {
            result.push_back(&cmd);
        }
    }
    std::sort(result.begin(),
              result.end(),
              [](const Command* a, const Command* b) { return a->label < b->label; });
    return result;
}

std::vector<std::string> CommandRegistry::categories() const
{
    std::lock_guard          lock(mutex_);
    std::vector<std::string> cats;
    for (const auto& [id, cmd] : commands_)
    {
        if (std::find(cats.begin(), cats.end(), cmd.category) == cats.end())
        {
            cats.push_back(cmd.category);
        }
    }
    std::sort(cats.begin(), cats.end());
    return cats;
}

size_t CommandRegistry::count() const
{
    std::lock_guard lock(mutex_);
    return commands_.size();
}

void CommandRegistry::set_enabled(const std::string& id, bool enabled)
{
    std::lock_guard lock(mutex_);
    auto            it = commands_.find(id);
    if (it != commands_.end())
    {
        it->second.enabled = enabled;
    }
}

void CommandRegistry::set_shortcut(const std::string& id, const std::string& shortcut)
{
    std::lock_guard lock(mutex_);
    auto            it = commands_.find(id);
    if (it != commands_.end())
        it->second.shortcut = shortcut;
}

// ─── Recent commands ─────────────────────────────────────────────────────────

void CommandRegistry::record_execution(const std::string& id)
{
    std::lock_guard lock(mutex_);
    // Remove existing entry if present
    std::erase(recent_ids_, id);
    // Insert at front
    recent_ids_.insert(recent_ids_.begin(), id);
    // Trim
    if (recent_ids_.size() > MAX_RECENT)
    {
        recent_ids_.resize(MAX_RECENT);
    }
}

std::vector<const Command*> CommandRegistry::recent_commands(size_t max_count) const
{
    std::lock_guard             lock(mutex_);
    std::vector<const Command*> result;
    for (const auto& id : recent_ids_)
    {
        if (result.size() >= max_count)
            break;
        auto it = commands_.find(id);
        if (it != commands_.end())
        {
            result.push_back(&it->second);
        }
    }
    return result;
}

void CommandRegistry::clear_recent()
{
    std::lock_guard lock(mutex_);
    recent_ids_.clear();
}

}   // namespace spectra
