#pragma once

// Parse CLI field specs: "/topic:field" or "/topic.field.path"

#include <string>
#include <vector>

namespace spectra::adapters::ros2
{

struct BagFieldSpec
{
    std::string topic;
    std::string field_path;
    std::string column_name;   // topic + "/" + field_path
};

// Parse one user token into topic + field_path.
// Returns false if the spec cannot be parsed.
bool parse_bag_field_spec(const std::string& token, BagFieldSpec& out);

// Resolve topic prefix against known bag topics (longest match wins).
bool resolve_bag_field_spec(const std::string&              token,
                            const std::vector<std::string>& bag_topics,
                            BagFieldSpec&                   out);

}   // namespace spectra::adapters::ros2
