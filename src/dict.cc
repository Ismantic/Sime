#include "dict.h"
#include "ustr.h"

#include <cstring>
#include <fstream>
#include <sstream>

namespace sime {

bool Dict::Load(const std::filesystem::path& path) {
    dict_.clear();
    entries_.clear();

    std::ifstream in(path);
    if (!in) {
        return false;
    }

    UnitParser parser;
    std::string line;
    while (std::getline(in, line)) {
        auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue;

        // Parse: 汉字 拼音
        std::istringstream ss(line.substr(start));
        std::string hanzi, pinyin;
        if (!(ss >> hanzi >> pinyin)) continue;

        std::vector<Unit> units;
        if (!parser.ParseStr(pinyin, units)) continue;
        if (units.empty()) continue;

        std::size_t idx = entries_.size();
        entries_.push_back(ustr::ToU32(hanzi));

        std::string key = MakeKey(units.data(), units.size());
        dict_[key].push_back(idx);
    }

    return !entries_.empty();
}

std::vector<std::size_t> Dict::Lookup(
    const Unit* units, std::size_t len) const {
    std::string key = MakeKey(units, len);
    auto it = dict_.find(key);
    if (it == dict_.end()) return {};
    return it->second;
}

const std::u32string& Dict::TextAt(std::size_t idx) const {
    return entries_[idx];
}

std::string Dict::MakeKey(const Unit* units, std::size_t len) {
    std::string key;
    key.resize(len * sizeof(std::uint32_t));
    for (std::size_t i = 0; i < len; ++i) {
        std::uint32_t v = units[i].value;
        std::memcpy(key.data() + i * sizeof(v), &v, sizeof(v));
    }
    return key;
}

} // namespace sime
