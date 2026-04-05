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
        std::size_t cnt = 0; // digits consumed (0 = all)
    };

    // Decode digit sequence into ranked pinyin parses (full match only).
    std::vector<Result> Decode(std::string_view digits,
                               std::size_t num = 5) const;

    struct SentenceResult {
        Result best;                  // beam search best full-match parse
        std::vector<Result> candidates; // single-syllable exact matches
    };

    // best: beam search over prefix + digits (with tail expansion).
    // candidates: single-syllable exact matches from digit prefixes.
    // prefix: locked confirmed syllables (empty for initial decode).
    SentenceResult DecodeSentence(std::string_view digits,
                                  const std::vector<Unit>& prefix = {},
                                  std::size_t num = 18) const;

    struct SyllableEntry {
        TokenID token_id = 0;
        Unit unit;
    };

    const std::unordered_map<std::string, std::vector<SyllableEntry>>&
    DigitMap() const { return digit_map_; }

private:

    void BuildDigitMap();

    static char LetterToDigit(char c);
    static std::string PinyinToDigits(const char* pinyin);

    // digit_string → candidate syllables
    std::unordered_map<std::string, std::vector<SyllableEntry>> digit_map_;

    // TokenID - StartToken → Unit (for backtrace)
    std::vector<Unit> token_to_unit_;

    // Unit.value → TokenID (for prefix lookup)
    std::unordered_map<std::uint32_t, TokenID> unit_to_token_;

    Scorer scorer_;
    bool ready_ = false;
};

} // namespace sime
