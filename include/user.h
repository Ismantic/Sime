#pragma once

#include "common.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sime {

class UserSentence {
public:
    using Sentence = std::vector<TokenID>;
    using TokenTextFn = std::function<std::string(TokenID)>;

    // One candidate pick: the token IDs the decoder produced.
    struct Selection {
        Sentence tokens;
    };

    // One paragraph = a sequence of consecutive selections that share
    // an unbroken context (i.e. weren't separated by punctuation, IM
    // switch or focus loss). Bigram counting flattens all selections.
    struct Entry {
        std::vector<Selection> selections;
    };

    UserSentence() = default;

    void Clear();

    // vocab_sig: opaque tag identifying the vocabulary the stored
    // token IDs were saved against. Mismatch on Load → reject the file.
    // token_text: optional id→string callback used by Save to emit
    // a tab-separated, per-token-aligned annotation column for
    // human-readable inspection. Null = ids only, no annotation.
    bool Load(const std::filesystem::path& path,
              std::string_view vocab_sig = {});
    bool Save(const std::filesystem::path& path,
              std::string_view vocab_sig = {},
              const TokenTextFn& token_text = {}) const;
    bool LoadText(std::istream& in, std::string_view vocab_sig = {});
    bool SaveText(std::ostream& out,
                  std::string_view vocab_sig = {},
                  const TokenTextFn& token_text = {}) const;

    // Migration path for vocab-mismatched files. Reads the file
    // ignoring stored token IDs, joins each paragraph's text column
    // back to a continuous string, and re-tokenizes via `tokenize`
    // (typically a Cutter.Cut bound by Sime). Each new token becomes
    // its own Selection. Caller persists the migrated state with a
    // fresh vocab_sig.
    using TokenizeFn =
        std::function<std::vector<std::pair<TokenID, std::string>>(
            std::string_view)>;
    bool LoadAndMigrate(const std::filesystem::path& path,
                        const TokenizeFn& tokenize);

    void SetMaxSentenceCount(std::size_t count);
    std::size_t MaxSentenceCount() const { return max_sentence_count_; }
    std::size_t SentenceCount() const { return sentences_.size(); }

    void SetWeight(float_t weight);
    float_t Weight() const { return weight_; }

    // Records a single user pick. `context` is the recent committed
    // tokens (typically `scorer_.Num()-1` deep). If the most recent
    // paragraph's flattened tail equals `context`, the new selection
    // is appended to that paragraph; otherwise a new paragraph starts.
    void Add(const Sentence& context, const Sentence& tokens);

    std::uint32_t UnigramCount(TokenID token) const;
    std::uint32_t BigramCount(TokenID prev, TokenID cur) const;
    std::uint32_t TotalUnigramCount() const { return total_unigram_count_; }
    // N1+(prev, *) — number of distinct successor tokens after `prev`.
    // Used by Ney's absolute-discounting backoff weight (γ).
    std::uint32_t DistinctSuccessors(TokenID prev) const;
    // Cost adjustment to add to LM cost for a transition prev→cur.
    // `lm_step` is the LM cost (positive, = -log P) for the same
    // transition. Returns ≤ 0; never makes the cost worse than LM
    // alone. Mirrors libime's UserLanguageModel::score: linear mixture
    // in probability space, max-floored at the LM score.
    float_t CostAdjustment(TokenID prev, TokenID cur, float_t lm_step) const;

    const std::deque<Entry>& Sentences() const { return sentences_; }

private:
    static std::uint64_t BigramKey(TokenID prev, TokenID cur);
    static Sentence CleanSentence(const Sentence& sentence);
    static Sentence Flatten(const Entry& entry);
    static bool EndsWith(const Sentence& sentence, const Sentence& suffix);

    void StartParagraph(Selection selection);
    void AppendToLatest(Selection selection);
    void AdjustEntryCounts(const Entry& entry, int delta);
    void Rebuild();

    std::size_t max_sentence_count_ = 8192;
    // libime's UserLanguageModel uses 0.2 as its default mixture
    // weight (constants.h: DEFAULT_USER_LANGUAGE_MODEL_USER_WEIGHT).
    float_t weight_ = 0.2;
    std::deque<Entry> sentences_;
    std::unordered_map<TokenID, std::uint32_t> unigram_;
    std::unordered_map<std::uint64_t, std::uint32_t> bigram_;
    // N1+(prev, *): distinct-successor count per prev token. Maintained
    // incrementally when bigram counts cross the 0 boundary.
    std::unordered_map<TokenID, std::uint32_t> distinct_successors_;
    std::uint32_t total_unigram_count_ = 0;
};

} // namespace sime
