#include "csv_loader.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

namespace spectra
{

namespace
{

char detect_delimiter(const std::string& line)
{
    int commas     = 0;
    int semicolons = 0;
    int tabs       = 0;
    for (char c : line)
    {
        if (c == ',')
            ++commas;
        else if (c == ';')
            ++semicolons;
        else if (c == '\t')
            ++tabs;
    }
    if (tabs >= commas && tabs >= semicolons)
        return '\t';
    if (semicolons > commas)
        return ';';
    return ',';
}

bool try_parse_float(const std::string& s, float& out)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    float val = std::strtof(s.c_str(), &end);
    // Must consume the entire string (ignoring trailing whitespace)
    while (end && *end && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (end == s.c_str() || (end && *end != '\0'))
        return false;
    out = val;
    return true;
}

bool try_parse_double(const std::string& s, double& out)
{
    if (s.empty())
        return false;
    char*  end = nullptr;
    double val = std::strtod(s.c_str(), &end);
    while (end && *end && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (end == s.c_str() || (end && *end != '\0'))
        return false;
    out = val;
    return true;
}

std::string trim_copy(const std::string& s)
{
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])))
        ++begin;

    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;

    return s.substr(begin, end - begin);
}

bool parse_fixed_width_int(std::string_view s, size_t pos, size_t width, int& out)
{
    if (pos + width > s.size())
        return false;

    int value = 0;
    for (size_t i = 0; i < width; ++i)
    {
        const auto ch = static_cast<unsigned char>(s[pos + i]);
        if (!std::isdigit(ch))
            return false;
        value = value * 10 + static_cast<int>(ch - '0');
    }

    out = value;
    return true;
}

bool starts_with_four_digits(std::string_view s)
{
    return s.size() >= 4 && std::isdigit(static_cast<unsigned char>(s[0])) != 0
           && std::isdigit(static_cast<unsigned char>(s[1])) != 0
           && std::isdigit(static_cast<unsigned char>(s[2])) != 0
           && std::isdigit(static_cast<unsigned char>(s[3])) != 0;
}

bool looks_like_datetime_candidate(std::string_view s)
{
    if (!starts_with_four_digits(s))
        return false;

    for (size_t i = 4; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '-' || c == '/' || c == '.' || c == 'T' || c == 't' || c == '_' || c == 'Z'
            || c == 'z' || c == ' ' || c == ':' || c == '+' || c == ',')
        {
            return true;
        }
    }

    return s.size() >= 14;
}

bool is_date_separator(char c)
{
    return c == '-' || c == '/' || c == '.';
}

bool is_time_separator(char c)
{
    return c == ':' || c == '-';
}

bool is_fraction_separator(char c)
{
    return c == '.' || c == ',' || c == '_';
}

void skip_spaces(std::string_view s, size_t& pos)
{
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
        ++pos;
}

bool parse_fractional_part(std::string_view s, size_t& pos, double& out)
{
    if (pos >= s.size() || !is_fraction_separator(s[pos]))
        return true;

    ++pos;
    if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
        return false;

    double scale = 0.1;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
    {
        out += static_cast<double>(s[pos] - '0') * scale;
        scale *= 0.1;
        ++pos;
    }
    return true;
}

bool parse_date_part(std::string_view s, size_t& pos, int& year, int& month, int& day)
{
    if (!parse_fixed_width_int(s, 0, 4, year))
        return false;

    if (s.size() >= 10 && is_date_separator(s[4]) && s[7] == s[4])
    {
        if (!parse_fixed_width_int(s, 5, 2, month) || !parse_fixed_width_int(s, 8, 2, day))
            return false;
        pos = 10;
        return true;
    }

    if (s.size() >= 8 && std::isdigit(static_cast<unsigned char>(s[4]))
        && std::isdigit(static_cast<unsigned char>(s[5]))
        && std::isdigit(static_cast<unsigned char>(s[6]))
        && std::isdigit(static_cast<unsigned char>(s[7])))
    {
        if (!parse_fixed_width_int(s, 4, 2, month) || !parse_fixed_width_int(s, 6, 2, day))
            return false;
        pos = 8;
        return true;
    }

    return false;
}

