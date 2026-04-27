#include "dict.h"

#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>

namespace sime {

namespace {

struct DictEntry {
    const char* text;
    std::uint32_t value;
};

const DictEntry PinyinDict[] = {
#include "dict.inc"
};

constexpr std::size_t PinyinDictSize = sizeof(PinyinDict) / sizeof(PinyinDict[0]);

} // namespace

Dict::~Dict() { Clear(); }

void Dict::Clear() {
    for (int t = 0; t < DatCount; ++t) {
        dats_[t] = trie::DoubleArray();
        side_offsets_[t].clear();
    }
    scratch_.clear();
    token_count_ = 0;
    token_strs_.clear();
    if (mmap_addr_ && mmap_addr_ != MAP_FAILED) {
        munmap(mmap_addr_, mmap_len_);
    }
    mmap_addr_ = nullptr;
    mmap_len_ = 0;
}

bool Dict::Load(const std::filesystem::path& path) {
    Clear();

    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;

    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return false; }
    const auto size = static_cast<std::size_t>(st.st_size);
    if (size < 5 * sizeof(uint32_t)) { close(fd); return false; }

    mmap_addr_ = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mmap_addr_ == MAP_FAILED) {
        mmap_addr_ = nullptr;
        return false;
    }
    mmap_len_ = size;

    const char* base = static_cast<const char*>(mmap_addr_);

    // Parse header
    const auto* header = reinterpret_cast<const uint32_t*>(base);
    token_count_ = header[0];
    uint32_t token_offset = header[1];
    uint32_t section_offsets[DatCount];
    for (int t = 0; t < DatCount; ++t)
        section_offsets[t] = header[2 + t];
    uint32_t total_size = header[4];
    if (total_size != size) { Clear(); return false; }

    // Parse token text table — pointers into mmap'd memory.
    token_strs_.reserve(token_count_);
    auto p = reinterpret_cast<const char32_t*>(base + token_offset);
    for (uint32_t i = 0; i < token_count_; ++i) {
        token_strs_.push_back(p);
        while (*p++) {}
    }

    // Parse DAT sections
    for (int t = 0; t < DatCount; ++t) {
        std::size_t offset = section_offsets[t];
        if (offset >= size) continue;

        // Zero-copy attach DAT.
        std::size_t dat_consumed = 0;
        if (!dats_[t].MmapAttach(base + offset, size - offset, &dat_consumed)) {
            continue;
        }
        offset += dat_consumed;

        if (offset + sizeof(uint32_t) > size) continue;

        // Parse side table: build offset index, no data copy.
        uint32_t entry_count = 0;
        std::memcpy(&entry_count, base + offset, sizeof(entry_count));
        offset += sizeof(entry_count);

        side_offsets_[t].resize(entry_count);

        for (uint32_t e = 0; e < entry_count; ++e) {
            side_offsets_[t][e] = static_cast<uint32_t>(offset);

            if (offset + sizeof(uint16_t) > size) break;
            uint16_t item_count = 0;
            std::memcpy(&item_count, base + offset, sizeof(item_count));
            offset += sizeof(item_count);

            for (uint16_t j = 0; j < item_count; ++j) {
                if (offset + sizeof(TokenID) + sizeof(uint16_t) > size) break;
                offset += sizeof(TokenID);
                uint16_t pieces_len = 0;
                std::memcpy(&pieces_len, base + offset, sizeof(pieces_len));
                offset += sizeof(pieces_len);
                if (offset + pieces_len > size) break;
                offset += pieces_len + 1;  // +1 for null terminator
            }
        }
    }

    return true;
}

