#pragma once

#include <stdint.h>
#include <assert.h>

#include <vector>
#include <memory>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include <iostream>
#include <iomanip>

namespace trie {

class DartsException : public std::runtime_error {
public:
    explicit DartsException(const std::string &m) : std::runtime_error(m) {}
};

class ArrayUnit {
private:
    uint8_t label_;
    bool eow_;
    union {
        uint32_t index_;
        int32_t value_;
    };
    uint32_t parent_;

public:
    ArrayUnit() : label_(0), eow_(false), index_(0), parent_(0) {}

    bool Eow() const { return eow_; }

    int32_t Value() const {
        return (HasValue()) ? value_ : 0;
    }

    uint8_t Label() const { return label_; }

    uint32_t Index() const {
        return (!HasValue()) ? index_ : 0;
    }

    uint32_t Parent() const { return parent_; }

    bool HasValue() const { return label_ == '\0' && eow_; }

    void SetEow(bool e) { eow_ = e; }

    void SetValue(int32_t v) {
        value_ = v;
        label_ = '\0';
        eow_ = true;
    }

    void SetLabel(uint8_t label) {
        label_ = label;
    }

    void SetIndex(uint32_t offset) {
        index_ = offset;
    }

    void SetParent(uint32_t parent) { parent_ = parent; }

    bool IsEmpty() const {
        return index_ == 0 && label_ == 0 && !eow_ && parent_ == 0;
    }

    void Print(const std::string& p = "") const {
        std::cout << p << "Unit: Eow=" << eow_
                  << ", Label=" << static_cast<int>(label_)
                  << ", Parent=" << parent_;
        if (HasValue()) {
            std::cout << ", Value=" << value_;
        } else {
            std::cout << ", Index=" << index_;
        }
        std::cout << std::endl;
    }
};

template <typename T = int32_t>
class DoubleArray {
public:
    using result_type = T;

    struct SearchResult {
        T value;
        std::size_t length;
        bool found;

        SearchResult() : value(T{}), length(0), found(false) {}
        SearchResult(T v, std::size_t n) : value(v), length(n), found(true) {}
    };

private:
    std::size_t size_;
    std::unique_ptr<ArrayUnit[]> array_;

public:
    DoubleArray() : size_(0) {}

    DoubleArray(DoubleArray&& other) noexcept
        : size_(other.size_), array_(std::move(other.array_)) {
        other.size_ = 0;
    }

    DoubleArray& operator=(DoubleArray&& other) noexcept {
        if (this != &other) {
            size_ = other.size_;
            array_ = std::move(other.array_);
            other.size_ = 0;
        }
        return *this;
    }

    DoubleArray(const DoubleArray&) = delete;
    DoubleArray& operator=(const DoubleArray&) = delete;

    void Build(const std::vector<std::string>& strs) {
        if (!IsSort(strs)) {
            throw DartsException("Strs not sort?");
        }

        std::vector<T> default_values;
        for (size_t i = 0; i < strs.size(); ++i) {
            default_values.push_back(static_cast<T>(i));
        }
        BuildInternal(strs, default_values);
    }

    void Build(const std::vector<std::string>& strs, const std::vector<T>& values) {
        if (strs.size() != values.size()) {
            throw DartsException("Strs and Values size mismatch");
        }
        if (!IsSort(strs)) {
            throw DartsException("Strs not sort?");
        }

        BuildInternal(strs, values);
    }

    SearchResult GetUnit(const std::string& str) const {
        if (Empty()) return SearchResult();

        size_t pos = 0;
        const char* s = str.c_str();
        size_t n = str.length();

        for (size_t i = 0; i < n; ++i) {
            if (pos >= size_) return SearchResult();

            ArrayUnit unit = array_[pos];
            uint32_t index = unit.Index();
            uint8_t ch = static_cast<uint8_t>(s[i]);

            size_t prev_pos = pos;
            size_t next_pos = pos ^ index ^ ch;

            if (next_pos >= size_) return SearchResult();

            pos = next_pos;
            unit = array_[pos];

            if (unit.Label() != ch || unit.Parent() != prev_pos) {
                return SearchResult();
            }
        }

        ArrayUnit unit = array_[pos];
        if (!unit.Eow()) return SearchResult();

        size_t value_pos = pos ^ unit.Index();
        if (value_pos >= size_) return SearchResult();

        ArrayUnit value_node = array_[value_pos];
        if (!value_node.HasValue()) return SearchResult();

        return SearchResult(static_cast<T>(value_node.Value()), n);
    }

