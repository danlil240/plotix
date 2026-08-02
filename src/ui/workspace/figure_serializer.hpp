#pragma once

#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>
#include <iosfwd>
#include <string>
#include <string_view>

#include "overlay_snapshot.hpp"

namespace spectra
{

// Binary figure serializer (.spectra format)
// Saves and loads complete figure state: all axes, series data, colors, styles,
// grid settings, limits, labels, legend, camera (3D), etc.
//
// Format: Magic(4) + Version(4) + Chunks...
//   Each chunk: Tag(2) + Length(4) + Data(Length)
//
// Optimized for speed (direct memcpy of float arrays) and disk space
// (no text overhead, float data stored as raw bytes).

class FigureSerializer
{
   public:
    // Save a figure to a binary .spectra file. Returns true on success.
    // If overlay is non-null, overlay data (annotations, markers, etc.) is
    // saved as additional chunks.
    static bool save(const std::string&     path,
                     const Figure&          figure,
                     const OverlaySnapshot* overlay = nullptr);

    // Load a figure from a binary .spectra file into an existing Figure.
    // Clears existing axes/series and replaces with file contents.
    // If overlay is non-null, overlay chunks are loaded into it.
    // Returns true on success.
    static bool load(const std::string& path, Figure& figure, OverlaySnapshot* overlay = nullptr);

    // In-memory equivalents used by atomic workspace/autosave snapshots.
    static bool serialize(const Figure&          figure,
                          std::string&           bytes,
                          const OverlaySnapshot* overlay = nullptr);
    static bool deserialize(std::string_view bytes,
                            Figure&          figure,
                            OverlaySnapshot* overlay = nullptr);

    // Open a native OS save dialog and save the figure.
    // Returns true if the user selected a file and save succeeded.
    static bool save_with_dialog(const Figure& figure, const OverlaySnapshot* overlay = nullptr);

    // Open a native OS open dialog and load into the figure.
    // Returns true if the user selected a file and load succeeded.
    static bool load_with_dialog(Figure& figure, OverlaySnapshot* overlay = nullptr);

   private:
    static bool save_stream(std::ostream&          stream,
                            const Figure&          figure,
                            const OverlaySnapshot* overlay);
    static bool load_stream(std::istream& stream, Figure& figure, OverlaySnapshot* overlay);
};

}   // namespace spectra
