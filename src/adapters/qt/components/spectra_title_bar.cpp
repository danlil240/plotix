// spectra_title_bar.cpp — Custom frameless title bar implementation.

#include "spectra_title_bar.hpp"
#include "spectra_design_tokens.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QWindow>

namespace spectra::adapters::qt
{

SpectraTitleBar::~SpectraTitleBar() = default;

SpectraTitleBar::SpectraTitleBar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(spectra_geometry().title_bar_height);
    setAttribute(Qt::WA_StyledBackground, true);

    build_buttons();
}

void SpectraTitleBar::build_buttons()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Left spacer (balances centered title)
    layout->addStretch();

    // Centered title
    // Title is painted in paintEvent, no label needed

    // Right spacer to balance
    layout->addStretch();

    // Window control buttons
    const int btn_size = spectra_geometry().title_bar_height;

    auto make_btn = [this, btn_size](const QString& text) -> QPushButton*
    {
        auto* btn = new QPushButton(text, this);
        btn->setFixedSize(btn_size, btn_size);
        btn->setFlat(true);
        btn->setFocusPolicy(Qt::StrongFocus);
        btn->setCursor(Qt::ArrowCursor);
        return btn;
    };

    btn_min_   = make_btn(QStringLiteral("\u2500"));   // horizontal bar
    btn_max_   = make_btn(QStringLiteral("\u25A1"));   // white square
    btn_close_ = make_btn(QStringLiteral("\u2715"));   // X mark
    btn_min_->setObjectName("window_minimize_button");
    btn_max_->setObjectName("window_maximize_button");
    btn_close_->setObjectName("window_close_button");
    btn_min_->setAccessibleName("Minimize window");
    btn_max_->setAccessibleName("Maximize or restore window");
    btn_close_->setAccessibleName("Close window");

    layout->addWidget(btn_min_);
    layout->addWidget(btn_max_);
    layout->addWidget(btn_close_);

    connect(btn_min_, &QPushButton::clicked, this, &SpectraTitleBar::on_minimize);
    connect(btn_max_, &QPushButton::clicked, this, &SpectraTitleBar::on_maximize);
    connect(btn_close_, &QPushButton::clicked, this, &SpectraTitleBar::on_close);

    // Style buttons
    auto style_btn = [](QPushButton* btn)
    {
        btn->setStyleSheet(QString("QPushButton {"
                                   "  background: transparent;"
                                   "  border: none;"
                                   "  padding: 0;"
                                   "  font-size: 11px;"
                                   "  font-family: 'Inter';"
                                   "}"));
    };

    style_btn(btn_min_);
    style_btn(btn_max_);
    style_btn(btn_close_);
}

void SpectraTitleBar::set_title(const QString& title)
{
    title_ = title;
    update();
}

void SpectraTitleBar::set_window(QWidget* window)
{
    window_ = window;
}

int SpectraTitleBar::height_hint() const
{
    return spectra_geometry().title_bar_height;
}

void SpectraTitleBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();
    const auto& g = spectra_geometry();

    // Background
    p.fillRect(rect(), c.window_base);

    // Bottom hairline border
    p.setPen(QPen(c.border_subtle, 1));
    p.drawLine(0, height() - 1, width(), height() - 1);

    // Centered title
    auto& fm = SpectraFontManager::instance();
    p.setFont(fm.font_small());
    p.setPen(c.text_muted);
    QRect text_rect(0, 0, width() - g.title_bar_height * 3, height());
    p.drawText(text_rect, Qt::AlignCenter, title_);
}

void SpectraTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        // Start system move for Wayland/X11 compatibility
        if (window_ && window_->windowHandle())
        {
            window_->windowHandle()->startSystemMove();
        }
    }
    QWidget::mousePressEvent(event);
}

void SpectraTitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        on_maximize();
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SpectraTitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
}

void SpectraTitleBar::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
}

void SpectraTitleBar::on_minimize()
{
    if (window_)
        window_->showMinimized();
    emit minimized();
}

void SpectraTitleBar::on_maximize()
{
    if (!window_)
        return;

    if (window_->isMaximized())
    {
        window_->showNormal();
        is_maximized_ = false;
    }
    else
    {
        window_->showMaximized();
        is_maximized_ = true;
    }
    emit maximized();
}

void SpectraTitleBar::on_close()
{
    if (window_)
        window_->close();
    emit closed();
}

}   // namespace spectra::adapters::qt
