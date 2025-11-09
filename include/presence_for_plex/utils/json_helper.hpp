#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <expected>
#include <optional>

namespace presence_for_plex::utils {

/**
 * @brief Helper utilities for safe JSON parsing and field extraction
 */
class JsonHelper {
public:
    /**
     * @brief Safely parse a JSON string
     *
     * @param json_string The JSON string to parse
     * @return std::expected<nlohmann::json, std::string> Parsed JSON or error message
     */
    static std::expected<nlohmann::json, std::string> safe_parse(const std::string& json_string);

    /**
     * @brief Get a required field from JSON, returning error if missing
     *
     * @tparam T The expected type of the field
     * @param json The JSON object
     * @param field The field name
     * @return std::expected<T, std::string> The value or error message
     */
    template<typename T>
    static std::expected<T, std::string> get_required(const nlohmann::json& json, const std::string& field);

    /**
     * @brief Get an optional field from JSON with a default value
     *
     * @tparam T The expected type of the field
     * @param json The JSON object
     * @param field The field name
     * @param default_value The default value if field is missing
     * @return T The value or default
     */
    template<typename T>
    static T get_optional(const nlohmann::json& json, const std::string& field, const T& default_value);

    /**
     * @brief Check if a field exists and is of the expected type
     *
     * @param json The JSON object
     * @param field The field name
     * @return bool True if field exists and is valid
     */
    static bool has_field(const nlohmann::json& json, const std::string& field);

    /**
     * @brief Check if a field exists and is a non-empty array
     *
     * @param json The JSON object
     * @param field The field name
     * @return bool True if field is a non-empty array
     */
    static bool has_array(const nlohmann::json& json, const std::string& field);

    /**
     * @brief Safely iterate over a JSON array
     *
     * @tparam Func Function type that takes const nlohmann::json&
     * @param json The JSON object
     * @param field The array field name
     * @param func The function to call for each element
     */
    template<typename Func>
    static void for_each_in_array(const nlohmann::json& json, const std::string& field, Func&& func);
};

// Template implementations
template<typename T>
std::expected<T, std::string> JsonHelper::get_required(const nlohmann::json& json, const std::string& field) {
    if (!json.contains(field)) {
        return std::unexpected("Missing required field: " + field);
    }

    try {
        return json[field].get<T>();
    } catch (const std::exception& e) {
        return std::unexpected("Failed to extract field '" + field + "': " + e.what());
    }
}

template<typename T>
T JsonHelper::get_optional(const nlohmann::json& json, const std::string& field, const T& default_value) {
    if (!json.contains(field)) {
        return default_value;
    }

    try {
        return json[field].get<T>();
    } catch (const std::exception&) {
        return default_value;
    }
}

template<typename Func>
void JsonHelper::for_each_in_array(const nlohmann::json& json, const std::string& field, Func&& func) {
    if (!has_array(json, field)) {
        return;
    }

    for (const auto& element : json[field]) {
        func(element);
    }
}

} // namespace presence_for_plex::utils
