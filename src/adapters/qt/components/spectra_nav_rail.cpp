// spectra_nav_rail.cpp — Navigation rail implementation.

#include "spectra_nav_rail.hpp"
#include "spectra_nav_button.hpp"
#include "spectra_design_tokens.hpp"
#include "ui/layout/layout_manager.hpp"
#include "ui/theme/design_tokens.hpp"
#include "ui/theme/icons.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace spectra::adapters::qt
{

struct NavItem
{
    ui::Icon    icon;
    const char* label;
    const char* shortcut;
    bool        separator;
};

static const NavItem nav_items[] = {
    {ui::Icon::MousePointer, "Select", "V", false},
    {ui::Icon::Hand, "Pan", "H", false},
    {ui::Icon::ZoomIn, "Zoom", "Z", false},
    {ui::Icon::Last, nullptr, nullptr, true},
    {ui::Icon::Ruler, "Measure", "M", false},
    {ui::Icon::Comment, "Annotate", "A", false},
    {ui::Icon::VectorSquare, "ROI", "R", false},
    {ui::Icon::Last, nullptr, nullptr, true},
    {ui::Icon::MapPin, "Markers", "K", false},
    {ui::Icon::MagicWand, "Transform", "T", false},
    // Legacy SpectraNavRail::build_items emits exactly three separators
    // (sep1/sep2/sep3); a fourth here produced a double gap before Inspector.
    {ui::Icon::Last, nullptr, nullptr, true},
    {ui::Icon::Axes, "Inspector", "I", false},
    {ui::Icon::Timeline, "Timeline", "L", false},
    {ui::Icon::ChartLine, "Curve Editor", "C", false},
    {ui::Icon::Wrench, "Plugins", "P", false},
    {ui::Icon::Broadcast, "Topics", "O", false},
    {ui::Icon::Settings, "Settings", ",", false},
    {ui::Icon::Help, "Help", "?", false},
};

static constexpr int nav_item_count = sizeof(nav_items) / sizeof(nav_items[0]);

class NavSeparator : public QWidget
{
   public:
    explicit NavSeparator(QWidget* parent) : QWidget(parent)
    {
        setFixedHeight(static_cast<int>(LayoutManager::NAV_RAIL_SEPARATOR_HEIGHT));
    }

    void set_scale(float scale)
    {
        scale_ = scale;
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        // Legacy draws the hairline inset by SPACE_4 * scale at alpha 32.
        QColor line = spectra_colors().border_subtle;
        line.setAlpha(32);
        const int inset = qRound(ui::tokens::SPACE_4 * scale_);
        QPainter  painter(this);
        painter.setPen(QPen(line, 1));
        painter.drawLine(inset, height() / 2, width() - inset, height() / 2);
    }

   private:
    float scale_ = 1.0f;
};

SpectraNavRail::~SpectraNavRail() = default;

SpectraNavRail::SpectraNavRail(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(spectra_geometry().nav_rail_width);

    layout_ = new QVBoxLayout(this);
    // Legacy uses NAV_RAIL_VERTICAL_PADDING split across top and bottom.
    const int pad = static_cast<int>(LayoutManager::NAV_RAIL_VERTICAL_PADDING) / 2;
    layout_->setContentsMargins(0, pad, 0, pad);
    layout_->setSpacing(0);

    build_buttons();

    // Legacy packs cells from the top; remaining space stays empty.
    layout_->addStretch(1);

    // Set Pan as default active
    set_active_tool(1);

    // Initial cell sizes are applied on the first resize.
    apply_cell_metrics();
}

void SpectraNavRail::build_buttons()
{
    for (int i = 0; i < nav_item_count; ++i)
    {
        if (nav_items[i].separator)
        {
            auto* sep = new NavSeparator(this);
            layout_->addWidget(sep);
            separators_.append(sep);
            continue;
        }

        const auto codepoint = static_cast<uint32_t>(nav_items[i].icon);
        auto*      btn       = new SpectraNavButton(SpectraFontManager::icon_codepoint(codepoint),
                                         QString::fromUtf8(nav_items[i].label),
                                         QString::fromUtf8(nav_items[i].shortcut),
                                         this);

        const int index = buttons_.size();
        connect(btn,
                &SpectraNavButton::clicked,
                this,
                [this, index]() { emit tool_selected(index); });

        layout_->addWidget(btn);
        buttons_.append(btn);
    }
}

void SpectraNavRail::apply_cell_metrics()
{
    if (height() <= 0 || buttons_.isEmpty())
        return;

    int visible_buttons = 0;
    int visible_seps    = 0;
    for (const auto* btn : buttons_)
        if (!btn->isHidden())
            ++visible_buttons;
    for (const auto* sep : separators_)
        if (!sep->isHidden())
            ++visible_seps;

    if (visible_buttons == 0)
        return;

    const float available       = static_cast<float>(height());
    const float pad             = LayoutManager::NAV_RAIL_VERTICAL_PADDING;
    const float nominal_content = visible_buttons * LayoutManager::NAV_RAIL_CELL_HEIGHT
                                  + visible_seps * LayoutManager::NAV_RAIL_SEPARATOR_HEIGHT;
    const float min_scale =
        LayoutManager::NAV_RAIL_CELL_HEIGHT_MIN / LayoutManager::NAV_RAIL_CELL_HEIGHT;

    // Start from the legacy scale model, then clamp to the actual content area
    // so short windows never clip the trailing buttons.
    float scale =
        LayoutManager::nav_rail_scale_for_height(available, visible_buttons, visible_seps);
    if (nominal_content > 0.0f)
    {
        const float fitting_scale = std::max(0.0f, (available - pad) / nominal_content);
        scale                     = std::min(scale, fitting_scale);
    }
    scale = std::max(min_scale, std::min(1.0f, scale));

    const int cell_h = qRound(LayoutManager::NAV_RAIL_CELL_HEIGHT * scale);
    const int sep_h  = qRound(LayoutManager::NAV_RAIL_SEPARATOR_HEIGHT * scale);

    for (auto* btn : buttons_)
    {
        if (!btn->isHidden())
            btn->setFixedHeight(cell_h);
    }
    for (auto* sep : separators_)
    {
        sep->set_scale(scale);
        if (!sep->isHidden())
            sep->setFixedHeight(sep_h);
    }
}

void SpectraNavRail::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    apply_cell_metrics();
}

