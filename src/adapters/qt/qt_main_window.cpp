// qt_main_window.cpp — Production Qt main window for Spectra.

#include "qt_main_window.hpp"

#include "figure_canvas_widget.hpp"
#include "split_view_container.hpp"
#include "panels/command_palette_dialog.hpp"
#include "panels/data_editor_widget.hpp"
#include "panels/export_widget.hpp"
#include "panels/inspector_widget.hpp"
#include "panels/settings_widget.hpp"
#include "panels/shortcut_widget.hpp"
#include "panels/timeline_widget.hpp"
#include "panels/curve_editor_widget.hpp"
#include "panels/topics_widget.hpp"
#include "panels/transform_widget.hpp"
#include "panels/accessibility_widget.hpp"
#include "panels/plugin_panel_widget.hpp"
#include "panels/plugins_widget.hpp"
#include "qt_action_bridge.hpp"
#include "qt_runtime.hpp"

#include "components/spectra_design_tokens.hpp"
#include "components/spectra_title_bar.hpp"
#include "components/spectra_app_header.hpp"
#include "components/spectra_nav_rail.hpp"
#include "components/spectra_document_tab_bar.hpp"
#include "components/spectra_status_bar.hpp"
#include "components/spectra_inspector_drawer.hpp"
#include "components/spectra_canvas_frame.hpp"
#include "spectra_icon_embedded.hpp"

#include "app/application_services.hpp"
#include "io/export_registry.hpp"
#include "ui/animation/timeline_editor.hpp"
#include "ui/data/axis_link.hpp"
#include "math/data_transform.hpp"
#include "ui/automation/automation_json.hpp"
#include "ui/input/input.hpp"
#include "ui/settings/settings_store.hpp"
#include "ui/theme/theme.hpp"
#include "ui/workspace/workspace.hpp"

#include <spectra/export.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/frame.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QPoint>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QUrl>
#include <QSignalBlocker>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <sstream>
#include <utility>

namespace spectra::adapters::qt
{
namespace
{

QString automation_menu_label(QString label)
{
    label.remove('&');
    return label;
}

void append_automation_menu_actions(std::ostringstream&    result,
                                    const QList<QAction*>& actions,
                                    bool&                  first_item)
{
    for (const QAction* action : actions)
    {
        if (!action || !action->isVisible())
            continue;
        if (action->isSeparator())
        {
            if (!first_item)
                result << ',';
            first_item = false;
            result << R"({"separator":true})";
            continue;
        }
        if (action->menu())
        {
            append_automation_menu_actions(result, action->menu()->actions(), first_item);
            continue;
        }

        const QString label = automation_menu_label(action->text());
        if (label.isEmpty())
            continue;
        if (!first_item)
            result << ',';
        first_item = false;
        result << R"({"label":")" << json_escape(label.toStdString()) << R"(","enabled":)"
               << (action->isEnabled() ? "true" : "false") << R"(,"checkable":)"
               << (action->isCheckable() ? "true" : "false") << '}';
    }
}

struct ToolUiState
{
    int         nav_index;
    const char* name;
    const char* command_id;
};

ToolUiState tool_ui_state(ToolMode tool)
{
    switch (tool)
    {
        case ToolMode::Select:
            return {0, "Select", "tool.select"};
        case ToolMode::Pan:
            return {1, "Pan", "tool.pan"};
        case ToolMode::BoxZoom:
            return {2, "Zoom", "tool.box_zoom"};
        case ToolMode::Measure:
            return {3, "Measure", "tool.measure"};
        case ToolMode::Annotate:
            return {4, "Annotate", "tool.annotate"};
        case ToolMode::ROI:
            return {5, "ROI", "tool.roi"};
    }
    return {1, "Pan", "tool.pan"};
}

bool tool_mode_for_nav_index(int nav_index, ToolMode& tool)
{
    switch (nav_index)
    {
        case 0:
            tool = ToolMode::Select;
            return true;
        case 1:
            tool = ToolMode::Pan;
            return true;
        case 2:
            tool = ToolMode::BoxZoom;
            return true;
        case 3:
            tool = ToolMode::Measure;
            return true;
        case 4:
            tool = ToolMode::Annotate;
            return true;
        case 5:
            tool = ToolMode::ROI;
            return true;
        default:
            return false;
    }
}

QString css_color(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

QColor composite_color(const ui::Color& foreground, const ui::Color& background)
{
    const float alpha = std::clamp(foreground.a, 0.0f, 1.0f);
    return QColor::fromRgbF(foreground.r * alpha + background.r * (1.0f - alpha),
                            foreground.g * alpha + background.g * (1.0f - alpha),
                            foreground.b * alpha + background.b * (1.0f - alpha));
}

QString icon_image_url(uint32_t codepoint, const QColor& color, int size, const QString& name)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(color);
    painter.setFont(SpectraFontManager::instance().font_icon(qMax(size - 4, 10)));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, SpectraFontManager::icon_codepoint(codepoint));
    painter.end();

    // The name must stay free of '#': Qt's stylesheet url() loader treats the
    // string as a plain path, so a percent-encoded '#' from QUrl would make the
    // pixmap unresolvable and silently drop the indicator icon.
    const QString filename = QStringLiteral("spectra_%1_%2_%3x%3_%4.png")
                                 .arg(name)
                                 .arg(color.name(QColor::HexArgb).mid(1))
                                 .arg(size)
                                 .arg(QCoreApplication::applicationPid());
    const QString dir = QDir(QDir::temp()).filePath(QStringLiteral("spectra_theme_icons"));
    QDir().mkpath(dir);

    QString path = QDir(dir).absoluteFilePath(filename);
    if (!pixmap.save(path))
    {
        qWarning() << "Failed to write theme icon:" << path;
        return {};
    }
    // Qt resolves stylesheet url() through QPixmap, which expects a plain file
    // path. A "file://" URL silently fails to load and drops the indicator.
    return path;
}

double figure_zoom_level(const Figure& figure)
{
    if (figure.axes().empty() || !figure.axes()[0])
        return 1.0;

    const Axes& axes       = *figure.axes()[0];
    const auto  x_limits   = axes.x_limits();
    const auto  view_range = x_limits.max - x_limits.min;
    if (!(view_range > 0.0))
        return 1.0;

    double data_min = x_limits.max;
    double data_max = x_limits.min;
    for (const auto& series : axes.series())
    {
        if (!series)
            continue;
        std::span<const float> x_data;
        if (const auto* line = dynamic_cast<const LineSeries*>(series.get()))
            x_data = line->x_data();
        else if (const auto* scatter = dynamic_cast<const ScatterSeries*>(series.get()))
            x_data = scatter->x_data();
        if (x_data.empty())
            continue;
        const auto [minimum, maximum] = std::minmax_element(x_data.begin(), x_data.end());
        data_min                      = std::min(data_min, static_cast<double>(*minimum));
        data_max                      = std::max(data_max, static_cast<double>(*maximum));
    }

    const double data_range = data_max - data_min;
    return data_range > 0.0 ? data_range / view_range : 1.0;
}

}   // namespace

SpectraMainWindow::~SpectraMainWindow() = default;

SpectraMainWindow::SpectraMainWindow(QtRuntime*           runtime,
                                     FigureRegistry*      registry,
                                     QtActionBridge*      action_bridge,
                                     ApplicationServices* services,
                                     TimelineResolver     timeline_resolver,
                                     QWidget*             parent)
    : QMainWindow(parent), runtime_(runtime), registry_(registry), action_bridge_(action_bridge),
      services_(services), timeline_resolver_(std::move(timeline_resolver))
{
    setWindowTitle("Spectra");
    QPixmap app_icon;
    app_icon.loadFromData(SpectraIcon_png_data, SpectraIcon_png_size, "PNG");
    setWindowIcon(QIcon(app_icon));
    resize(1280, 720);

    // Load custom fonts before any UI is built
    load_fonts();

    // Load Spectra font manager (Inter + icon font)
    SpectraFontManager::instance().load_fonts();

    // Central split-view container for figure documents
    central_view_ = new QtSplitViewContainer(runtime_, registry_, this);
    central_view_->setObjectName("central_view");

    // Forward signals from the split view container
    connect(central_view_,
            &QtSplitViewContainer::figure_closed,
            this,
            [this](FigureId id)
            {
                if (doc_tab_bar_)
                    doc_tab_bar_->remove_tab(static_cast<int>(id));
                emit figure_closed(id);
            });
    connect(central_view_,
            &QtSplitViewContainer::figure_activated,
            this,
            [this](FigureId id)
            {
                if (doc_tab_bar_)
                    doc_tab_bar_->set_active_tab(static_cast<int>(id));
                emit figure_activated(id);
                sync_active_figure_panels(id);
                sync_active_tool_ui();
            });
    connect(central_view_,
            &QtSplitViewContainer::figure_detach_requested,
            this,
            &SpectraMainWindow::figure_detach_requested);
    connect(central_view_,
            &QtSplitViewContainer::figure_redock_requested,
            this,
            &SpectraMainWindow::figure_redock_requested);
    connect(central_view_,
            &QtSplitViewContainer::split_layout_changed,
            this,
            &SpectraMainWindow::workspace_state_changed);
    connect(central_view_,
            &QtSplitViewContainer::external_figure_drop_requested,
            this,
            &SpectraMainWindow::figure_move_to_pane_requested);
    connect(central_view_,
            &QtSplitViewContainer::canvas_created,
            this,
            [this](FigureId id, FigureCanvasWidget* canvas)
            {
                if (!canvas || !canvas->vulkanWindow())
                    return;
                auto* vw = canvas->vulkanWindow();
                connect(vw,
                        &SpectraVulkanWindow::persistentStateChanged,
                        this,
                        &SpectraMainWindow::workspace_state_changed);
                connect(vw,
                        &SpectraVulkanWindow::persistentStateChanged,
                        this,
                        [this, id]()
                        {
                            if (active_figure_id() == id)
                                sync_active_tool_ui();
                        });
                vw->setInspectorToggleCallbacks(
                    [this]()
                    {
                        if (spectra_inspector_)
                            spectra_inspector_->toggle();
                    },
                    [this]() { return spectra_inspector_ && spectra_inspector_->is_open(); });

                // Status belongs to the active canvas. Background tabs and
                // split panes can keep rendering without replacing it.
                if (spectra_status_)
                {
                    connect(vw,
                            &SpectraVulkanWindow::cursorMoved,
                            this,
                            [this, id](double x, double y, bool valid)
                            {
                                if (!spectra_status_ || active_figure_id() != id)
                                    return;
                                if (valid)
                                    spectra_status_->set_cursor_coords(x, y);
                                else
                                    spectra_status_->clear_cursor_coords();
                            });
                    connect(vw,
                            &SpectraVulkanWindow::frameStats,
                            this,
                            [this, id](int fps, double gpu_ms)
                            {
                                if (!spectra_status_ || active_figure_id() != id)
                                    return;
                                spectra_status_->set_fps(fps);
                                spectra_status_->set_gpu_frame_time(gpu_ms);
                                sync_active_zoom(id);
                            });
                }
                connect(vw,
                        &SpectraVulkanWindow::frameStats,
                        this,
                        [this, id](int, double)
                        {
                            if (active_figure_id() != id)
                                return;
                            if (inspector_panel_)
                                inspector_panel_->sync_from_model();
                            sync_document_title(id);
                        });
                TimelineEditor* timeline = timeline_resolver_ ? timeline_resolver_(id) : nullptr;
                canvas->setAnimationTick(
                    [this, id, timeline, last_playhead = std::numeric_limits<float>::quiet_NaN()](
                        float dt) mutable
                    {
                        Figure* figure = registry_ ? registry_->get(id) : nullptr;
                        if (!figure || !timeline)
                            return;

                        const PlaybackState state_before = timeline->playback_state();
                        if (state_before == PlaybackState::Playing)
                            timeline->advance(dt);

                        const float playhead = timeline->playhead();
                        if (state_before != PlaybackState::Playing && std::isfinite(last_playhead)
                            && std::abs(playhead - last_playhead) < 0.0001f)
                            return;

                        if (state_before != PlaybackState::Playing)
                            timeline->evaluate_at_playhead();

                        figure->anim_.fps      = timeline->fps();
                        figure->anim_.duration = timeline->duration();
                        figure->anim_.loop     = timeline->loop_mode() != LoopMode::None;
                        figure->anim_.time     = playhead;

                        if (figure->anim_.on_frame)
                        {
                            Frame frame;
                            frame.elapsed_sec = playhead;
                            frame.dt          = state_before == PlaybackState::Playing ? dt : 0.0f;
                            frame.number      = timeline->current_frame();
                            frame.paused = timeline->playback_state() != PlaybackState::Playing;
                            figure->anim_.on_frame(frame);
                        }
                        last_playhead = playhead;
                    });
            });

    // Build menus (still needed for QMenu popups used by custom menu strip)
    build_menus();
    build_panels();
    build_command_palette();

    // Build the new Spectra custom UI (title bar, header, nav rail, etc.)
    build_spectra_ui();

    refresh_theme();

    // Invalidate any saved dock state — start with clean default layout
    setDockNestingEnabled(true);

    // Show welcome page initially (no figures open)
    show_welcome_page();
}

