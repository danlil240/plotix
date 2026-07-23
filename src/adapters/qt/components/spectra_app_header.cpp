// spectra_app_header.cpp — Custom application header implementation.

#include "spectra_app_header.hpp"
#include "spectra_design_tokens.hpp"
#include "spectra_menu_strip.hpp"
#include "spectra_icon_embedded.hpp"
#include "ui/theme/icons.hpp"

#include <QHBoxLayout>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>

namespace spectra::adapters::qt
{

// ─── Logo Widget ─────────────────────────────────────────────────────────────

class SpectraLogoWidget : public QWidget
{
   public:
    explicit SpectraLogoWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(26, 26);
        logo_.loadFromData(SpectraIcon_png_data, SpectraIcon_png_size, "PNG");
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(rect(), logo_);
    }

   private:
    QPixmap logo_;
};

// ─── Wordmark Widget ─────────────────────────────────────────────────────────

class SpectraWordmarkWidget : public QWidget
{
   public:
    explicit SpectraWordmarkWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        auto&        fm = SpectraFontManager::instance();
        QFont        f  = fm.font_wordmark();
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
        p.setPen(spectra_colors().text_secondary);
        p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, "SPECTRA");
    }
};

// ─── Vertical Divider ────────────────────────────────────────────────────────

// ─── Home Button ─────────────────────────────────────────────────────────────

class SpectraHomeButton : public QPushButton
{
   public:
    explicit SpectraHomeButton(QWidget* parent = nullptr) : QPushButton(parent)
    {
        setFixedSize(28, 28);
        setCursor(Qt::PointingHandCursor);
        setToolTip("Home (Ctrl+H)");

        setStyleSheet(QString("QPushButton {"
                              "  background: transparent;"
                              "  border: none;"
                              "  border-radius: %1px;"
                              "}"
                              "QPushButton:hover {"
                              "  background: rgba(26, 35, 50, 140);"
                              "}")
                          .arg(spectra_geometry().radius_md));
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const auto& c  = spectra_colors();
        auto&       fm = SpectraFontManager::instance();
        p.setFont(fm.font_icon(14));
        p.setPen(underMouse() ? c.text_primary : c.text_secondary);
        const auto home = static_cast<uint32_t>(ui::Icon::Home);
        p.drawText(rect(), Qt::AlignCenter, fm.icon_codepoint(home));
    }
};

// ─── SpectraAppHeader ────────────────────────────────────────────────────────

SpectraAppHeader::~SpectraAppHeader() = default;

SpectraAppHeader::SpectraAppHeader(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(spectra_geometry().header_height);
    setAttribute(Qt::WA_StyledBackground, true);

    build_layout();
}

void SpectraAppHeader::build_layout()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 0, 20, 0);
    layout->setSpacing(0);

    // Logo
    logo_widget_ = new SpectraLogoWidget(this);
    layout->addWidget(logo_widget_);

    layout->addSpacing(11);

    // Wordmark
    wordmark_widget_ = new SpectraWordmarkWidget(this);
    layout->addWidget(wordmark_widget_);

    layout->addSpacing(17);

    // Home button
    home_btn_ = new SpectraHomeButton(this);
    connect(home_btn_, &QPushButton::clicked, this, &SpectraAppHeader::home_clicked);
    layout->addWidget(home_btn_);

    layout->addSpacing(13);

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

    p.fillRect(rect(), c.header_surface);

    const int       aurora_x = static_cast<int>(width() * 0.55);
    QLinearGradient purple_grad(aurora_x, 0, width(), height());
    purple_grad.setColorAt(
        0,
        QColor(c.purple_ambient.red(), c.purple_ambient.green(), c.purple_ambient.blue(), 0));
    purple_grad.setColorAt(
        1,
        QColor(c.purple_ambient.red(), c.purple_ambient.green(), c.purple_ambient.blue(), 18));
    p.fillRect(QRect(aurora_x, 0, width() - aurora_x, height()), purple_grad);

    QColor border = c.border_subtle;
    border.setAlpha(90);
    p.setPen(QPen(border, 1));
    p.drawLine(0, height() - 1, width(), height() - 1);
}

}   // namespace spectra::adapters::qt
