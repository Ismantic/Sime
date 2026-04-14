#pragma once

#include <cstdint>

namespace sime {

struct Unit {
    std::uint32_t value = 0;

    constexpr Unit() = default;
    constexpr explicit Unit(std::uint32_t v) : value(v) {}

    constexpr explicit operator bool() const { return value != 0; }

    constexpr bool operator==(const Unit& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const Unit& other) const {
        return !(*this == other);
    }
};

} // namespace sime
