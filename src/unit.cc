
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

const char* const kInitials[] = {"", "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h", "j", "q", "x", "zh", "ch", "sh", "r", "z", "c", "s", "y", "w", "hm"};
constexpr std::size_t kNumInitials = sizeof(kInitials) / sizeof(*kInitials);

const char* const kFinals[] = {"", "a", "o", "e", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng", "er", "i", "ia", "ie", "iao", "iu", "ian", "in", "iang", "ing", "u", "ua", "uo", "uai", "ui", "uan", "un", "uang", "ong", "v", "ve", "ue", "iong", "ng"};
constexpr std::size_t kNumFinals = sizeof(kFinals) / sizeof(*kFinals);

const UnitEntry kPinyinTable[] = {
#include "data.inc"
};

constexpr std::size_t kPinyinTableSize = sizeof(kPinyinTable) / sizeof(kPinyinTable[0]);

const UnitEntry* GetEntry(const char* text) {
    std::size_t left = 0;
    std::size_t right = kPinyinTableSize;
    while (left < right) {
        std::size_t mid = left + (right - left) / 2;
        int cmp = CompareEntry(text, kPinyinTable[mid]);
        if (cmp == 0) {
            return &kPinyinTable[mid];
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
        return Unit(entry->i);
    }
    return Unit{};
}

const char* UnitData::Decode(Unit syllable,
                             const char** initial,
                             const char** final) {
    if (initial) {
        *initial = kInitials[syllable.I()];
    }
    if (final) {
        *final = kFinals[syllable.A()];
    }
    static char buffer[128];
    Compose(kInitials[syllable.I()], kFinals[syllable.A()], buffer, sizeof(buffer));
    if (const UnitEntry* entry = GetEntry(buffer)) {
        return entry->text;
    }
    return buffer;
}

const char* const* UnitData::Is(std::size_t& count) {
    count = kNumInitials;
    return kInitials;
}

const char* const* UnitData::As(std::size_t& count) {
    count = kNumFinals;
    return kFinals;
}

const UnitEntry* UnitData::GetTable(std::size_t& count) {
    count = kPinyinTableSize;
    return kPinyinTable;
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

bool UnitParser::ParseToken(std::string_view token,
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
    std::size_t max_len = MaxUnitLength();
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

bool UnitParser::ParseUnits(std::string_view phone,
                            std::vector<Unit>& units) const {
    units.clear();
    std::size_t pos = 0;
    while (pos <= phone.size()) {
        std::size_t next = phone.find('\'', pos);
        std::string_view segment =
            phone.substr(pos, next == std::string::npos
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
        std::unordered_map<std::string, Unit> table;
        std::size_t count = 0;
        const UnitEntry* entries = UnitData::GetTable(count);
        table.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            std::string key(entries[i].text);
            for (char& ch : key) {
                ch = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch)));
            }
            table.emplace(std::move(key), Unit(entries[i].i));
        }
        return table;
    }();
    return lookup;
}

std::size_t UnitParser::MaxUnitLength() {
    static const std::size_t max_len = [] {
        std::size_t count = 0;
        const UnitEntry* entries = UnitData::GetTable(count);
        std::size_t max_value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            max_value = std::max<std::size_t>(max_value,
                                              std::strlen(entries[i].text));
        }
        return max_value;
    }();
    return max_len;
}

} // namespace sime
