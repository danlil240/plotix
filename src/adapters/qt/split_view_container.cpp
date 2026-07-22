// split_view_container.cpp — Qt split-pane central widget implementation.

#include "split_view_container.hpp"

#include "figure_canvas_widget.hpp"
#include "qt_runtime.hpp"

#include "ui/docking/split_view.hpp"
#include "ui/input/input.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace spectra::adapters::qt
{

// ─── PaneWidget ──────────────────────────────────────────────────────────────
// A single pane in the split view — a QTabWidget holding figure canvases.

struct QtSplitViewContainer::PaneWidget
{
    QTabWidget*          tab_widget = nullptr;
    FigureId             active_id  = INVALID_FIGURE_ID;

    struct FigureTab
    {
        FigureId              id = INVALID_FIGURE_ID;
        FigureCanvasWidget*   canvas  = nullptr;
        std::unique_ptr<InputHandler> input;
    };
    std::unordered_map<FigureId, FigureTab> figure_tabs;
};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

QtSplitViewContainer::QtSplitViewContainer(QtRuntime*      runtime,
                                           FigureRegistry* registry,
                                           QWidget*        parent)
    : QWidget(parent),
      runtime_(runtime),
      registry_(registry),
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
    PaneWidget* pane = active_pane();
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
    if (auto* model_pane = split_view_->active_pane())
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
    if (!figure->axes().empty() && figure->axes()[0])
        input->set_active_axes_base(figure->axes()[0].get());

    auto* canvas = new FigureCanvasWidget(runtime_, figure, input.get(), pane->tab_widget);
    canvas->setObjectName(QString("canvas_%1").arg(id));

    std::string title = figure->tab_title();
    if (title.empty())
        title = "Figure " + std::to_string(id);

    int idx = pane->tab_widget->addTab(canvas, QString::fromStdString(title));

    PaneWidget::FigureTab tab;
    tab.id     = id;
    tab.canvas = canvas;
    tab.input  = std::move(input);
    pane->figure_tabs[id] = std::move(tab);
    pane->active_id = id;

    canvas->startAnimationTimer();

    hide_welcome_page();
    pane->tab_widget->setCurrentIndex(idx);
    split_view_->set_active_figure_index(id);

    SPECTRA_LOG_INFO("qt_split_view", "Added figure tab: id={} pane={}", id, find_pane_index(pane));
    return idx;
}

