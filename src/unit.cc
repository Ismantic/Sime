
#include "unit.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sime {


namespace {

constexpr int CompareEntry(const char* key, const UnitEntry& entry) {
    return std::strcmp(key, entry.text);
}

const char* const Is[] = {"", "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h", "j", "q", "x", "zh", "ch", "sh", "r", "z", "c", "s", "y", "w", "hm"};
constexpr std::size_t NumIs = sizeof(Is) / sizeof(*Is);

const char* const As[] = {"", "a", "o", "e", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng", "er", "i", "ia", "ie", "iao", "iu", "ian", "in", "iang", "ing", "u", "ua", "uo", "uai", "ui", "uan", "un", "uang", "ong", "v", "ve", "ue", "iong", "ng"};
constexpr std::size_t NumAs = sizeof(As) / sizeof(*As);

const UnitEntry Dict[] = {
#include "dict.inc"
};

constexpr std::size_t DictSize = sizeof(Dict) / sizeof(Dict[0]);

const UnitEntry* GetEntry(const char* text) {
    std::size_t left = 0;
    std::size_t right = DictSize;
    while (left < right) {
        std::size_t mid = left + (right - left) / 2;
        int cmp = CompareEntry(text, Dict[mid]);
        if (cmp == 0) {
            return &Dict[mid];
        }
        if (cmp > 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return nullptr;
}

void Compose(const char* initial, const char* final, char* buf, std::size_t len) {
    std::snprintf(buf, len, "%s%s", initial, final);
}

} // namespace

Unit UnitData::Encode(const char* text) {
    if (!text) {
        return Unit{};
    }
    if (const UnitEntry* entry = GetEntry(text)) {
        return Unit(entry->value);
    }
    return Unit{};
}

const char* UnitData::Decode(Unit unit,
                             const char** i,
                             const char** a) {
    int ii = unit.I();
    int ai = unit.A();
    if (static_cast<std::size_t>(ii) >= NumIs || static_cast<std::size_t>(ai) >= NumAs) {
        if (i) { *i = ""; }
        if (a) { *a = ""; }
        return "";
    }
    if (i) {
        *i = Is[ii];
    }
    if (a) {
        *a = As[ai];
    }
    static char buffer[128];
    Compose(Is[ii], As[ai], buffer, sizeof(buffer));
    if (const UnitEntry* entry = GetEntry(buffer)) {
        return entry->text;
    }
    return buffer;
}

const char* const* UnitData::GetIs(std::size_t& count) {
    count = NumIs;
    return Is;
}

const char* const* UnitData::GetAs(std::size_t& count) {
    count = NumAs;
    return As;
}

const UnitEntry* UnitData::GetDict(std::size_t& count) {
    count = DictSize;
    return Dict;
}

bool UnitParser::IsDelimiter(char ch) {
    switch (ch) {
        case '\'':
        case '-':
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case ',':
        case '.':
            return true;
        default:
            return false;
    }
}

bool UnitParser::ParseStr(std::string_view token,
                          std::vector<Unit>& units) const {
    units.clear();
    if (token.empty()) {
        return true;
    }
    std::string normalized;
    normalized.reserve(token.size());
    for (char ch : token) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            normalized.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    if (normalized.empty()) {
        return true;
    }

    const auto& lookup = UnitLookup();
    std::size_t max_len = MaxUnitSize();
    const std::size_t n = normalized.size();
    std::vector<int> back(n + 1, -1);
    std::vector<Unit> matched(n + 1);
    back[0] = 0;

    std::string buffer;
    for (std::size_t i = 0; i < n; ++i) {
        if (back[i] < 0) {
            continue;
        }
        std::size_t limit = std::min(max_len, n - i);
        for (std::size_t len = limit; len > 0; --len) {
            buffer.assign(normalized.data() + i, len);
            auto it = lookup.find(buffer);
            if (it == lookup.end()) {
                continue;
            }
            if (back[i + len] >= 0) {
                continue;
            }
            back[i + len] = static_cast<int>(len);
            matched[i + len] = it->second;
        }
    }
    if (back[n] < 0) {
        return false;
    }

    std::vector<Unit> sequence;
    for (std::size_t idx = n; idx > 0;) {
        int len = back[idx];
        if (len <= 0) {
            return false;
        }
        sequence.push_back(matched[idx]);
        idx -= static_cast<std::size_t>(len);
    }
    std::reverse(sequence.begin(), sequence.end());
    units = std::move(sequence);
    return true;
}

bool UnitParser::ParseUnits(std::string_view input,
                            std::vector<Unit>& units) const {
    units.clear();
    std::size_t pos = 0;
    while (pos <= input.size()) {
        std::size_t next = input.find('\'', pos);
        std::string_view segment =
            input.substr(pos, next == std::string::npos
                                   ? std::string::npos
                                   : next - pos);
        if (!segment.empty()) {
            std::string buffer(segment);
            if (const Unit syl = UnitData::Encode(buffer.c_str());
                syl.value != 0) {
                units.push_back(syl);
            } else {
                return false;
            }
        }
        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }
    return !units.empty();
}

const std::unordered_map<std::string, Unit>& UnitParser::UnitLookup() {
    static const std::unordered_map<std::string, Unit> lookup = [] {
        std::unordered_map<std::string, Unit> dict;
        std::size_t count = 0;
        const UnitEntry* entries = UnitData::GetDict(count);
        dict.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            std::string key(entries[i].text);
            for (char& ch : key) {
                ch = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch)));
            }
            dict.emplace(std::move(key), Unit(entries[i].value));
        }
        return dict;
    }();
    return lookup;
}

