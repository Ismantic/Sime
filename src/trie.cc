#include "trie.h"
#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string_view>

namespace sime {

const Move* Node::GetMove() const {
    const char* p = reinterpret_cast<const char*>(this + 1);
    return reinterpret_cast<const Move*>(p);
}

const Entry* Node::GetEntry() const {
    const char* p = 
        reinterpret_cast<const char*>(this + 1) + 
        sizeof(Move) * move_count;
    return reinterpret_cast<const Entry*>(p);
}

Trie::~Trie() { Clear(); }

void Trie::Clear() {
    blob_.clear();
    wstrs_.clear();
}

uint32_t Trie::StrCount() const {
    if (blob_.size() < sizeof(std::uint32_t)) {
        return 0;
    }
    return *reinterpret_cast<const std::uint32_t*>(blob_.data());
}

uint32_t Trie::NodeCount() const {
    if (blob_.size() < 2 * sizeof(std::uint32_t)) {
        return 0;
    }
    return reinterpret_cast<const std::uint32_t*>(blob_.data())[1];
}

std::uint32_t Trie::StrIndex() const {
    if (blob_.size() < 3 * sizeof(std::uint32_t)) {
        return 0;
    }
    return reinterpret_cast<const std::uint32_t*>(blob_.data())[2];
}

std::uint32_t Trie::RootIndex() const {
    return 3U * static_cast<std::uint32_t>(sizeof(std::uint32_t));
}

const Node* Trie::NodeFrom(std::uint32_t i) const {
    if (blob_.empty() || i < RootIndex() || i >= blob_.size()) {
        return nullptr;
    }
    const char* p = blob_.data() + i;
    return reinterpret_cast<const Node*>(p);
}

const Node* Trie::Root() const {
    return NodeFrom(RootIndex());
}

bool Trie::Load(const std::filesystem::path& path) {
    Clear();

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        return false;
    }
    blob_.resize(static_cast<std::size_t>(size));
    if (!in.read(blob_.data(), size)) {
        Clear();
        return false;
    }

    uint32_t count = StrCount();
    wstrs_.reserve(count);
    const char* base = blob_.data() + StrIndex();
    auto p = reinterpret_cast<const char32_t*>(base);
    for (uint32_t i = 0; i < count; ++i) {
        wstrs_.push_back(p);
        while (*p++) {}
    }

    return true;
}

const Node* Trie::DoMove(const Node* node, Unit u) const {
    if (node == nullptr || node->move_count == 0) {
        return nullptr;
    }
    const auto* begin = node->GetMove();
    const auto* end = begin + node->move_count;
    const auto it = std::lower_bound(
        begin,
        end,
        u.value,
        [](const Move& move, std::uint32_t value) {
            return move.unit.value < value;
        });
    if (it == end || it->unit.value != u.value) {
        return nullptr;
    }
    return NodeFrom(it->i);
}

const Entry* Trie::GetEntry(const Node* node, uint32_t& count) const {
    if (node == nullptr) {
        count = 0;
        return nullptr;
    } 
    count = node->count;
    return node->GetEntry();
}

const char32_t* Trie::StrAt(uint32_t i) const {
    if (i >= wstrs_.size()) {
        return nullptr;
    }
    return wstrs_[i];
}

} // namespace sime