// ── Figure tab management ─────────────────────────────────────────────────────

void SpectraMainWindow::sync_active_figure_panels(FigureId id)
{
    if (inspector_panel_)
        inspector_panel_->set_active_figure(id);
    if (export_panel_)
        export_panel_->set_active_figure(id);
    if (transform_panel_)
        transform_panel_->set_active_figure(id);
    if (data_editor_panel_)
        data_editor_panel_->set_active_figure(id);
    if (accessibility_panel_)
        accessibility_panel_->set_active_figure(id);
    if (timeline_panel_)
    {
        timeline_panel_->set_timeline(timeline_resolver_ ? timeline_resolver_(id) : nullptr);
        timeline_panel_->set_figure(registry_ ? registry_->get(id) : nullptr);
    }
    if (curve_editor_panel_)
        curve_editor_panel_->set_timeline(timeline_resolver_ ? timeline_resolver_(id) : nullptr);
    sync_document_title(id);
    sync_active_zoom(id);
}

void SpectraMainWindow::sync_document_title(FigureId id)
{
    Figure* figure = registry_ && id != INVALID_FIGURE_ID ? registry_->get(id) : nullptr;
    if (!figure)
        return;

    std::string title = figure->tab_title();
    if (title.empty())
        title = "Figure " + std::to_string(id);
    const QString visible_title = QString::fromStdString(title);
    if (doc_tab_bar_)
        doc_tab_bar_->set_tab_title(static_cast<int>(id), visible_title);
    if (central_view_)
        central_view_->set_figure_title(id, visible_title);
}

void SpectraMainWindow::sync_active_zoom(FigureId id)
{
    Figure* figure = registry_ && id != INVALID_FIGURE_ID ? registry_->get(id) : nullptr;
    if (spectra_status_)
        spectra_status_->set_zoom(figure ? figure_zoom_level(*figure) : 1.0);
}

int SpectraMainWindow::add_figure_tab(FigureId id)
{
    if (!central_view_)
        return -1;
    int idx = central_view_->add_figure_tab(id);
    if (idx >= 0)
    {
        hide_welcome_page();
        if (doc_tab_bar_ && registry_)
        {
            if (auto* figure = registry_->get(id))
            {
                std::string title = figure->tab_title();
                if (title.empty())
                    title = "Figure " + std::to_string(id);
                doc_tab_bar_->add_tab(QString::fromStdString(title), static_cast<int>(id));
                doc_tab_bar_->set_active_tab(static_cast<int>(id));
            }
        }
        // QTabWidget emits currentChanged while the first tab is still being
        // inserted, before the split container has recorded its FigureId.
        // Activate explicitly so every model-backed panel receives the first
        // document as well as later tab switches.
        central_view_->activate_figure(id);
        sync_active_tool_ui();
        SPECTRA_LOG_INFO("qt_main_window", "Added figure tab: id=" + std::to_string(id));
    }
    return idx;
}

bool SpectraMainWindow::close_figure_tab(FigureId id)
{
    if (!central_view_)
        return false;
    if (!central_view_->close_figure_tab(id))
        return false;

    if (central_view_->figure_tab_count() == 0)
        show_welcome_page();
    sync_active_figure_panels(active_figure_id());
    SPECTRA_LOG_INFO("qt_main_window", "Closed figure tab: id=" + std::to_string(id));
    return true;
}

bool SpectraMainWindow::release_figure_tab(FigureId id)
{
    if (!central_view_ || !central_view_->release_figure_tab(id))
        return false;

    if (doc_tab_bar_)
        doc_tab_bar_->remove_tab(static_cast<int>(id));
    if (central_view_->figure_tab_count() == 0)
        show_welcome_page();
    sync_active_figure_panels(active_figure_id());
    SPECTRA_LOG_INFO("qt_main_window", "Released figure tab: id=" + std::to_string(id));
    return true;
}

FigureId SpectraMainWindow::active_figure_id() const
{
    return central_view_ ? central_view_->active_figure_id() : INVALID_FIGURE_ID;
}

FigureCanvasWidget* SpectraMainWindow::canvas_for(FigureId id) const
{
    return central_view_ ? central_view_->canvas_for(id) : nullptr;
}

int SpectraMainWindow::figure_tab_count() const
{
    return central_view_ ? central_view_->figure_tab_count() : 0;
}

std::vector<FigureId> SpectraMainWindow::open_figure_ids() const
{
    return central_view_ ? central_view_->open_figure_ids() : std::vector<FigureId>{};
}

std::string SpectraMainWindow::serialize_split_layout() const
{
    return central_view_ ? central_view_->serialize_split_layout() : std::string{};
}

bool SpectraMainWindow::restore_split_layout(const std::string&                            layout,
                                             const std::unordered_map<FigureId, FigureId>& id_map)
{
    return central_view_ && central_view_->restore_split_layout(layout, id_map);
}

std::string SpectraMainWindow::automation_menu_state() const
{
    const std::array<std::pair<const char*, QMenu*>, 9> menus = {{
        {"File", menu_file_},
        {"Edit", menu_edit_},
        {"View", menu_view_},
        {"Tools", menu_tools_},
        {"Plot", menu_figure_},
        {"Data", menu_data_},
        {"Axes", menu_axes_},
        {"Transforms", menu_transforms_},
        {"Help", menu_help_},
    }};

    std::ostringstream result;
    result << R"({"menus":[)";
    bool first_menu = true;
    for (const auto& [name, menu] : menus)
    {
        if (!menu)
            continue;
        if ((menu == menu_axes_ || menu == menu_transforms_) && menu->actions().isEmpty())
            continue;
        if (!first_menu)
            result << ',';
        first_menu = false;
        result << R"({"name":")" << name << R"(","items":[)";
        bool first_item = true;
        append_automation_menu_actions(result, menu->actions(), first_item);
        result << "]}";
    }
    result << "]}";
    return result.str();
}

bool SpectraMainWindow::is_inspector_open() const
{
    return spectra_inspector_ && spectra_inspector_->is_open();
}

bool SpectraMainWindow::is_nav_rail_compact() const
{
    return nav_rail_ && nav_rail_->is_compact();
}

// ── Welcome page ──────────────────────────────────────────────────────────────

void SpectraMainWindow::show_welcome_page()
{
    if (central_view_)
        central_view_->show_welcome_page();
}

void SpectraMainWindow::hide_welcome_page()
{
    if (central_view_)
        central_view_->hide_welcome_page();
}

// ── Status bar ────────────────────────────────────────────────────────────────

void SpectraMainWindow::set_status(const std::string& message)
{
    if (status_label_)
        status_label_->setText(QString::fromStdString(message));
    if (spectra_status_)
        spectra_status_->set_message(QString::fromStdString(message));
}

void SpectraMainWindow::set_active_tool(ToolMode tool)
{
    const ToolMode before = central_view_ ? central_view_->active_tool() : ToolMode::Pan;
    if (central_view_)
        central_view_->set_active_tool(tool);
    sync_active_tool_ui();
    if (before != tool)
        emit workspace_state_changed();
}

void SpectraMainWindow::set_detached_host(bool detached)
{
    if (central_view_)
        central_view_->set_detached_host(detached);
}

void SpectraMainWindow::set_axis_link_manager(AxisLinkManager* manager)
{
    axis_link_manager_ = manager;
    if (central_view_)
        central_view_->set_axis_link_manager(manager);
}

bool SpectraMainWindow::autofit_active_axes()
{
    if (!central_view_)
        return false;
    InputHandler* input = central_view_->input_handler_for(active_figure_id());
    AxesBase*     axes  = input ? input->active_axes_base() : nullptr;
    if (auto* axes2d = dynamic_cast<Axes*>(axes))
        axes2d->auto_fit();
    else if (auto* axes3d = dynamic_cast<Axes3D*>(axes))
        axes3d->auto_fit();
    else
        return false;

    if (FigureCanvasWidget* canvas = canvas_for(active_figure_id()))
        if (canvas->vulkanWindow())
            canvas->vulkanWindow()->requestFrame();
    set_status("Auto-fit active axes");
    emit workspace_state_changed();
    return true;
}

