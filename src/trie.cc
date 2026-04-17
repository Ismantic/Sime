#include "trie.h"

#include <cassert>
#include <cstring>

namespace trie {

// --- DoubleArray ---

void DoubleArray::Build(const std::vector<std::string>& keys,
                        const std::vector<uint32_t>& values) {
    assert(keys.size() == values.size());
    Builder b;
    b.Run(keys, values);
    array_ = b.GetResult(size_);
}

bool DoubleArray::Get(std::string_view key, uint32_t& out) const {
    if (Empty()) return false;

    std::size_t pos = 0;
    for (std::size_t i = 0; i < key.size(); ++i) {
        if (pos >= size_) return false;
        auto ch = static_cast<uint8_t>(key[i]);
        std::size_t prev = pos;
        std::size_t next = pos ^ array_[pos].index ^ ch;
        if (next >= size_) return false;
        pos = next;
        if (array_[pos].label != ch || array_[pos].parent != prev)
            return false;
    }
    if (!array_[pos].eow) return false;
    std::size_t vp = pos ^ array_[pos].index;
    if (vp >= size_ || !array_[vp].HasValue()) return false;
    out = static_cast<uint32_t>(array_[vp].value);
    return true;
}

std::vector<SearchResult> DoubleArray::PrefixSearch(
    std::string_view str, std::size_t max_num) const {
    std::vector<SearchResult> results;
    if (Empty()) return results;

    std::size_t pos = 0;

    // Check empty-string key at root.
    if (array_[0].eow) {
        std::size_t vp = 0 ^ array_[0].index;
        if (vp < size_ && array_[vp].HasValue()) {
            results.push_back({static_cast<uint32_t>(array_[vp].value), 0});
            if (results.size() >= max_num) return results;
        }
    }

    for (std::size_t i = 0; i < str.size(); ++i) {
        if (pos >= size_) break;
        auto ch = static_cast<uint8_t>(str[i]);
        std::size_t prev = pos;
        std::size_t next = pos ^ array_[pos].index ^ ch;
        if (next >= size_) break;
        pos = next;
        if (array_[pos].label != ch || array_[pos].parent != prev) break;

        if (array_[pos].eow) {
            std::size_t vp = pos ^ array_[pos].index;
            if (vp < size_ && array_[vp].HasValue()) {
                results.push_back(
                    {static_cast<uint32_t>(array_[vp].value), i + 1});
                if (results.size() >= max_num) break;
            }
        }
    }
    return results;
}

std::vector<SearchResult> DoubleArray::FindWordsWithPrefix(
    std::string_view prefix, std::size_t max_num) const {
    std::vector<SearchResult> results;
    if (Empty()) return results;

    std::size_t pos = 0;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        auto ch = static_cast<uint8_t>(prefix[i]);
        std::size_t prev = pos;
        pos ^= array_[pos].index ^ ch;
        if (pos >= size_) return results;
        if (array_[pos].label != ch || array_[pos].parent != prev)
            return results;
    }

    std::string word(prefix);
    CollectWords(pos, word, results, max_num);
    return results;
}

void DoubleArray::CollectWords(std::size_t pos, std::string& word,
                               std::vector<SearchResult>& results,
                               std::size_t max_num) const {
    if (results.size() >= max_num || pos >= size_) return;

    if (array_[pos].eow) {
        std::size_t vp = pos ^ array_[pos].index;
        if (vp < size_ && array_[vp].HasValue()) {
            results.push_back(
                {static_cast<uint32_t>(array_[vp].value), word.size()});
            if (results.size() >= max_num) return;
        }
    }

    uint32_t base = array_[pos].index;
    for (int ch = 1; ch <= 255; ++ch) {
        std::size_t child = pos ^ base ^ static_cast<unsigned>(ch);
        if (child >= size_ || child == pos) continue;
        if (array_[child].label != ch || array_[child].parent != pos) continue;
        word.push_back(static_cast<char>(ch));
        CollectWords(child, word, results, max_num);
        word.pop_back();
        if (results.size() >= max_num) return;
    }
}

// --- Serialize / Deserialize ---

void DoubleArray::Serialize(std::vector<char>& buffer) const {
    auto sz = static_cast<uint32_t>(size_);
    std::size_t offset = buffer.size();
    // Each ArrayUnit: label(1) + eow(1) + index/value(4) + parent(4) = 10 bytes
    constexpr std::size_t kUnitBytes = 10;
    buffer.resize(offset + sizeof(sz) + sz * kUnitBytes);
    std::memcpy(buffer.data() + offset, &sz, sizeof(sz));
    offset += sizeof(sz);
    for (uint32_t i = 0; i < sz; ++i) {
        const auto& u = array_[i];
        buffer[offset++] = static_cast<char>(u.label);
        buffer[offset++] = static_cast<char>(u.eow ? 1 : 0);
        std::memcpy(buffer.data() + offset, &u.index, sizeof(u.index));
        offset += sizeof(u.index);
        std::memcpy(buffer.data() + offset, &u.parent, sizeof(u.parent));
        offset += sizeof(u.parent);
    }
}