Dict::Entry Dict::GetEntry(DatType type, uint32_t index) const {
    if (index >= side_offsets_[type].size()) return {nullptr, 0};
    scratch_.clear();

    const char* base = static_cast<const char*>(mmap_addr_);
    auto offset = static_cast<std::size_t>(side_offsets_[type][index]);
    uint16_t item_count = 0;
    std::memcpy(&item_count, base + offset, sizeof(item_count));
    offset += sizeof(item_count);

    for (uint16_t j = 0; j < item_count; ++j) {
        TokenID id = 0;
        std::memcpy(&id, base + offset, sizeof(id));
        offset += sizeof(id);

        uint16_t pieces_len = 0;
        std::memcpy(&pieces_len, base + offset, sizeof(pieces_len));
        offset += sizeof(pieces_len);

        const char* pieces = base + offset;
        offset += pieces_len + 1;  // +1 for null terminator

        scratch_.push_back({id, pieces});
    }
    return {scratch_.data(), static_cast<uint32_t>(scratch_.size())};
}

const char32_t* Dict::TokenAt(uint32_t i) const {
    if (i >= token_strs_.size()) return nullptr;
    return token_strs_[i];
}

bool Dict::IsKnownPinyin(const std::string& text) {
    constexpr uint32_t FinalMask = 0xFFF;
    std::size_t left = 0;
    std::size_t right = PinyinDictSize;
    while (left < right) {
        std::size_t mid = left + (right - left) / 2;
        int cmp = std::strcmp(text.c_str(), PinyinDict[mid].text);
        if (cmp == 0) return (PinyinDict[mid].value & FinalMask) != 0;
        if (cmp > 0) left = mid + 1;
        else right = mid;
    }
    return false;
}

bool Dict::IsKnownT9Syllable(std::string_view digits) {
    if (digits.empty()) return false;
    static const std::unordered_set<std::string> set = []() {
        std::unordered_set<std::string> s;
        constexpr uint32_t FinalMask = 0xFFF;
        for (std::size_t i = 0; i < PinyinDictSize; ++i) {
            if ((PinyinDict[i].value & FinalMask) == 0) continue;
            s.insert(LettersToNums(PinyinDict[i].text));
        }
        return s;
    }();
    return set.contains(std::string(digits));
}

char Dict::LetterToNum(char c) {
    switch (c) {
    case 'a': case 'b': case 'c':
    case 'A': case 'B': case 'C': return '2';
    case 'd': case 'e': case 'f':
    case 'D': case 'E': case 'F': return '3';
    case 'g': case 'h': case 'i':
    case 'G': case 'H': case 'I': return '4';
    case 'j': case 'k': case 'l':
    case 'J': case 'K': case 'L': return '5';
    case 'm': case 'n': case 'o':
    case 'M': case 'N': case 'O': return '6';
    case 'p': case 'q': case 'r': case 's':
    case 'P': case 'Q': case 'R': case 'S': return '7';
    case 't': case 'u': case 'v':
    case 'T': case 'U': case 'V': return '8';
    case 'w': case 'x': case 'y': case 'z':
    case 'W': case 'X': case 'Y': case 'Z': return '9';
    default: return 0;
    }
}

std::string Dict::LettersToNums(std::string_view letters) {
    std::string result;
    for (char ch : letters) {
        char d = LetterToNum(ch);
        if (d != 0) result.push_back(d);
    }
    return result;
}

const char* Dict::NumToLetters(uint8_t digit) {
    switch (digit) {
    case '2': return "abcABC";
    case '3': return "defDEF";
    case '4': return "ghiGHI";
    case '5': return "jklJKL";
    case '6': return "mnoMNO";
    case '7': return "pqrsPQRS";
    case '8': return "tuvTUV";
    case '9': return "wxyzWXYZ";
    default: return nullptr;
    }
}

const char* Dict::NumToLettersLower(uint8_t digit) {
    switch (digit) {
    case '2': return "abc";
    case '3': return "def";
    case '4': return "ghi";
    case '5': return "jkl";
    case '6': return "mno";
    case '7': return "pqrs";
    case '8': return "tuv";
    case '9': return "wxyz";
    default: return nullptr;
    }
}

} // namespace sime