void SpectraMainWindow::toggle_canvas_fullscreen()
{
    const bool show_chrome = !is_nav_rail_visible() && !is_inspector_open();
    set_nav_rail_visible(show_chrome);
    on_toggle_inspector(show_chrome);
    set_status(show_chrome ? "Canvas chrome restored" : "Canvas fullscreen");
}

bool SpectraMainWindow::link_active_axes(LinkAxis axis)
{
    Figure* figure = registry_ ? registry_->get(active_figure_id()) : nullptr;
    if (!axis_link_manager_ || !figure)
        return false;

    bool changed = false;
    if (axis != LinkAxis::Z)
    {
        std::vector<Axes*> axes2d;
        for (auto& axes : figure->axes_mut())
            if (axes)
                axes2d.push_back(axes.get());
        if (axes2d.size() >= 2)
        {
            const LinkAxis    axes2d_mode = axis == LinkAxis::All ? LinkAxis::Both : axis;
            const LinkGroupId group =
                axis_link_manager_->create_group(axes2d_mode == LinkAxis::X   ? "X Link"
                                                 : axes2d_mode == LinkAxis::Y ? "Y Link"
                                                                              : "XY Link",
                                                 axes2d_mode);
            for (Axes* axes : axes2d)
                axis_link_manager_->add_to_group(group, axes);
            changed = true;
        }
    }

    std::vector<Axes3D*> axes3d;
    for (auto& axes : figure->all_axes_mut())
        if (auto* candidate = dynamic_cast<Axes3D*>(axes.get()))
            axes3d.push_back(candidate);
    if (axes3d.size() >= 2)
    {
        for (size_t index = 1; index < axes3d.size(); ++index)
            axis_link_manager_->link_3d(axes3d.front(), axes3d[index], axis);
        changed = true;
    }

    if (changed)
    {
        emit workspace_state_changed();
        set_status("Linked active figure axes");
    }
    return changed;
}

bool SpectraMainWindow::unlink_all_axes()
{
    if (!axis_link_manager_)
        return false;
    const bool changed =
        axis_link_manager_->group_count() > 0 || axis_link_manager_->group_3d_count() > 0;
    axis_link_manager_->clear();
    if (changed)
    {
        emit workspace_state_changed();
        set_status("Unlinked all axes");
    }
    return changed;
}

bool SpectraMainWindow::toggle_active_crosshair()
{
    FigureCanvasWidget* canvas = canvas_for(active_figure_id());
    if (!canvas || !canvas->vulkanWindow())
        return false;
    canvas->vulkanWindow()->toggleCrosshair();
    return true;
}

bool SpectraMainWindow::clear_active_markers()
{
    FigureCanvasWidget* canvas = canvas_for(active_figure_id());
    if (!canvas || !canvas->vulkanWindow() || !canvas->vulkanWindow()->clearMarkers())
        return false;
    sync_active_tool_ui();
    set_status("Cleared data markers");
    return true;
}

void SpectraMainWindow::set_nav_rail_visible(bool visible)
{
    set_nav_rail_visible_internal(visible, /*notify=*/true);
}

void SpectraMainWindow::set_nav_rail_visible_internal(bool visible, bool notify)
{
    const bool changed = is_nav_rail_visible() != visible;
    if (!in_welcome_state_)
        nav_rail_user_pref_ = visible;
    if (nav_rail_)
        nav_rail_->setVisible(visible);
    if (auto* action =
            action_bridge_ ? action_bridge_->action_for("panel.toggle_nav_rail") : nullptr)
    {
        action->setCheckable(true);
        const QSignalBlocker blocker(action);
        action->setChecked(visible);
    }
    if (settings_panel_)
        settings_panel_->set_nav_rail_visible(visible);
    if (changed && notify)
        emit workspace_state_changed();
}

bool SpectraMainWindow::is_nav_rail_visible() const
{
    return nav_rail_ && !nav_rail_->isHidden();
}

void SpectraMainWindow::on_welcome_page_visible(bool visible)
{
    in_welcome_state_ = visible;

    // Welcome-state chrome transitions are derived from the document model, not
    // direct user workspace edits, so they must not mark persistence dirty.
    if (nav_rail_)
        set_nav_rail_visible_internal(nav_rail_user_pref_, /*notify=*/false);
    if (doc_tab_bar_)
        doc_tab_bar_->setVisible(!visible);
    if (spectra_status_)
        spectra_status_->setVisible(!visible);
    if (canvas_frame_)
        canvas_frame_->setVisible(true);
}

void SpectraMainWindow::sync_active_tool_ui()
{
    const ToolUiState state =
        tool_ui_state(central_view_ ? central_view_->active_tool() : ToolMode::Pan);
    if (nav_rail_)
    {
        nav_rail_->set_active_tool(state.nav_index);
        FigureCanvasWidget* canvas = canvas_for(active_figure_id());
        nav_rail_->set_button_active(
            6,
            canvas && canvas->vulkanWindow() && canvas->vulkanWindow()->markerCount() > 0);
    }
    if (spectra_status_)
        spectra_status_->set_active_tool(QString::fromUtf8(state.name));
}

void SpectraMainWindow::set_timeline_visible(bool visible)
{
    if (timeline_panel_)
        timeline_panel_->setHidden(!visible);
    if (auto* action =
            action_bridge_ ? action_bridge_->action_for("panel.toggle_timeline") : nullptr)
    {
        const QSignalBlocker blocker(action);
        action->setChecked(visible);
    }
    if (settings_panel_)
        settings_panel_->set_timeline_visible(visible);
}

// ── Private slots ─────────────────────────────────────────────────────────────

void SpectraMainWindow::on_tab_changed(int index)
{
    (void)index;
    // Tab changes are now handled by QtSplitViewContainer signals.
}

void SpectraMainWindow::on_tab_close_requested(int index)
{
    (void)index;
    // Tab close requests are now handled by QtSplitViewContainer internally.
}

// ── Private: build menus ──────────────────────────────────────────────────────

void SpectraMainWindow::build_menus()
{
    if (!action_bridge_)
        return;

    // Standard menus (used as popup QMenu objects for custom menu strip)
    menu_file_       = new QMenu("&File", this);
    menu_edit_       = new QMenu("&Edit", this);
    menu_view_       = new QMenu("&View", this);
    menu_tools_      = new QMenu("&Tools", this);
    menu_figure_     = new QMenu("&Plot", this);
    menu_data_       = new QMenu("&Data", this);
    menu_axes_       = new QMenu("&Axes", this);
    menu_transforms_ = new QMenu("&Transforms", this);
    menu_help_       = new QMenu("&Help", this);
    menu_file_->setObjectName("menu_file");
    menu_edit_->setObjectName("menu_edit");
    menu_view_->setObjectName("menu_view");
    menu_tools_->setObjectName("menu_tools");
    menu_figure_->setObjectName("menu_plot");
    menu_data_->setObjectName("menu_data");
    menu_axes_->setObjectName("menu_axes");
    menu_transforms_->setObjectName("menu_transforms");
    menu_help_->setObjectName("menu_help");

    // Submenus
    menu_view_panels_ = new QMenu("Panels", menu_view_);
    menu_view_panels_->setObjectName("menu_view_panels");
    menu_view_splits_ = new QMenu("Splits", menu_view_);
    menu_view_splits_->setObjectName("menu_view_splits");
    menu_tools_theme_ = new QMenu("Theme", menu_tools_);
    menu_tools_theme_->setObjectName("menu_tools_theme");
    menu_tools_anim_ = new QMenu("Animation", menu_tools_);
    menu_tools_anim_->setObjectName("menu_tools_anim");

    // Route each action by command ID prefix, not just category.
    // This fixes the gap where Figure lifecycle commands appeared under Plot,
    // Help was empty, and View was oversized with duplicated panel toggles.
    auto categorized = action_bridge_->actions_by_category();
    for (const auto& [category, actions] : categorized)
    {
        for (auto* action : actions)
        {
            // Extract command ID from objectName ("action_" prefix).
            const QString objName = action->objectName();
            const QString cmdId   = objName.mid(7);

            QMenu* target = nullptr;

            // ── Route by command ID prefix ───────────────────────────
            if (cmdId.startsWith("figure.new") || cmdId.startsWith("figure.close")
                || cmdId.startsWith("figure.next_tab") || cmdId.startsWith("figure.prev_tab")
                || cmdId.startsWith("figure.tab_") || cmdId.startsWith("figure.move_to_window"))
                target = menu_file_;
            else if (cmdId.startsWith("file."))
                target = menu_file_;
            else if (cmdId == "app.quit" || cmdId == "app.new_window"
                     || cmdId == "app.command_palette")
                target = menu_file_;
            else if (cmdId == "help.show")
                target = menu_help_;
            else if (cmdId.startsWith("edit."))
                target = menu_edit_;
            else if (cmdId.startsWith("series."))
                target = menu_edit_;
            else if (cmdId.startsWith("view.split_") || cmdId.startsWith("view.close_split")
                     || cmdId.startsWith("view.reset_splits")
                     || cmdId.startsWith("view.reset_layout"))
                target = menu_view_splits_;
            else if (cmdId.startsWith("view."))
                target = menu_view_;
            else if (cmdId.startsWith("panel."))
                target = menu_view_panels_;
            else if (cmdId.startsWith("tool."))
                target = menu_tools_;
            else if (cmdId.startsWith("theme."))
                target = menu_tools_theme_;
            else if (cmdId.startsWith("anim."))
                target = menu_tools_anim_;
            else if (cmdId.startsWith("accessibility."))
                target = menu_tools_;
            else if (cmdId.startsWith("plot."))
                target = menu_figure_;
            else if (cmdId.startsWith("data."))
                target = menu_data_;
            else if (category == "Axes")
                target = menu_axes_;
            else if (category == "Transforms")
                target = menu_transforms_;
            else
            {
                target = get_or_create_menu(category);
            }

            if (!target)
                continue;

            target->addAction(action);
            addAction(action);
        }
    }

    auto add_axis_action = [this](const char* object_name, const char* label, LinkAxis axis)
    {
        auto* action = menu_axes_->addAction(label);
        action->setObjectName(object_name);
        connect(action, &QAction::triggered, this, [this, axis]() { link_active_axes(axis); });
        return action;
    };
    QAction* link_x   = add_axis_action("axes_link_x", "Link X Axes", LinkAxis::X);
    QAction* link_y   = add_axis_action("axes_link_y", "Link Y Axes", LinkAxis::Y);
    QAction* link_z   = add_axis_action("axes_link_z", "Link Z Axes", LinkAxis::Z);
    QAction* link_all = add_axis_action("axes_link_all", "Link All Axes", LinkAxis::All);
    menu_axes_->addSeparator();
    QAction* unlink_all = menu_axes_->addAction("Unlink All");
    unlink_all->setObjectName("axes_unlink_all");
    connect(unlink_all, &QAction::triggered, this, [this]() { unlink_all_axes(); });
    connect(menu_axes_,
            &QMenu::aboutToShow,
            this,
            [this, link_x, link_y, link_z, link_all, unlink_all]()
            {
                Figure*      figure = registry_ ? registry_->get(active_figure_id()) : nullptr;
                const size_t axes2d = figure ? figure->axes().size() : 0;
                size_t       axes3d = 0;
                if (figure)
                    for (const auto& axes : figure->all_axes())
                        axes3d += dynamic_cast<Axes3D*>(axes.get()) ? 1 : 0;
                const bool xy_available = axis_link_manager_ && (axes2d >= 2 || axes3d >= 2);
                link_x->setEnabled(xy_available);
                link_y->setEnabled(xy_available);
                link_z->setEnabled(axis_link_manager_ && axes3d >= 2);
                link_all->setEnabled(xy_available);
                unlink_all->setEnabled(axis_link_manager_
                                       && (axis_link_manager_->group_count() > 0
                                           || axis_link_manager_->group_3d_count() > 0));
            });

    rebuild_transform_menu();
    connect(menu_transforms_,
            &QMenu::aboutToShow,
            this,
            &SpectraMainWindow::rebuild_transform_menu);

    // Add submenus to their parents (only if non-empty).
    if (!menu_tools_theme_->actions().isEmpty())
    {
        menu_tools_->addSeparator();
        menu_tools_->addMenu(menu_tools_theme_);
    }
    if (!menu_tools_anim_->actions().isEmpty())
    {
        menu_tools_->addSeparator();
        menu_tools_->addMenu(menu_tools_anim_);
    }
    if (!menu_view_panels_->actions().isEmpty())
    {
        menu_view_->addSeparator();
        menu_view_->addMenu(menu_view_panels_);
    }
    if (!menu_view_splits_->actions().isEmpty())
    {
        menu_view_->addSeparator();
        menu_view_->addMenu(menu_view_splits_);
    }
}

