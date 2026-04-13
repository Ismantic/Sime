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

    // Letter Unit for English word paths: initial = LetterBase + (0..25),
    // final = 0, tone = 0.  Distinct from all pinyin Units (initial 0..24).
    static constexpr int LetterBase = 30;
    static constexpr Unit Letter(char c) {
        return Unit(LetterBase + (c - 'a'), 0, 0);
    }
    constexpr bool IsLetter() const {
        int i = I();
        return i >= LetterBase && i < LetterBase + 26 && A() == 0 && T() == 0;
    }
};

struct UnitEntry {
    const char* text;
    std::uint32_t value;
};

class UnitData {
public:
    static Unit Encode(const char* text);
    static const char* Decode(Unit syllable,
                              const char** i = nullptr,
                              const char** a = nullptr);

    static const char* const* GetIs(std::size_t& count);
    static const char* const* GetAs(std::size_t& count);
    static const UnitEntry* GetDict(std::size_t& count);

    // Return all valid Units whose pinyin starts with the given prefix.
    // e.g. "l" → [la, lai, lan, ..., le, ...], "zh" → [zha, zhai, ...]
    // Also handles fuzzy: "z" matches z- AND zh-, "c"→c-/ch-, "s"→s-/sh-
    static std::vector<Unit> ExpandIncomplete(const std::string& prefix);
};

struct ParseResult {
    bool complete = false;
    std::vector<Unit> units;
    std::size_t matched_len = 0;
};

class UnitParser {
public:
    bool ParseStr(std::string_view token, std::vector<Unit>& units) const;
    bool ParseUnits(std::string_view input, std::vector<Unit>& units) const;

    ParseResult ParseTokenEnhanced(std::string_view token,
                                   bool allow_partial = true) const;

    static bool IsDelimiter(char ch);

private:
    static const std::unordered_map<std::string, Unit>& UnitLookup();
    static std::size_t MaxUnitSize();
};

} // namespace sime