bool DoubleArray::Deserialize(const char* data, std::size_t size) {
    if (size < sizeof(uint32_t)) return false;
    uint32_t sz = 0;
    std::memcpy(&sz, data, sizeof(sz));
    constexpr std::size_t kUnitBytes = 10;
    if (size < sizeof(sz) + sz * kUnitBytes) return false;

    size_ = sz;
    array_ = std::make_unique<ArrayUnit[]>(sz);
    std::size_t offset = sizeof(sz);
    for (uint32_t i = 0; i < sz; ++i) {
        auto& u = array_[i];
        u.label = static_cast<uint8_t>(data[offset++]);
        u.eow = data[offset++] != 0;
        std::memcpy(&u.index, data + offset, sizeof(u.index));
        offset += sizeof(u.index);
        std::memcpy(&u.parent, data + offset, sizeof(u.parent));
        offset += sizeof(u.parent);
    }
    return true;
}

// --- Builder ---

void DoubleArray::Builder::Run(const std::vector<std::string>& keys,
                               const std::vector<uint32_t>& values) {
    // Build intermediate trie.
    auto root = std::make_unique<TrieNode>();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        TrieNode* cur = root.get();
        for (char c : keys[i]) {
            auto ch = static_cast<uint8_t>(c);
            auto& child = cur->children[ch];
            if (!child) child = std::make_unique<TrieNode>();
            cur = child.get();
        }
        cur->eow = true;
        cur->value = values[i];
    }

    // Convert to double array.
    units_.resize(1024);
    used_.resize(1024, false);
    used_[0] = true;
    units_[0].label = 0;
    if (!root->children.empty() || root->eow)
        ConvertNode(root.get(), 0);

    // Trim trailing unused.
    while (!units_.empty() && !used_.back()) {
        units_.pop_back();
        used_.pop_back();
    }
}

std::unique_ptr<ArrayUnit[]> DoubleArray::Builder::GetResult(std::size_t& size) {
    size = units_.size();
    auto result = std::make_unique<ArrayUnit[]>(size);
    for (std::size_t i = 0; i < size; ++i) result[i] = units_[i];
    return result;
}

void DoubleArray::Builder::ConvertNode(TrieNode* node, std::size_t pos) {
    std::vector<uint8_t> labels;
    std::vector<TrieNode*> children;
    for (const auto& [ch, child] : node->children) {
        labels.push_back(ch);
        children.push_back(child.get());
    }
    if (node->eow) {
        labels.push_back(0);
        children.push_back(nullptr);
    }
    if (labels.empty()) return;

    uint32_t base = SetupChildren(labels, pos, node);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (labels[i] != 0) {
            ConvertNode(children[i], base ^ labels[i]);
        }
    }
}

uint32_t DoubleArray::Builder::SetupChildren(
    const std::vector<uint8_t>& labels, std::size_t pos, TrieNode* node) {
    uint32_t base = FindFreeBase(labels);
    units_[pos].index = static_cast<uint32_t>(pos ^ base);

    for (std::size_t i = 0; i < labels.size(); ++i) {
        std::size_t p = base ^ labels[i];
        EnsureSize(p + 1);
        used_[p] = true;
        units_[p].label = labels[i];
        units_[p].parent = static_cast<uint32_t>(pos);
        if (labels[i] == 0) {
            units_[p].value = static_cast<int32_t>(node->value);
            units_[p].eow = true;
            units_[pos].eow = true;
        }
    }
    return base;
}

uint32_t DoubleArray::Builder::FindFreeBase(const std::vector<uint8_t>& labels) {
    uint32_t start = (prev_base_ > 256) ? prev_base_ - 256 : 1;
    for (uint32_t base = start; ; ++base) {
        bool ok = true;
        for (auto l : labels) {
            std::size_t p = base ^ l;
            EnsureSize(p + 1);
            if (used_[p]) { ok = false; break; }
        }
        if (ok) {
            prev_base_ = base;
            return base;
        }
    }
}

void DoubleArray::Builder::EnsureSize(std::size_t n) {
    if (n > units_.size()) {
        std::size_t ns = std::max(n, units_.size() * 2);
        units_.resize(ns);
        used_.resize(ns, false);
    }
}

} // namespace trie
