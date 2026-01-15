#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sime {

struct Unit {
    std::uint32_t value = 0;

    constexpr Unit() = default;
    constexpr explicit Unit(std::uint32_t v) : value(v) {}
    constexpr Unit(int i, int a, int t)
        : value(Pack(i, a, t)) {}

    static constexpr std::uint32_t Pack(int i, int a, int t) {
        return (static_cast<std::uint32_t>(t) & 0xFU) |
               ((static_cast<std::uint32_t>(a) & 0xFFU) << 4U) |
               ((static_cast<std::uint32_t>(i) & 0xFFU) << 12U);
    }

    constexpr int T() const { return static_cast<int>(value & 0xFU); }
    constexpr int A() const { return static_cast<int>((value >> 4U) & 0xFFU); }
    constexpr int I() const { return static_cast<int>((value >> 12U) & 0xFFU); }

    constexpr explicit operator bool() const { return value != 0; }

    constexpr bool operator==(const Unit& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const Unit& other) const {
        return !(*this == other);
    }

    constexpr bool Full() const { return A() != 0; }
};

struct UnitEntry {
    const char* text;
    std::uint32_t i;
};

class UnitData {
public:
    static Unit Encode(const char* text);
    static const char* Decode(Unit syllable,
                              const char** i = nullptr,
                              const char** a = nullptr);

    static const char* const* Is(std::size_t& count);
    static const char* const* As(std::size_t& count);
    static const UnitEntry* GetTable(std::size_t& count);
};

class UnitParser {
public:
    bool ParseToken(std::string_view token, std::vector<Unit>& units) const;
    bool ParseUnits(std::string_view phone, std::vector<Unit>& units) const;

    static bool IsDelimiter(char ch);

private:
    static const std::unordered_map<std::string, Unit>& UnitLookup();
    static std::size_t MaxUnitLength();
};

} // namespace sime
