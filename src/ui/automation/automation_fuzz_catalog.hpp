#pragma once

#include <array>
#include <cstdlib>
#include <random>
#include <string>

namespace spectra::automation
{

enum class FuzzAction
{
    ExecuteCommand,
    MouseClick,
    MouseDrag,
    MouseScroll,
    KeyPress,
    CreateFigure,
    CloseFigure,
    SwitchTab,
    AddSeries,
    UpdateData,
    LargeDataset,
    SplitDock,
    WaitFrames,
    WindowResize,
    WindowDrag,
    TabDetach,
};

struct FuzzActionWeight
{
    FuzzAction action;
    int        weight;
};

inline constexpr std::array<FuzzActionWeight, 16> kFuzzActionWeights = {
    {{FuzzAction::ExecuteCommand, 15},
     {FuzzAction::MouseClick, 15},
     {FuzzAction::MouseDrag, 10},
     {FuzzAction::MouseScroll, 10},
     {FuzzAction::KeyPress, 10},
     {FuzzAction::CreateFigure, 5},
     {FuzzAction::CloseFigure, 3},
     {FuzzAction::SwitchTab, 8},
     {FuzzAction::AddSeries, 8},
     {FuzzAction::UpdateData, 5},
     {FuzzAction::LargeDataset, 1},
     {FuzzAction::SplitDock, 3},
     {FuzzAction::WaitFrames, 7},
     {FuzzAction::WindowResize, 3},
     {FuzzAction::WindowDrag, 3},
     {FuzzAction::TabDetach, 2}}};

inline const char* fuzz_action_name(FuzzAction action)
{
    switch (action)
    {
        case FuzzAction::ExecuteCommand:
            return "ExecuteCommand";
        case FuzzAction::MouseClick:
            return "MouseClick";
        case FuzzAction::MouseDrag:
            return "MouseDrag";
        case FuzzAction::MouseScroll:
            return "MouseScroll";
        case FuzzAction::KeyPress:
            return "KeyPress";
        case FuzzAction::CreateFigure:
            return "CreateFigure";
        case FuzzAction::CloseFigure:
            return "CloseFigure";
        case FuzzAction::SwitchTab:
            return "SwitchTab";
        case FuzzAction::AddSeries:
            return "AddSeries";
        case FuzzAction::UpdateData:
            return "UpdateData";
        case FuzzAction::LargeDataset:
            return "LargeDataset";
        case FuzzAction::SplitDock:
            return "SplitDock";
        case FuzzAction::WaitFrames:
            return "WaitFrames";
        case FuzzAction::WindowResize:
            return "WindowResize";
        case FuzzAction::WindowDrag:
            return "WindowDrag";
        case FuzzAction::TabDetach:
            return "TabDetach";
    }
    return "Unknown";
}

inline bool parse_fuzz_action(const std::string& name, FuzzAction& result)
{
    for (const auto& entry : kFuzzActionWeights)
    {
        if (name == fuzz_action_name(entry.action))
        {
            result = entry.action;
            return true;
        }
    }
    return false;
}

inline FuzzAction pick_weighted_fuzz_action(std::mt19937& rng)
{
    const bool skip_drag = std::getenv("SPECTRA_FUZZ_SKIP_DRAG") != nullptr;
    int        total     = 0;
    for (const auto& entry : kFuzzActionWeights)
    {
        if (skip_drag
            && (entry.action == FuzzAction::TabDetach || entry.action == FuzzAction::WindowDrag))
            continue;
        total += entry.weight;
    }
    if (total <= 0)
        return FuzzAction::WaitFrames;

    std::uniform_int_distribution<int> distribution(0, total - 1);
    const int                          roll       = distribution(rng);
    int                                cumulative = 0;
    for (const auto& entry : kFuzzActionWeights)
    {
        if (skip_drag
            && (entry.action == FuzzAction::TabDetach || entry.action == FuzzAction::WindowDrag))
            continue;
        cumulative += entry.weight;
        if (roll < cumulative)
            return entry.action;
    }
    return FuzzAction::WaitFrames;
}

}   // namespace spectra::automation
