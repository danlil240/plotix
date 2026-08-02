// split_view_container.cpp — Qt split-pane central widget implementation.

#include "split_view_container.hpp"

#include "figure_canvas_widget.hpp"
#include "qt_runtime.hpp"

#include "ui/docking/split_view.hpp"
#include "ui/input/input.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include "components/spectra_design_tokens.hpp"
#include "spectra_icon_embedded.hpp"

#include <spectra/version.hpp>

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace spectra::adapters::qt
{

namespace
{
constexpr auto FIGURE_TAB_MIME = "application/x-spectra-figure-id";

class PaneTabBar final : public QTabBar
{
   public:
    explicit PaneTabBar(QWidget* parent = nullptr) : QTabBar(parent) { setAcceptDrops(true); }
    std::function<void(FigureId)> on_figure_dropped;

   protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        pressed_index_ =
            event->button() == Qt::LeftButton ? tabAt(event->position().toPoint()) : -1;
        press_pos_ = event->position().toPoint();
        QTabBar::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        QTabBar::mouseMoveEvent(event);
        if (pressed_index_ < 0 || !(event->buttons() & Qt::LeftButton)
            || rect().contains(event->position().toPoint())
            || (event->position().toPoint() - press_pos_).manhattanLength()
                   < QApplication::startDragDistance())
            return;

        const FigureId id = tabData(pressed_index_).toULongLong();
        if (id == INVALID_FIGURE_ID)
            return;
        auto* mime = new QMimeData;
        mime->setData(FIGURE_TAB_MIME, QByteArray::number(id));
        QDrag drag(this);
        drag.setMimeData(mime);
        pressed_index_ = -1;
        drag.exec(Qt::MoveAction);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        pressed_index_ = -1;
        QTabBar::mouseReleaseEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(FIGURE_TAB_MIME))
            event->acceptProposedAction();
        else
            QTabBar::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->mimeData()->hasFormat(FIGURE_TAB_MIME))
            event->acceptProposedAction();
        else
            QTabBar::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        bool           ok = false;
        const FigureId id = event->mimeData()->data(FIGURE_TAB_MIME).toULongLong(&ok);
        if (ok && id != INVALID_FIGURE_ID)
        {
            if (on_figure_dropped)
                on_figure_dropped(id);
            event->acceptProposedAction();
            return;
        }
        QTabBar::dropEvent(event);
    }

   private:
    int    pressed_index_ = -1;
    QPoint press_pos_;
};

class PaneTabWidget final : public QTabWidget
{
   public:
    explicit PaneTabWidget(QWidget* parent = nullptr) : QTabWidget(parent) {}
    void install_tab_bar(QTabBar* tab_bar) { setTabBar(tab_bar); }
};

// ─── WelcomePage ─────────────────────────────────────────────────────────────
// Custom-painted empty-state screen matching the legacy ImGui welcome page.

class WelcomePage final : public QWidget
{
   public:
    explicit WelcomePage(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_StyledBackground, false);
        setAutoFillBackground(false);
        logo_.loadFromData(SpectraIcon_png_data, SpectraIcon_png_size, "PNG");
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const auto& c = spectra_colors();
        p.fillRect(rect(), c.bg_canvas);

        const int w  = width();
        const int h  = height();
        const int cx = w / 2;

        // Logo sizing matches the legacy ImGui clamp: ~28% of content height,
        // bounded so it does not dominate small windows.
        const int logo_size = std::clamp(static_cast<int>(h * 0.28), 180, 220);
        const int logo_y    = static_cast<int>(h * 0.45 - logo_size * 0.5);
        const int logo_x    = cx - logo_size / 2;
        const int center_y  = logo_y + logo_size / 2;

        // Subtle accent glow behind the logo, same low-alpha circle the
        // legacy renderer draws.
        QColor glow = c.cyan_accent;
        glow.setAlpha(16);
        p.setBrush(glow);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(cx, center_y), logo_size * 0.62, logo_size * 0.62);

        // Logo image
        const QRect logo_rect(logo_x, logo_y, logo_size, logo_size);
        p.drawPixmap(logo_rect, logo_, logo_.rect());

        // Title
        QFont title_font = SpectraFontManager::instance().font_title();
        title_font.setPixelSize(26);
        p.setFont(title_font);
        p.setPen(c.text_primary);
        p.drawText(QRect(0, center_y + 155, w, 100),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QStringLiteral("Spectra"));

        // Subtitle
        QFont sub_font = SpectraFontManager::instance().font_small();
        sub_font.setPixelSize(12);
        p.setFont(sub_font);
        QColor sub_color = c.text_secondary;
        sub_color.setAlpha(220);
        p.setPen(sub_color);
        p.drawText(QRect(0, center_y + 187, w, 80),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QStringLiteral("GPU-Accelerated Scientific Visualization"));

        // Keyboard hint at the bottom
        QFont hint_font = sub_font;
        hint_font.setPixelSize(11);
        p.setFont(hint_font);
        QColor hint_color = c.text_secondary;
        hint_color.setAlpha(220);
        p.setPen(hint_color);
        p.drawText(QRect(0, h - 52 - 50, w, 50),
                   Qt::AlignHCenter | Qt::AlignBottom,
                   QStringLiteral("Press Ctrl+T to create a new figure"));

        // Version
        QColor ver_color = c.text_muted;
        ver_color.setAlpha(220);
        p.setPen(ver_color);
        QString version;
