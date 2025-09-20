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
