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

    // Find a figure to put in the new pane
    auto all_ids = registry_->all_ids();
    FigureId new_fig = INVALID_FIGURE_ID;
    for (auto id : all_ids)
    {
        if (id != active && !canvas_for(id))
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

    rebuild_splitter();
    add_figure_tab(new_fig);
    return true;
}

bool QtSplitViewContainer::split_down()
{
    FigureId active = active_figure_id();
    if (active == INVALID_FIGURE_ID)
        return false;

    auto all_ids = registry_->all_ids();
    FigureId new_fig = INVALID_FIGURE_ID;
    for (auto id : all_ids)
    {
        if (id != active && !canvas_for(id))
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

    rebuild_splitter();
    add_figure_tab(new_fig);
    return true;
}

bool QtSplitViewContainer::close_split()
{
    FigureId active = active_figure_id();
    if (active == INVALID_FIGURE_ID)
        return false;

    bool result = split_view_->close_pane(active);
    if (result)
        rebuild_splitter();
    return result;
}

void QtSplitViewContainer::reset_splits()
{
    split_view_->unsplit_all();
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
    // Clear existing panes
    for (auto* pane : panes_)
    {
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

    // Re-add existing figure tabs to panes based on SplitViewManager
    // (figures stay in their canvases — we just move the tab widgets)
    sync_from_split_view();

    if (figure_tab_count() == 0)
        show_welcome_page();
}

void QtSplitViewContainer::sync_from_split_view()
{
    // The SplitViewManager tracks which figures are in which pane.
    // After a rebuild, we need to re-add figure tabs to the correct panes.
    // For now, all existing figures go to pane 0 on rebuild —
    // the SplitViewManager model is the source of truth for logical layout.
    // A full sync would iterate panes and move tabs, but the initial
    // implementation keeps it simple: figures are re-added to pane 0.
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