#ifdef SPECTRA_VERSION_STRING
        version = QStringLiteral("v") + SPECTRA_VERSION_STRING;
#else
        version = QStringLiteral("v0.1.2");
#endif
        p.drawText(QRect(0, h - 30 - 30, w, 30), Qt::AlignHCenter | Qt::AlignBottom, version);
    }

   private:
    QPixmap logo_;
};

}   // namespace

// ─── PaneWidget ──────────────────────────────────────────────────────────────
// A single pane in the split view — a QTabWidget holding figure canvases.

struct QtSplitViewContainer::PaneWidget
{
    QTabWidget* tab_widget = nullptr;
    FigureId    active_id  = INVALID_FIGURE_ID;

    struct FigureTab
    {
        FigureId                      id     = INVALID_FIGURE_ID;
        FigureCanvasWidget*           canvas = nullptr;
        std::unique_ptr<InputHandler> input;
    };
    std::unordered_map<FigureId, FigureTab> figure_tabs;
};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

QtSplitViewContainer::QtSplitViewContainer(QtRuntime*      runtime,
                                           FigureRegistry* registry,
                                           QWidget*        parent)
    : QWidget(parent), runtime_(runtime), registry_(registry),
      split_view_(std::make_unique<SplitViewManager>())
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    splitter_ = new QSplitter(this);
    splitter_->setObjectName("split_view_splitter");
    layout->addWidget(splitter_);

    // Create initial single pane
    rebuild_splitter();
    show_welcome_page();
}

QtSplitViewContainer::~QtSplitViewContainer() = default;

// ─── Figure tab management ────────────────────────────────────────────────────

int QtSplitViewContainer::add_figure_tab(FigureId id)
{
    PaneWidget* requested_pane = next_figure_target_pane_;
    next_figure_target_pane_   = nullptr;
    if (!registry_ || id == INVALID_FIGURE_ID)
        return -1;

    Figure* figure = registry_->get(id);
    if (!figure)
        return -1;

    // Check if already open in any pane
    for (auto* pane : panes_)
    {
        auto it = pane->figure_tabs.find(id);
        if (it != pane->figure_tabs.end())
        {
            int idx = pane->tab_widget->indexOf(it->second.canvas);
            if (idx >= 0)
            {
                pane->tab_widget->setCurrentIndex(idx);
                split_view_->set_active_figure_index(id);
                return idx;
            }
        }
    }

    // Add to active pane
    PaneWidget* pane = requested_pane ? requested_pane : active_pane();
    if (!pane)
    {
        if (panes_.empty())
            rebuild_splitter();
        pane = active_pane();
        if (!pane)
            return -1;
    }

    // Keep the logical split tree authoritative. The previous implementation
    // changed only active_figure_index(), so the root continued to contain the
    // sentinel figure 0 and split_active() could never find a real document.
    SplitPane* model_pane = nullptr;
    if (requested_pane)
    {
        const int index       = find_pane_index(requested_pane);
        auto      model_panes = split_view_->all_panes();
        if (index >= 0 && static_cast<size_t>(index) < model_panes.size())
            model_pane = model_panes[static_cast<size_t>(index)];
    }
    else
    {
        model_pane = split_view_->active_pane();
    }
    if (model_pane)
    {
        if (model_pane->has_figure(0))
            model_pane->remove_figure(0);
        model_pane->add_figure(id);
    }
    else if (auto* root = split_view_->root(); root && root->is_leaf())
    {
        root->remove_figure(0);
        root->add_figure(id);
    }

    auto input = std::make_unique<InputHandler>();
    input->set_figure(figure);
    input->set_axis_link_manager(axis_link_manager_);
    if (!figure->axes().empty() && figure->axes()[0])
        input->set_active_axes_base(figure->axes()[0].get());
    else if (!figure->all_axes().empty() && figure->all_axes()[0])
        input->set_active_axes_base(figure->all_axes()[0].get());

    auto* canvas = new FigureCanvasWidget(runtime_, figure, input.get(), pane->tab_widget);
    canvas->setObjectName(QString("canvas_%1").arg(id));
    connect(canvas, &FigureCanvasWidget::activated, this, [this, id]() { activate_figure(id); });
    emit canvas_created(id, canvas);

    std::string title = figure->tab_title();
    if (title.empty())
        title = "Figure " + std::to_string(id);

    int idx = pane->tab_widget->addTab(canvas, QString::fromStdString(title));
    pane->tab_widget->tabBar()->setTabData(idx, QVariant::fromValue<qulonglong>(id));

    PaneWidget::FigureTab tab;
    tab.id                = id;
    tab.canvas            = canvas;
    tab.input             = std::move(input);
    pane->figure_tabs[id] = std::move(tab);
    pane->active_id       = id;

    canvas->startAnimationTimer();

    hide_welcome_page();
    pane->tab_widget->setCurrentIndex(idx);
    split_view_->set_active_figure_index(id);

    SPECTRA_LOG_INFO("qt_split_view", "Added figure tab: id={} pane={}", id, find_pane_index(pane));
    return idx;
}