    std::vector<SearchResult> PrefixSearch(const std::string& str,
                                           std::size_t max_num = 96) const {
        std::vector<SearchResult> results;
        if (Empty()) return results;

        size_t pos = 0;
        const char* s = str.c_str();
        size_t n = str.length();

        if (array_[0].Eow()) {
            size_t value_pos = 0 ^ array_[0].Index();
            if (value_pos < size_ && array_[value_pos].HasValue()) {
                ArrayUnit value_node = array_[value_pos];
                results.emplace_back(static_cast<T>(value_node.Value()), 0);
                if (results.size() >= max_num) return results;
            }
        }

        for (size_t i = 0; i < n; ++i) {
            if (pos >= size_) break;

            ArrayUnit unit = array_[pos];
            uint32_t index = unit.Index();
            uint8_t ch = static_cast<uint8_t>(s[i]);

            size_t prev_pos = pos;
            size_t next_pos = pos ^ index ^ ch;
            if (next_pos >= size_) break;

            pos = next_pos;
            ArrayUnit next_unit = array_[pos];

            if (next_unit.Label() != ch || next_unit.Parent() != prev_pos) break;

            if (next_unit.Eow()) {
                size_t value_pos = pos ^ next_unit.Index();
                if (value_pos < size_ && array_[value_pos].HasValue()) {
                    ArrayUnit value_node = array_[value_pos];
                    results.emplace_back(static_cast<T>(value_node.Value()), i + 1);
                    if (results.size() >= max_num) break;
                }
            }
        }

        return results;
    }

    std::vector<SearchResult> FindWordsWithPrefix(const std::string& prefix,
                                                 std::size_t max_num = 96) const {
        std::vector<SearchResult> results;
        if (Empty()) return results;

        size_t pos = 0;
        for (size_t i = 0; i < prefix.length(); ++i) {
            ArrayUnit unit = array_[pos];
            size_t prev_pos = pos;
            pos ^= unit.Index() ^ static_cast<uint8_t>(prefix[i]);

            if (pos >= size_) return results;

            unit = array_[pos];
            if (unit.Label() != static_cast<uint8_t>(prefix[i]) ||
                unit.Parent() != prev_pos) {
                return results;
            }
        }

        std::string current_word = prefix;
        std::set<T> found_values;

        CollectPrefixWords(pos, current_word, results, found_values, max_num);

        return results;
    }

    size_t Size() const { return size_; }
    bool Empty() const { return size_ == 0; }

    void Print() const {
        std::cout << "=== DoubleArray ===" << std::endl;
        std::cout << "Size: " << size_ << std::endl;

        size_t use_count = 0;
        size_t value_count = 0;
        size_t branch_count = 0;

        for (std::size_t i = 0; i < size_; ++i) {
            if (!array_[i].IsEmpty()) {
                use_count++;
                if (array_[i].HasValue()) {
                    value_count++;
                } else {
                    branch_count++;
                }
            }
        }
        float rate = size_ > 0 ? (100.0 * use_count / size_) : 0.0;

        std::cout << "Memory Usage:" << std::endl;
        std::cout << "  Total units: " << size_ << std::endl;
        std::cout << "  Used units: " << use_count << std::endl;
        std::cout << "  Empty units: " << (size_ - use_count) << std::endl;
        std::cout << "  Usage rate: " << std::fixed << std::setprecision(2) << rate << "%" << std::endl;
        std::cout << "  Branch nodes: " << branch_count << std::endl;
        std::cout << "  Value nodes: " << value_count << std::endl;
        std::cout << "  Memory size: " << (size_ * sizeof(ArrayUnit)) << " bytes ("
                  << std::fixed << std::setprecision(1) << (size_ * sizeof(ArrayUnit) / 1024.0 / 1024.0) << " MB)" << std::endl;

        std::cout << std::endl;

        for (std::size_t i = 0; i < std::min(size_, std::size_t(20)); ++i) {
            std::cout << "Pos " << i << ": ";
            array_[i].Print();
        }
        if (size_ > 20) {
            std::cout << "... (omit " << (size_ - 20) << " units)" << std::endl;
        }
    }

private:
    void CollectPrefixWords(size_t pos, std::string& current_word,
                           std::vector<SearchResult>& results, std::set<T>& found_values,
                           std::size_t max_num) const {
        if (results.size() >= max_num) return;
        if (pos >= size_) return;

        ArrayUnit unit = array_[pos];

        if (unit.Eow()) {
            size_t value_pos = pos ^ unit.Index();
            if (value_pos < size_ && array_[value_pos].HasValue()) {
                ArrayUnit value_node = array_[value_pos];
                T value = static_cast<T>(value_node.Value());

                if (found_values.find(value) == found_values.end()) {
                    found_values.insert(value);
                    results.emplace_back(value, current_word.length());
                    if (results.size() >= max_num) return;
                }
            }
        }

        uint32_t base_index = unit.Index();

        for (int ch = 0; ch <= 255; ++ch) {
            size_t child_pos = pos ^ base_index ^ ch;

            if (child_pos >= size_) continue;
            if (child_pos == pos) continue;

            ArrayUnit child_unit = array_[child_pos];

            if (child_unit.Label() == ch && child_unit.Label() != '\0' && child_unit.Parent() == pos) {
                current_word.push_back(static_cast<char>(ch));
                CollectPrefixWords(child_pos, current_word, results, found_values, max_num);
                current_word.pop_back();

                if (results.size() >= max_num) return;
            }
        }
    }