void QtSplitViewContainer::close_figure_tab(FigureId id)
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
        return;
    }
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
    for (auto* pane : panes_)
    {
        for (const auto& [id, tab] : pane->figure_tabs)
            ids.push_back(id);
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

void QtSplitViewContainer::set_active_tool(ToolMode tool)
{
    PaneWidget* pane = active_pane();
    if (!pane)
        return;

    const FigureId id = active_figure_id();
    auto it = pane->figure_tabs.find(id);
    if (it != pane->figure_tabs.end() && it->second.input)
        it->second.input->set_tool_mode(tool);
}

ToolMode QtSplitViewContainer::active_tool() const
{
    PaneWidget* pane = active_pane();
    if (!pane)
        return ToolMode::Pan;

    const FigureId id = active_figure_id();
    auto it = pane->figure_tabs.find(id);
    if (it == pane->figure_tabs.end() || !it->second.input)
        return ToolMode::Pan;
    return it->second.input->tool_mode();
}

// ─── Welcome page ─────────────────────────────────────────────────────────────

void QtSplitViewContainer::show_welcome_page()
{
    if (!welcome_page_)
    {
        welcome_page_ = new QWidget(this);
        welcome_page_->setObjectName("welcome_page");

        auto* layout = new QVBoxLayout(welcome_page_);
        layout->setAlignment(Qt::AlignCenter);

        auto* title = new QLabel("Spectra", welcome_page_);
        QFont title_font = title->font();
        title_font.setPointSize(32);
        title_font.setBold(true);
        title->setFont(title_font);
        title->setAlignment(Qt::AlignCenter);

        auto* subtitle = new QLabel("Scientific plotting, accelerated.", welcome_page_);
        QFont sub_font = subtitle->font();
        sub_font.setPointSize(14);
        subtitle->setFont(sub_font);
        subtitle->setAlignment(Qt::AlignCenter);
        subtitle->setStyleSheet("color: gray;");

        auto* hint = new QLabel("Create a new figure or open a file to begin.",
                                welcome_page_);
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet("color: gray; padding-top: 20px;");

        layout->addWidget(title);
        layout->addWidget(subtitle);
        layout->addWidget(hint);
    }

    // Add welcome page to the first pane if no tabs exist
    if (figure_tab_count() == 0 && !panes_.empty())
    {
        auto* pane = panes_[0];
        int idx = pane->tab_widget->indexOf(welcome_page_);
        if (idx < 0)
        {
            pane->tab_widget->addTab(welcome_page_, "");
            pane->tab_widget->tabBar()->setTabVisible(pane->tab_widget->count() - 1, false);
        }
    }
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
    auto all_ids = registry_->all_ids();
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

    auto all_ids = registry_->all_ids();
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
    bool result = split_view_->close_pane(active);
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
            const auto active_it = std::find(merged_ids.begin(), merged_ids.end(), active);
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
            auto it = std::find(ids.begin(), ids.end(), active);
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

    auto all_panes = split_view_->all_panes();
    if (all_panes.empty())
    {
        // Create a single default pane
        auto* pane = new PaneWidget();
        pane->tab_widget = new QTabWidget(splitter_);
        pane->tab_widget->setTabsClosable(true);
        pane->tab_widget->setMovable(true);
        pane->tab_widget->setObjectName("split_pane_0");
        pane->tab_widget->tabBar()->setVisible(is_split());

        connect(pane->tab_widget, &QTabWidget::currentChanged,
                this, &QtSplitViewContainer::on_tab_changed);
        connect(pane->tab_widget, &QTabWidget::tabCloseRequested,
                this, &QtSplitViewContainer::on_tab_close_requested);
        pane->tab_widget->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(pane->tab_widget, &QWidget::customContextMenuRequested,
                this, &QtSplitViewContainer::on_tab_context_menu);

        splitter_->addWidget(pane->tab_widget);
        panes_.push_back(pane);
    }
    else
    {
        // Determine orientation from the root split
        auto* root = split_view_->root();
        bool horizontal = root && !root->is_leaf()
                          && root->split_direction() == SplitDirection::Horizontal;

        splitter_->setOrientation(horizontal ? Qt::Horizontal : Qt::Vertical);

        for (size_t i = 0; i < all_panes.size(); ++i)
        {
            auto* pane = new PaneWidget();
            pane->tab_widget = new QTabWidget(splitter_);
            pane->tab_widget->setTabsClosable(true);
            pane->tab_widget->setMovable(true);
            pane->tab_widget->setObjectName(QString("split_pane_%1").arg(i));
            pane->tab_widget->tabBar()->setVisible(is_split());

            connect(pane->tab_widget, &QTabWidget::currentChanged,
                    this, &QtSplitViewContainer::on_tab_changed);
            connect(pane->tab_widget, &QTabWidget::tabCloseRequested,
                    this, &QtSplitViewContainer::on_tab_close_requested);
            pane->tab_widget->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(pane->tab_widget, &QWidget::customContextMenuRequested,
                    this, &QtSplitViewContainer::on_tab_context_menu);

            splitter_->addWidget(pane->tab_widget);
            panes_.push_back(pane);
        }

        // Set equal sizes
        QList<int> sizes;
        int total = splitter_->orientation() == Qt::Horizontal
                        ? splitter_->width() : splitter_->height();
        if (total <= 0)
            total = 800;
        int per = total / static_cast<int>(all_panes.size());
        for (size_t i = 0; i < all_panes.size(); ++i)
            sizes.append(per);
        splitter_->setSizes(sizes);
    }

    const auto model_panes = split_view_->all_panes();
    const size_t pane_total = std::min(model_panes.size(), panes_.size());
    for (size_t pane_index = 0; pane_index < pane_total; ++pane_index)
    {
        SplitPane* model_pane = model_panes[pane_index];
        PaneWidget* pane = panes_[pane_index];
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
            auto preserved = preserved_tabs.find(id);
            if (preserved != preserved_tabs.end())
            {
                tab = std::move(preserved->second);
                preserved_tabs.erase(preserved);
                tab.canvas->setParent(pane->tab_widget);
            }
            else
            {
                tab.id = id;
                tab.input = std::make_unique<InputHandler>();
                tab.input->set_figure(figure);
                if (!figure->axes().empty() && figure->axes()[0])
                    tab.input->set_active_axes_base(figure->axes()[0].get());
                tab.canvas = new FigureCanvasWidget(
                    runtime_, figure, tab.input.get(), pane->tab_widget);
                tab.canvas->setObjectName(QString("canvas_%1").arg(id));
                tab.canvas->startAnimationTimer();
            }

            std::string title = figure->tab_title();
            if (title.empty())
                title = "Figure " + std::to_string(id);
            pane->tab_widget->addTab(tab.canvas, QString::fromStdString(title));
            pane->figure_tabs.emplace(id, std::move(tab));
        }

        const FigureId active = model_pane->figure_index();
        auto active_it = pane->figure_tabs.find(active);
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

    const auto model_panes = split_view_->all_panes();
    const size_t count = std::min(model_panes.size(), panes_.size());
    for (size_t pane_index = 0; pane_index < count; ++pane_index)
    {
        SplitPane* model_pane = model_panes[pane_index];
        PaneWidget* pane      = panes_[pane_index];
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
            if (!figure->axes().empty() && figure->axes()[0])
                input->set_active_axes_base(figure->axes()[0].get());

            auto* canvas = new FigureCanvasWidget(runtime_, figure, input.get(), pane->tab_widget);
            canvas->setObjectName(QString("canvas_%1").arg(id));

            std::string title = figure->tab_title();
            if (title.empty())
                title = "Figure " + std::to_string(id);
            pane->tab_widget->addTab(canvas, QString::fromStdString(title));

            PaneWidget::FigureTab tab;
            tab.id     = id;
            tab.canvas = canvas;
            tab.input  = std::move(input);
            pane->figure_tabs.emplace(id, std::move(tab));
            canvas->startAnimationTimer();
        }

        const FigureId active = model_pane->figure_index();
        auto active_it = pane->figure_tabs.find(active);
        if (active_it != pane->figure_tabs.end())
        {
            pane->active_id = active;
            pane->tab_widget->setCurrentWidget(active_it->second.canvas);
        }
    }
}