void SpectraMainWindow::rebuild_transform_menu()
{
    if (!menu_transforms_)
        return;
    menu_transforms_->clear();
    for (const auto& name : TransformRegistry::instance().available_transforms())
    {
        auto* action = menu_transforms_->addAction(QString::fromStdString(name));
        action->setObjectName(QString("transform_%1").arg(QString::fromStdString(name)));
        connect(action,
                &QAction::triggered,
                this,
                [this, name]()
                {
                    if (!transform_panel_)
                        return;
                    transform_panel_->show();
                    transform_panel_->raise();
                    transform_panel_->apply_named_transform(name);
                });
    }
    menu_transforms_->addSeparator();
    auto* formula = menu_transforms_->addAction("Custom Formula...");
    formula->setObjectName("transform_custom_formula");
    connect(formula,
            &QAction::triggered,
            this,
            [this]()
            {
                if (transform_panel_)
                    transform_panel_->focus_custom_formula();
            });
}

QMenu* SpectraMainWindow::get_or_create_menu(const std::string& path)
{
    // Check if it already exists in our menu list
    QString name = QString::fromStdString(path);
    // Not using menuBar() anymore — create standalone menu
    return new QMenu(name, this);
}

// ── Private: build toolbar ────────────────────────────────────────────────────

void SpectraMainWindow::build_toolbar()
{
    toolbar_ = addToolBar("Main");
    toolbar_->setObjectName("main_toolbar");
    toolbar_->setMovable(false);

    // Add key actions to toolbar
    if (action_bridge_)
    {
        // Add common file/view actions
        const std::vector<std::string> toolbar_cmds = {
            "file.new",
            "file.open",
            "file.save",
            "view.reset",
            "view.fit",
        };

        for (const auto& cmd_id : toolbar_cmds)
        {
            QAction* action = action_bridge_->action_for(cmd_id);
            if (action)
                toolbar_->addAction(action);
        }
    }
}

// ── Private: build status bar ─────────────────────────────────────────────────

void SpectraMainWindow::build_status_bar()
{
    // Old status bar replaced by SpectraStatusBar in build_spectra_ui()
    // Keep status_label_ for backward compat with set_status()
    status_label_ = new QLabel(this);
    status_label_->setObjectName("status_label");
}

// ── Private: build panels ────────────────────────────────────────────────────

void SpectraMainWindow::build_panels()
{
    if (!services_)
        return;

    // Inspector panel
    inspector_panel_ = new QtInspectorWidget(registry_, services_, this);
    inspector_panel_->setObjectName("inspector_dock");

    // Topics / data sources panel (hidden by default)
    topics_panel_ = new QtTopicsWidget(&services_->data_sources(), this);
    topics_panel_->setObjectName("topics_dock");
    addDockWidget(Qt::RightDockWidgetArea, topics_panel_);
    topics_panel_->hide();

    // Settings panel (hidden by default)
    settings_panel_ = new QtSettingsWidget(&services_->settings(), &services_->theme(), this);
    settings_panel_->setObjectName("settings_dock");
    addDockWidget(Qt::LeftDockWidgetArea, settings_panel_);
    settings_panel_->hide();
    connect(settings_panel_,
            &QtSettingsWidget::inspector_visibility_changed,
            this,
            &SpectraMainWindow::on_toggle_inspector);
    connect(settings_panel_,
            &QtSettingsWidget::nav_rail_visibility_changed,
            this,
            &SpectraMainWindow::set_nav_rail_visible);
    connect(settings_panel_,
            &QtSettingsWidget::timeline_visibility_changed,
            this,
            &SpectraMainWindow::set_timeline_visible);
    connect(settings_panel_,
            &QtSettingsWidget::settings_changed,
            this,
            &SpectraMainWindow::refresh_theme);

    // Timeline panel (hidden by default)
    timeline_panel_ =
        new QtTimelineWidget(timeline_resolver_ ? timeline_resolver_(active_figure_id()) : nullptr,
                             this);
    timeline_panel_->setObjectName("timeline_dock");
    timeline_panel_->set_undo_manager(&services_->undo());
    timeline_panel_->set_figure(registry_ ? registry_->get(active_figure_id()) : nullptr);
    addDockWidget(Qt::BottomDockWidgetArea, timeline_panel_);
    timeline_panel_->hide();
    connect(timeline_panel_,
            &QtTimelineWidget::timeline_changed,
            this,
            &SpectraMainWindow::workspace_state_changed);

    // Curve editor (hidden by default) shares the active figure's exact
    // TimelineEditor/KeyframeInterpolator with the Timeline panel.
    curve_editor_panel_ = new QtCurveEditorWidget(
        timeline_resolver_ ? timeline_resolver_(active_figure_id()) : nullptr,
        this);
    curve_editor_panel_->setObjectName("curve_editor_dock");
    curve_editor_panel_->set_undo_manager(&services_->undo());
    addDockWidget(Qt::BottomDockWidgetArea, curve_editor_panel_);
    curve_editor_panel_->hide();
    connect(curve_editor_panel_,
            &QtCurveEditorWidget::timeline_changed,
            this,
            [this]()
            {
                if (timeline_panel_)
                    timeline_panel_->refresh();
                emit workspace_state_changed();
            });

    // Export panel (hidden by default)
    export_panel_ = new QtExportWidget(&services_->export_formats(),
                                       registry_,
                                       services_->dialog_service(),
                                       this);
    export_panel_->set_export_callback(
        [this](FigureId           id,
               const std::string& format,
               const std::string& path,
               uint32_t           width,
               uint32_t           height)
        {
            FigureCanvasWidget*  canvas_widget = canvas_for(id);
            SpectraVulkanWindow* canvas = canvas_widget ? canvas_widget->vulkanWindow() : nullptr;
            Figure*              figure = registry_ ? registry_->get(id) : nullptr;
            if (!canvas || !figure)
                return false;
            if (format == "png_builtin")
                return canvas->savePng(path, width, height);

            std::vector<uint8_t> pixels;
            uint32_t             captured_width  = 0;
            uint32_t             captured_height = 0;
            if (!canvas->captureRgba(pixels, captured_width, captured_height, width, height))
                return false;

            OverlaySnapshot              overlay;
            const bool                   has_overlay = canvas->captureOverlaySnapshot(overlay);
            std::vector<OverlaySnapshot> overlays;
            if (has_overlay)
                overlays.push_back(overlay);
            const auto workspace = Workspace::capture({figure},
                                                      0,
                                                      services_->theme().current_theme_name(),
                                                      false,
                                                      0.0f,
                                                      false,
                                                      has_overlay ? &overlays : nullptr);
            return services_->export_formats().export_figure(format,
                                                             Workspace::serialize_json(workspace),
                                                             pixels.data(),
                                                             captured_width,
                                                             captured_height,
                                                             path);
        });
    export_panel_->setObjectName("export_dock");
    addDockWidget(Qt::RightDockWidgetArea, export_panel_);
    export_panel_->hide();

    // Shortcut editor panel (hidden by default)
    shortcut_panel_ = new QtShortcutWidget(&services_->shortcuts(), action_bridge_, this);
    shortcut_panel_->setObjectName("shortcut_dock");
    addDockWidget(Qt::LeftDockWidgetArea, shortcut_panel_);
    shortcut_panel_->hide();
    connect(shortcut_panel_,
            &QtShortcutWidget::shortcuts_changed,
            this,
            &SpectraMainWindow::workspace_state_changed);
    connect(settings_panel_,
            &QtSettingsWidget::settings_changed,
            this,
            &SpectraMainWindow::workspace_state_changed);

    // Transform panel (hidden by default)
    transform_panel_ =
        new QtTransformWidget(registry_, &services_->undo(), services_->redraw_request(), this);
    transform_panel_->setObjectName("transform_dock");
    addDockWidget(Qt::RightDockWidgetArea, transform_panel_);
    transform_panel_->hide();
    connect(transform_panel_,
            &QtTransformWidget::data_changed,
            this,
            &SpectraMainWindow::workspace_state_changed);

    // Data editor panel (hidden by default)
    data_editor_panel_ = new QtDataEditorWidget(registry_,
                                                &services_->undo(),
                                                services_->redraw_request(),
                                                services_->clipboard_service(),
                                                services_->dialog_service(),
                                                this);
    data_editor_panel_->setObjectName("data_editor_dock");
    addDockWidget(Qt::RightDockWidgetArea, data_editor_panel_);
    data_editor_panel_->hide();
    connect(data_editor_panel_,
            &QtDataEditorWidget::data_changed,
            this,
            &SpectraMainWindow::workspace_state_changed);

    // Accessibility panel (hidden by default)
    accessibility_panel_ = new QtAccessibilityWidget(registry_, services_->dialog_service(), this);
    accessibility_panel_->setObjectName("accessibility_dock");
    addDockWidget(Qt::LeftDockWidgetArea, accessibility_panel_);
    accessibility_panel_->hide();

    // Plugin UI panel (hidden by default)
    plugin_panel_ = new QtPluginPanelWidget(&services_->plugin_ui(), this);
    plugin_panel_->setObjectName("plugin_panel_dock");
    addDockWidget(Qt::RightDockWidgetArea, plugin_panel_);
    plugin_panel_->hide();

    // Plugins management panel (hidden by default)
    plugins_panel_ = new QtPluginsWidget(&services_->plugins(),
                                         &services_->plugin_ui(),
                                         services_->dialog_service(),
                                         this);
    plugins_panel_->setObjectName("plugins_mgmt_dock");
    addDockWidget(Qt::LeftDockWidgetArea, plugins_panel_);
    plugins_panel_->hide();

    // Native docking changes are workspace mutations even when they happen by
    // direct pointer interaction instead of a registered panel command.
    for (QDockWidget* dock : findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        connect(dock,
                &QDockWidget::visibilityChanged,
                this,
                &SpectraMainWindow::workspace_state_changed);
        connect(dock,
                &QDockWidget::topLevelChanged,
                this,
                &SpectraMainWindow::workspace_state_changed);
        connect(dock,
                &QDockWidget::dockLocationChanged,
                this,
                &SpectraMainWindow::workspace_state_changed);
    }

    // Menu entries use the same QAction instances as the command palette,
    // shortcuts, and automation. Keep their check state synchronized with
    // the authoritative panel surface without adding a second local action.
    auto bind_panel_action = [this](const char* command_id, QDockWidget* panel)
    {
        QAction* action = action_bridge_ ? action_bridge_->action_for(command_id) : nullptr;
        if (!action || !panel)
            return;
        action->setCheckable(true);
        {
            const QSignalBlocker blocker(action);
            action->setChecked(panel->isVisible());
        }
        connect(panel,
                &QDockWidget::visibilityChanged,
                action,
                [action](bool visible)
                {
                    const QSignalBlocker blocker(action);
                    action->setChecked(visible);
                });
    };

    inspector_toggle_action_ =
        action_bridge_ ? action_bridge_->action_for("panel.toggle_inspector") : nullptr;
    if (inspector_toggle_action_)
    {
        inspector_toggle_action_->setCheckable(true);
        inspector_toggle_action_->setChecked(false);
    }
    bind_panel_action("panel.toggle_topics", topics_panel_);
    bind_panel_action("panel.open_settings", settings_panel_);
    bind_panel_action("panel.toggle_timeline", timeline_panel_);
    bind_panel_action("panel.toggle_curve_editor", curve_editor_panel_);
    bind_panel_action("panel.toggle_data_editor", data_editor_panel_);
    bind_panel_action("panel.toggle_plugins", plugins_panel_);
}

