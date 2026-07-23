// spectra_document_tab_bar.cpp — Custom document tab bar implementation.

#include "spectra_document_tab_bar.hpp"
#include "spectra_design_tokens.hpp"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>

namespace spectra::adapters::qt
{

SpectraDocumentTabBar::~SpectraDocumentTabBar() = default;

SpectraDocumentTabBar::SpectraDocumentTabBar(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(spectra_geometry().tab_bar_height);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
}

void SpectraDocumentTabBar::add_tab(const QString& title, int id)
{
    for (auto& existing : tabs_)
    {
        if (existing.id == id)
        {
            existing.title = title;
            active_id_     = id;
            update();
            return;
        }
    }

    SpectraTab tab;
    tab.title = title;
    tab.id    = id;
    tabs_.append(tab);

    if (active_id_ < 0)
        active_id_ = id;

    update();
}

void SpectraDocumentTabBar::remove_tab(int id)
{
    for (int i = 0; i < tabs_.size(); ++i)
    {
        if (tabs_[i].id == id)
        {
            tabs_.removeAt(i);
            if (active_id_ == id)
            {
                active_id_ = tabs_.isEmpty() ? -1 : tabs_.first().id;
            }
            break;
        }
    }
    update();
}

void SpectraDocumentTabBar::set_active_tab(int id)
{
    active_id_ = id;
    update();
}

void SpectraDocumentTabBar::set_tab_title(int id, const QString& title)
{
    for (auto& tab : tabs_)
    {
        if (tab.id == id)
        {
            tab.title = title;
            break;
        }
    }
    update();
}

void SpectraDocumentTabBar::set_tab_modified(int id, bool modified)
{
    for (auto& tab : tabs_)
    {
        if (tab.id == id)
        {
            tab.modified = modified;
            break;
        }
    }
    update();
}

int SpectraDocumentTabBar::height_hint() const
{
    return spectra_geometry().tab_bar_height;
}

void SpectraDocumentTabBar::update_layout()
{
    tab_layouts_.clear();

    auto& fm       = SpectraFontManager::instance();
    QFont tab_font = fm.font_base();
    tab_font.setPixelSize(14);

    int       x              = 2;
    const int tab_h          = height();
    const int min_w          = 60;
    const int max_w          = 150;
    const int close_btn_size = 12;

    for (int i = 0; i < tabs_.size(); ++i)
    {
        QFontMetrics metrics(tab_font);
        int          text_w = metrics.horizontalAdvance(tabs_[i].title);
        int          tab_w  = qBound(min_w, text_w + 16 + close_btn_size, max_w);

        TabLayout tl;
        tl.rect       = QRect(x, 0, tab_w, tab_h);
        tl.close_rect = QRect(x + tab_w - close_btn_size - 4,
                              (tab_h - close_btn_size) / 2,
                              close_btn_size,
                              close_btn_size);
        tl.index      = i;
        tab_layouts_.append(tl);

        x += tab_w + 1;
    }

    // Add button
    add_btn_rect_ = QRect(x, 0, 22, tab_h);
}

int SpectraDocumentTabBar::tab_at(const QPoint& pos) const
{
    for (int i = 0; i < tab_layouts_.size(); ++i)
    {
        if (tab_layouts_[i].rect.contains(pos))
            return i;
    }
    return -1;
}

bool SpectraDocumentTabBar::is_add_button(const QPoint& pos) const
{
    return add_btn_rect_.contains(pos);
}

void SpectraDocumentTabBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c  = spectra_colors();
    auto&       fm = SpectraFontManager::instance();

    // Background
    p.fillRect(rect(), c.workspace_surface);

    // Bottom hairline
    p.setPen(QPen(c.border_subtle, 1));
    p.drawLine(0, height() - 1, width(), height() - 1);

    update_layout();

    // Draw tabs
    QFont tab_font = fm.font_base();
    tab_font.setPixelSize(14);
    p.setFont(tab_font);