void QtSplitViewContainer::update_active_from_focus()
{
    // Determine which pane has focus and update the active figure
    for (int i = 0; i < splitter_->count(); ++i)
    {
        auto* tw = qobject_cast<QTabWidget*>(splitter_->widget(i));
        if (tw && tw->hasFocus())
        {
            int idx = tw->currentIndex();
            if (idx >= 0)
            {
                QWidget* w = tw->widget(idx);
                for (auto* pane : panes_)
                {
                    if (pane->tab_widget == tw)
                    {
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
    }
}

// ─── Private: pane helpers ────────────────────────────────────────────────────

QtSplitViewContainer::PaneWidget* QtSplitViewContainer::active_pane() const
{
    if (panes_.empty())
        return nullptr;

    // Find the pane whose tab widget has focus, or the first pane
    for (auto* pane : panes_)
    {
        if (pane->tab_widget && pane->tab_widget->hasFocus())
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
    FigureId fid = INVALID_FIGURE_ID;
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

    auto* detach_action = menu.addAction("Detach to New Window");
    detach_action->setObjectName("split_tab_detach");
    connect(detach_action, &QAction::triggered, this, [this, fid]() {
        emit figure_detach_requested(fid);
    });

    menu.addSeparator();

    if (is_split())
    {
        auto* close_split_action = menu.addAction("Close Split Pane");
        close_split_action->setObjectName("split_tab_close_split");
        connect(close_split_action, &QAction::triggered, this, [this, fid]() {
            close_figure_tab(fid);
            close_split();
        });
    }

    auto* close_action = menu.addAction("Close");
    close_action->setObjectName("split_tab_close");
    connect(close_action, &QAction::triggered, this, [this, fid]() {
        close_figure_tab(fid);
    });

    menu.exec(tw->mapToGlobal(pos));
}

}   // namespace spectra::adapters::qt