// ── Private: build command palette ──────────────────────────────────────────

void SpectraMainWindow::build_command_palette()
{
    if (!services_)
        return;

    command_palette_ = new QtCommandPaletteDialog(services_->commands(), this);
    command_palette_->setObjectName("command_palette");

    // Ctrl+K shortcut to toggle the palette
    palette_shortcut_ = new QShortcut(QKeySequence("Ctrl+K"), this);
    palette_shortcut_->setObjectName("palette_shortcut");
    connect(palette_shortcut_,
            &QShortcut::activated,
            this,
            &SpectraMainWindow::open_command_palette);

    // Add command palette action to the File menu
    if (menu_file_)
    {
        menu_file_->addSeparator();
        auto* palette_action = menu_file_->addAction("Command Palette...");
        palette_action->setObjectName("file_command_palette");
        palette_action->setShortcut(QKeySequence("Ctrl+K"));
        connect(palette_action,
                &QAction::triggered,
                this,
                &SpectraMainWindow::open_command_palette);
    }
}

void SpectraMainWindow::open_command_palette()
{
    if (command_palette_)
        command_palette_->open_palette();
}

bool SpectraMainWindow::cancel_transient_ui()
{
    if (QWidget* popup = QApplication::activePopupWidget())
    {
        popup->close();
        return true;
    }

    if (command_palette_ && command_palette_->isVisible())
    {
        command_palette_->close_palette();
        return true;
    }

    const auto dialogs = findChildren<QDialog*>();
    for (auto it = dialogs.crbegin(); it != dialogs.crend(); ++it)
    {
        if (*it && (*it)->isVisible())
        {
            (*it)->reject();
            return true;
        }
    }
    return false;
}

// ── Panel toggle slots ──────────────────────────────────────────────────────

void SpectraMainWindow::toggle_inspector()
{
    if (spectra_inspector_)
        on_toggle_inspector(!spectra_inspector_->is_open());
}

