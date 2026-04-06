#pragma once

#include "common.h"
#include "score.h"
#include "unit.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sime {

class NineDecoder {
public:
    bool Load(const std::filesystem::path& pinyin_model_path);
    bool Ready() const { return ready_; }

    struct Result {
        std::vector<Unit> units;
        float_t score = 0.0;
        std::size_t cnt = 0;
    };

    // Decode num sequence into ranked unit parses (full match only).
    std::vector<Result> Decode(std::string_view nums,
                               std::size_t num = 5) const;

private:
    struct SyllableEntry {
        TokenID token_id = 0;
        Unit unit;
    };

    void BuildNumMap();
    static char LetterToNum(char c);
    static std::string UnitToNum(const char* unit);

    std::unordered_map<std::string, std::vector<SyllableEntry>> num_map_;
    std::vector<Unit> token_to_unit_;
    std::unordered_map<std::uint32_t, TokenID> unit_to_token_;
    Scorer scorer_;
    bool ready_ = false;
};

} // namespace sime