void SpectraNavRail::set_active_tool(int tool_index)
{
    active_ = tool_index;
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->set_active(i == tool_index);
}

void SpectraNavRail::set_button_visible(int tool_index, bool visible)
{
    if (tool_index >= 0 && tool_index < buttons_.size())
    {
        buttons_[tool_index]->setVisible(visible);
        apply_cell_metrics();
    }
}

void SpectraNavRail::set_button_active(int tool_index, bool active)
{
    if (tool_index >= 0 && tool_index < buttons_.size())
        buttons_[tool_index]->set_active(active);
}

void SpectraNavRail::set_compact_mode(bool compact)
{
    compact_      = compact;
    const auto& g = spectra_geometry();
    setFixedWidth(compact ? g.nav_rail_width_compact : g.nav_rail_width);

    for (auto* btn : buttons_)
        btn->set_compact_mode(compact);
}

int SpectraNavRail::width_hint() const
{
    const auto& g = spectra_geometry();
    return compact_ ? g.nav_rail_width_compact : g.nav_rail_width;
}

void SpectraNavRail::paintEvent(QPaintEvent*)
{
    QPainter painter(this);

    // Legacy `SpectraNavRail::draw` renders the rail with NoBackground: the tool
    // cells sit directly on the window surface. Only a right-edge hairline (plus
    // a shadow cast onto the canvas, which belongs to the neighbouring widget)
    // separates it from the workspace — no card, gradient, or accent edge.
    const auto& colors = spectra_colors();
    painter.fillRect(rect(), colors.window_base);

    QColor hairline = colors.border_subtle;
    hairline.setAlphaF(0.52f);
    painter.setPen(QPen(hairline, 1));
    painter.drawLine(width() - 1, 0, width() - 1, height());
}

}   // namespace spectra::adapters::qt
