// spectra_app_header.cpp — Custom application header implementation.

#include "spectra_app_header.hpp"
#include "spectra_design_tokens.hpp"
#include "spectra_menu_strip.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

namespace spectra::adapters::qt
{

// ─── Logo Widget ─────────────────────────────────────────────────────────────

class SpectraLogoWidget : public QWidget
{
public:
    explicit SpectraLogoWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(32, 32);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const auto& c = spectra_colors();

        // Draw a rounded square with gradient
        QRectF r(2, 2, 28, 28);
        QLinearGradient grad(r.topLeft(), r.bottomRight());
        grad.setColorAt(0, c.cyan_accent);
        grad.setColorAt(1, c.purple_ambient);

        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawRoundedRect(r, 8, 8);

        // Draw "S" letter
        auto& fm = SpectraFontManager::instance();
        p.setFont(fm.font_semibold());
        p.setPen(QColor(0x0D, 0x0E, 0x11));
        p.drawText(rect(), Qt::AlignCenter, "S");
    }
};

// ─── Wordmark Widget ─────────────────────────────────────────────────────────

class SpectraWordmarkWidget : public QWidget
{
public:
    explicit SpectraWordmarkWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto& fm = SpectraFontManager::instance();
        QFont f = fm.font_wordmark();
        QFontMetrics metrics(f);
        setFixedWidth(metrics.horizontalAdvance("SPECTRA") + 4);
        setFixedHeight(metrics.height() + 2);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        auto& fm = SpectraFontManager::instance();
        p.setFont(fm.font_wordmark());
        p.setPen(spectra_colors().text_primary);
        p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, "SPECTRA");
    }
};

// ─── Vertical Divider ────────────────────────────────────────────────────────

class SpectraDivider : public QWidget
{
public:
    explicit SpectraDivider(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedWidth(1);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setPen(QPen(spectra_colors().border_default, 1));
        p.drawLine(0, 8, 0, height() - 8);
    }
};

// ─── Home Button ─────────────────────────────────────────────────────────────

class SpectraHomeButton : public QPushButton
{
public:
    explicit SpectraHomeButton(QWidget* parent = nullptr)
        : QPushButton(parent)
    {
        setFixedSize(36, 36);
        setCursor(Qt::PointingHandCursor);
        setToolTip("Home (Ctrl+H)");

        setStyleSheet(QString(
            "QPushButton {"
            "  background: transparent;"
            "  border: none;"
            "  border-radius: %1px;"
            "  color: #C8CDD6;"
            "  font-size: 16px;"
            "  font-family: 'Inter';"
            "}"
            "QPushButton:hover {"
            "  background: #1F2229;"
            "  color: #E8ECF1;"
            "}"
        ).arg(spectra_geometry().radius_md));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        // Draw house icon using QPainter
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const auto& c = spectra_colors();
        bool hovered = underMouse();

        QColor color = hovered ? c.text_primary : c.text_secondary;

        // Draw a simple house icon
        int cx = width() / 2;
        int cy = height() / 2;
        int sz = 14;

        QPainterPath path;
        path.moveTo(cx, cy - sz / 2);                    // top
        path.lineTo(cx + sz / 2, cy);                     // right
        path.lineTo(cx + sz / 2, cy + sz / 2);            // bottom right
        path.lineTo(cx - sz / 2, cy + sz / 2);            // bottom left
        path.lineTo(cx - sz / 2, cy);                     // left
        path.closeSubpath();

        p.fillPath(path, color);
    }
};

// ─── SpectraAppHeader ────────────────────────────────────────────────────────

SpectraAppHeader::~SpectraAppHeader() = default;

SpectraAppHeader::SpectraAppHeader(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(spectra_geometry().header_height);
    setAttribute(Qt::WA_StyledBackground, true);

    build_layout();
}

void SpectraAppHeader::build_layout()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(0);

    // Logo
    logo_widget_ = new SpectraLogoWidget(this);
    layout->addWidget(logo_widget_);

    layout->addSpacing(10);

    // Wordmark
    wordmark_widget_ = new SpectraWordmarkWidget(this);
    layout->addWidget(wordmark_widget_);

    layout->addSpacing(16);

    // Divider
    auto* divider = new SpectraDivider(this);
    layout->addWidget(divider);

    layout->addSpacing(12);

    // Home button
    home_btn_ = new SpectraHomeButton(this);
    connect(home_btn_, &QPushButton::clicked, this, &SpectraAppHeader::home_clicked);
    layout->addWidget(home_btn_);

    layout->addSpacing(16);

    // Menu strip
    menu_strip_ = new SpectraMenuStrip(this);
    layout->addWidget(menu_strip_);

    layout->addStretch();
}

void SpectraAppHeader::set_menu(const QString& label, QMenu* menu)
{
    if (menu_strip_)
        menu_strip_->add_menu_button(label, menu);
}

void SpectraAppHeader::add_menu(const QString& label, QMenu* menu)
{
    if (menu_strip_)
        menu_strip_->add_menu_button(label, menu);
}

int SpectraAppHeader::height_hint() const
{
    return spectra_geometry().header_height;
}

void SpectraAppHeader::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    // Background — dark navy surface
    p.fillRect(rect(), c.header_surface);

    // Ambient glow toward the right side
    // Cyan glow
    QLinearGradient cyan_grad(width() - 300, 0, width(), height());
    cyan_grad.setColorAt(0, QColor(c.cyan_accent.red(), c.cyan_accent.green(),
                                   c.cyan_accent.blue(), 0));
    cyan_grad.setColorAt(1, QColor(c.cyan_accent.red(), c.cyan_accent.green(),
                                   c.cyan_accent.blue(), 20));
    p.fillRect(QRect(width() - 300, 0, 300, height()), cyan_grad);

    // Purple glow
    QLinearGradient purple_grad(width() - 180, 0, width(), height());
    purple_grad.setColorAt(0, QColor(c.purple_ambient.red(), c.purple_ambient.green(),
                                     c.purple_ambient.blue(), 0));
    purple_grad.setColorAt(1, QColor(c.purple_ambient.red(), c.purple_ambient.green(),
                                     c.purple_ambient.blue(), 16));
    p.fillRect(QRect(width() - 180, 0, 180, height()), purple_grad);

    // Bottom hairline border
    p.setPen(QPen(c.border_subtle, 1));
    p.drawLine(0, height() - 1, width(), height() - 1);
}

}   // namespace spectra::adapters::qt