bool QtSplitViewContainer::close_figure_tab(FigureId id)
{
    return remove_figure_tab(id, true);
}

bool QtSplitViewContainer::release_figure_tab(FigureId id)
{
    return remove_figure_tab(id, false);
}

bool QtSplitViewContainer::remove_figure_tab(FigureId id, bool notify_closed)
{
    for (auto* pane : panes_)
    {
        auto it = pane->figure_tabs.find(id);
        if (it == pane->figure_tabs.end())
            continue;

        int idx = pane->tab_widget->indexOf(it->second.canvas);
        if (idx >= 0)
            pane->tab_widget->removeTab(idx);

        if (it->second.canvas)
            it->second.canvas->stopAnimationTimer();

        if (auto* model_pane = split_view_->pane_for_figure(id))
            model_pane->remove_figure(id);

        pane->figure_tabs.erase(it);

        // Update active id for this pane
        if (pane->tab_widget->count() > 0)
        {
            int cur = pane->tab_widget->currentIndex();
            if (cur >= 0)
            {
                QWidget* w = pane->tab_widget->widget(cur);
                for (const auto& [fid, tab] : pane->figure_tabs)
                {
                    if (tab.canvas == w)
                    {
                        pane->active_id = fid;
                        break;
                    }
                }
            }
        }
        else
        {
            pane->active_id = INVALID_FIGURE_ID;
        }

        if (notify_closed)
            emit figure_closed(id);

        // If all panes are empty, show welcome
        bool any_tabs = false;
        for (auto* p : panes_)
        {
            if (p->tab_widget->count() > 0)
            {
                any_tabs = true;
                break;
            }
        }
        if (!any_tabs)
            show_welcome_page();

        SPECTRA_LOG_INFO("qt_split_view", "Closed figure tab: id={}", id);
        return true;
    }

    return false;
}

FigureId QtSplitViewContainer::active_figure_id() const
{
    PaneWidget* pane = active_pane();
    if (!pane || pane->tab_widget->count() == 0)
        return INVALID_FIGURE_ID;

    int idx = pane->tab_widget->currentIndex();
    if (idx < 0)
        return INVALID_FIGURE_ID;

    QWidget* widget = pane->tab_widget->widget(idx);
    for (const auto& [id, tab] : pane->figure_tabs)
    {
        if (tab.canvas == widget)
            return id;
    }
    return INVALID_FIGURE_ID;
}

FigureCanvasWidget* QtSplitViewContainer::canvas_for(FigureId id) const
{
    for (auto* pane : panes_)
    {
        auto it = pane->figure_tabs.find(id);
        if (it != pane->figure_tabs.end())
            return it->second.canvas;
    }
    return nullptr;
}

InputHandler* QtSplitViewContainer::input_handler_for(FigureId id) const
{
    for (auto* pane : panes_)
    {
        const auto found = pane->figure_tabs.find(id);
        if (found != pane->figure_tabs.end())
            return found->second.input.get();
    }
    return nullptr;
}

int QtSplitViewContainer::figure_tab_count() const
{
    int count = 0;
    for (auto* pane : panes_)
        count += pane->tab_widget->count();
    return count;
}

std::vector<FigureId> QtSplitViewContainer::open_figure_ids() const
{
    std::vector<FigureId> ids;
    for (const SplitPane* pane : split_view_->all_panes())
    {
        if (!pane)
            continue;
        for (FigureId id : pane->figure_indices())
        {
            if (registry_ && registry_->get(id))
                ids.push_back(id);
        }
    }
    return ids;
}

bool QtSplitViewContainer::activate_figure(FigureId id)
{
    PaneWidget* pane = pane_for_figure(id);
    if (!pane)
        return false;

    auto it = pane->figure_tabs.find(id);
    if (it == pane->figure_tabs.end())
        return false;

    const int index = pane->tab_widget->indexOf(it->second.canvas);
    if (index < 0)
        return false;

    pane->tab_widget->setCurrentIndex(index);
    pane->active_id = id;
    split_view_->set_active_figure_index(id);
    it->second.canvas->setFocus(Qt::OtherFocusReason);
    emit figure_activated(id);
    return true;
}

bool QtSplitViewContainer::set_figure_title(FigureId id, const QString& title)
{
    PaneWidget* pane = pane_for_figure(id);
    if (!pane)
        return false;

    auto it = pane->figure_tabs.find(id);
    if (it == pane->figure_tabs.end())
        return false;

    const int index = pane->tab_widget->indexOf(it->second.canvas);
    if (index < 0)
        return false;
    if (pane->tab_widget->tabText(index) != title)
        pane->tab_widget->setTabText(index, title);
    return true;
}

