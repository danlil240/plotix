#pragma once

// SpectraDocumentTabBar — custom document tab bar.
//
// Features:
// - Figure tabs with close buttons
// - Add (+) button at the end
// - Active tab with cyan underline
// - Drag/detach support
// - Custom-painted, no QTabBar

#include <QWidget>
#include <QList>

class QHBoxLayout;

namespace spectra::adapters::qt
{

struct SpectraTab
{
    QString  title;
    int      id       = -1;
    bool     modified = false;
};

class SpectraDocumentTabBar : public QWidget
{
    Q_OBJECT
public:
    explicit SpectraDocumentTabBar(QWidget* parent = nullptr);
    ~SpectraDocumentTabBar() override;
    SpectraDocumentTabBar(const SpectraDocumentTabBar&) = delete;
    SpectraDocumentTabBar& operator=(const SpectraDocumentTabBar&) = delete;

    void add_tab(const QString& title, int id);
    void remove_tab(int id);
    void set_active_tab(int id);
    void set_tab_title(int id, const QString& title);
    void set_tab_modified(int id, bool modified);

    int active_tab_id() const { return active_id_; }
    int tab_count() const { return tabs_.size(); }

    int height_hint() const;

signals:
    void tab_selected(int id);
    void tab_closed(int id);
    void tab_add_requested();
    void tab_detach_requested(int id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    struct TabLayout
    {
        QRect rect;
        QRect close_rect;
        int   index = -1;
    };

    QList<SpectraTab>  tabs_;
    int                active_id_  = -1;
    int                hover_tab_  = -1;
    int                drag_tab_   = -1;
    QPoint             drag_start_;

    QList<TabLayout>   tab_layouts_;
    QRect              add_btn_rect_;

    void update_layout();
    int  tab_at(const QPoint& pos) const;
    bool is_add_button(const QPoint& pos) const;
};

}   // namespace spectra::adapters::qt
