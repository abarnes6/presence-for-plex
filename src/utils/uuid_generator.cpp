#include "presence_for_plex/utils/uuid.hpp"
#include <array>
#include <cstdint>
#include <random>
#include <sstream>
#include <iomanip>

namespace presence_for_plex::utils {

static std::random_device& get_random_device() {
    static std::random_device rd;
    return rd;
}

static std::mt19937& get_rng() {
    static std::mt19937 gen(get_random_device()());
    return gen;
}

std::string generate_uuid_v4() {
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    auto& rng = get_rng();

    std::array<uint8_t, 16> bytes{};
    for (auto& byte : bytes) {
        byte = static_cast<uint8_t>(dist(rng));
    }

    // Set version to 4 (random UUID)
    bytes[6] = (bytes[6] & 0x0F) | 0x40;

    // Set variant to RFC 4122
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }

    return oss.str();
}

}