std::size_t UnitParser::MaxUnitSize() {
    static const std::size_t max_size = [] {
        std::size_t count = 0;
        const UnitEntry* entries = UnitData::GetDict(count);
        std::size_t max_value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            max_value = std::max<std::size_t>(max_value,
                                              std::strlen(entries[i].text));
        }
        return max_value;
    }();
    return max_size;
}

ParseResult UnitParser::ParseTokenEnhanced(std::string_view token,
                                           bool allow_partial) const {
    ParseResult result;

    if (token.empty()) {
        result.complete = true;
        return result;
    }

    std::string normalized;
    normalized.reserve(token.size());
    for (char ch : token) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            normalized.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch))));
        }
    }

    if (normalized.empty()) {
        result.complete = true;
        return result;
    }

    const auto& lookup = UnitLookup();
    std::size_t max_len = MaxUnitSize();
    const std::size_t n = normalized.size();

    std::vector<int> back(n + 1, -1);
    std::vector<Unit> matched(n + 1);
    back[0] = 0;

    std::string buffer;
    for (std::size_t i = 0; i < n; ++i) {
        if (back[i] < 0) {
            continue;
        }
        std::size_t limit = std::min(max_len, n - i);
        for (std::size_t len = limit; len > 0; --len) {
            buffer.assign(normalized.data() + i, len);
            auto it = lookup.find(buffer);
            if (it == lookup.end()) {
                continue;
            }
            if (back[i + len] >= 0) {
                continue;
            }
            back[i + len] = static_cast<int>(len);
            matched[i + len] = it->second;
        }
    }

    if (back[n] >= 0) {
        result.complete = true;
        result.matched_len = n;
        std::vector<Unit> sequence;
        for (std::size_t idx = n; idx > 0;) {
            int len = back[idx];
            if (len <= 0) {
                result.complete = false;
                result.matched_len = 0;
                result.units.clear();
                return result;
            }
            sequence.push_back(matched[idx]);
            idx -= static_cast<std::size_t>(len);
        }
        std::reverse(sequence.begin(), sequence.end());
        result.units = std::move(sequence);
        return result;
    }

    if (allow_partial) {
        std::size_t best_pos = 0;
        for (std::size_t i = n; i > 0; --i) {
            if (back[i] >= 0) {
                best_pos = i;
                break;
            }
        }
        if (best_pos > 0) {
            result.complete = false;
            result.matched_len = best_pos;
            std::vector<Unit> sequence;
            for (std::size_t idx = best_pos; idx > 0;) {
                int len = back[idx];
                if (len <= 0) break;
                sequence.push_back(matched[idx]);
                idx -= static_cast<std::size_t>(len);
            }
            std::reverse(sequence.begin(), sequence.end());
            result.units = std::move(sequence);
        }
    }

    return result;
}

} // namespace sime