QString QtSplitViewContainer::figure_title(FigureId id) const
{
    PaneWidget* pane = pane_for_figure(id);
    if (!pane)
        return {};

    auto it = pane->figure_tabs.find(id);
    if (it == pane->figure_tabs.end())
        return {};

    const int index = pane->tab_widget->indexOf(it->second.canvas);
    return index >= 0 ? pane->tab_widget->tabText(index) : QString{};
}

void QtSplitViewContainer::set_active_tool(ToolMode tool)
{
    PaneWidget* pane = active_pane();
    if (!pane)
        return;

    const FigureId id = active_figure_id();
    auto           it = pane->figure_tabs.find(id);
    if (it != pane->figure_tabs.end() && it->second.input)
        it->second.input->set_tool_mode(tool);
}

ToolMode QtSplitViewContainer::active_tool() const
{
    PaneWidget* pane = active_pane();
    if (!pane)
        return ToolMode::Pan;

    const FigureId id = active_figure_id();
    auto           it = pane->figure_tabs.find(id);
    if (it == pane->figure_tabs.end() || !it->second.input)
        return ToolMode::Pan;
    return it->second.input->tool_mode();
}

void QtSplitViewContainer::set_axis_link_manager(AxisLinkManager* manager)
{
    axis_link_manager_ = manager;
    for (auto* pane : panes_)
        for (auto& [id, tab] : pane->figure_tabs)
        {
            (void)id;
            if (tab.input)
                tab.input->set_axis_link_manager(manager);
        }
}

// ─── Welcome page ─────────────────────────────────────────────────────────────

void QtSplitViewContainer::show_welcome_page()
{
    if (!welcome_page_)
    {
        auto* page = new WelcomePage(this);
        page->setObjectName("welcome_page");
        welcome_page_ = page;
    }

    // Add welcome page to the first pane if no tabs exist
    if (figure_tab_count() == 0 && !panes_.empty())
    {
        auto* pane = panes_[0];
        int   idx  = pane->tab_widget->indexOf(welcome_page_);
        if (idx < 0)
        {
            pane->tab_widget->addTab(welcome_page_, "");
            pane->tab_widget->tabBar()->setTabVisible(pane->tab_widget->count() - 1, false);
        }
    }

    emit welcome_page_visible(true);
}

void QtSplitViewContainer::hide_welcome_page()
{
    if (welcome_page_)
    {
        for (auto* pane : panes_)
        {
            int idx = pane->tab_widget->indexOf(welcome_page_);
            if (idx >= 0)
                pane->tab_widget->removeTab(idx);
        }
    }

    emit welcome_page_visible(false);
}

// ─── Split operations ─────────────────────────────────────────────────────────

bool QtSplitViewContainer::split_right()
{
    FigureId active = active_figure_id();
    if (active == INVALID_FIGURE_ID)
        return false;

    // Prefer moving another already-open document from this pane. Falling
    // back to an unopened registry figure keeps the operation useful for
    // publisher-created documents that have not been surfaced as tabs yet.
    auto     all_ids = registry_->all_ids();
    FigureId new_fig = INVALID_FIGURE_ID;
    for (FigureId id : open_figure_ids())
    {
        if (id != active)
        {
            new_fig = id;
            break;
        }
    }
    for (auto id : all_ids)
    {
        if (new_fig == INVALID_FIGURE_ID && id != active && !canvas_for(id))
        {
            new_fig = id;
            break;
        }
    }
    if (new_fig == INVALID_FIGURE_ID)
        return false;

    SplitPane* new_pane = split_view_->split_active(SplitDirection::Horizontal, new_fig);
    if (new_pane == nullptr)
        return false;

    // split() copies every source tab into the first child. This document is
    // being moved to the new child, so remove the duplicate model entry.
    if (new_pane->parent() && new_pane->parent()->first())
        new_pane->parent()->first()->remove_figure(new_fig);

    rebuild_splitter();
    return true;
}

bool QtSplitViewContainer::split_down()
{
    FigureId active = active_figure_id();
    if (active == INVALID_FIGURE_ID)
        return false;

    auto     all_ids = registry_->all_ids();
    FigureId new_fig = INVALID_FIGURE_ID;
    for (FigureId id : open_figure_ids())
    {
        if (id != active)
        {
            new_fig = id;
            break;
        }
    }
    for (auto id : all_ids)
    {
        if (new_fig == INVALID_FIGURE_ID && id != active && !canvas_for(id))
        {
            new_fig = id;
            break;
        }
    }
    if (new_fig == INVALID_FIGURE_ID)
        return false;

    SplitPane* new_pane = split_view_->split_active(SplitDirection::Vertical, new_fig);
    if (new_pane == nullptr)
        return false;

    if (new_pane->parent() && new_pane->parent()->first())
        new_pane->parent()->first()->remove_figure(new_fig);

    rebuild_splitter();
    return true;
}