bool parse_time_part(std::string_view s,
                     size_t&          pos,
                     int&             hour,
                     int&             minute,
                     int&             second,
                     double&          fractional_second)
{
    if (!parse_fixed_width_int(s, pos, 2, hour))
        return false;
    pos += 2;

    if (pos >= s.size())
        return false;

    if (is_time_separator(s[pos]))
    {
        const char sep = s[pos];
        ++pos;

        if (!parse_fixed_width_int(s, pos, 2, minute))
            return false;
        pos += 2;

        if (pos < s.size() && s[pos] == sep)
        {
            ++pos;
            if (!parse_fixed_width_int(s, pos, 2, second))
                return false;
            pos += 2;

            if (!parse_fractional_part(s, pos, fractional_second))
                return false;
        }

        return true;
    }

    if (!std::isdigit(static_cast<unsigned char>(s[pos])))
        return false;

    if (!parse_fixed_width_int(s, pos, 2, minute))
        return false;
    pos += 2;

    if (pos + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))
        && std::isdigit(static_cast<unsigned char>(s[pos + 1])))
    {
        if (!parse_fixed_width_int(s, pos, 2, second))
            return false;
        pos += 2;

        if (!parse_fractional_part(s, pos, fractional_second))
            return false;
    }

    return true;
}

bool parse_timezone_part(std::string_view s,
                         size_t&          pos,
                         bool&            has_timezone,
                         int&             timezone_sign,
                         int&             timezone_hours,
                         int&             timezone_minutes)
{
    skip_spaces(s, pos);
    if (pos >= s.size())
        return true;

    if (s[pos] == 'Z' || s[pos] == 'z')
    {
        has_timezone = true;
        ++pos;
        return true;
    }

    if (s[pos] != '+' && s[pos] != '-')
        return false;

    has_timezone  = true;
    timezone_sign = (s[pos] == '+') ? 1 : -1;
    ++pos;

    if (!parse_fixed_width_int(s, pos, 2, timezone_hours))
        return false;
    pos += 2;

    if (pos < s.size() && (s[pos] == ':' || s[pos] == '_'))
    {
        ++pos;
        if (!parse_fixed_width_int(s, pos, 2, timezone_minutes))
            return false;
        pos += 2;
        return true;
    }

    if (pos + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))
        && std::isdigit(static_cast<unsigned char>(s[pos + 1])))
    {
        if (!parse_fixed_width_int(s, pos, 2, timezone_minutes))
            return false;
        pos += 2;
    }

    return true;
}

bool try_parse_datetime(const std::string& s, double& out)
{
    const std::string trimmed = trim_copy(s);
    if (trimmed.empty())
        return false;

    const std::string_view view(trimmed);

    int    year_value  = 0;
    int    month_value = 0;
    int    day_value   = 0;
    size_t pos         = 0;
    if (!parse_date_part(view, pos, year_value, month_value, day_value))
        return false;

    int    hour_value        = 0;
    int    minute_value      = 0;
    int    second_value      = 0;
    double fractional_second = 0.0;
    bool   has_timezone      = false;
    int    timezone_sign     = 1;
    int    timezone_hours    = 0;
    int    timezone_minutes  = 0;

    if (pos < view.size())
    {
        if (view[pos] == 'T' || view[pos] == 't' || view[pos] == '_')
            ++pos;
        else if (std::isspace(static_cast<unsigned char>(view[pos])))
            skip_spaces(view, pos);
        else if (pos != 8 || !std::isdigit(static_cast<unsigned char>(view[pos])))
            return false;

        if (pos >= view.size())
            return false;

        if (!parse_time_part(view, pos, hour_value, minute_value, second_value, fractional_second))
            return false;

        if (!parse_timezone_part(view,
                                 pos,
                                 has_timezone,
                                 timezone_sign,
                                 timezone_hours,
                                 timezone_minutes))
            return false;

        skip_spaces(view, pos);
        if (pos != view.size())
            return false;
    }

    if (hour_value > 23 || minute_value > 59 || second_value > 59 || timezone_hours > 23
        || timezone_minutes > 59)
    {
        return false;
    }

    using namespace std::chrono;

    const year_month_day ymd{year{year_value},
                             month{static_cast<unsigned>(month_value)},
                             day{static_cast<unsigned>(day_value)}};
    if (!ymd.ok())
        return false;

    double seconds = duration<double>(sys_days{ymd}.time_since_epoch()).count();
    seconds += static_cast<double>(hour_value * 3600 + minute_value * 60 + second_value);
    seconds += fractional_second;

    if (has_timezone)
    {
        const auto timezone_offset =
            static_cast<double>(timezone_sign * (timezone_hours * 3600 + timezone_minutes * 60));
        seconds -= timezone_offset;
    }

    out = seconds;
    return true;
}

