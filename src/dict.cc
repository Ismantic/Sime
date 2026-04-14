#include "dict.h"
#include "ustr.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>

namespace sime {

const Dict::Move* Dict::Node::GetMove() const {
    const char* p = reinterpret_cast<const char*>(this + 1);
    return reinterpret_cast<const Dict::Move*>(p);
}

const std::uint32_t* Dict::Node::GetToken() const {
    const char* p = 
        reinterpret_cast<const char*>(this + 1) + 
        sizeof(Dict::Move) * move_count;
    return reinterpret_cast<const std::uint32_t*>(p);
}

Dict::~Dict() { Clear(); }

void Dict::Clear() {
    blob_.clear();
    token_strs_.clear();
}

uint32_t Dict::NodeCount() const {
    if (blob_.size() < sizeof(std::uint32_t)) {
        return 0;
    }
    return *reinterpret_cast<const std::uint32_t*>(blob_.data());
}

uint32_t Dict::TokenCount() const {
    if (blob_.size() < 2 * sizeof(std::uint32_t)) {
        return 0;
    }
    return reinterpret_cast<const std::uint32_t*>(blob_.data())[1];
}

std::uint32_t Dict::TokenIndex() const {
    if (blob_.size() < 3 * sizeof(std::uint32_t)) {
        return 0;
    }
    return reinterpret_cast<const std::uint32_t*>(blob_.data())[2];
}

std::uint32_t Dict::PieceIndex() const {
    if (blob_.size() < 4 * sizeof(std::uint32_t)) {
        return 0;
    }
    return reinterpret_cast<const std::uint32_t*>(blob_.data())[3];
}

std::uint32_t Dict::RootIndex() const {
    return 4U * static_cast<std::uint32_t>(sizeof(std::uint32_t));
}

const Dict::Node* Dict::NodeFrom(std::uint32_t i) const {
    if (blob_.empty() || i < RootIndex() || i >= blob_.size()) {
        return nullptr;
    }
    const char* p = blob_.data() + i;
    return reinterpret_cast<const Dict::Node*>(p);
}

const Dict::Node* Dict::Root() const {
    return NodeFrom(RootIndex());
}

bool Dict::Load(const std::filesystem::path& path) {
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

    uint32_t count = TokenCount();
    token_strs_.reserve(count);
    const char* base = blob_.data() + TokenIndex();
    auto p = reinterpret_cast<const char32_t*>(base);
    for (uint32_t i = 0; i < count; ++i) {
        token_strs_.push_back(p);
        while (*p++) {}
    }

    // Load piece table
    std::uint32_t pi = PieceIndex();
    if (pi > 0 && pi < blob_.size()) {
        if (!piece_.Deserialize(blob_.data() + pi,
                                blob_.size() - pi)) {
            Clear();
            return false;
        }
    }

    return true;
}

const Dict::Node* Dict::DoMove(const Dict::Node* node, Unit u) const {
    if (node == nullptr || node->move_count == 0) {
        return nullptr;
    }
    const auto* begin = node->GetMove();
    const auto* end = begin + node->move_count;
    const auto it = std::lower_bound(
        begin,
        end,
        u.value,
        [](const Dict::Move& move, std::uint32_t value) {
            return move.unit.value < value;
        });
    if (it == end || it->unit.value != u.value) {
        return nullptr;
    }
    return NodeFrom(it->next);
}

const std::uint32_t* Dict::GetToken(const Dict::Node* node, uint32_t& count) const {
    if (node == nullptr) {
        count = 0;
        return nullptr;
    } 
    count = node->count;
    return node->GetToken();
}

const char32_t* Dict::TokenAt(uint32_t i) const {
    if (i >= token_strs_.size()) {
        return nullptr;
    }
    return token_strs_[i];
}

} // namespace sime