bool QtSplitViewContainer::close_split()
{
    FigureId active = active_figure_id();
    if (active == INVALID_FIGURE_ID)
        return false;

    const auto open_ids = open_figure_ids();
    bool       result   = split_view_->close_pane(active);
    if (result)
    {
        // Closing a pane changes layout, not document ownership. Merge every
        // open document into the remaining pane instead of silently closing
        // the documents that happened to live in the removed branch.
        const auto remaining = split_view_->all_panes();
        if (!remaining.empty())
        {
            for (FigureId id : open_ids)
                remaining.front()->add_figure(id);
            const auto& merged_ids = remaining.front()->figure_indices();
            const auto  active_it  = std::find(merged_ids.begin(), merged_ids.end(), active);
            if (active_it != merged_ids.end())
                remaining.front()->set_active_local_index(
                    static_cast<size_t>(active_it - merged_ids.begin()));
            split_view_->set_active_figure_index(active);
        }
        rebuild_splitter();
    }
    return result;
}

void QtSplitViewContainer::reset_splits()
{
    const auto open_ids = open_figure_ids();
    const auto active   = active_figure_id();
    split_view_->unsplit_all();
    if (auto* root = split_view_->root())
    {
        root->remove_figure(0);
        for (FigureId id : open_ids)
            root->add_figure(id);
        if (active != INVALID_FIGURE_ID)
        {
            const auto& ids = root->figure_indices();
            auto        it  = std::find(ids.begin(), ids.end(), active);
            if (it != ids.end())
                root->set_active_local_index(static_cast<size_t>(it - ids.begin()));
            split_view_->set_active_figure_index(active);
        }
    }
    rebuild_splitter();
}

bool QtSplitViewContainer::is_split() const
{
    return split_view_->is_split();
}

size_t QtSplitViewContainer::pane_count() const
{
    return split_view_->pane_count();
}

bool QtSplitViewContainer::move_figure_to_pane(FigureId id, size_t target_pane_index)
{
    SplitPane* source = split_view_->pane_for_figure(id);
    auto       panes  = split_view_->all_panes();
    if (!source || target_pane_index >= panes.size() || !panes[target_pane_index]
        || panes[target_pane_index] == source)
        return false;

    SplitPane* target = panes[target_pane_index];
    source->remove_figure(id);
    target->add_figure(id);
    split_view_->set_active_figure_index(id);
    rebuild_splitter();
    activate_figure(id);
    emit split_layout_changed();
    return true;
}

bool QtSplitViewContainer::set_next_figure_target_pane(size_t target_pane_index)
{
    if (target_pane_index >= panes_.size() || !panes_[target_pane_index])
        return false;
    next_figure_target_pane_ = panes_[target_pane_index];
    return true;
}

std::string QtSplitViewContainer::serialize_split_layout() const
{
    return split_view_->serialize();
}

bool QtSplitViewContainer::restore_split_layout(
    const std::string&                            layout,
    const std::unordered_map<FigureId, FigureId>& id_map)
{
    if (layout.empty() || !split_view_->deserialize(layout))
        return false;

    bool complete = true;
    split_view_->remap_figure_ids(
        [&id_map, &complete](FigureId old_id)
        {
            if (id_map.empty())
                return old_id;
            const auto it = id_map.find(old_id);
            if (it == id_map.end())
            {
                complete = false;
                return INVALID_FIGURE_ID;
            }
            return it->second;
        });
    if (!complete)
        return false;

    rebuild_splitter();
    return true;
}

// ─── Private: rebuild splitter from SplitViewManager state ────────────────────

