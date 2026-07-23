// spectra_nav_rail.cpp — Navigation rail implementation.

#include "spectra_nav_rail.hpp"
#include "spectra_nav_button.hpp"
#include "spectra_design_tokens.hpp"
#include "ui/theme/icons.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QVBoxLayout>

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
    {ui::Icon::Last, nullptr, nullptr, true},
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
    explicit NavSeparator(QWidget* parent) : QWidget(parent) { setFixedHeight(7); }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QColor line = spectra_colors().border_subtle;
        line.setAlpha(50);
        QPainter painter(this);
        painter.setPen(QPen(line, 1));
        painter.drawLine(16, height() / 2, width() - 16, height() / 2);
    }
};

SpectraNavRail::~SpectraNavRail() = default;

SpectraNavRail::SpectraNavRail(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(spectra_geometry().nav_rail_width);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 12, 0, 12);
    layout_->setSpacing(0);

    build_buttons();

    // Set Pan as default active
    set_active_tool(1);
}

void SpectraNavRail::build_buttons()
{
    for (int i = 0; i < nav_item_count; ++i)
    {
        if (nav_items[i].separator)
        {
            layout_->addWidget(new NavSeparator(this));
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
                [this, index]()
                {
                    set_active_tool(index);
                    emit tool_selected(index);
                });

        layout_->addWidget(btn, 1);
        buttons_.append(btn);
    }
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
        buttons_[tool_index]->setVisible(visible);
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
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto& colors = spectra_colors();
    painter.fillRect(rect(), colors.header_surface);

    const QRectF    rail = QRectF(rect()).adjusted(4.5, 3.5, -4.5, -3.5);
    QLinearGradient glass(rail.topLeft(), rail.topRight());
    glass.setColorAt(0.0, QColor(17, 27, 43, 246));
    glass.setColorAt(0.70, QColor(20, 27, 45, 246));
    glass.setColorAt(1.0, QColor(29, 24, 48, 246));
    painter.setBrush(glass);
    painter.setPen(QPen(QColor(55, 72, 104, 190), 1));
    painter.drawRoundedRect(rail, 8, 8);

    QLinearGradient edge(rail.topRight(), rail.bottomRight());
    edge.setColorAt(0.0, QColor(107, 199, 242, 70));
    edge.setColorAt(1.0, QColor(124, 92, 252, 70));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QBrush(edge), 1));
    painter.drawLine(QPointF(rail.right(), rail.top() + 8),
                     QPointF(rail.right(), rail.bottom() - 8));
}

}   // namespace spectra::adapters::qt
