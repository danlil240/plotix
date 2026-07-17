#pragma once

#include <string>
#include <vector>

namespace spectra
{

// Lightweight CSV parser for numeric data.
// Supports comma, semicolon, and tab delimiters.
// First row is treated as header if it contains non-numeric values.
struct CsvData
{
    std::vector<std::string>        headers;   // Column names (empty if no header row)
    std::vector<std::vector<float>> columns;   // Column-major data (relative to column_offsets)
    // Per-column double-precision base offset.  Columns whose values exceed
    // float precision (datetimes, epoch timestamps) are stored as small
    // relative floats; the absolute value is column_offsets[c] + columns[c][i].
    // Zero for ordinary numeric columns.
    std::vector<double> column_offsets;
    size_t              num_rows = 0;
    size_t              num_cols = 0;
    std::string         error;   // Non-empty on parse failure
};

// Parse a CSV file from disk.
CsvData parse_csv(const std::string& path);

// List .csv files in a directory (non-recursive).
std::vector<std::string> list_csv_files(const std::string& directory);

}   // namespace spectra
