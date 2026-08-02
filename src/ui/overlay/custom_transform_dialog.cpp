#ifdef SPECTRA_USE_IMGUI

    #include "custom_transform_dialog.hpp"

    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/logger.hpp>
    #include <spectra/series.hpp>
    #include <algorithm>
    #include <cstring>

    #include "math/data_transform.hpp"
    #include "math/expression_eval.hpp"
    #include "ui/imgui/widgets.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui
{

void CustomTransformDialog::open(Figure* figure)
{
    figure_ = figure;
    open_   = true;
    std::memset(formula_buf_, 0, sizeof(formula_buf_));
    std::memset(name_buf_, 0, sizeof(name_buf_));
    validation_error_.clear();
    formula_valid_   = false;
    formula_changed_ = true;
    target_axes_     = 0;
    target_series_   = -1;
    apply_to_x_      = false;
    refresh_series_info();
}

void CustomTransformDialog::close()
{
    open_   = false;
    figure_ = nullptr;
}

void CustomTransformDialog::set_fonts(ImFont* body, ImFont* heading, ImFont* title)
{
    font_body_    = body;
    font_heading_ = heading;
    font_title_   = title;
}

void CustomTransformDialog::refresh_series_info()
{
    series_info_.clear();
    if (!figure_)
        return;

    int ai = 0;
    for (auto& ax : figure_->axes())
    {
        if (!ax)
        {
            ++ai;
            continue;
        }
        int si = 0;
        for (auto& sp : ax->series())
        {
            if (!sp)
            {
                ++si;
                continue;
            }
            SeriesInfo info;
            info.label      = sp->label().empty() ? ("Series " + std::to_string(si)) : sp->label();
            info.axes_index = ai;
            info.series_index = si;

            if (auto* ls = dynamic_cast<const LineSeries*>(sp.get()))
                info.point_count = ls->point_count();
            else if (auto* sc = dynamic_cast<const ScatterSeries*>(sp.get()))
                info.point_count = sc->point_count();
            else
                info.point_count = 0;

            series_info_.push_back(info);
            ++si;
        }
        ++ai;
    }
}

void CustomTransformDialog::draw()
{
    if (!open_ || !figure_)
        return;

    const auto& colors = ui::theme();

    ImGuiIO& io       = ImGui::GetIO();
    float    dialog_w = 620.0f;
    float    dialog_h = 600.0f;

    // ── Modal dim backdrop — focuses attention on the floating card ──
    {
        ImDrawList* bd = ImGui::GetBackgroundDrawList();
        bd->AddRectFilled(
            ImVec2(0.0f, 0.0f),
            io.DisplaySize,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.bg_overlay.r, colors.bg_overlay.g, colors.bg_overlay.b, 0.55f)));
    }

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dialog_w, dialog_h), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500.0f, 400.0f), ImVec2(900.0f, 860.0f));

    // Refined floating card: elevated surface, large radius, accent hairline.
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.28f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tokens::MODAL_RADIUS);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    bool             still_open = open_;
    ImGuiWindowFlags flags      = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##custom_xform", nullptr, flags))
    {
        ImVec2      win_min = ImGui::GetWindowPos();
        ImVec2      win_sz  = ImGui::GetWindowSize();
        ImDrawList* dl      = ImGui::GetWindowDrawList();

        // ── Integrated header strip ──
        float  header_h   = tokens::MODAL_HEADER_H;
        ImVec2 header_max = ImVec2(win_min.x + win_sz.x, win_min.y + header_h);
        dl->AddRectFilledMultiColor(
            win_min,
            header_max,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 1.0f)),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 1.0f)),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 1.0f)),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 1.0f)));
        // Accent top edge + bottom hairline ground the header.
        dl->AddRectFilled(win_min,
                          ImVec2(win_min.x + win_sz.x, win_min.y + 2.0f),
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.7f)),
                          tokens::MODAL_RADIUS,
                          ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(win_min.x, header_max.y),
                    ImVec2(header_max.x, header_max.y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_subtle.r,
                                                          colors.border_subtle.g,
                                                          colors.border_subtle.b,
                                                          0.6f)),
                    1.0f);

        // Title
        if (font_title_)
            ImGui::PushFont(font_title_);
        ImVec2 title_sz = ImGui::CalcTextSize("Add Custom Transformation");
        dl->AddText(
            ImVec2(win_min.x + tokens::MODAL_PADDING, win_min.y + (header_h - title_sz.y) * 0.5f),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 1.0f)),
            "Add Custom Transformation");
        if (font_title_)
            ImGui::PopFont();

        // Close button (top-right)
        {
            float  cb = 28.0f;
            ImVec2 cb_min(header_max.x - cb - tokens::SPACE_3, win_min.y + (header_h - cb) * 0.5f);
            ImGui::SetCursorScreenPos(cb_min);
            if (ImGui::InvisibleButton("##xform_close", ImVec2(cb, cb)))
                still_open = false;
            bool ch = ImGui::IsItemHovered();
            if (ch)
                dl->AddRectFilled(
                    cb_min,
                    ImVec2(cb_min.x + cb, cb_min.y + cb),
                    ImGui::ColorConvertFloat4ToU32(
                        ImVec4(colors.error.r, colors.error.g, colors.error.b, 0.20f)),
                    tokens::RADIUS_SM);
            ImVec4 xc = ch ? ImVec4(colors.error.r, colors.error.g, colors.error.b, 1.0f)
                           : ImVec4(colors.text_secondary.r,
                                    colors.text_secondary.g,
                                    colors.text_secondary.b,
                                    0.8f);
            ImVec2 xs = ImGui::CalcTextSize("\xC3\x97");
            dl->AddText(ImVec2(cb_min.x + (cb - xs.x) * 0.5f, cb_min.y + (cb - xs.y) * 0.5f),
                        ImGui::ColorConvertFloat4ToU32(xc),
                        "\xC3\x97");
        }

        // ── Scrollable padded body ──
        ImGui::SetCursorScreenPos(ImVec2(win_min.x, header_max.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(tokens::MODAL_PADDING, tokens::SPACE_4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(tokens::SPACE_2, tokens::SPACE_2));
        ImGui::BeginChild("##xform_body", ImVec2(0, 0), ImGuiChildFlags_None);

        draw_formula_input();
        ImGui::Spacing();
        draw_variable_reference();
        ImGui::Spacing();
        draw_function_reference();
        ImGui::Spacing();
        draw_preview();
        ImGui::Spacing();
        ImGui::Spacing();
        draw_action_buttons();

        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        // Preserve ESC-to-dismiss now that the native title bar is gone.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
            && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            still_open = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    if (!still_open)
        close();
}

void CustomTransformDialog::draw_formula_input()
{
    const auto& colors = ui::theme();

    // ── Name field ──
    if (font_heading_)
        ImGui::PushFont(font_heading_);
    ImGui::TextColored(
        ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 1.0f),
        "TRANSFORM NAME");
    if (font_heading_)
        ImGui::PopFont();

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##xform_name",
                             "e.g. My Custom Transform",
                             name_buf_,
                             sizeof(name_buf_));

    ImGui::Spacing();

    // ── Formula field ──
    if (font_heading_)
        ImGui::PushFont(font_heading_);
    ImGui::TextColored(
        ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 1.0f),
        "FORMULA  (computes new Y value for each point)");
    if (font_heading_)
        ImGui::PopFont();

    // Multi-line input with monospace hint
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextMultiline("##xform_formula",
                                  formula_buf_,
                                  sizeof(formula_buf_),
                                  ImVec2(-1, 60.0f)))
    {
        formula_changed_ = true;
    }

    // Quick-insert buttons for common patterns
    ImGui::TextColored(
        ImVec4(colors.text_tertiary.r, colors.text_tertiary.g, colors.text_tertiary.b, 1.0f),
        "Quick insert:");
    ImGui::SameLine();

    // auto insert_text = [this](const char* text)
    // {
    //     size_t len      = std::strlen(formula_buf_);
    //     size_t text_len = std::strlen(text);
    //     if (len + text_len < sizeof(formula_buf_) - 1)
    //     {
    //         std::strcat(formula_buf_, text);
    //         formula_changed_ = true;
    //     }
    // };

    // float button_h = ImGui::GetFrameHeight() * 0.85f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleColor(
        ImGuiCol_Button,
        ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f));

    const char* snippets[] = {"sin(x)",
                              "log10(y)",
                              "y^2",
                              "sqrt(y)",
                              "abs(y)",
                              "s0_y",
                              "y * s1_y",
                              "exp(-x)",
                              "y > 0 ? y : 0"};
    for (const char* snip : snippets)
    {
        if (ImGui::SmallButton(snip))
        {
            std::strncpy(formula_buf_, snip, sizeof(formula_buf_) - 1);
            formula_buf_[sizeof(formula_buf_) - 1] = '\0';
            formula_changed_                       = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // ── Target selection ──
    ImGui::Spacing();
    if (font_heading_)
        ImGui::PushFont(font_heading_);
    ImGui::TextColored(
        ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 1.0f),
        "APPLY TO");
    if (font_heading_)
        ImGui::PopFont();

    // Axes selector
    {
        int axes_count = static_cast<int>(figure_->axes().size());
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::BeginCombo(
                "Axes##xform_axes",
                target_axes_ < 0 ? "All Axes" : ("Axes " + std::to_string(target_axes_)).c_str()))
        {
            if (ImGui::Selectable("All Axes", target_axes_ < 0))
            {
                target_axes_ = -1;
                refresh_series_info();
            }
            for (int i = 0; i < axes_count; ++i)
            {
                bool sel = (target_axes_ == i);
                if (ImGui::Selectable(("Axes " + std::to_string(i)).c_str(), sel))
                {
                    target_axes_ = i;
                    refresh_series_info();
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SameLine();

    // Series selector
    {
        const char* series_label = "All Visible";
        if (target_series_ >= 0 && target_series_ < static_cast<int>(series_info_.size()))
        {
            series_label = series_info_[target_series_].label.c_str();
        }

        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("Series##xform_series", series_label))
        {
            if (ImGui::Selectable("All Visible", target_series_ < 0))
                target_series_ = -1;
            for (int i = 0; i < static_cast<int>(series_info_.size()); ++i)
            {
                bool sel = (target_series_ == i);
                if (ImGui::Selectable(series_info_[i].label.c_str(), sel))
                    target_series_ = i;
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SameLine();
    ImGui::Checkbox("Output to X", &apply_to_x_);

    // Validate on change
    if (formula_changed_)
    {
        validate_formula();
        formula_changed_ = false;
    }

    // Show validation result
    if (formula_buf_[0] != '\0')
    {
        if (formula_valid_)
        {
            ImGui::TextColored(ImVec4(colors.success.r, colors.success.g, colors.success.b, 1.0f),
                               "\xE2\x9C\x93 Valid expression");
        }
        else
        {
            ImGui::TextColored(ImVec4(colors.error.r, colors.error.g, colors.error.b, 1.0f),
                               "\xE2\x9C\x97 %s",
                               validation_error_.c_str());
        }
    }
}

void CustomTransformDialog::draw_variable_reference()
{
    const auto& colors = ui::theme();

    if (!ImGui::CollapsingHeader("Available Variables", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::Indent(tokens::SPACE_2);

    // Built-in variables
    ImGui::TextColored(ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f),
                       "Built-in:");

    ImGui::BulletText("x, t  — current X value (time)");
    ImGui::BulletText("y     — current Y value");
    ImGui::BulletText("i     — current point index (0-based)");
    ImGui::BulletText("n     — total number of points");
    ImGui::BulletText("pi    — 3.14159...");
    ImGui::BulletText("e     — 2.71828...");

    // Series variables
    if (!series_info_.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f),
                           "Series data:");

        for (size_t i = 0; i < series_info_.size(); ++i)
        {
            const auto& si = series_info_[i];
            ImGui::BulletText("s%zu_x, s%zu_y — %s (%zu pts)",
                              i,
                              i,
                              si.label.c_str(),
                              si.point_count);
        }
        ImGui::Spacing();
        ImGui::TextColored(
            ImVec4(colors.text_tertiary.r, colors.text_tertiary.g, colors.text_tertiary.b, 1.0f),
            "Tip: use s0_y * s1_y to multiply two series together");
    }

    ImGui::Unindent(tokens::SPACE_2);
}

void CustomTransformDialog::draw_function_reference()
{
    const auto& colors = ui::theme();

    if (!ImGui::CollapsingHeader("Available Functions"))
        return;

    ImGui::Indent(tokens::SPACE_2);

    struct FuncGroup
    {
        const char* name;
        const char* funcs;
    };

    const FuncGroup groups[] = {
        {"Trigonometric", "sin, cos, tan, asin, acos, atan, atan2(y,x)"},
        {"Hyperbolic", "sinh, cosh, tanh"},
        {"Exponential", "exp, log (ln), log2, log10"},
        {"Power", "sqrt, cbrt, pow(x,n), y^n"},
        {"Rounding", "abs, floor, ceil, round, sign"},
        {"Comparison", "min(a,b), max(a,b), clamp(v,lo,hi)"},
        {"Conversion", "deg (rad\xe2\x86\x92\xc2\xb0), rad (\xc2\xb0\xe2\x86\x92rad)"},
        {"Arithmetic", "+ - * / % (modulo) ^ (power)"},
        {"Conditional", "condition ? if_true : if_false"},
    };

    for (const auto& g : groups)
    {
        ImGui::TextColored(ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f),
                           "%s:",
                           g.name);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", g.funcs);
    }

    ImGui::Unindent(tokens::SPACE_2);
}

void CustomTransformDialog::draw_preview()
{
    const auto& colors = ui::theme();

    if (!formula_valid_ || formula_buf_[0] == '\0')
        return;

    if (!ImGui::CollapsingHeader("Preview (first 8 values)", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // Parse the formula
    ExpressionParser parser;
    auto             ast = parser.parse(formula_buf_);
    if (!ast)
        return;

    // Find a series to preview
    const LineSeries*    ls_preview = nullptr;
    const ScatterSeries* sc_preview = nullptr;

    if (figure_ && !figure_->axes().empty())
    {
        int ai = (target_axes_ >= 0) ? target_axes_ : 0;
        if (ai < static_cast<int>(figure_->axes().size()) && figure_->axes()[ai])
        {
            for (auto& sp : figure_->axes()[ai]->series())
            {
                if (!sp || !sp->visible())
                    continue;
                ls_preview = dynamic_cast<const LineSeries*>(sp.get());
                if (ls_preview)
                    break;
                sc_preview = dynamic_cast<const ScatterSeries*>(sp.get());
                if (sc_preview)
                    break;
            }
        }
    }

    std::span<const float> x_data;
    std::span<const float> y_data;
    if (ls_preview)
    {
        x_data = ls_preview->x_data();
        y_data = ls_preview->y_data();
    }
    else if (sc_preview)
    {
        x_data = sc_preview->x_data();
        y_data = sc_preview->y_data();
    }

    if (x_data.empty() || y_data.empty())
    {
        ImGui::TextColored(
            ImVec4(colors.text_tertiary.r, colors.text_tertiary.g, colors.text_tertiary.b, 1.0f),
            "No series data available for preview");
        return;
    }

    // Build evaluation context
    ExprContext ctx;
    ctx.n = std::min(x_data.size(), y_data.size());

    // Add all series as references
    if (figure_ && !figure_->axes().empty())
    {
        for (auto& ax : figure_->axes())
        {
            if (!ax)
                continue;
            for (auto& sp : ax->series())
            {
                if (!sp)
                    continue;
                ExprContext::SeriesRef ref;
                if (auto* l = dynamic_cast<const LineSeries*>(sp.get()))
                {
                    ref.x     = l->x_data();
                    ref.y     = l->y_data();
                    ref.label = l->label();
                }
                else if (auto* s = dynamic_cast<const ScatterSeries*>(sp.get()))
                {
                    ref.x     = s->x_data();
                    ref.y     = s->y_data();
                    ref.label = s->label();
                }
                ctx.series_data.push_back(ref);
            }
        }
    }

    // Show preview table
    if (ImGui::BeginTable("##preview_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("i", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("y (input)", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("result", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        size_t preview_n = std::min(ctx.n, size_t{8});
        for (size_t i = 0; i < preview_n; ++i)
        {
            ctx.x = x_data[i];
            ctx.y = y_data[i];
            ctx.t = x_data[i];
            ctx.i = i;

            float result = evaluate(*ast, ctx);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i);
            ImGui::TableNextColumn();
            ImGui::Text("%.4g", static_cast<double>(x_data[i]));
            ImGui::TableNextColumn();
            ImGui::Text("%.4g", static_cast<double>(y_data[i]));
            ImGui::TableNextColumn();

            if (std::isnan(result))
            {
                ImGui::TextColored(
                    ImVec4(colors.warning.r, colors.warning.g, colors.warning.b, 1.0f),
                    "NaN");
            }
            else if (std::isinf(result))
            {
                ImGui::TextColored(
                    ImVec4(colors.warning.r, colors.warning.g, colors.warning.b, 1.0f),
                    "%sInf",
                    result < 0 ? "-" : "+");
            }
            else
            {
                ImGui::TextColored(
                    ImVec4(colors.success.r, colors.success.g, colors.success.b, 1.0f),
                    "%.4g",
                    static_cast<double>(result));
            }
        }
        ImGui::EndTable();
    }
}

void CustomTransformDialog::draw_action_buttons()
{
    const auto& colors = ui::theme();

    float avail   = ImGui::GetContentRegionAvail().x;
    float btn_w   = 120.0f;
    float spacing = tokens::SPACE_2;

    // Right-align buttons
    float total_btns = btn_w * 3 + spacing * 2;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - total_btns);

    // Cancel
    if (ImGui::Button("Cancel", ImVec2(btn_w, 0)))
    {
        close();
        return;
    }

    ImGui::SameLine(0.0f, spacing);

    // Save as preset (registers in TransformRegistry)
    bool can_save = formula_valid_ && name_buf_[0] != '\0';
    if (!can_save)
        ImGui::BeginDisabled();
    if (ImGui::Button("Save Preset", ImVec2(btn_w, 0)))
    {
        // Register the transform in the registry
        std::string formula_str = formula_buf_;
        std::string xform_name  = name_buf_;

        TransformRegistry::instance().register_xy_transform(
            xform_name,
            [formula_str](std::span<const float> x_in,
                          std::span<const float> y_in,
                          std::vector<float>&    x_out,
                          std::vector<float>&    y_out)
            {
                ExpressionParser parser;
                auto             ast = parser.parse(formula_str);
                if (!ast)
                    return;

                const size_t n = std::min(x_in.size(), y_in.size());
                x_out.resize(n);
                y_out.resize(n);

                ExprContext ctx;
                ctx.n = n;

                for (size_t i = 0; i < n; ++i)
                {
                    ctx.x = x_in[i];
                    ctx.y = y_in[i];
                    ctx.t = x_in[i];
                    ctx.i = i;

                    float result = evaluate(*ast, ctx);
                    x_out[i]     = x_in[i];
                    y_out[i]     = result;
                }
            },
            "Custom: " + formula_str);

        SPECTRA_LOG_INFO("transform", "Saved custom transform preset: {}", xform_name);
    }
    if (!can_save)
        ImGui::EndDisabled();

    ImGui::SameLine(0.0f, spacing);

    // Apply
    if (!formula_valid_)
        ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(colors.accent_hover.r, colors.accent_hover.g, colors.accent_hover.b, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(colors.text_inverse.r, colors.text_inverse.g, colors.text_inverse.b, 1.0f));

    if (ImGui::Button("Apply", ImVec2(btn_w, 0)))
    {
        apply_transform();
    }

    ImGui::PopStyleColor(3);

    if (!formula_valid_)
        ImGui::EndDisabled();
}

void CustomTransformDialog::validate_formula()
{
    if (formula_buf_[0] == '\0')
    {
        formula_valid_ = false;
        validation_error_.clear();
        return;
    }

    auto info = parse_expression(formula_buf_);
    if (!info.ast)
    {
        formula_valid_    = false;
        validation_error_ = info.error;
        return;
    }

    formula_valid_ = true;
    validation_error_.clear();
}

void CustomTransformDialog::apply_transform()
{
    if (!figure_ || !formula_valid_)
        return;

    ExpressionParser parser;
    auto             ast = parser.parse(formula_buf_);
    if (!ast)
        return;

    // Build series references for the evaluation context
    std::vector<ExprContext::SeriesRef> all_series_refs;
    for (auto& ax : figure_->axes())
    {
        if (!ax)
            continue;
        for (auto& sp : ax->series())
        {
            if (!sp)
                continue;
            ExprContext::SeriesRef ref;
            if (auto* l = dynamic_cast<const LineSeries*>(sp.get()))
            {
                ref.x     = l->x_data();
                ref.y     = l->y_data();
                ref.label = l->label();
            }
            else if (auto* s = dynamic_cast<const ScatterSeries*>(sp.get()))
            {
                ref.x     = s->x_data();
                ref.y     = s->y_data();
                ref.label = s->label();
            }
            all_series_refs.push_back(ref);
        }
    }

    // Apply to target series
    int ax_start = (target_axes_ >= 0) ? target_axes_ : 0;
    int ax_end = (target_axes_ >= 0) ? target_axes_ + 1 : static_cast<int>(figure_->axes().size());

    int series_applied = 0;
    int global_si      = 0;

    for (int ai = ax_start; ai < ax_end; ++ai)
    {
        if (ai >= static_cast<int>(figure_->axes().size()) || !figure_->axes()[ai])
            continue;

        auto& ax = figure_->axes_mut()[ai];
        for (auto& sp : ax->series_mut())
        {
            if (!sp)
            {
                ++global_si;
                continue;
            }

            // Filter by target_series_ if set
            if (target_series_ >= 0 && global_si != target_series_)
            {
                ++global_si;
                continue;
            }

            // Skip invisible series when applying to all
            if (target_series_ < 0 && !sp->visible())
            {
                ++global_si;
                continue;
            }

            std::span<const float> x_data;
            std::span<const float> y_data;
            auto*                  ls = dynamic_cast<LineSeries*>(sp.get());
            auto*                  sc = dynamic_cast<ScatterSeries*>(sp.get());

            if (ls)
            {
                x_data = ls->x_data();
                y_data = ls->y_data();
            }
            else if (sc)
            {
                x_data = sc->x_data();
                y_data = sc->y_data();
            }
            else
            {
                ++global_si;
                continue;
            }

            const size_t n = std::min(x_data.size(), y_data.size());

            ExprContext ctx;
            ctx.n           = n;
            ctx.series_data = all_series_refs;

            std::vector<float> result_x(n);
            std::vector<float> result_y(n);

            for (size_t i = 0; i < n; ++i)
            {
                ctx.x = x_data[i];
                ctx.y = y_data[i];
                ctx.t = x_data[i];
                ctx.i = i;

                float result = evaluate(*ast, ctx);

                if (apply_to_x_)
                {
                    result_x[i] = result;
                    result_y[i] = y_data[i];
                }
                else
                {
                    result_x[i] = x_data[i];
                    result_y[i] = result;
                }
            }

            if (ls)
                ls->set_x(result_x).set_y(result_y);
            else if (sc)
                sc->set_x(result_x).set_y(result_y);

            ++series_applied;
            ++global_si;
        }
        ax->auto_fit();
    }

    std::string formula_desc = formula_buf_;
    SPECTRA_LOG_INFO("transform",
                     "Applied custom transform '{}' to {} series",
                     formula_desc,
                     series_applied);

    close();
}

}   // namespace spectra::ui

#endif
