
#include "unit.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace sime {


namespace {

inline int CompareEntry(const char* key, const UnitEntry& entry) {
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
    thread_local char buffer[128];
    Compose(Is[ii], As[ai], buffer, sizeof(buffer));
    if (const UnitEntry* entry = GetEntry(buffer)) {
        return entry->text;
    }
    return buffer;
}

const UnitEntry* UnitData::GetDict(std::size_t& count) {
    count = DictSize;
    return Dict;
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

} // namespace sime