std::vector<std::string> split_line(const std::string& line, char delim)
{
    std::vector<std::string> fields;
    std::string              field;
    bool                     in_quotes = false;

    for (char c : line)
    {
        if (c == '"')
        {
            in_quotes = !in_quotes;
        }
        else if (c == delim && !in_quotes)
        {
            // Trim whitespace
            while (!field.empty() && std::isspace(static_cast<unsigned char>(field.front())))
                field.erase(field.begin());
            while (!field.empty() && std::isspace(static_cast<unsigned char>(field.back())))
                field.pop_back();
            fields.push_back(field);
            field.clear();
        }
        else
        {
            field += c;
        }
    }
    // Last field
    while (!field.empty() && std::isspace(static_cast<unsigned char>(field.front())))
        field.erase(field.begin());
    while (!field.empty() && std::isspace(static_cast<unsigned char>(field.back())))
        field.pop_back();
    fields.push_back(field);

    return fields;
}

}   // namespace

CsvData parse_csv(const std::string& path)
{
    CsvData result;

    std::ifstream file(path);
    if (!file.is_open())
    {
        result.error = "Cannot open file: " + path;
        return result;
    }

    // Read all lines
    std::vector<std::string> lines;
    std::string              line;
    while (std::getline(file, line))
    {
        // Strip trailing \r (Windows line endings)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            lines.push_back(line);
    }

    if (lines.empty())
    {
        result.error = "File is empty";
        return result;
    }

    // Detect delimiter from first line
    char delim = detect_delimiter(lines[0]);

    // Parse first line to detect if it's a header
    auto first_fields = split_line(lines[0], delim);
    result.num_cols   = first_fields.size();

    if (result.num_cols == 0)
    {
        result.error = "No columns detected";
        return result;
    }

    // Check if first row is a header (at least one non-numeric field)
    bool has_header = false;
    for (const auto& f : first_fields)
    {
        float  dummy          = 0.0f;
        double datetime_dummy = 0.0;
        if (!trim_copy(f).empty() && !try_parse_float(f, dummy)
            && !try_parse_datetime(f, datetime_dummy))
        {
            has_header = true;
            break;
        }
    }

    size_t data_start = 0;
    if (has_header)
    {
        result.headers = first_fields;
        data_start     = 1;
    }
    else
    {
        // Generate default column names
        for (size_t i = 0; i < result.num_cols; ++i)
            result.headers.push_back("Column " + std::to_string(i + 1));
    }

    // Initialize columns
    result.columns.resize(result.num_cols);
    // Per-column double base for values that exceed float precision
    // (datetimes and large numeric timestamps).  The base is recorded in
    // CsvData::column_offsets so callers can recover absolute values.
    std::vector<std::optional<double>> column_bases(result.num_cols);

    // Parse data rows
    for (size_t i = data_start; i < lines.size(); ++i)
    {
        auto fields = split_line(lines[i], delim);
        for (size_t c = 0; c < result.num_cols; ++c)
        {
            float val = 0.0f;
            if (c < fields.size())
            {
                const std::string trimmed_field  = trim_copy(fields[c]);
                double            datetime_value = 0.0;
                const bool        has_datetime_shape =
                    looks_like_datetime_candidate(std::string_view(trimmed_field));

                if (has_datetime_shape && try_parse_datetime(trimmed_field, datetime_value))
                {
                    if (!column_bases[c].has_value())
                        column_bases[c] = datetime_value;
                    val = static_cast<float>(datetime_value - *column_bases[c]);
                }
                else
                {
                    // Parse as double first to detect large numeric values
                    // (e.g. epoch timestamps ~1.7e9) that exceed float's
                    // ~7-digit precision.  Store them relative to the first
                    // value in the column; the base is recorded in
                    // column_offsets so the absolute value is preserved.
                    double double_val = 0.0;
                    if (try_parse_double(trimmed_field, double_val) && std::abs(double_val) > 1e6)
                    {
                        if (!column_bases[c].has_value())
                            column_bases[c] = double_val;
                        val = static_cast<float>(double_val - *column_bases[c]);
                    }
                    else
                    {
                        try_parse_float(fields[c], val);
                    }
                }
            }
            result.columns[c].push_back(val);
        }
    }

    result.column_offsets.resize(result.num_cols, 0.0);
    for (size_t c = 0; c < result.num_cols; ++c)
        result.column_offsets[c] = column_bases[c].value_or(0.0);

    result.num_rows = lines.size() - data_start;
    return result;
}

std::vector<std::string> list_csv_files(const std::string& directory)
{
    std::vector<std::string> files;
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file())
                continue;
            auto ext = entry.path().extension().string();
            // Case-insensitive .csv check
            std::transform(ext.begin(),
                           ext.end(),
                           ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext == ".csv" || ext == ".tsv" || ext == ".txt")
                files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());
    }
    catch (const std::exception&)
    {
        // Directory not accessible — return empty
    }
    return files;
}

}   // namespace spectra
