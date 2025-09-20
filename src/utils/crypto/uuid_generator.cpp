#include "presence_for_plex/utils/uuid.hpp"
#include <chrono>
#include <array>
#include <random>
#include <string>
#include <string_view>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace presence_for_plex::utils {

Uuid::Uuid(const std::array<std::uint8_t, 16>& bytes) : m_bytes(bytes) {}

Uuid Uuid::generate_v4() {
    std::array<std::uint8_t, 16> bytes{};
    auto& rng = get_rng();
    std::uniform_int_distribution<std::uint16_t> dist(0, 255);

    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(dist(rng));
    }

    // Set version (4 bits) to 4 (random UUID)
    bytes[6] = (bytes[6] & 0x0F) | 0x40;

    // Set variant (2 bits) to 10 (RFC 4122 variant)
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    return Uuid(bytes);
}

Uuid Uuid::from_string(std::string_view uuid_str) {
    if (uuid_str.length() != 36) {
        throw std::invalid_argument("Invalid UUID string length");
    }

    std::array<std::uint8_t, 16> bytes{};
    std::size_t byte_index = 0;

    for (std::size_t i = 0; i < uuid_str.length(); i += 2) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid_str[i] != '-') {
                throw std::invalid_argument("Invalid UUID string format");
            }
            ++i;
        }

        if (i + 1 >= uuid_str.length()) {
            throw std::invalid_argument("Invalid UUID string format");
        }

        char hex_chars[3] = {uuid_str[i], uuid_str[i + 1], '\0'};
        char* end_ptr = nullptr;
        auto value = std::strtoul(hex_chars, &end_ptr, 16);

        if (end_ptr != hex_chars + 2) {
            throw std::invalid_argument("Invalid hexadecimal characters in UUID");
        }

        bytes[byte_index++] = static_cast<std::uint8_t>(value);
    }

    return Uuid(bytes);
}

std::string Uuid::to_string() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (std::size_t i = 0; i < m_bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<unsigned>(m_bytes[i]);
    }

    return oss.str();
}

std::random_device& Uuid::get_random_device() {
    static thread_local std::random_device rd;
    return rd;
}

std::mt19937& Uuid::get_rng() {
    static thread_local std::mt19937 gen(static_cast<std::mt19937::result_type>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return gen;
}

}