    class Builder {
    private:
        struct TrieNode {
            std::map<uint8_t, std::unique_ptr<TrieNode>> down_nodes;
            bool eow = false;
            T value = T{};
        };

        std::vector<ArrayUnit> units_;
        std::vector<bool> uses_;
        std::unique_ptr<TrieNode> root_;

        uint32_t prev_pos_ = 0;

    public:
        void Build(const std::vector<std::string>& strs, const std::vector<T>& values) {
            if (strs.empty()) return;

            BuildTrie(strs, values);
            ConvertTrie();
        }

        std::unique_ptr<ArrayUnit[]> GetResult(std::size_t& size) {
            size = units_.size();
            auto result = std::make_unique<ArrayUnit[]>(size);

            for (std::size_t i = 0; i < size; ++i) {
                result[i] = units_[i];
            }

            return result;
        }

    private:
        void BuildTrie(const std::vector<std::string>& strs,
                       const std::vector<T>& values) {
            root_ = std::make_unique<TrieNode>();

            for (std::size_t i = 0; i < strs.size(); ++i) {
                InsertToTrie(strs[i], values[i]);
            }
        }

        void InsertToTrie(const std::string& str, T value) {
            TrieNode* current = root_.get();

            for (char c : str) {
                uint8_t t = static_cast<uint8_t>(c);
                if (current->down_nodes.find(t) == current->down_nodes.end()) {
                    current->down_nodes[t] = std::make_unique<TrieNode>();
                }
                current = current->down_nodes[t].get();
            }

            current->eow = true;
            current->value = value;
        }

        void ConvertTrie() {
            size_t esize = 1024;
            units_.resize(esize);
            uses_.resize(esize, false);

            uses_[0] = true;
            units_[0].SetLabel('\0');

            if (!root_->down_nodes.empty()) {
                ConvertNode(root_.get(), 0);
            }

            TrimArrays();
        }

        void ConvertNode(TrieNode* node, size_t pos) {
            std::vector<uint8_t> es;
            std::vector<TrieNode*> down_nodes;

            for (const auto& n : node->down_nodes) {
                es.push_back(n.first);
                down_nodes.push_back(n.second.get());
            }

            if (node->eow) {
                es.push_back('\0');
                down_nodes.push_back(nullptr);
            }

            if (es.empty()) return;

            uint32_t index = SetupDownNodes(es, pos, node);

            for (size_t i = 0; i < es.size(); ++i) {
                uint8_t e = es[i];
                if (e != '\0') {
                    size_t down_pos = index ^ e;
                    ConvertNode(down_nodes[i], down_pos);
                }
            }
        }

        uint32_t SetupDownNodes(const std::vector<uint8_t>& es, size_t pos, TrieNode* node) {
            if (es.empty()) return 0;

            uint32_t index = GetFreeIndex(es, pos);
            units_[pos].SetIndex(static_cast<uint32_t>(pos ^ index));

            for (size_t i = 0; i < es.size(); ++i) {
                uint8_t e = es[i];
                size_t p = index ^ e;

                EnsureSize(p + 1);
                uses_[p] = true;

                if (e == '\0') {
                    units_[p].SetLabel(e);
                    units_[p].SetValue(static_cast<int32_t>(node->value));
                    units_[p].SetEow(true);
                    units_[p].SetParent(static_cast<uint32_t>(pos));
                    units_[pos].SetEow(true);
                } else {
                    units_[p].SetLabel(e);
                    units_[p].SetParent(static_cast<uint32_t>(pos));
                }
            }

            return index;
        }

        uint32_t GetFreeIndex(const std::vector<uint8_t>& es, std::size_t /*pos*/) {
            if (es.empty()) return 0;

            uint32_t start_index = (prev_pos_ > 256) ? prev_pos_ - 256 : 1;

            for (uint32_t i = start_index; ; ++i) {
                bool v = true;

                for (uint8_t e : es) {
                    size_t p = i ^ e;
                    EnsureSize(p + 1);

                    if (uses_[p]) {
                        v = false;
                        break;
                    }
                }

                if (v) {
                    prev_pos_ = i;
                    return i;
                }
            }
        }

        void EnsureSize(std::size_t size) {
            if (size > units_.size()) {
                std::size_t new_size = std::max(size, units_.size()*2);
                units_.resize(new_size);
                uses_.resize(new_size, false);
            }
        }

        void TrimArrays() {
            while (!units_.empty() && !uses_.back()) {
                units_.pop_back();
                uses_.pop_back();
            }
        }
    };

    void BuildInternal(const std::vector<std::string>& strs, const std::vector<T>& values) {
        Builder e;
        e.Build(strs, values);
        array_ = e.GetResult(size_);
    }

    static bool IsSort(const std::vector<std::string>& strs) {
        for (size_t i = 1; i < strs.size(); ++i) {
            if (strs[i-1] > strs[i]) return false;
        }
        return true;
    }
};

} // namespace trie
