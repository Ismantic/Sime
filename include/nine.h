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
        std::vector<Unit> pinyin;
        float_t score = 0.0;
    };

    // Decode digit sequence into ranked pinyin parses.
    std::vector<Result> Decode(std::string_view digits,
                               std::size_t num = 5) const;

private:
    struct SyllableEntry {
        TokenID token_id = 0;
        Unit unit;
    };

    void BuildDigitMap();

    static char LetterToDigit(char c);
    static std::string PinyinToDigits(const char* pinyin);

    // digit_string → candidate syllables
    std::unordered_map<std::string, std::vector<SyllableEntry>> digit_map_;

    // TokenID - StartToken → Unit (for backtrace)
    std::vector<Unit> token_to_unit_;

    Scorer scorer_;
    bool ready_ = false;
};

} // namespace sime
