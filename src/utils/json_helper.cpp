#include "presence_for_plex/utils/json_helper.hpp"

namespace presence_for_plex::utils {

std::expected<nlohmann::json, std::string> JsonHelper::safe_parse(const std::string& json_string) {
    if (json_string.empty()) {
        return std::unexpected("Empty JSON string");
    }

    // Check if it starts with XML/HTML (common error response)
    if (!json_string.empty() && json_string[0] == '<') {
        return std::unexpected("Response appears to be XML/HTML, not JSON");
    }

    try {
        return nlohmann::json::parse(json_string);
    } catch (const nlohmann::json::parse_error& e) {
        return std::unexpected("JSON parse error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return std::unexpected("Error parsing JSON: " + std::string(e.what()));
    }
}

bool JsonHelper::has_field(const nlohmann::json& json, const std::string& field) {
    return json.contains(field) && !json[field].is_null();
}

bool JsonHelper::has_array(const nlohmann::json& json, const std::string& field) {
    return json.contains(field) && json[field].is_array() && !json[field].empty();
}

} // namespace presence_for_plex::utils