    for (int i = 0; i < tab_layouts_.size(); ++i)
    {
        const auto& tl        = tab_layouts_[i];
        const auto& tab       = tabs_[i];
        bool        is_active = (tab.id == active_id_);
        bool        is_hover  = (i == hover_tab_);

        // Tab background
        if (is_active)
        {
            QColor active_bg = c.elevated_surface;
            active_bg = QColor::fromRgbF(active_bg.redF() * 0.84 + c.cyan_accent.redF() * 0.16,
                                         active_bg.greenF() * 0.84 + c.cyan_accent.greenF() * 0.16,
                                         active_bg.blueF() * 0.84 + c.cyan_accent.blueF() * 0.16,
                                         0.96);
            p.setBrush(active_bg);
            p.setPen(QPen(c.border_default, 1));
            p.drawRoundedRect(QRectF(tl.rect).adjusted(0.5, 1.5, -0.5, 0.5), 4, 4);
        }
        else if (is_hover)
        {
            p.fillRect(tl.rect,
                       QColor(c.elevated_surface.red(),
                              c.elevated_surface.green(),
                              c.elevated_surface.blue(),
                              120));
        }

        // Tab text
        QColor text_color = is_active ? c.text_primary : c.text_muted;
        p.setPen(text_color);

        QRect text_rect = tl.rect.adjusted(8, 0, -18, 0);
        p.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, tab.title);

        // Close button (X)
        if (is_active || is_hover)
        {
            QColor close_color = c.text_muted;
            close_color.setAlpha(is_hover ? 216 : 128);
            p.setPen(QPen(close_color, 1.5));
            const QPoint center = tl.close_rect.center();
            p.drawLine(center.x() - 3, center.y() - 3, center.x() + 3, center.y() + 3);
            p.drawLine(center.x() - 3, center.y() + 3, center.x() + 3, center.y() - 3);
        }

        // Active tab underline (cyan)
        if (is_active)
        {
            p.setPen(Qt::NoPen);
            p.setBrush(c.cyan_accent);
            QRect underline(tl.rect.x() + 5, height() - 2, tl.rect.width() - 10, 2);
            p.drawRect(underline);
        }
    }

    // Add (+) button
    p.setPen(c.text_muted);
    p.setFont(fm.font_base());
    p.drawText(add_btn_rect_, Qt::AlignCenter, QStringLiteral("+"));
}

void SpectraDocumentTabBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    QPoint pos = event->pos();

    // Check add button
    if (is_add_button(pos))
    {
        emit tab_add_requested();
        return;
    }

    // Check close button
    for (int i = 0; i < tab_layouts_.size(); ++i)
    {
        if (tab_layouts_[i].close_rect.contains(pos))
        {
            emit tab_closed(tabs_[i].id);
            return;
        }
    }

    // Check tab selection
    int idx = tab_at(pos);
    if (idx >= 0 && idx < tabs_.size())
    {
        set_active_tab(tabs_[idx].id);
        emit tab_selected(tabs_[idx].id);

        // Start potential drag
        drag_tab_   = idx;
        drag_start_ = pos;
    }
}

void SpectraDocumentTabBar::mouseMoveEvent(QMouseEvent* event)
{
    QPoint pos = event->pos();

    int new_hover = tab_at(pos);
    if (new_hover != hover_tab_)
    {
        hover_tab_ = new_hover;
        update();
    }

    // Check for detach drag
    if (drag_tab_ >= 0 && (event->buttons() & Qt::LeftButton))
    {
        int distance = (pos - drag_start_).manhattanLength();
        if (distance > 40)
        {
            emit tab_detach_requested(tabs_[drag_tab_].id);
            drag_tab_ = -1;
        }
    }
}

void SpectraDocumentTabBar::mouseReleaseEvent(QMouseEvent*)
{
    drag_tab_ = -1;
}

}   // namespace spectra::adapters::qt