void QtSplitViewContainer::rebuild_splitter()
{
    const FigureId previously_active = split_view_->active_figure_index();

    // QTabWidget reparents the welcome page into its private stacked widget.
    // Detach it before deleting that widget or welcome_page_ becomes dangling.
    if (welcome_page_)
    {
        for (auto* pane : panes_)
        {
            const int welcome_index = pane->tab_widget->indexOf(welcome_page_);
            if (welcome_index >= 0)
                pane->tab_widget->removeTab(welcome_index);
        }
        welcome_page_->setParent(this);
    }

    // Preserve live canvases and their InputHandlers while only rebuilding
    // the Qt pane hierarchy. Destroying them here reset tool/gesture state and
    // forced Vulkan surface churn on every split/unsplit operation.
    std::unordered_map<FigureId, PaneWidget::FigureTab> preserved_tabs;
    for (auto* pane : panes_)
    {
        for (auto& [id, tab] : pane->figure_tabs)
        {
            if (tab.canvas)
            {
                const int index = pane->tab_widget->indexOf(tab.canvas);
                if (index >= 0)
                    pane->tab_widget->removeTab(index);
                tab.canvas->setParent(this);
            }
            preserved_tabs.emplace(id, std::move(tab));
        }
        pane->figure_tabs.clear();
        if (pane->tab_widget)
        {
            pane->tab_widget->setParent(nullptr);
            delete pane->tab_widget;
        }
        delete pane;
    }
    panes_.clear();

    delete splitter_;
    splitter_ = new QSplitter(this);
    splitter_->setObjectName("split_view_splitter");
    if (auto* root_layout = qobject_cast<QVBoxLayout*>(layout()))
        root_layout->addWidget(splitter_);

    auto create_leaf = [this](QWidget* parent) -> PaneWidget*
    {
        auto* pane       = new PaneWidget();
        auto* tab_widget = new PaneTabWidget(parent);
        pane->tab_widget = tab_widget;
        auto* tab_bar    = new PaneTabBar(pane->tab_widget);
        tab_widget->install_tab_bar(tab_bar);
        pane->tab_widget->setTabsClosable(true);
        pane->tab_widget->setMovable(true);
        pane->tab_widget->setObjectName(QString("split_pane_%1").arg(panes_.size()));
        pane->tab_widget->tabBar()->setVisible(is_split());
        connect(pane->tab_widget,
                &QTabWidget::currentChanged,
                this,
                &QtSplitViewContainer::on_tab_changed);
        connect(pane->tab_widget,
                &QTabWidget::tabCloseRequested,
                this,
                &QtSplitViewContainer::on_tab_close_requested);
        pane->tab_widget->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(pane->tab_widget,
                &QWidget::customContextMenuRequested,
                this,
                &QtSplitViewContainer::on_tab_context_menu);
        panes_.push_back(pane);
        tab_bar->on_figure_dropped = [this, pane](FigureId id)
        {
            const int index = find_pane_index(pane);
            if (index >= 0)
                QTimer::singleShot(
                    0,
                    this,
                    [this, id, index]()
                    {
                        if (split_view_->pane_for_figure(id))
                            move_figure_to_pane(id, static_cast<size_t>(index));
                        else
                            emit external_figure_drop_requested(id, static_cast<size_t>(index));
                    });
        };
        connect(tab_bar,
                &QTabBar::tabMoved,
                this,
                [this, pane, tab_bar](int, int to)
                {
                    const int pane_index = find_pane_index(pane);
                    if (pane_index < 0 || to < 0)
                        return;
                    const auto model_panes = split_view_->all_panes();
                    if (static_cast<size_t>(pane_index) >= model_panes.size())
                        return;
                    const FigureId id = tab_bar->tabData(to).toULongLong();
                    if (model_panes[pane_index]->move_figure(id, static_cast<size_t>(to)))
                        emit split_layout_changed();
                });
        return pane;
    };

    SplitPane*                                          root = split_view_->root();
    std::function<QWidget*(SplitPane*, QWidget*, bool)> build_node;
    build_node = [this, &create_leaf, &build_node](SplitPane* node,
                                                   QWidget*   parent,
                                                   bool       root_node) -> QWidget*
    {
        if (!node || node->is_leaf())
            return create_leaf(parent)->tab_widget;

        auto* native_splitter = root_node ? splitter_ : new QSplitter(parent);
        native_splitter->setObjectName(QString("split_node_%1").arg(node->id()));
        native_splitter->setOrientation(
            node->split_direction() == SplitDirection::Horizontal ? Qt::Horizontal : Qt::Vertical);
        native_splitter->addWidget(build_node(node->first(), native_splitter, false));
        native_splitter->addWidget(build_node(node->second(), native_splitter, false));

        const int total = 1000;
        const int first = std::clamp(static_cast<int>(node->split_ratio() * total), 1, total - 1);
        native_splitter->setSizes({first, total - first});
        connect(native_splitter,
                &QSplitter::splitterMoved,
                this,
                [node, native_splitter](int, int)
                {
                    const QList<int> sizes = native_splitter->sizes();
                    if (sizes.size() != 2 || sizes[0] + sizes[1] <= 0)
                        return;
                    node->set_split_ratio(static_cast<float>(sizes[0])
                                          / static_cast<float>(sizes[0] + sizes[1]));
                });
        return native_splitter;
    };

    if (root && root->is_split())
        build_node(root, this, true);
    else
        splitter_->addWidget(build_node(root, splitter_, false));

    const auto   model_panes = split_view_->all_panes();
    const size_t pane_total  = std::min(model_panes.size(), panes_.size());
    for (size_t pane_index = 0; pane_index < pane_total; ++pane_index)
    {
        SplitPane*  model_pane = model_panes[pane_index];
        PaneWidget* pane       = panes_[pane_index];
        if (!model_pane || !pane)
            continue;

        for (FigureId id : model_pane->figure_indices())
        {
            if (id == 0 || id == INVALID_FIGURE_ID)
                continue;

            Figure* figure = registry_ ? registry_->get(id) : nullptr;
            if (!figure)
                continue;

            PaneWidget::FigureTab tab;
            auto                  preserved = preserved_tabs.find(id);
            if (preserved != preserved_tabs.end())
            {
                tab = std::move(preserved->second);
                preserved_tabs.erase(preserved);
                tab.canvas->setParent(pane->tab_widget);
            }
            else
            {
                tab.id    = id;
                tab.input = std::make_unique<InputHandler>();
                tab.input->set_figure(figure);
                tab.input->set_axis_link_manager(axis_link_manager_);
                if (!figure->axes().empty() && figure->axes()[0])
                    tab.input->set_active_axes_base(figure->axes()[0].get());
                else if (!figure->all_axes().empty() && figure->all_axes()[0])
                    tab.input->set_active_axes_base(figure->all_axes()[0].get());
                tab.canvas =
                    new FigureCanvasWidget(runtime_, figure, tab.input.get(), pane->tab_widget);
                tab.canvas->setObjectName(QString("canvas_%1").arg(id));
                connect(tab.canvas,
                        &FigureCanvasWidget::activated,
                        this,
                        [this, id]() { activate_figure(id); });
                emit canvas_created(id, tab.canvas);
                tab.canvas->startAnimationTimer();
            }

            std::string title = figure->tab_title();
            if (title.empty())
                title = "Figure " + std::to_string(id);
            const int tab_index =
                pane->tab_widget->addTab(tab.canvas, QString::fromStdString(title));
            pane->tab_widget->tabBar()->setTabData(tab_index, QVariant::fromValue<qulonglong>(id));
            pane->figure_tabs.emplace(id, std::move(tab));
        }

        const FigureId active    = model_pane->figure_index();
        auto           active_it = pane->figure_tabs.find(active);
        if (active_it != pane->figure_tabs.end())
        {
            pane->active_id = active;
            pane->tab_widget->setCurrentWidget(active_it->second.canvas);
        }
    }

    // Any tab not present in the resulting model is genuinely closed. Stop
    // its timer before normal QWidget ownership destroys it.
    for (auto& [id, tab] : preserved_tabs)
    {
        (void)id;
        if (tab.canvas)
        {
            tab.canvas->stopAnimationTimer();
            delete tab.canvas;
        }
    }

    if (previously_active != INVALID_FIGURE_ID)
    {
        if (PaneWidget* pane = pane_for_figure(previously_active))
        {
            auto tab = pane->figure_tabs.find(previously_active);
            if (tab != pane->figure_tabs.end())
            {
                pane->tab_widget->setCurrentWidget(tab->second.canvas);
                pane->active_id = previously_active;
                split_view_->set_active_figure_index(previously_active);
            }
        }
    }

    if (figure_tab_count() == 0)
        show_welcome_page();
}

