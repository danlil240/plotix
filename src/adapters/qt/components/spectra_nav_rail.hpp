#pragma once

// SpectraNavRail — left navigation/tool rail.
//
// Contains: Select, Pan, Zoom, Measure, Annotate, ROI, Markers,
// Transform, Data, Timeline, Topics, Help.
//
// Features:
// - Icon plus label
// - Original ordering
// - ~96 logical pixels wide at desktop size
// - Active Pan state with cyan border/glow
// - Keyboard focus state
// - Tooltips containing shortcuts
// - Same semantic ToolMode and commands as before
// - No duplicated tool state in Qt

#include <QWidget>
#include <QList>

class QVBoxLayout;

namespace spectra::adapters::qt
{

class SpectraNavButton;

class SpectraNavRail : public QWidget
{
    Q_OBJECT
public:
    explicit SpectraNavRail(QWidget* parent = nullptr);
    ~SpectraNavRail() override;
    SpectraNavRail(const SpectraNavRail&) = delete;
    SpectraNavRail& operator=(const SpectraNavRail&) = delete;

    void set_active_tool(int tool_index);
    void set_compact_mode(bool compact);
    bool is_compact() const { return compact_; }

    int width_hint() const;

signals:
    void tool_selected(int tool_index);

private:
    void build_buttons();

    QVBoxLayout*           layout_   = nullptr;
    QList<SpectraNavButton*> buttons_;
    bool                   compact_  = false;
    int                    active_   = 1;  // Pan is default active
};

}   // namespace spectra::adapters::qt