void SpectraMainWindow::on_toggle_inspector(bool checked)
{
    if (spectra_inspector_)
    {
        if (checked)
            spectra_inspector_->open();
        else
            spectra_inspector_->close();
    }
    if (inspector_toggle_action_)
    {
        const QSignalBlocker blocker(inspector_toggle_action_);
        inspector_toggle_action_->setChecked(checked);
    }
    if (settings_panel_)
        settings_panel_->set_inspector_visible(checked);
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_topics()
{
    if (topics_panel_)
        topics_panel_->setHidden(!topics_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_settings()
{
    if (settings_panel_)
        settings_panel_->setHidden(!settings_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_timeline()
{
    if (timeline_panel_)
        set_timeline_visible(timeline_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_curve_editor()
{
    if (curve_editor_panel_)
        curve_editor_panel_->setHidden(!curve_editor_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_export()
{
    if (export_panel_)
        export_panel_->setHidden(!export_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_shortcuts()
{
    if (shortcut_panel_)
        shortcut_panel_->setHidden(!shortcut_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_transform()
{
    if (transform_panel_)
        transform_panel_->setHidden(!transform_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_data_editor()
{
    if (data_editor_panel_)
        data_editor_panel_->setHidden(!data_editor_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_accessibility()
{
    if (accessibility_panel_)
        accessibility_panel_->setHidden(!accessibility_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_plugin_panel()
{
    if (plugin_panel_)
        plugin_panel_->setHidden(!plugin_panel_->isHidden());
    emit workspace_state_changed();
}

void SpectraMainWindow::on_toggle_plugins()
{
    if (plugins_panel_)
        plugins_panel_->setHidden(!plugins_panel_->isHidden());
    emit workspace_state_changed();
}

// ── Split view ────────────────────────────────────────────────────────────────

bool SpectraMainWindow::split_right()
{
    const bool changed = central_view_ && central_view_->split_right();
    if (changed)
        emit workspace_state_changed();
    return changed;
}

bool SpectraMainWindow::split_down()
{
    const bool changed = central_view_ && central_view_->split_down();
    if (changed)
        emit workspace_state_changed();
    return changed;
}

bool SpectraMainWindow::close_split()
{
    const bool changed = central_view_ && central_view_->close_split();
    if (changed)
        emit workspace_state_changed();
    return changed;
}

void SpectraMainWindow::reset_splits()
{
    if (central_view_)
        central_view_->reset_splits();
    emit workspace_state_changed();
}

bool SpectraMainWindow::is_split() const
{
    return central_view_ ? central_view_->is_split() : false;
}

size_t SpectraMainWindow::pane_count() const
{
    return central_view_ ? central_view_->pane_count() : 0;
}

void SpectraMainWindow::on_split_right()
{
    split_right();
}

void SpectraMainWindow::on_split_down()
{
    split_down();
}

void SpectraMainWindow::on_close_split()
{
    close_split();
}

void SpectraMainWindow::on_reset_splits()
{
    reset_splits();
}

// ── Tab context menu (detach support) ──────────────────────────────────────

void SpectraMainWindow::on_tab_context_menu(const QPoint& pos)
{
    (void)pos;
    // Tab context menu is now handled by QtSplitViewContainer internally.
}

void SpectraMainWindow::on_detach_tab()
{
    // Detach is now handled by QtSplitViewContainer's own context menu.
}

// ── Layout reset ───────────────────────────────────────────────────────────────

void SpectraMainWindow::on_reset_layout()
{
    reset_layout();
}

void SpectraMainWindow::reset_layout()
{
    // Hide all docks except Inspector
    if (settings_panel_)
        settings_panel_->hide();
    if (shortcut_panel_)
        shortcut_panel_->hide();
    if (accessibility_panel_)
        accessibility_panel_->hide();
    if (plugins_panel_)
        plugins_panel_->hide();
    if (topics_panel_)
        topics_panel_->hide();
    if (export_panel_)
        export_panel_->hide();
    if (transform_panel_)
        transform_panel_->hide();
    if (data_editor_panel_)
        data_editor_panel_->hide();
    if (plugin_panel_)
        plugin_panel_->hide();
    if (timeline_panel_)
        timeline_panel_->hide();
    if (curve_editor_panel_)
        curve_editor_panel_->hide();

    // Show inspector (hidden by default per Vision.png)
    if (spectra_inspector_)
        spectra_inspector_->close();

    // Reset splits to single pane
    reset_splits();

    // Reset all panel toggle check states to match the hidden state
    if (menu_view_panels_)
    {
        for (auto* action : menu_view_panels_->actions())
        {
            if (action->isCheckable() && action != inspector_toggle_action_)
            {
                QSignalBlocker blocker(action);
                action->setChecked(false);
            }
        }
    }
    if (inspector_toggle_action_)
    {
        QSignalBlocker blocker(inspector_toggle_action_);
        inspector_toggle_action_->setChecked(false);
    }

    // Restore the legacy desktop client size.
    resize(1280, 720);
    emit workspace_state_changed();
}

// ── Build Spectra custom UI ───────────────────────────────────────────────────

void SpectraMainWindow::build_spectra_ui()
{
    // Hide the default Qt menu bar — we use SpectraAppHeader with SpectraMenuStrip
    menuBar()->setVisible(false);

    // Create custom title bar
    title_bar_ = new SpectraTitleBar(this);
    title_bar_->setObjectName("spectra_title_bar");
    title_bar_->set_title("Spectra");
    title_bar_->set_window(this);

    // Create app header with logo, wordmark, menus, and glow
    app_header_ = new SpectraAppHeader(this);
    app_header_->setObjectName("spectra_app_header");

    // Populate menu strip with the QMenu objects from build_menus()
    if (menu_file_)
        app_header_->add_menu("File", menu_file_);
    if (menu_edit_)
        app_header_->add_menu("Edit", menu_edit_);
    if (menu_view_)
        app_header_->add_menu("View", menu_view_);
    if (menu_tools_)
        app_header_->add_menu("Tools", menu_tools_);
    if (menu_figure_)
        app_header_->add_menu("Plot", menu_figure_);
    if (menu_data_)
        app_header_->add_menu("Data", menu_data_);
    if (menu_axes_ && !menu_axes_->actions().isEmpty())
        app_header_->add_menu("Axes", menu_axes_);
    if (menu_transforms_ && !menu_transforms_->actions().isEmpty())
        app_header_->add_menu("Transforms", menu_transforms_);
    if (menu_help_)
        app_header_->add_menu("Help", menu_help_);

    // Wire Home button to view.home command
    connect(app_header_,
            &SpectraAppHeader::home_clicked,
            this,
            [this]()
            {
                if (action_bridge_)
                {
                    if (auto* action = action_bridge_->action_for("view.home"))
                    {
                        action->trigger();
                        return;
                    }
                }
                // Fallback: auto-fit active figure
                if (central_view_)
                    central_view_->activate_figure(central_view_->active_figure_id());
            });

    // Create navigation rail
    nav_rail_ = new SpectraNavRail(this);
    nav_rail_->setObjectName("spectra_nav_rail");
    if (auto* action =
            action_bridge_ ? action_bridge_->action_for("panel.toggle_nav_rail") : nullptr)
    {
        action->setCheckable(true);
        action->setChecked(true);
    }
    // Hide buttons that have no panel or command wired yet
    nav_rail_->set_button_visible(6, true);     // Markers
    nav_rail_->set_button_visible(10, true);    // Curve Editor
    nav_rail_->set_button_visible(14, false);   // Help (accessible via menu)
    connect(
        nav_rail_,
        &SpectraNavRail::tool_selected,
        this,
        [this](int tool)
        {
            ToolMode requested_tool = ToolMode::Pan;
            if (tool_mode_for_nav_index(tool, requested_tool))
            {
                const ToolUiState requested_state = tool_ui_state(requested_tool);
                if (action_bridge_)
                {
                    if (auto* action = action_bridge_->action_for(requested_state.command_id))
                    {
                        action->trigger();
                        sync_active_tool_ui();
                        return;
                    }
                }
                set_active_tool(requested_tool);
                return;
            }

            auto trigger_panel_command = [this](const char* command_id)
            {
                QAction* action = action_bridge_ ? action_bridge_->action_for(command_id) : nullptr;
                if (!action)
                    return false;
                action->trigger();
                return true;
            };
            switch (tool)
            {
                case 6:
                    clear_active_markers();
                    break;
                case 7:
                    if (transform_panel_)
                        transform_panel_->show();
                    break;
                case 8:
                    if (!trigger_panel_command("panel.toggle_inspector") && spectra_inspector_)
                        spectra_inspector_->toggle();
                    break;
                case 9:
                    if (!trigger_panel_command("panel.toggle_timeline") && timeline_panel_)
                        timeline_panel_->show();
                    break;
                case 10:
                    if (!trigger_panel_command("panel.toggle_curve_editor") && curve_editor_panel_)
                        curve_editor_panel_->show();
                    break;
                case 11:
                    if (!trigger_panel_command("panel.toggle_plugins") && plugins_panel_)
                        plugins_panel_->show();
                    break;
                case 12:
                    if (!trigger_panel_command("panel.toggle_topics") && topics_panel_)
                        topics_panel_->show();
                    break;
                case 13:
                    if (!trigger_panel_command("panel.open_settings") && settings_panel_)
                        settings_panel_->show();
                    break;
                default:
                    break;
            }
        });

    // Create document tab bar
    doc_tab_bar_ = new SpectraDocumentTabBar(this);
    doc_tab_bar_->setObjectName("spectra_doc_tab_bar");
    connect(doc_tab_bar_,
            &SpectraDocumentTabBar::tab_add_requested,
            this,
            [this]()
            {
                // Keep the visible add control on the same command path as
                // Ctrl+T, menus, the command palette, and automation.
                if (action_bridge_)
                {
                    if (auto* action = action_bridge_->action_for("figure.new"))
                    {
                        action->trigger();
                        return;
                    }
                }

                // Preserve the signal fallback for lightweight hosts that do
                // not install a command bridge.
                emit figure_activated(INVALID_FIGURE_ID);
            });
    connect(doc_tab_bar_,
            &SpectraDocumentTabBar::tab_selected,
            this,
            [this](int id)
            {
                if (central_view_)
                    central_view_->activate_figure(static_cast<FigureId>(id));
            });
    connect(doc_tab_bar_,
            &SpectraDocumentTabBar::tab_closed,
            this,
            [this](int id) { close_figure_tab(static_cast<FigureId>(id)); });
    connect(doc_tab_bar_,
            &SpectraDocumentTabBar::tab_detach_requested,
            this,
            [this](int id) { emit figure_detach_requested(static_cast<FigureId>(id)); });
    connect(doc_tab_bar_,
            &SpectraDocumentTabBar::figure_dropped,
            this,
            [this](int id, int)
            {
                if (id >= 0)
                    emit figure_drop_requested(static_cast<FigureId>(id));
            });

    // Create canvas frame wrapping the central view
    canvas_frame_ = new SpectraCanvasFrame(central_view_, this);
    canvas_frame_->setObjectName("spectra_canvas_frame");

    // Create custom status bar
    spectra_status_ = new SpectraStatusBar(this);
    spectra_status_->setObjectName("spectra_status_bar");

    // Hide navigation/document/status chrome when the split view shows the
    // welcome page, and restore the user's nav-rail preference when a figure
    // is opened.
    connect(central_view_,
            &QtSplitViewContainer::welcome_page_visible,
            this,
            &SpectraMainWindow::on_welcome_page_visible);

    // Create inspector drawer (hidden by default)
    spectra_inspector_ = new SpectraInspectorDrawer(this);
    spectra_inspector_->setObjectName("spectra_inspector");
    spectra_inspector_->set_content_widget(inspector_panel_);
    connect(spectra_inspector_,
            &SpectraInspectorDrawer::opened,
            this,
            [this]()
            {
                if (inspector_toggle_action_)
                {
                    const QSignalBlocker blocker(inspector_toggle_action_);
                    inspector_toggle_action_->setChecked(true);
                }
                if (settings_panel_)
                    settings_panel_->set_inspector_visible(true);
            });
    connect(spectra_inspector_,
            &SpectraInspectorDrawer::closed,
            this,
            [this]()
            {
                if (inspector_toggle_action_)
                {
                    const QSignalBlocker blocker(inspector_toggle_action_);
                    inspector_toggle_action_->setChecked(false);
                }
                if (settings_panel_)
                    settings_panel_->set_inspector_visible(false);
            });
    // Match the legacy shell hierarchy: header, then a full-height rail beside
    // the document tabs and canvas, then the status strip.
    central_container_ = new QWidget(this);
    central_container_->setObjectName("spectra_central_container");

    auto* main_layout = new QVBoxLayout(central_container_);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    // Native Qt window decoration replaces the platform decoration owned by
    // GLFW. Do not add a second title strip inside the client area.
    title_bar_->hide();

    // App header below title bar
    main_layout->addWidget(app_header_);

    // Workspace: the rail begins directly below the header. The tab strip is
    // part of the document column and therefore begins at x = rail width.
    auto* middle_layout = new QHBoxLayout();
    middle_layout->setContentsMargins(0, 0, 0, 0);
    middle_layout->setSpacing(0);

    middle_layout->addWidget(nav_rail_);

    auto* document_layout = new QVBoxLayout();
    document_layout->setContentsMargins(0, 0, 0, 0);
    document_layout->setSpacing(0);
    document_layout->addWidget(doc_tab_bar_);
    document_layout->addWidget(canvas_frame_, 1);
    middle_layout->addLayout(document_layout, 1);

    middle_layout->addWidget(spectra_inspector_);

    main_layout->addLayout(middle_layout, 1);

    // Status bar at bottom
    main_layout->addWidget(spectra_status_);

    // Set as central widget (replaces setCentralWidget(central_view_))
    setCentralWidget(central_container_);

    // Inspector is hidden by default
    spectra_inspector_->setVisible(false);

    // Apply persisted shell visibility after all custom surfaces exist.
    if (services_)
    {
        const auto& settings = services_->settings().data();
        nav_rail_user_pref_  = settings.nav_rail_visible;
        set_nav_rail_visible(nav_rail_user_pref_);
        on_toggle_inspector(settings.inspector_visible);
        set_timeline_visible(settings.timeline_visible);
    }
}

void SpectraMainWindow::update_compact_mode()
{
    if (!nav_rail_)
        return;

    bool compact = width() < 1100;
    nav_rail_->set_compact_mode(compact);

    // In compact mode, inspector becomes overlay instead of taking layout space
    if (spectra_inspector_ && spectra_inspector_->is_open())
    {
        // TODO: overlay positioning for compact mode
    }
}

// ── Styling ────────────────────────────────────────────────────────────────────

void SpectraMainWindow::refresh_theme()
{
    if (services_)
    {
        if (settings_panel_)
            settings_panel_->set_theme_name(services_->theme().current_theme_name());
        apply_theme(services_->theme().colors());
        return;
    }

    ui::ThemeManager fallback;
    fallback.ensure_initialized();
    apply_theme(fallback.colors());
}

void SpectraMainWindow::apply_theme(const ui::ThemeColors& theme)
{
    set_spectra_colors(theme);

    // The geometry and control-state rules stay stable while every color is
    // substituted from the same ThemeManager palette used by the renderer.
    QString style = QStringLiteral(R"(
        QMainWindow {
            background-color: #0A0F18;
        }
        QDialog {
            background-color: #111827;
        }
        QMenuBar {
            background-color: #0A0F18;
            color: #C7D6EB;
            border: none;
            padding: 2px 4px;
            font-family: "Inter";
            font-size: 13px;
        }
        QMenuBar::item {
            background: transparent;
            padding: 4px 12px;
            border-radius: 4px;
        }
        QMenuBar::item:selected {
            background-color: #1A2332;
            color: #EDF0F7;
        }
        QMenuBar::item:pressed {
            background-color: #222D3F;
        }
        QMenu {
            background-color: #111827;
            color: #C7D6EB;
            border: 1px solid #2E3D57;
            border-radius: 8px;
            padding: 6px;
            font-family: "Inter";
            font-size: 13px;
        }
        QMenu::item {
            padding: 6px 24px 6px 16px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: #222D3F;
            color: #EDF0F7;
        }
        QMenu::separator {
            height: 1px;
            background: #2E3D57;
            margin: 4px 8px;
        }
        QStatusBar {
            background-color: #0D0E11;
            color: #8A909C;
            border-top: 1px solid #1A1C22;
            font-family: "Inter";
            font-size: 11px;
            min-height: 28px;
        }
        QStatusBar::item { border: none; }
        QDockWidget {
            color: #C8CDD6;
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
            font-family: "Inter";
            font-size: 12px;
        }
        QDockWidget::title {
            background-color: #15171C;
            border: none;
            padding: 6px 12px;
            border-bottom: 1px solid #23262E;
        }
        QDockWidget > QWidget,
        QDockWidget QScrollArea,
        QDockWidget QScrollArea > QWidget > QWidget,
        QDockWidget QTabWidget,
        QDockWidget QTabWidget > QWidget {
            background-color: #15171C;
            color: #C8CDD6;
        }
        QTabWidget::pane {
            border: none;
            background-color: #0D0E11;
        }
        QTabBar::tab {
            background-color: transparent;
            color: #8A909C;
            padding: 6px 16px;
            border: none;
            font-family: "Inter";
            font-size: 12px;
        }
        QTabBar::tab:selected {
            color: #E8ECF1;
            border-bottom: 2px solid #7C5CFC;
        }
        QTabBar::tab:hover:!selected {
            color: #C8CDD6;
        }
        QWidget#welcome_page {
            background-color: #0D0E11;
        }
        QLabel {
            color: #C8CDD6;
            font-family: "Inter";
        }
        QToolBar {
            background-color: #0D0E11;
            border: none;
            spacing: 4px;
            padding: 4px;
        }
        QSplitter::handle {
            background-color: #1A1C22;
        }
        QSplitter::handle:horizontal { width: 1px; }
        QSplitter::handle:vertical { height: 1px; }
        QScrollArea {
            background-color: #15171C;
            border: none;
        }
        QPushButton {
            background-color: #1A1C22;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            padding: 6px 14px;
            font-family: "Inter";
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #23262E;
            color: #E8ECF1;
        }
        QPushButton:pressed {
            background-color: #2A2D36;
        }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
            background-color: #15171C;
            color: #E8ECF1;
            border: 1px solid #23262E;
            border-radius: 6px;
            padding: 5px 8px;
            font-family: "Inter";
            font-size: 13px;
        }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
            border-color: #7C5CFC;
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        QComboBox QAbstractItemView {
            background-color: #15171C;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            selection-background-color: #1F2229;
        }
        QCheckBox {
            color: #C8CDD6;
            font-family: "Inter";
            font-size: 13px;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 4px;
            border: 1px solid #23262E;
            background-color: #15171C;
        }
        QCheckBox::indicator:checked {
            background-color: #7C5CFC;
            border-color: #7C5CFC;
        }
        QTreeWidget, QTreeWidget::item {
            background-color: #15171C;
            color: #C8CDD6;
            border: none;
            font-family: "Inter";
            font-size: 12px;
        }
        QTreeWidget::item:selected {
            background-color: #1F2229;
            color: #E8ECF1;
        }
        QHeaderView::section {
            background-color: #15171C;
            color: #8A909C;
            border: none;
            border-bottom: 1px solid #23262E;
            padding: 4px 8px;
            font-family: "Inter";
            font-size: 11px;
        }
        QGroupBox {
            color: #8A909C;
            border: 1px solid #23262E;
            border-radius: 8px;
            margin-top: 12px;
            padding-top: 8px;
            font-family: "Inter";
            font-size: 12px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 4px;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #2A2D36;
            border-radius: 4px;
            min-height: 24px;
        }
        QScrollBar::handle:vertical:hover {
            background: #3A3D46;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 8px;
            margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: #2A2D36;
            border-radius: 4px;
            min-width: 24px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #3A3D46;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0;
        }

        /* ── QListWidget ──────────────────────────────────────────── */
        QListWidget {
            background-color: #15171C;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            padding: 4px;
            font-family: "Inter";
            font-size: 12px;
            outline: none;
        }
        QListWidget::item {
            padding: 4px 8px;
            border-radius: 4px;
        }
        QListWidget::item:selected {
            background-color: #1F2229;
            color: #E8ECF1;
        }
        QListWidget::item:hover:!selected {
            background-color: #1A1C22;
        }

        /* ── QTableWidget ─────────────────────────────────────────── */
        QTableWidget {
            background-color: #15171C;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            gridline-color: #23262E;
            font-family: "Inter";
            font-size: 12px;
            outline: none;
        }
        QTableWidget::item {
            padding: 4px 8px;
        }
        QTableWidget::item:selected {
            background-color: #1F2229;
            color: #E8ECF1;
        }
        QTableCornerButton::section {
            background-color: #15171C;
            border: none;
            border-bottom: 1px solid #23262E;
            border-right: 1px solid #23262E;
        }

        /* ── Disabled states ──────────────────────────────────────── */
        QPushButton:disabled, QLineEdit:disabled, QSpinBox:disabled,
        QDoubleSpinBox:disabled, QComboBox:disabled, QCheckBox:disabled,
        QRadioButton:disabled, QGroupBox:disabled {
            color: #4A4D56;
            background-color: #111316;
            border-color: #1A1C22;
        }
        QCheckBox::indicator:disabled {
            background-color: #111316;
            border-color: #1A1C22;
        }

        /* ── QComboBox down-arrow ─────────────────────────────────── */
        QComboBox::down-arrow {
            image: url("{combobox_arrow}");
            background-color: transparent;
            border: none;
            width: 16px;
            height: 16px;
        }
        QComboBox::down-arrow:on {
            image: url("{combobox_arrow_on}");
            border: none;
        }
        QComboBox::down-arrow:disabled {
            image: url("{combobox_arrow_disabled}");
            border: none;
        }

        /* ── QSpinBox / QDoubleSpinBox buttons ────────────────────── */
        QSpinBox::up-button, QDoubleSpinBox::up-button,
        QSpinBox::down-button, QDoubleSpinBox::down-button {
            background-color: #1A1C22;
            border: none;
            width: 18px;
        }
        QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
        QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
            background-color: #23262E;
        }
        QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
            image: url("{spinbox_up}");
            background-color: transparent;
            border: none;
            width: 16px;
            height: 16px;
        }
        QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled {
            image: url("{spinbox_up_disabled}");
            border: none;
        }
        QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
            image: url("{spinbox_down}");
            background-color: transparent;
            border: none;
            width: 16px;
            height: 16px;
        }
        QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled {
            image: url("{spinbox_down_disabled}");
            border: none;
        }

        /* ── QCheckBox checkmark ──────────────────────────────────── */
        QCheckBox::indicator:checked {
            background-color: #7C5CFC;
            border-color: #7C5CFC;
            image: url("{checkbox_checked}");
        }
        QCheckBox::indicator:checked:disabled {
            image: url("{checkbox_checked_disabled}");
        }

        /* ── QRadioButton ─────────────────────────────────────────── */
        QRadioButton {
            color: #C8CDD6;
            font-family: "Inter";
            font-size: 13px;
            spacing: 8px;
        }
        QRadioButton::indicator {
            width: 16px;
            height: 16px;
            border-radius: 8px;
            border: 1px solid #23262E;
            background-color: #15171C;
        }
        QRadioButton::indicator:checked {
            background-color: #7C5CFC;
            border-color: #7C5CFC;
        }

        /* ── QMenu checkable indicators ───────────────────────────── */
        QMenu::indicator {
            width: 16px;
            height: 16px;
        }
        QMenu::indicator:checked {
            background-color: #7C5CFC;
            border-radius: 3px;
        }

        /* ── QDockWidget close/float buttons ──────────────────────── */
        QDockWidget::close-button, QDockWidget::float-button {
            background-color: transparent;
            border: none;
            padding: 2px;
        }
        QDockWidget::close-button:hover, QDockWidget::float-button:hover {
            background-color: #23262E;
            border-radius: 3px;
        }
        QDockWidget::close-button:pressed, QDockWidget::float-button:pressed {
            background-color: #2A2D36;
        }

        /* ── QPlainTextEdit / QTextEdit ───────────────────────────── */
        QPlainTextEdit, QTextEdit {
            background-color: #15171C;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            padding: 4px 8px;
            font-family: "Inter";
            font-size: 12px;
        }
        QPlainTextEdit:focus, QTextEdit:focus {
            border-color: #7C5CFC;
        }

        /* ── QProgressBar ─────────────────────────────────────────── */
        QProgressBar {
            background-color: #15171C;
            border: 1px solid #23262E;
            border-radius: 4px;
            text-align: center;
            color: #C8CDD6;
            font-family: "Inter";
            font-size: 11px;
        }
        QProgressBar::chunk {
            background-color: #7C5CFC;
            border-radius: 3px;
        }

        /* ── QSlider ──────────────────────────────────────────────── */
        QSlider::groove:horizontal {
            background-color: #23262E;
            height: 4px;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background-color: #7C5CFC;
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 7px;
        }
        QSlider::handle:horizontal:hover {
            background-color: #8C6CFF;
        }
        QSlider::groove:vertical {
            background-color: #23262E;
            width: 4px;
            border-radius: 2px;
        }
        QSlider::handle:vertical {
            background-color: #7C5CFC;
            width: 14px;
            height: 14px;
            margin: 0 -5px;
            border-radius: 7px;
        }
        QSlider::handle:vertical:hover {
            background-color: #8C6CFF;
        }

        /* ── QToolTip ─────────────────────────────────────────────── */
        QToolTip {
            background-color: #111827;
            color: #C7D6EB;
            border: 1px solid #2E3D57;
            border-radius: 4px;
            padding: 4px 8px;
            font-family: "Inter";
            font-size: 12px;
        }

        /* ── QFrame ───────────────────────────────────────────────── */
        QFrame#spectra_canvas_frame {
            background-color: #0D0E11;
            border: none;
        }
        QWidget#spectra_central_container {
            background-color: #0A0F18;
        }
    )");

    const auto&  colors   = spectra_colors();
    const QColor selected = composite_color(theme.accent_muted, theme.bg_secondary);
    const QColor hovered  = composite_color(theme.hover_highlight, theme.bg_secondary);
    const QColor disabled = composite_color(theme.text_tertiary, theme.bg_primary);

    // Indicator icons are rendered from the icon font so they stay crisp across
    // themes and DPR. The paths are written to a temp directory and then embedded
    // into the stylesheet as image URLs.
    const QString checkbox_checked_url =
        icon_image_url(0xf00c, colors.text_primary, 16, "checkbox");
    const QString checkbox_checked_disabled_url =
        icon_image_url(0xf00c, disabled, 16, "checkbox_disabled");
    const QString checkbox_checked_accent_url =
        icon_image_url(0xf00c, colors.cyan_accent, 16, "checkbox_accent");
    const QString combobox_arrow_url =
        icon_image_url(0xf078, colors.text_muted, 16, "combobox_arrow");
    const QString combobox_arrow_on_url =
        icon_image_url(0xf077, colors.text_muted, 16, "combobox_arrow_on");
    const QString combobox_arrow_disabled_url =
        icon_image_url(0xf078, disabled, 16, "combobox_arrow_disabled");
    const QString spinbox_up_url = icon_image_url(0xf077, colors.text_muted, 16, "spinbox_up");
    const QString spinbox_up_disabled_url =
        icon_image_url(0xf077, disabled, 16, "spinbox_up_disabled");
    const QString spinbox_down_url = icon_image_url(0xf078, colors.text_muted, 16, "spinbox_down");
    const QString spinbox_down_disabled_url =
        icon_image_url(0xf078, disabled, 16, "spinbox_down_disabled");

    const std::array<std::pair<const char*, QColor>, 20> substitutions = {{
        {"#0A0F18", colors.window_base},
        {"#0D0E11", colors.bg_canvas},
        {"#111316", colors.window_base},
        {"#111827", colors.panel_surface},
        {"#15171C", colors.panel_surface},
        {"#1A1C22", hovered},
        {"#1A2332", colors.input_surface},
        {"#1F2229", selected},
        {"#222D3F", colors.elevated_surface},
        {"#23262E", colors.border_default},
        {"#2A2D36", colors.elevated_surface},
        {"#2E3D57", colors.border_subtle},
        {"#3A3D46", colors.border_strong},
        {"#4A4D56", disabled},
        {"#7C5CFC", colors.cyan_accent},
        {"#8C6CFF",
         QColor::fromRgbF(theme.accent_hover.r, theme.accent_hover.g, theme.accent_hover.b)},
        {"#8A909C", colors.text_muted},
        {"#C7D6EB", colors.text_secondary},
        {"#C8CDD6", colors.text_secondary},
        {"#E8ECF1", colors.text_primary},
    }};
    for (const auto& [source, target] : substitutions)
        style.replace(QLatin1String(source), css_color(target), Qt::CaseInsensitive);
    style.replace(QLatin1String("#EDF0F7"), css_color(colors.text_primary), Qt::CaseInsensitive);

    style.replace(QLatin1String("{checkbox_checked}"), checkbox_checked_url);
    style.replace(QLatin1String("{checkbox_checked_disabled}"), checkbox_checked_disabled_url);
    style.replace(QLatin1String("{combobox_arrow}"), combobox_arrow_url);
    style.replace(QLatin1String("{combobox_arrow_on}"), combobox_arrow_on_url);
    style.replace(QLatin1String("{combobox_arrow_disabled}"), combobox_arrow_disabled_url);
    style.replace(QLatin1String("{spinbox_up}"), spinbox_up_url);
    style.replace(QLatin1String("{spinbox_up_disabled}"), spinbox_up_disabled_url);
    style.replace(QLatin1String("{spinbox_down}"), spinbox_down_url);
    style.replace(QLatin1String("{spinbox_down_disabled}"), spinbox_down_disabled_url);

    // Override native input/checkbox/button styling to match the token palette
    // used by the custom widgets and the legacy ImGui inspector (bg_tertiary
    // surface, cyan accent focus, no heavy default border).
    auto rgba = [](const QColor& c, int alpha)
    { return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha); };
    QString    input_override = QStringLiteral(R"(
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
            background-color: __BG_TERTIARY__;
            color: __TEXT_PRIMARY__;
            border: 1px solid __BORDER_SUBTLE__;
            border-radius: __RADIUS_MD__px;
            padding: 4px 8px;
            font-family: "Inter";
            font-size: 13px;
            selection-background-color: __CYAN_ACCENT_DIM__;
        }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus,
        QPlainTextEdit:focus, QTextEdit:focus {
            border-color: __CYAN_ACCENT__;
        }
        QSpinBox::up-button, QDoubleSpinBox::up-button,
        QSpinBox::down-button, QDoubleSpinBox::down-button {
            width: 0px;
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        QComboBox::down-arrow {
            image: url("__COMBOBOX_ARROW_URL__");
            background-color: transparent;
            border: none;
            width: 16px;
            height: 16px;
        }
        QComboBox QAbstractItemView {
            background-color: __BG_ELEVATED__;
            color: __TEXT_PRIMARY__;
            border: 1px solid __BORDER_SUBTLE__;
            border-radius: __RADIUS_MD__px;
            selection-background-color: __INPUT_SURFACE__;
        }
        QCheckBox {
            color: __TEXT_PRIMARY__;
            spacing: 8px;
            font-family: "Inter";
            font-size: 13px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: __RADIUS_SM__px;
            border: 1px solid __BORDER_SUBTLE__;
            background-color: __BG_TERTIARY__;
        }
        QCheckBox::indicator:checked {
            background-color: __BG_TERTIARY__;
            border-color: __CYAN_ACCENT__;
            image: url("__CHECKBOX_ACCENT_URL__");
        }
        QPushButton {
            background-color: __BUTTON_NORMAL__;
            color: __TEXT_PRIMARY__;
            border: none;
            border-radius: __RADIUS_MD__px;
            padding: 6px 14px;
            font-family: "Inter";
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: __BUTTON_HOVER__;
        }
        QPushButton:pressed {
            background-color: __BUTTON_PRESSED__;
        }
        QPushButton:disabled {
            color: __TEXT_MUTED__;
            background-color: __INPUT_SURFACE__;
        }

        /* ── Inspector surfaces ───────────────────────────────────────
           The drawer paints the panel surface itself, so the panel, its
           scroll areas, viewports, and page containers must stay
           transparent. Without this they fall back to the default light
           palette and the token-painted custom widgets become dark text
           on a near-white background. */
        QWidget#inspector_panel,
        QWidget#inspector_panel QScrollArea,
        QWidget#inspector_panel QScrollArea > QWidget,
        QWidget#inspector_panel QScrollArea > QWidget > QWidget,
        QWidget#inspector_panel QStackedWidget,
        QWidget#inspector_panel QStackedWidget > QWidget,
        QWidget#inspector_figure_page,
        QWidget#inspector_series_page,
        QWidget#inspector_axes_page,
        QWidget#inspector_data_page {
            background: transparent;
            border: none;
        }
        QWidget#inspector_panel QLabel,
        QWidget#inspector_panel QCheckBox {
            background: transparent;
        }
    )");
    const auto substitute     = [&input_override](const QString& key, const QString& value)
    { input_override.replace(key, value); };
    substitute("__BG_TERTIARY__", css_color(colors.bg_tertiary));
    substitute("__TEXT_PRIMARY__", css_color(colors.text_primary));
    substitute("__BORDER_SUBTLE__", css_color(colors.border_subtle));
    substitute("__RADIUS_MD__", QString::number(static_cast<int>(ui::tokens::RADIUS_MD)));
    substitute("__RADIUS_SM__", QString::number(static_cast<int>(ui::tokens::RADIUS_SM)));
    substitute("__CYAN_ACCENT_DIM__", css_color(colors.cyan_accent_dim));
    substitute("__CYAN_ACCENT__", css_color(colors.cyan_accent));
    substitute("__COMBOBOX_ARROW_URL__", combobox_arrow_url);
    substitute("__BG_ELEVATED__", css_color(colors.elevated_surface));
    substitute("__INPUT_SURFACE__", css_color(colors.input_surface));
    substitute("__CHECKBOX_ACCENT_URL__", checkbox_checked_accent_url);
    substitute("__BUTTON_NORMAL__", rgba(colors.bg_tertiary, 153));
    substitute("__BUTTON_HOVER__", rgba(colors.purple_dim, 128));
    substitute("__BUTTON_PRESSED__", rgba(colors.cyan_accent_dim, 179));
    substitute("__TEXT_MUTED__", css_color(colors.text_muted));
    style += input_override;

    // Application scope covers detached windows and native popup/dialog
    // surfaces. Custom-painted widgets read the synchronized token palette.
    qApp->setStyleSheet(style);
    for (QWidget* widget : QApplication::allWidgets())
        widget->update();
}

void SpectraMainWindow::load_fonts()
{
    // Load Inter font from third_party/Inter-Regular.ttf
    QString inter_path = QApplication::applicationDirPath() + "/../third_party/Inter-Regular.ttf";
    int     font_id    = QFontDatabase::addApplicationFont(inter_path);
    if (font_id < 0)
    {
        // Try absolute path relative to source tree
        inter_path = QApplication::applicationDirPath() + "/../../third_party/Inter-Regular.ttf";
        font_id    = QFontDatabase::addApplicationFont(inter_path);
    }
    if (font_id >= 0)
    {
        QStringList families = QFontDatabase::applicationFontFamilies(font_id);
        if (!families.isEmpty())
        {
            QApplication::setFont(QFont(families.first(), 13));
        }
    }
}

void SpectraMainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    // Update compact mode based on window width
    update_compact_mode();

    // Enforce minimum canvas width: if central widget would be < 640 logical px
    // due to docks, hide non-essential docks to protect canvas space.
    int central_width = central_view_ ? central_view_->width() : 0;
    if (central_width > 0 && central_width < 640)
    {
        // Hide all docks except inspector to give canvas maximum space
        if (topics_panel_ && topics_panel_->isVisible())
            topics_panel_->hide();
        if (settings_panel_ && settings_panel_->isVisible())
            settings_panel_->hide();
        if (timeline_panel_ && timeline_panel_->isVisible())
            timeline_panel_->hide();
        if (export_panel_ && export_panel_->isVisible())
            export_panel_->hide();
        if (shortcut_panel_ && shortcut_panel_->isVisible())
            shortcut_panel_->hide();
        if (transform_panel_ && transform_panel_->isVisible())
            transform_panel_->hide();
        if (data_editor_panel_ && data_editor_panel_->isVisible())
            data_editor_panel_->hide();
        if (accessibility_panel_ && accessibility_panel_->isVisible())
            accessibility_panel_->hide();
        if (plugin_panel_ && plugin_panel_->isVisible())
            plugin_panel_->hide();
        if (plugins_panel_ && plugins_panel_->isVisible())
            plugins_panel_->hide();

        // If still too narrow, hide inspector too
        int new_central = central_view_ ? central_view_->width() : 0;
        if (new_central > 0 && new_central < 640)
        {
            if (spectra_inspector_ && spectra_inspector_->is_open())
                spectra_inspector_->close();
        }
    }
}

}   // namespace spectra::adapters::qt
