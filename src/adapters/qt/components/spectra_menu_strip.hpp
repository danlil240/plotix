#pragma once

// SpectraMenuStrip — custom menu button strip for the application header.
//
// Contains custom SpectraMenuButton widgets that do NOT look like QMenuBar.
// Each button shows a QMenu popup when clicked, and hovering another button
// while any menu is open switches to that menu (VS Code-style menu behavior).

#include <QPushButton>
#include <QWidget>
#include <QList>

class QEnterEvent;
class QHBoxLayout;
class QMenu;
class QTimer;

namespace spectra::adapters::qt
{

class SpectraMenuStrip;

class SpectraMenuButton : public QPushButton
{
    Q_OBJECT

   public:
    explicit SpectraMenuButton(const QString& label, QMenu* menu, QWidget* parent = nullptr);
    QMenu* menu() const;

   public slots:
    void popup_menu();

   protected:
    void enterEvent(QEnterEvent* event) override;

   private:
    QMenu* menu_ = nullptr;
};

class SpectraMenuStrip : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraMenuStrip(QWidget* parent = nullptr);
    ~SpectraMenuStrip() override;
    SpectraMenuStrip(const SpectraMenuStrip&)            = delete;
    SpectraMenuStrip& operator=(const SpectraMenuStrip&) = delete;

    void add_menu_button(const QString& label, QMenu* menu);

    // Called by a button when the cursor enters it while a menu may be open.
    void on_button_entered(SpectraMenuButton* button);

   private:
    QHBoxLayout*              layout_ = nullptr;
    QList<SpectraMenuButton*> buttons_;
    QMenu*                    open_menu_         = nullptr;
    SpectraMenuButton*        open_button_       = nullptr;
    QTimer*                   hover_timer_       = nullptr;
    bool                      had_hover_         = false;
    bool                      switching_         = false;
    int                       menu_exit_counter_ = 0;

    SpectraMenuButton* button_for_menu(QMenu* menu) const;
    bool               is_cursor_over_button(SpectraMenuButton* button) const;
    bool               is_cursor_in_top_zone(const QPoint& cursor) const;
    bool               is_cursor_in_menu_zone(const QPoint& cursor) const;

   private slots:
    void on_menu_about_to_show();
    void on_menu_about_to_hide();
    void on_hover_poll();
};

}   // namespace spectra::adapters::qt
