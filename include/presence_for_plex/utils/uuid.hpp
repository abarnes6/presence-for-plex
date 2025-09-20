#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace presence_for_plex::utils {

class Uuid {
public:
    Uuid() = default;
    explicit Uuid(const std::array<std::uint8_t, 16>& bytes);

    [[nodiscard]] static Uuid generate_v4();
    [[nodiscard]] std::string to_string() const;

private:
    std::array<std::uint8_t, 16> m_bytes{};

    static std::random_device& get_random_device();
    static std::mt19937& get_rng();
};

}