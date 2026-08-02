#pragma once

#include <mutex>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/plot_style.hpp>
#include <string>
#include <vector>

namespace spectra
{

// Serialized snapshot of a single series for clipboard operations.
// Stores a deep copy of all data + style so the original can be deleted.
struct SeriesSnapshot
{
    enum class Type
    {
        Line,
        Scatter,
        Line3D,
        Scatter3D
    };

    Type               type = Type::Line;
    std::string        label;
    Color              color;
    PlotStyle          style;
    bool               visible    = true;
    float              line_width = 2.0f;   // LineSeries / LineSeries3D
    float              point_size = 4.0f;   // ScatterSeries / ScatterSeries3D
    double             x_offset   = 0.0;
    std::vector<float> x_data;
    std::vector<float> y_data;
    std::vector<float> z_data;   // 3D only (empty for 2D)

    bool is_3d() const { return type == Type::Line3D || type == Type::Scatter3D; }
    bool is_2d() const { return type == Type::Line || type == Type::Scatter; }
};

// Manages copy/cut/paste of series data across figures and tabs.
// Thread-safe.  Not a singleton — stack-allocated in App and passed by pointer.
class SeriesClipboard
{
   public:
    SeriesClipboard() = default;

    // Take a deep-copy snapshot of a series.
    static SeriesSnapshot snapshot(const Series& series);

    // Materialise a snapshot into a new series on the given axes.
    // Returns pointer to the newly created series (owned by axes).
    static Series* paste_to(AxesBase& axes, const SeriesSnapshot& snap);

    // Copy: snapshot the series into the internal clipboard buffer.
    void copy(const Series& series);

    // Cut: snapshot + mark for deferred deletion.
    // The caller is responsible for actually removing the series from its axes
    // after this call returns (the clipboard only stores the data).
    void cut(const Series& series);

    // Copy multiple series at once (replaces clipboard contents).
    void copy_multi(const std::vector<const Series*>& series_list);

    // Cut multiple series at once (replaces clipboard contents).
    void cut_multi(const std::vector<const Series*>& series_list);

    // Paste the clipboard contents into the given axes.
    // Returns the new series, or nullptr if clipboard is empty.
    Series* paste(AxesBase& axes);

    // Paste all clipboard contents into the given axes.
    // Returns vector of newly created series pointers.
    std::vector<Series*> paste_all(AxesBase& axes);

    // Query state
    bool   has_data() const;
    bool   is_cut() const;
    size_t count() const;

    // Clear the clipboard
    void clear();

    // Access the stored snapshot (for display purposes)
    const SeriesSnapshot* peek() const;

    // Access all stored snapshots
    const std::vector<SeriesSnapshot>& peek_all() const;

   private:
    mutable std::mutex          mutex_;
    std::vector<SeriesSnapshot> buffers_;
    bool                        has_data_ = false;
    bool                        is_cut_   = false;
};

}   // namespace spectra
