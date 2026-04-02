#pragma once

#include "unit.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace sime {

class Dict {
public:
    // Load dict entries from file. Format: "汉字 拼音" per line.
    bool Load(const std::filesystem::path& path);

    // Given units[0..len), return indices of matching entries.
    std::vector<std::size_t> Lookup(const Unit* units, std::size_t len) const;

    const std::u32string& TextAt(std::size_t idx) const;

    std::size_t EntryCount() const { return entries_.size(); }
    bool Empty() const { return entries_.empty(); }

private:
    static std::string MakeKey(const Unit* units, std::size_t len);

    // key = packed unit sequence, value = indices into entries_
    std::unordered_map<std::string, std::vector<std::size_t>> dict_;
    std::vector<std::u32string> entries_;
};

} // namespace sime