void QtSplitViewContainer::sync_from_split_view()
{
    if (!registry_)
        return;

    const auto   model_panes = split_view_->all_panes();
    const size_t count       = std::min(model_panes.size(), panes_.size());
    for (size_t pane_index = 0; pane_index < count; ++pane_index)
    {
        SplitPane*  model_pane = model_panes[pane_index];
        PaneWidget* pane       = panes_[pane_index];
        if (!model_pane || !pane)
            continue;

        for (FigureId id : model_pane->figure_indices())
        {
            if (id == 0 || id == INVALID_FIGURE_ID)
                continue;

            Figure* figure = registry_->get(id);
            if (!figure)
                continue;

            auto input = std::make_unique<InputHandler>();
            input->set_figure(figure);
            input->set_axis_link_manager(axis_link_manager_);
            if (!figure->axes().empty() && figure->axes()[0])
                input->set_active_axes_base(figure->axes()[0].get());
            else if (!figure->all_axes().empty() && figure->all_axes()[0])
                input->set_active_axes_base(figure->all_axes()[0].get());

            auto* canvas = new FigureCanvasWidget(runtime_, figure, input.get(), pane->tab_widget);
            canvas->setObjectName(QString("canvas_%1").arg(id));
            connect(canvas,
                    &FigureCanvasWidget::activated,
                    this,
                    [this, id]() { activate_figure(id); });
            emit canvas_created(id, canvas);

            std::string title = figure->tab_title();
            if (title.empty())
                title = "Figure " + std::to_string(id);
            const int tab_index = pane->tab_widget->addTab(canvas, QString::fromStdString(title));
            pane->tab_widget->tabBar()->setTabData(tab_index, QVariant::fromValue<qulonglong>(id));

            PaneWidget::FigureTab tab;
            tab.id     = id;
            tab.canvas = canvas;
            tab.input  = std::move(input);
            pane->figure_tabs.emplace(id, std::move(tab));
            canvas->startAnimationTimer();
        }

        const FigureId active    = model_pane->figure_index();
        auto           active_it = pane->figure_tabs.find(active);
        if (active_it != pane->figure_tabs.end())
        {
            pane->active_id = active;
            pane->tab_widget->setCurrentWidget(active_it->second.canvas);
        }
    }
}

