#ifdef SPECTRA_USE_IMGUI

    #include "plot_overlay_dialog.hpp"

    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <cstring>

    #include "math/expression_eval.hpp"
    #include "ui/plot/plot_annotations.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui
{

void PlotOverlayDialog::open(Axes* axes, Mode mode)
{
    axes_  = axes;
    mode_  = mode;
    open_  = true;
    value_ = 0.0f;

    if (axes_)
    {
        const auto xl = axes_->x_limits();
        const auto yl = axes_->y_limits();
        xmin_         = static_cast<float>(xl.min);
        xmax_         = static_cast<float>(xl.max);
        if (mode_ == Mode::HorizontalLine)
            value_ = 0.0f;
        else if (mode_ == Mode::VerticalLine)
            value_ = 0.0f;
        else if (xmin_ == xmax_)
        {
            xmin_ = -1.0f;
            xmax_ = 1.0f;
        }
        (void)yl;
    }

    if (mode_ == Mode::Function)
    {
        std::strncpy(formula_, "x^2", sizeof(formula_) - 1);
        formula_[sizeof(formula_) - 1] = '\0';
        validate_function();
    }
}

void PlotOverlayDialog::close()
{
    open_ = false;
    axes_ = nullptr;
}

void PlotOverlayDialog::set_fonts(ImFont* body, ImFont* heading)
{
    font_body_    = body;
    font_heading_ = heading;
}

void PlotOverlayDialog::validate_function()
{
    validation_error_.clear();
    formula_valid_ = false;
    auto info      = parse_expression(formula_);
    if (!info.ast)
    {
        validation_error_ = info.error.empty() ? "Invalid expression" : info.error;
        return;
    }
    formula_valid_ = true;
}

void PlotOverlayDialog::apply()
{
    if (!axes_)
        return;

    switch (mode_)
    {
        case Mode::HorizontalLine:
            add_horizontal_reference_line(*axes_, value_);
            break;
        case Mode::VerticalLine:
            add_vertical_reference_line(*axes_, value_);
            break;
        case Mode::Function:
        {
            auto info = parse_expression(formula_);
            if (!info.ast)
                return;
            add_function_plot(*axes_, *info.ast, xmin_, xmax_, samples_, formula_);
            break;
        }
    }
    close();
}

void PlotOverlayDialog::draw()
{
    if (!open_ || !axes_)
        return;

    const char* title = "Plot Overlay";
    switch (mode_)
    {
        case Mode::HorizontalLine:
            title = "Add Horizontal Line";
            break;
        case Mode::VerticalLine:
            title = "Add Vertical Line";
            break;
        case Mode::Function:
            title = "Plot Function";
            break;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &open_, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    if (font_heading_)
        ImGui::PushFont(font_heading_);

    switch (mode_)
    {
        case Mode::HorizontalLine:
            ImGui::TextUnformatted("Draw a reference line at constant y.");
            ImGui::InputFloat("Y value", &value_, 0.1f, 1.0f, "%.4g");
            break;
        case Mode::VerticalLine:
            ImGui::TextUnformatted("Draw a reference line at constant x.");
            ImGui::InputFloat("X value", &value_, 0.1f, 1.0f, "%.4g");
            break;
        case Mode::Function:
            ImGui::TextUnformatted("Plot y = f(x) on the active axes.");
            if (ImGui::InputText("f(x)", formula_, sizeof(formula_)))
                validate_function();
            ImGui::InputFloat("X min", &xmin_, 0.1f, 1.0f, "%.4g");
            ImGui::InputFloat("X max", &xmax_, 0.1f, 1.0f, "%.4g");
            ImGui::InputInt("Samples", &samples_);
            if (samples_ < 2)
                samples_ = 2;
            if (!formula_valid_ && !validation_error_.empty())
                ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", validation_error_.c_str());
            ImGui::TextDisabled("Use variable x; e.g. x^2, sin(x), 2*x+1");
            break;
    }

    if (font_heading_)
        ImGui::PopFont();

    ImGui::Separator();
    const bool can_apply = mode_ != Mode::Function || formula_valid_;
    if (!can_apply)
        ImGui::BeginDisabled();
    if (ImGui::Button("Add", ImVec2(100, 0)))
        apply();
    if (!can_apply)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)))
        close();

    ImGui::End();

    if (!open_)
        axes_ = nullptr;
}

}   // namespace spectra::ui

#endif
