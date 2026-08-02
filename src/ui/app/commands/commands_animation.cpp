// commands_animation.cpp — Animation command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

namespace spectra
{

std::vector<CommandDescriptor> make_animation_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto& timeline_editor = ctx.ui_ctx.timeline_editor;

    cmds.push_back({"anim.toggle_play",
                    "Toggle Play/Pause",
                    "Space",
                    "Animation",
                    static_cast<uint16_t>(ui::Icon::Play),
                    [&]() { timeline_editor.toggle_play(); }});

    cmds.push_back({"anim.step_back",
                    "Step Frame Back",
                    "[",
                    "Animation",
                    static_cast<uint16_t>(ui::Icon::StepBackward),
                    [&]() { timeline_editor.step_backward(); }});

    cmds.push_back({"anim.step_forward",
                    "Step Frame Forward",
                    "]",
                    "Animation",
                    static_cast<uint16_t>(ui::Icon::StepForward),
                    [&]() { timeline_editor.step_forward(); }});

    cmds.push_back(
        {"anim.stop", "Stop Playback", "", "Animation", 0, [&]() { timeline_editor.stop(); }});

    cmds.push_back({"anim.go_to_start", "Go to Start", "", "Animation", 0, [&]() {
                        timeline_editor.set_playhead(0.0f);
                    }});

    cmds.push_back({"anim.go_to_end", "Go to End", "", "Animation", 0, [&]() {
                        timeline_editor.set_playhead(timeline_editor.duration());
                    }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