void QtSplitViewContainer::update_active_from_focus()
{
    QWidget* focused = QApplication::focusWidget();
    for (auto* pane : panes_)
    {
        auto* tw = pane ? pane->tab_widget : nullptr;
        if (tw && (tw->hasFocus() || (focused && tw->isAncestorOf(focused))))
        {
            int idx = tw->currentIndex();
            if (idx >= 0)
            {
                QWidget* w = tw->widget(idx);
                for (const auto& [fid, tab] : pane->figure_tabs)
                {
                    if (tab.canvas == w)
                    {
                        split_view_->set_active_figure_index(fid);
                        emit figure_activated(fid);
                        return;
                    }
                }
            }
        }
    }
}

// ─── Private: pane helpers ────────────────────────────────────────────────────

QtSplitViewContainer::PaneWidget* QtSplitViewContainer::active_pane() const
{
    if (panes_.empty())
        return nullptr;

    // A native QWindow container normally owns focus rather than QTabWidget.
    // Treat every focused descendant as selecting its pane.
    QWidget* focused = QApplication::focusWidget();
    for (auto* pane : panes_)
    {
        if (pane->tab_widget
            && (pane->tab_widget->hasFocus()
                || (focused && pane->tab_widget->isAncestorOf(focused))))
            return pane;
    }

    // Fall back to the pane containing the active figure
    FigureId active = split_view_->active_figure_index();
    if (active != INVALID_FIGURE_ID)
    {
        for (auto* pane : panes_)
        {
            if (pane->figure_tabs.count(active) > 0)
                return pane;
        }
    }

    return panes_[0];
}

QtSplitViewContainer::PaneWidget* QtSplitViewContainer::pane_for_figure(FigureId id) const
{
    for (auto* pane : panes_)
    {
        if (pane->figure_tabs.count(id) > 0)
            return pane;
    }
    return nullptr;
}

int QtSplitViewContainer::find_pane_index(PaneWidget* pane) const
{
    for (size_t i = 0; i < panes_.size(); ++i)
    {
        if (panes_[i] == pane)
            return static_cast<int>(i);
    }
    return -1;
}

// ─── Private slots ────────────────────────────────────────────────────────────

void QtSplitViewContainer::on_tab_changed(int index)
{
    if (index < 0)
        return;

    auto* tw = qobject_cast<QTabWidget*>(sender());
    if (!tw)
        return;

    QWidget* widget = tw->widget(index);
    for (auto* pane : panes_)
    {
        if (pane->tab_widget != tw)
            continue;

        for (const auto& [id, tab] : pane->figure_tabs)
        {
            if (tab.canvas == widget)
            {
                pane->active_id = id;
                split_view_->set_active_figure_index(id);
                emit figure_activated(id);
                return;
            }
        }
    }
}

void QtSplitViewContainer::on_tab_close_requested(int index)
{
    auto* tw = qobject_cast<QTabWidget*>(sender());
    if (!tw)
        return;

    QWidget* widget = tw->widget(index);
    for (auto* pane : panes_)
    {
        if (pane->tab_widget != tw)
            continue;

        for (const auto& [id, tab] : pane->figure_tabs)
        {
            if (tab.canvas == widget)
            {
                close_figure_tab(id);
                return;
            }
        }
    }
}

void QtSplitViewContainer::on_tab_context_menu(const QPoint& pos)
{
    auto* tw = qobject_cast<QTabWidget*>(sender());
    if (!tw)
        return;

    int index = tw->tabBar()->tabAt(pos);
    if (index < 0)
        return;

    QWidget* widget = tw->widget(index);
    FigureId fid    = INVALID_FIGURE_ID;
    for (auto* pane : panes_)
    {
        if (pane->tab_widget != tw)
            continue;
        for (const auto& [id, tab] : pane->figure_tabs)
        {
            if (tab.canvas == widget)
            {
                fid = id;
                break;
            }
        }
        if (fid != INVALID_FIGURE_ID)
            break;
    }

    if (fid == INVALID_FIGURE_ID)
        return;

    QMenu menu(this);
    menu.setObjectName("split_tab_context_menu");

    if (detached_host_)
    {
        auto* redock_action = menu.addAction("Move to Main Window");
        redock_action->setObjectName("split_tab_redock");
        connect(redock_action,
                &QAction::triggered,
                this,
                [this, fid]() { emit figure_redock_requested(fid); });
    }
    else
    {
        auto* detach_action = menu.addAction("Detach to New Window");
        detach_action->setObjectName("split_tab_detach");
        connect(detach_action,
                &QAction::triggered,
                this,
                [this, fid]() { emit figure_detach_requested(fid); });
    }

    menu.addSeparator();

    if (is_split())
    {
        auto* close_split_action = menu.addAction("Close Split Pane");
        close_split_action->setObjectName("split_tab_close_split");
        connect(close_split_action,
                &QAction::triggered,
                this,
                [this, fid]()
                {
                    close_figure_tab(fid);
                    close_split();
                });
    }

    auto* close_action = menu.addAction("Close");
    close_action->setObjectName("split_tab_close");
    connect(close_action, &QAction::triggered, this, [this, fid]() { close_figure_tab(fid); });

    menu.exec(tw->mapToGlobal(pos));
}

}   // namespace spectra::adapters::qt
