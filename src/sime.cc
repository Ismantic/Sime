#include "sime.h"

#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace sime {

namespace {

// Count syllables in `pieces` whose letters are not fully present in
// `input_slice`. When `input_is_digits` is true, compare pieces letters
// against their T9 digit form instead. Used to penalize abbreviated or
// tail-expanded matches relative to fully-typed ones.
std::size_t CountSyllableMismatch(const char* pieces,
                                  std::string_view input_slice,
                                  bool input_is_digits) {
    if (!pieces) return 0;
    std::size_t mismatch = 0;
    std::size_t ipos = 0;
    const char* p = pieces;
    while (*p) {
        const char* syl_end = p;
        while (*syl_end && *syl_end != '\'') ++syl_end;
        std::size_t syl_len = static_cast<std::size_t>(syl_end - p);

        while (ipos < input_slice.size() && input_slice[ipos] == '\'') ++ipos;

        std::size_t matched = 0;
        while (matched < syl_len && ipos < input_slice.size() &&
               input_slice[ipos] != '\'') {
            char expected = input_is_digits
                ? Dict::LetterToNum(p[matched])
                : p[matched];
            if (input_slice[ipos] != expected) break;
            ++matched;
            ++ipos;
        }
        if (matched < syl_len) ++mismatch;

        p = (*syl_end == '\'') ? syl_end + 1 : syl_end;
    }
    return mismatch;
}

struct Layer2Entry {
    DecodeResult result;
    bool exact = false;
};

bool BetterLayer2Entry(const Layer2Entry& lhs, const Layer2Entry& rhs) {
    if (lhs.exact != rhs.exact) return lhs.exact && !rhs.exact;
    if (lhs.result.score != rhs.result.score) {
        return lhs.result.score > rhs.result.score;
    }
    if (lhs.result.cnt != rhs.result.cnt) {
        return lhs.result.cnt > rhs.result.cnt;
    }
    return lhs.result.units.size() > rhs.result.units.size();
}

void PushBestLayer2Entry(std::vector<Layer2Entry>& best_entries,
                         std::unordered_map<std::string, std::size_t>& index_by_text,
                         Layer2Entry candidate) {
    auto it = index_by_text.find(candidate.result.text);
    if (it == index_by_text.end()) {
        index_by_text.emplace(candidate.result.text, best_entries.size());
        best_entries.push_back(std::move(candidate));
        return;
    }
    auto& current = best_entries[it->second];
    if (BetterLayer2Entry(candidate, current)) {
        current = std::move(candidate);
    }
}

} // namespace

Sime::Sime(const std::filesystem::path& dict_path,
                         const std::filesystem::path& model_path) {
    if (!dict_.Load(dict_path)) {
        return;
    }
    if (!scorer_.Load(model_path)) {
        dict_.Clear();
        return;
    }
    ready_ = true;
}

void Sime::ComputeEdgePenalties(std::vector<Node>& net,
                                std::string_view input,
                                float_t penalty_per_mismatch,
                                std::size_t t9_boundary) {
    for (auto& col : net) {
        for (auto& edge : col.es) {
            if (!edge.pieces || edge.id == NotToken) continue;
            auto slice = input.substr(
                edge.start, edge.end - edge.start);
            bool is_t9 = (edge.start >= t9_boundary);
            auto mismatch = CountSyllableMismatch(
                edge.pieces, slice, is_t9);
            edge.penalty =
                static_cast<float_t>(mismatch) * penalty_per_mismatch;
        }
    }
}

void Sime::InitNumNet(std::string_view start,
                              std::string_view nums,
                              std::vector<Node>& net,
                              bool expansion) const {
    const std::size_t p = start.size();
    const std::size_t d = nums.size();
    const std::size_t total = p + d;

    net.clear();
    net.resize(total + 2);

    auto emit = [&](std::size_t s, std::size_t new_col,
                    TokenID tid, const char* pieces) {
        net[s].es.push_back({s, new_col, tid, pieces});
    };

    auto emit_dat = [&](std::size_t s, Dict::DatType type,
                        std::string_view suffix, std::size_t max_end,
                        bool pinyin) {
        auto results = pinyin
            ? dict_.Dat(type).PrefixSearchPinyin(suffix, 512)
            : dict_.Dat(type).PrefixSearch(suffix, 512);
        for (const auto& r : results) {
            std::size_t new_col = s + r.length;
            if (new_col > max_end) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                emit(s, new_col, entry.items[i].id, entry.items[i].pieces);
            }
        }
    };

    auto expand_dat = [&](std::size_t s, Dict::DatType type,
                          std::string_view tail, std::size_t target_col,
                          std::size_t max_num, bool pinyin) {
        auto results = pinyin
            ? dict_.Dat(type).FindWordsWithPrefixPinyin(tail, max_num)
            : dict_.Dat(type).FindWordsWithPrefix(tail, max_num);
        for (const auto& r : results) {
            if (r.length <= tail.size()) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                emit(s, target_col, entry.items[i].id, entry.items[i].pieces);
            }
        }
    };

    for (std::size_t s = 0; s < total; ++s) {
        // === Prefix letter columns ===
        if (s < p) {
            if (start[s] == '\'') {
                net[s].es.push_back({s, s + 1, NotToken});
                continue;
            }

            std::string_view suffix = start.substr(s);
            emit_dat(s, Dict::LetterPinyin, suffix, p, true);
            emit_dat(s, Dict::LetterEn, suffix, p, false);

            // Tail expansion for prefix letters
            if (expansion && !Dict::IsKnownPinyin(std::string(suffix.substr(0, p - s)))) {
                expand_dat(s, Dict::LetterPinyin, suffix, p, 512, true);
                expand_dat(s, Dict::LetterEn, suffix, p, 1024, false);
            }

            // Cross-boundary: search with letters + digits combined.
            // PrefixSearchT9 now handles letter chars via AdvancePinyin,
            // so a mixed string like "b'9'3" works directly.
            // Only search from the last few letter columns — earlier columns
            // would need impractically long words to span into digit territory.
            if (!nums.empty() && (p - s) <= 6) {
                std::string cross(suffix);
                cross += nums;
                auto cross_emit = [&](Dict::DatType type,
                                      trie::DoubleArray::CharExpander expander,
                                      std::size_t max_num) {
                    auto results = dict_.Dat(type).PrefixSearchT9(
                        cross, expander, max_num);
                    for (const auto& r : results) {
                        std::size_t new_col = s + r.length;
                        if (new_col <= p) continue;  // already covered by letter-only search
                        if (new_col > total) continue;
                        auto entry = dict_.GetEntry(type, r.value);
                        for (uint32_t i = 0; i < entry.count; ++i) {
                            emit(s, new_col, entry.items[i].id,
                                 entry.items[i].pieces);
                        }
                    }
                };
                cross_emit(Dict::LetterPinyin, Dict::NumToLettersLower, 512);
                cross_emit(Dict::LetterEn, Dict::NumToLetters, 512);
            }
            continue;
        }

        // === Digit columns ===
        const std::size_t dpos = s - p;

        if (nums[dpos] == '\'') {
            net[s].es.push_back({s, s + 1, NotToken});
            continue;
        }

        // T9: expand digits to letters and search letter DATs
        std::string_view num_suffix = nums.substr(dpos);
        auto t9_emit = [&](Dict::DatType type, trie::DoubleArray::CharExpander expander,
                           std::size_t max_num) {
            auto results = dict_.Dat(type).PrefixSearchT9(
                num_suffix, expander, max_num);
            for (const auto& r : results) {
                std::size_t new_col = s + r.length;
                if (new_col > total) continue;
                auto entry = dict_.GetEntry(type, r.value);
                for (uint32_t i = 0; i < entry.count; ++i) {
                    emit(s, new_col, entry.items[i].id, entry.items[i].pieces);
                }
            }
        };
        // Pinyin DAT: lowercase only (no uppercase in pinyin)
        t9_emit(Dict::LetterPinyin, Dict::NumToLettersLower, 512);
        // English DAT: both cases
        t9_emit(Dict::LetterEn, Dict::NumToLetters, 512);

        // Tail expansion for digits
        if (expansion) {
            std::size_t tail_len = 0;
            while (dpos + tail_len < d && nums[dpos + tail_len] != '\'')
                ++tail_len;
            if (dpos + tail_len == d && tail_len > 0) {
                std::string tail(nums.substr(dpos, tail_len));
                auto t9_expand = [&](Dict::DatType type,
                                     trie::DoubleArray::CharExpander expander,
                                     std::size_t max_num) {
                    auto results = dict_.Dat(type).FindWordsWithPrefixT9(
                        tail, expander, max_num);
                    for (const auto& r : results) {
                        if (r.length <= tail.size()) continue;
                        auto entry = dict_.GetEntry(type, r.value);
                        for (uint32_t i = 0; i < entry.count; ++i) {
                            emit(s, total, entry.items[i].id,
                                 entry.items[i].pieces);
                        }
                    }
                };
                t9_expand(Dict::LetterPinyin, Dict::NumToLettersLower, 512);
                t9_expand(Dict::LetterEn, Dict::NumToLetters, 1024);
            }
        }
    }

    for (std::size_t i = 0; i < total; ++i) {
        PruneNode(net[i].es);
    }
    net[total].es.push_back({total, total + 1, NotToken});
}

std::string Sime::AbbreviatePieces(const char* full_pieces,
                                   std::string_view input) {
    // Parse syllables from full_pieces.
    struct Syl { const char* begin; std::size_t len; };
    std::vector<Syl> syls;
    for (const char* p = full_pieces; *p; ) {
        const char* s = p;
        while (*p && *p != '\'') ++p;
        if (p > s) syls.push_back({s, static_cast<std::size_t>(p - s)});
        if (*p == '\'') ++p;
    }
    if (syls.empty()) return full_pieces;

    // Check if input char matches a syllable letter.
    auto charMatch = [](char ic, char sc) -> bool {
        if (ic == sc) return true;
        if (ic >= '2' && ic <= '9') {
            const char* letters = Dict::NumToLettersLower(
                static_cast<uint8_t>(ic));
            for (const char* l = letters; *l; ++l) {
                if (*l == sc) return true;
            }
        }
        return false;
    };

    // Find the max prefix length of syllable syl that matches input
    // starting at ipos. Returns 0 if first char doesn't match.
    auto maxMatch = [&](const Syl& syl, std::size_t ipos) -> std::size_t {
        std::size_t matched = 0;
        for (std::size_t k = 0; k < syl.len && ipos + k < input.size()
                 && input[ipos + k] != '\''; ++k) {
            if (!charMatch(input[ipos + k], syl.begin[k])) break;
            ++matched;
        }
        return matched;
    };

    // Recursive search: for each syllable, try lengths 1..max (prefer
    // shorter = more abbreviated). Return true if all syllables can be
    // covered consuming all input non-separator chars.
    std::vector<std::size_t> chosen(syls.size(), 0);

    std::function<bool(std::size_t, std::size_t)> solve =
        [&](std::size_t si, std::size_t ipos) -> bool {
        // Skip input separators.
        while (ipos < input.size() && input[ipos] == '\'') ++ipos;
        if (si == syls.size()) return ipos >= input.size();
        if (ipos >= input.size()) return false;
        std::size_t mm = maxMatch(syls[si], ipos);
        if (mm == 0) return false;
        for (std::size_t len = 1; len <= mm; ++len) {
            chosen[si] = len;
            // Skip any input separator after the consumed digits.
            std::size_t next_ipos = ipos + len;
            if (solve(si + 1, next_ipos)) return true;
        }
        return false;
    };

    if (!solve(0, 0)) {
        // Fallback: greedy (original behavior) — shouldn't normally happen.
        return full_pieces;
    }

    // Build result from chosen lengths.
    std::string result;
    for (std::size_t i = 0; i < syls.size(); ++i) {
        if (i > 0) result += '\'';
        result.append(syls[i].begin, chosen[i]);
    }
    return result;
}

std::string Sime::ExtractUnits(const std::vector<Link>& path,
                               std::string_view input) {
    std::string py;
    for (const auto& link : path) {
        if (link.id == NotToken) continue;
        if (!link.pieces || link.pieces[0] == '\0') continue;
        if (!py.empty()) py += '\'';
        py += AbbreviatePieces(link.pieces,
                               input.substr(link.start, link.end - link.start));
    }
    return py;
}

std::string Sime::ExtractText(const std::vector<Link>& path) const {
    std::u32string u32;
    for (const auto& link : path) {
        u32 += ToText(link);
    }
    return TextFromU32(u32);
}

std::string Sime::TextFromU32(std::u32string& u32) {
    for (auto& ch : u32) {
        if (ch == 0x2581) ch = U' ';
    }
    return ustr::FromU32(u32);
}

std::vector<TokenID> Sime::ExtractTokens(
    const std::vector<Link>& path) const {
    std::vector<TokenID> ids;
    for (const auto& link : path) {
        if (link.id == NotToken) continue;
        ids.push_back(link.id);
    }
    return ids;
}

std::size_t Sime::CountPathMismatch(const std::vector<Link>& path,
                                    std::string_view input,
                                    std::size_t t9_boundary) {
    std::size_t mismatch = 0;
    for (const auto& link : path) {
        if (link.id == NotToken || !link.pieces) continue;
        bool is_t9 = link.start >= t9_boundary;
        auto slice = input.substr(link.start, link.end - link.start);
        mismatch += CountSyllableMismatch(link.pieces, slice, is_t9);
    }
    return mismatch;
}

void Sime::ResetNumDecodeCache(std::string_view start,
                               std::string_view nums) const {
    num_cache_.Clear();
    num_cache_.start.assign(start);
    num_cache_.nums.assign(nums);

    const std::size_t p = start.size();
    const std::size_t d = nums.size();
    const std::size_t total = p + d;
    num_cache_.starts.resize(total);
    num_cache_.exact_edges.resize(total);
    num_cache_.static_expansion_edges.resize(total);
    num_cache_.dynamic_expansion_edges.resize(total);

    auto emit_exact = [&](std::size_t s,
                          std::size_t max_end, Dict::DatType type,
                          const std::vector<trie::SearchResult>& results,
                          bool cross_only = false) {
        for (const auto& r : results) {
            std::size_t new_col = s + r.length;
            if (new_col > max_end) continue;
            if (cross_only && new_col <= p) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                num_cache_.exact_edges[s].push_back(
                    {s, new_col, entry.items[i].id, entry.items[i].pieces});
            }
        }
    };
    auto emit_expand = [&](std::vector<Link>& out, std::size_t s,
                           std::size_t target_col, std::size_t prefix_len,
                           Dict::DatType type,
                           const std::vector<trie::SearchResult>& results) {
        for (const auto& r : results) {
            if (r.length <= prefix_len) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                out.push_back({s, target_col, entry.items[i].id,
                               entry.items[i].pieces});
            }
        }
    };

    const auto& py_dat = dict_.Dat(Dict::LetterPinyin);
    const auto& en_dat = dict_.Dat(Dict::LetterEn);

    for (std::size_t s = 0; s < p; ++s) {
        if (start[s] == '\'') continue;

        auto suffix = start.substr(s);
        emit_exact(s, p, Dict::LetterPinyin,
                   py_dat.PrefixSearchPinyin(suffix, 512));
        emit_exact(s, p, Dict::LetterEn,
                   en_dat.PrefixSearch(suffix, 512));

        if (!Dict::IsKnownPinyin(std::string(suffix))) {
            emit_expand(num_cache_.static_expansion_edges[s], s, p,
                        suffix.size(), Dict::LetterPinyin,
                        py_dat.FindWordsWithPrefixPinyin(suffix, 512));
            emit_expand(num_cache_.static_expansion_edges[s], s, p,
                        suffix.size(), Dict::LetterEn,
                        en_dat.FindWordsWithPrefix(suffix, 1024));
        }

        if (!nums.empty() && (p - s) <= 6) {
            auto& state = num_cache_.starts[s];
            state.cross_active = true;
            state.py_t9_session.Bind(&py_dat, Dict::NumToLettersLower);
            state.en_t9_session.Bind(&en_dat, Dict::NumToLetters);
            std::string cross(suffix);
            cross += nums;
            state.py_t9_session.Append(cross);
            state.en_t9_session.Append(cross);
            emit_exact(s, total, Dict::LetterPinyin,
                       py_dat.PrefixSearchT9(cross, Dict::NumToLettersLower,
                                             512),
                       true);
            emit_exact(s, total, Dict::LetterEn,
                       en_dat.PrefixSearchT9(cross, Dict::NumToLetters, 512),
                       true);
        }
    }

    for (std::size_t dpos = 0; dpos < d; ++dpos) {
        const std::size_t s = p + dpos;
        if (nums[dpos] == '\'') continue;

        auto& state = num_cache_.starts[s];
        state.digit_active = true;
        state.py_t9_session.Bind(&py_dat, Dict::NumToLettersLower);
        state.en_t9_session.Bind(&en_dat, Dict::NumToLetters);
        auto suffix = nums.substr(dpos);
        state.py_t9_session.Append(suffix);
        state.en_t9_session.Append(suffix);

        emit_exact(s, total, Dict::LetterPinyin,
                   py_dat.PrefixSearchT9(suffix, Dict::NumToLettersLower, 512));
        emit_exact(s, total, Dict::LetterEn,
                   en_dat.PrefixSearchT9(suffix, Dict::NumToLetters, 512));
    }

    if (!nums.empty()) {
        for (std::size_t dpos = 0; dpos < d; ++dpos) {
            const std::size_t s = p + dpos;
            if (nums[dpos] == '\'') continue;
            std::size_t tail_len = 0;
            while (dpos + tail_len < d && nums[dpos + tail_len] != '\'') {
                ++tail_len;
            }
            if (dpos + tail_len != d || tail_len == 0) continue;
            std::string tail(nums.substr(dpos, tail_len));
            emit_expand(num_cache_.dynamic_expansion_edges[s], s, total,
                        tail_len, Dict::LetterPinyin,
                        py_dat.FindWordsWithPrefixT9(
                            tail, Dict::NumToLettersLower, 512));
            emit_expand(num_cache_.dynamic_expansion_edges[s], s, total,
                        tail_len, Dict::LetterEn,
                        en_dat.FindWordsWithPrefixT9(
                            tail, Dict::NumToLetters, 1024));
        }
    }

    RebuildNumNet();
}

void Sime::RebuildNumNet() const {
    const std::size_t p = num_cache_.start.size();
    const std::size_t total = p + num_cache_.nums.size();
    num_cache_.net.resize(total + 2);

    for (std::size_t s = 0; s < total; ++s) {
        auto& col = num_cache_.net[s];
        col.es.clear();
        col.states.Clear();

        bool is_sep = s < p
            ? num_cache_.start[s] == '\''
            : num_cache_.nums[s - p] == '\'';
        if (is_sep) {
            col.es.push_back({s, s + 1, NotToken});
            continue;
        }
        col.es.insert(col.es.end(),
                       num_cache_.exact_edges[s].begin(),
                       num_cache_.exact_edges[s].end());
        col.es.insert(col.es.end(),
                       num_cache_.static_expansion_edges[s].begin(),
                       num_cache_.static_expansion_edges[s].end());
        col.es.insert(col.es.end(),
                       num_cache_.dynamic_expansion_edges[s].begin(),
                       num_cache_.dynamic_expansion_edges[s].end());
        PruneNode(col.es, &num_cache_.score_cache);
    }
    auto& term = num_cache_.net[total];
    term.es.clear();
    term.states.Clear();
    term.es.push_back({total, total + 1, NotToken});

    auto& sentinel = num_cache_.net[total + 1];
    sentinel.es.clear();
    sentinel.states.Clear();
}

void Sime::AppendNumDecodeCache(std::string_view appended_tail) const {
    if (appended_tail.empty()) return;

    auto emit_exact = [&](std::size_t s, std::size_t input_len,
                          Dict::DatType type,
                          const std::vector<trie::SearchResult>& results,
                          bool cross_only = false) {
        for (const auto& r : results) {
            if (r.length != input_len) continue;
            std::size_t new_col = s + r.length;
            if (cross_only && new_col <= num_cache_.start.size()) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                num_cache_.exact_edges[s].push_back(
                    {s, new_col, entry.items[i].id, entry.items[i].pieces});
            }
        }
    };
    auto emit_expand = [&](std::vector<Link>& out, std::size_t s,
                           std::size_t target_col, std::size_t prefix_len,
                           Dict::DatType type,
                           const std::vector<trie::SearchResult>& results) {
        for (const auto& r : results) {
            if (r.length <= prefix_len) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                out.push_back({s, target_col, entry.items[i].id,
                               entry.items[i].pieces});
            }
        }
    };

    const auto& py_dat = dict_.Dat(Dict::LetterPinyin);
    const auto& en_dat = dict_.Dat(Dict::LetterEn);
    const std::size_t p = num_cache_.start.size();

    for (char ch : appended_tail) {
        const std::size_t old_d = num_cache_.nums.size();
        const std::size_t old_total = p + old_d;
        num_cache_.nums.push_back(ch);
        num_cache_.starts.resize(old_total + 1);
        num_cache_.exact_edges.resize(old_total + 1);
        num_cache_.static_expansion_edges.resize(old_total + 1);
        num_cache_.dynamic_expansion_edges.resize(old_total + 1);

        for (std::size_t s = 0; s < p; ++s) {
            auto& state = num_cache_.starts[s];
            if (!state.cross_active) continue;
            state.py_t9_session.Append(static_cast<uint8_t>(ch));
            state.en_t9_session.Append(static_cast<uint8_t>(ch));
            const std::size_t input_len = p + num_cache_.nums.size() - s;
            emit_exact(s, input_len, Dict::LetterPinyin,
                       state.py_t9_session.CollectPrefixMatchesCurrent(512),
                       true);
            emit_exact(s, input_len, Dict::LetterEn,
                       state.en_t9_session.CollectPrefixMatchesCurrent(512),
                       true);
        }

        for (std::size_t s = p; s < old_total; ++s) {
            auto& state = num_cache_.starts[s];
            if (!state.digit_active) continue;
            state.py_t9_session.Append(static_cast<uint8_t>(ch));
            state.en_t9_session.Append(static_cast<uint8_t>(ch));
            const std::size_t input_len = p + num_cache_.nums.size() - s;
            emit_exact(s, input_len, Dict::LetterPinyin,
                       state.py_t9_session.CollectPrefixMatchesCurrent(512));
            emit_exact(s, input_len, Dict::LetterEn,
                       state.en_t9_session.CollectPrefixMatchesCurrent(512));
        }

        if (ch != '\'') {
            auto& state = num_cache_.starts[old_total];
            state.digit_active = true;
            state.py_t9_session.Bind(&py_dat, Dict::NumToLettersLower);
            state.en_t9_session.Bind(&en_dat, Dict::NumToLetters);
            state.py_t9_session.Append(static_cast<uint8_t>(ch));
            state.en_t9_session.Append(static_cast<uint8_t>(ch));
            emit_exact(old_total, 1, Dict::LetterPinyin,
                       state.py_t9_session.CollectPrefixMatchesCurrent(512));
            emit_exact(old_total, 1, Dict::LetterEn,
                       state.en_t9_session.CollectPrefixMatchesCurrent(512));
        }
    }

    const std::size_t d = num_cache_.nums.size();
    const std::size_t total = p + d;
    for (std::size_t s = 0; s < total; ++s) {
        num_cache_.dynamic_expansion_edges[s].clear();
    }
    for (std::size_t dpos = 0; dpos < d; ++dpos) {
        const std::size_t s = p + dpos;
        if (num_cache_.nums[dpos] == '\'') continue;
        std::size_t tail_len = 0;
        while (dpos + tail_len < d && num_cache_.nums[dpos + tail_len] != '\'') {
            ++tail_len;
        }
        if (dpos + tail_len != d || tail_len == 0) continue;
        auto& state = num_cache_.starts[s];
        emit_expand(num_cache_.dynamic_expansion_edges[s], s, total,
                    tail_len, Dict::LetterPinyin,
                    state.py_t9_session.CollectCompletions(512));
        emit_expand(num_cache_.dynamic_expansion_edges[s], s, total,
                    tail_len, Dict::LetterEn,
                    state.en_t9_session.CollectCompletions(1024));
    }

    RebuildNumNet();
}

std::vector<DecodeResult> Sime::DecodeNumSentence(
    std::string_view nums,
    std::string_view start,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_) return results;
    for (char c : nums) {
        if (c == '\'') continue;
        if (c < '2' || c > '9') return results;
    }
    if (nums.empty() && start.empty()) return results;

    const std::size_t p = start.size();
    const std::size_t d = nums.size();
    const std::size_t total = p + d;
    std::string combined_input(start);
    combined_input += nums;

    std::vector<Node> net;
    const bool can_tail_expand = !nums.empty() && nums.back() != '\'';
    InitNumNet(start, nums, net, can_tail_expand);
    ComputeEdgePenalties(net, combined_input, PinyinMatchPenalty, p);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    // See DecodeSentence: Layer 1 dedups with a local set so beam members
    // that don't make the 1+extra cut remain eligible for Layer 2.
    std::unordered_set<std::string> dedup;
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;

    // === Layer 1: Full sentence N-best ===
    // (frag_penalty is already in beam scores via edge.penalty)
    {
        const auto tail = net[total + 1].states.GetStates();
        const std::size_t scan = std::min<std::size_t>(BeamSize, tail.size());
        const std::size_t full_cnt = start.size() + d;
        std::vector<DecodeResult> l1;
        l1.reserve(scan);
        std::unordered_set<std::string> l1_seen;
        for (std::size_t rank = 0; rank < scan; ++rank) {
            auto path = Backtrace(tail[rank], total + 1);
            std::string text = ExtractText(path);
            if (text.empty() || !l1_seen.insert(text).second) continue;
            std::string py = ExtractUnits(path, combined_input);

            l1.push_back({std::move(text), std::move(py),
                          ExtractTokens(path),
                          -tail[rank].score, full_cnt});
        }
        std::sort(l1.begin(), l1.end(),
                  [](const DecodeResult& a, const DecodeResult& b) {
                      return a.score > b.score;
                  });
        const std::size_t full_limit = 1 + extra;
        for (std::size_t i = 0; i < l1.size() && results.size() < full_limit;
             ++i) {
            dedup.insert(l1[i].text);
            results.push_back(std::move(l1[i]));
        }
    }

    // === Layer 2: unigram alternatives at column 0 ===
    // Split into two tiers: full-pinyin prefix matches first (mismatch==0),
    // then abbreviated prefix matches (mismatch>0), each sorted by score.
    std::vector<Layer2Entry> best_l2;
    std::unordered_map<std::string, std::size_t> l2_index_by_text;
    std::vector<DecodeResult> l2_full;
    std::vector<DecodeResult> l2_abbrev;

    for (const auto& edge : net[0].es) {
        if (edge.id == NotToken) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;

        std::string text_utf8 = TextFromU32(text_u32);
        if (dedup.contains(text_utf8)) continue;

        std::size_t distance = (total > edge.end) ? (total - edge.end) : 0;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;

        bool is_t9 = (edge.start >= p);
        auto slice = std::string_view(combined_input).substr(
            edge.start, edge.end - edge.start);
        std::size_t mismatch = CountSyllableMismatch(edge.pieces, slice, is_t9);

        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty;

        std::string edge_py = edge.pieces
            ? AbbreviatePieces(edge.pieces, slice)
            : "";
        std::size_t cnt = edge.end;
        PushBestLayer2Entry(
            best_l2,
            l2_index_by_text,
            {{std::move(text_utf8), std::move(edge_py),
              ExtractTokens({edge}), score, cnt},
             mismatch == 0});
    }

    for (auto& entry : best_l2) {
        if (entry.exact) {
            l2_full.push_back(std::move(entry.result));
        } else {
            l2_abbrev.push_back(std::move(entry.result));
        }
    }

    auto by_score = [](const DecodeResult& a, const DecodeResult& b) {
        return a.score > b.score;
    };
    std::sort(l2_full.begin(), l2_full.end(), by_score);
    std::sort(l2_abbrev.begin(), l2_abbrev.end(), by_score);
    for (auto& r : l2_full) results.push_back(std::move(r));
    for (auto& r : l2_abbrev) results.push_back(std::move(r));

    return results;
}

std::vector<DecodeResult> Sime::DecodeNumSentenceCache(
    std::string_view nums,
    std::string_view start,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_) return results;
    for (char c : nums) {
        if (c == '\'') continue;
        if (c < '2' || c > '9') return results;
    }
    if (nums.empty() && start.empty()) return results;

    const std::size_t p = start.size();
    const std::size_t d = nums.size();
    const std::size_t total = p + d;
    std::string combined_input(start);
    combined_input += nums;

    if (num_cache_.start != start || num_cache_.nums.empty() ||
        !IsAppendOnly(num_cache_.nums, nums)) {
        ResetNumDecodeCache(start, nums);
    } else if (num_cache_.nums != nums) {
        AppendNumDecodeCache(nums.substr(num_cache_.nums.size()));
    }

    // Use cached net directly (no copy). RebuildNumNet already assembled it.
    auto& net = num_cache_.net;

    // Clear states from previous call, apply penalties, run beam search.
    for (auto& col : net) {
        col.states.Clear();
        col.states.SetMaxTop(BeamSize);
    }
    ComputeEdgePenalties(net, combined_input, PinyinMatchPenalty, p);

    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    std::unordered_set<std::string> dedup;
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;

    {
        const auto tail = net[total + 1].states.GetStates();
        const std::size_t scan = std::min<std::size_t>(BeamSize, tail.size());
        const std::size_t full_cnt = start.size() + d;
        std::vector<DecodeResult> l1;
        l1.reserve(scan);
        std::unordered_set<std::string> l1_seen;
        for (std::size_t rank = 0; rank < scan; ++rank) {
            auto path = Backtrace(tail[rank], total + 1);
            std::string text = ExtractText(path);
            if (text.empty() || !l1_seen.insert(text).second) continue;
            std::string py = ExtractUnits(path, combined_input);

            l1.push_back({std::move(text), std::move(py),
                          ExtractTokens(path),
                          -tail[rank].score, full_cnt});
        }
        std::sort(l1.begin(), l1.end(),
                  [](const DecodeResult& a, const DecodeResult& b) {
                      return a.score > b.score;
                  });
        const std::size_t full_limit = 1 + extra;
        for (std::size_t i = 0; i < l1.size() && results.size() < full_limit;
             ++i) {
            dedup.insert(l1[i].text);
            results.push_back(std::move(l1[i]));
        }
    }

    std::vector<Layer2Entry> best_l2;
    std::unordered_map<std::string, std::size_t> l2_index_by_text;
    std::vector<DecodeResult> l2_full;
    std::vector<DecodeResult> l2_abbrev;

    for (const auto& edge : net[0].es) {
        if (edge.id == NotToken) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;

        std::string text_utf8 = TextFromU32(text_u32);
        if (dedup.contains(text_utf8)) continue;

        std::size_t distance = (total > edge.end) ? (total - edge.end) : 0;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;

        bool is_t9 = (edge.start >= p);
        auto slice = std::string_view(combined_input).substr(
            edge.start, edge.end - edge.start);
        std::size_t mismatch = CountSyllableMismatch(edge.pieces, slice, is_t9);

        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty;

        std::string edge_py = edge.pieces
            ? AbbreviatePieces(edge.pieces, slice)
            : "";
        std::size_t cnt = edge.end;
        PushBestLayer2Entry(
            best_l2,
            l2_index_by_text,
            {{std::move(text_utf8), std::move(edge_py),
              ExtractTokens({edge}), score, cnt},
             mismatch == 0});
    }

    for (auto& entry : best_l2) {
        if (entry.exact) {
            l2_full.push_back(std::move(entry.result));
        } else {
            l2_abbrev.push_back(std::move(entry.result));
        }
    }

    auto by_score = [](const DecodeResult& a, const DecodeResult& b) {
        return a.score > b.score;
    };
    std::sort(l2_full.begin(), l2_full.end(), by_score);
    std::sort(l2_abbrev.begin(), l2_abbrev.end(), by_score);
    for (auto& r : l2_full) results.push_back(std::move(r));
    for (auto& r : l2_abbrev) results.push_back(std::move(r));

    return results;
}

std::vector<DecodeResult> Sime::DecodeNumStr(
    std::string_view nums,
    std::string_view start,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_) return results;
    for (char c : nums) {
        if (c == '\'') continue;
        if (c < '2' || c > '9') return results;
    }
    if (nums.empty() && start.empty()) return results;

    const std::size_t max_top = num == 0 ? 1 : num;
    const std::size_t total = start.size() + nums.size();
    std::string combined_input(start);
    combined_input += nums;

    std::vector<Node> net;
    InitNumNet(start, nums, net, /*expansion=*/false);
    ComputeEdgePenalties(net, combined_input, PinyinMatchPenalty, start.size());

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    auto tail_states = net.back().states.GetStates();
    std::unordered_set<std::string> dedup;
    for (std::size_t rank = 0;
         rank < tail_states.size() && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], total + 1);
        std::string text = ExtractText(path);
        if (text.empty() || !dedup.insert(text).second) continue;
        std::string py = ExtractUnits(path, combined_input);
        results.push_back({std::move(text), std::move(py),
                           ExtractTokens(path),
                           -tail_states[rank].score,
                           start.size() + nums.size()});
    }

    return results;
}

namespace {

std::string NormalizeInput(std::string_view input) {
    std::string lower;
    lower.reserve(input.size());
    for (char c : input) {
        if (c == '\'') {
            lower.push_back(c);
        } else if (c == ' ') {
            // Space → ▁ (U+2581, UTF-8: E2 96 81)
            lower.push_back(static_cast<char>(0xE2));
            lower.push_back(static_cast<char>(0x96));
            lower.push_back(static_cast<char>(0x81));
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            lower.push_back(c);
        }
    }
    return lower;
}

} // namespace

bool Sime::IsAppendOnly(std::string_view prev, std::string_view next) {
    return next.size() >= prev.size() &&
           next.substr(0, prev.size()) == prev;
}

void Sime::BuildSentenceNetFromCache(std::vector<Node>& net) const {
    const auto& input = sentence_cache_.input;
    const std::size_t total = input.size();
    net.clear();
    net.resize(total + 2);
    for (std::size_t s = 0; s < total; ++s) {
        if (input[s] == '\'') {
            net[s].es.push_back({s, s + 1, NotToken});
            continue;
        }
        net[s].es = sentence_cache_.exact_edges[s];
        net[s].es.insert(net[s].es.end(),
                         sentence_cache_.expansion_edges[s].begin(),
                         sentence_cache_.expansion_edges[s].end());
        PruneNode(net[s].es);
    }
    net[total].es.push_back({total, total + 1, NotToken});
}

void Sime::ResetSentenceDecodeCache(std::string_view input) const {
    sentence_cache_.Clear();
    sentence_cache_.input.assign(input);
    const std::size_t total = input.size();
    sentence_cache_.starts.resize(total);
    sentence_cache_.exact_edges.resize(total);
    sentence_cache_.expansion_edges.resize(total);

    auto emit_exact = [&](std::vector<Link>& out, std::size_t s,
                          Dict::DatType type,
                          const std::vector<trie::SearchResult>& results) {
        for (const auto& r : results) {
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                out.push_back({s, s + r.length, entry.items[i].id,
                               entry.items[i].pieces});
            }
        }
    };
    auto emit_expand = [&](std::vector<Link>& out, std::size_t s,
                           std::size_t prefix_len,
                           Dict::DatType type,
                           const std::vector<trie::SearchResult>& results) {
        for (const auto& r : results) {
            if (r.length <= prefix_len) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                out.push_back({s, total, entry.items[i].id,
                               entry.items[i].pieces});
            }
        }
    };

    const auto& py_dat = dict_.Dat(Dict::LetterPinyin);
    const auto& en_dat = dict_.Dat(Dict::LetterEn);

    for (std::size_t s = 0; s < total; ++s) {
        if (input[s] == '\'') continue;

        auto suffix = input.substr(s);
        auto& start = sentence_cache_.starts[s];
        start.active = true;
        start.py_states = py_dat.StartPinyinStates();
        start.en_state = en_dat.StartExactState();
        for (char ch : suffix) {
            py_dat.AdvancePinyinStates(start.py_states,
                                       static_cast<uint8_t>(ch));
            en_dat.AdvanceExactState(start.en_state,
                                     static_cast<uint8_t>(ch));
        }

        emit_exact(sentence_cache_.exact_edges[s], s, Dict::LetterPinyin,
                   py_dat.PrefixSearchPinyin(suffix, 512));
        emit_exact(sentence_cache_.exact_edges[s], s, Dict::LetterEn,
                   en_dat.PrefixSearch(suffix, 512));

        if (!Dict::IsKnownPinyin(std::string(suffix))) {
            emit_expand(sentence_cache_.expansion_edges[s], s, suffix.size(),
                        Dict::LetterPinyin,
                        py_dat.CollectCompletionsPinyin(start.py_states,
                                                        suffix.size(), 512));
            emit_expand(sentence_cache_.expansion_edges[s], s, suffix.size(),
                        Dict::LetterEn,
                        en_dat.CollectCompletionsExact(start.en_state,
                                                       suffix.size(), 1024));
        }
    }
}

void Sime::AppendSentenceDecodeCache(std::string_view appended_tail) const {
    if (appended_tail.empty()) return;

    auto emit_exact = [&](std::vector<Link>& out, std::size_t s,
                          std::size_t input_len, Dict::DatType type,
                          const std::vector<trie::SearchResult>& results) {
        for (const auto& r : results) {
            if (r.length != input_len) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                out.push_back({s, s + r.length, entry.items[i].id,
                               entry.items[i].pieces});
            }
        }
    };
    auto emit_expand = [&](std::vector<Link>& out, std::size_t s,
                           std::size_t total, std::size_t prefix_len,
                           Dict::DatType type,
                           const std::vector<trie::SearchResult>& results) {
        for (const auto& r : results) {
            if (r.length <= prefix_len) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                out.push_back({s, total, entry.items[i].id,
                               entry.items[i].pieces});
            }
        }
    };

    const auto& py_dat = dict_.Dat(Dict::LetterPinyin);
    const auto& en_dat = dict_.Dat(Dict::LetterEn);

    for (char ch : appended_tail) {
        const std::size_t old_total = sentence_cache_.input.size();
        sentence_cache_.input.push_back(ch);
        sentence_cache_.starts.resize(old_total + 1);
        sentence_cache_.exact_edges.resize(old_total + 1);
        sentence_cache_.expansion_edges.resize(old_total + 1);

        for (std::size_t s = 0; s < old_total; ++s) {
            auto& start = sentence_cache_.starts[s];
            if (!start.active) continue;
            py_dat.AdvancePinyinStates(start.py_states,
                                       static_cast<uint8_t>(ch));
            en_dat.AdvanceExactState(start.en_state,
                                     static_cast<uint8_t>(ch));
            const std::size_t input_len = sentence_cache_.input.size() - s;
            emit_exact(sentence_cache_.exact_edges[s], s, input_len,
                       Dict::LetterPinyin,
                       py_dat.CollectPrefixMatchesPinyin(start.py_states,
                                                         input_len, 512));
            emit_exact(sentence_cache_.exact_edges[s], s, input_len,
                       Dict::LetterEn,
                       en_dat.CollectPrefixMatchesExact(start.en_state,
                                                       input_len, 512));
        }

        if (ch != '\'') {
            auto& start = sentence_cache_.starts[old_total];
            start.active = true;
            start.py_states = py_dat.StartPinyinStates();
            start.en_state = en_dat.StartExactState();
            py_dat.AdvancePinyinStates(start.py_states,
                                       static_cast<uint8_t>(ch));
            en_dat.AdvanceExactState(start.en_state,
                                     static_cast<uint8_t>(ch));
            emit_exact(sentence_cache_.exact_edges[old_total], old_total, 1,
                       Dict::LetterPinyin,
                       py_dat.CollectPrefixMatchesPinyin(start.py_states,
                                                         1, 512));
            emit_exact(sentence_cache_.exact_edges[old_total], old_total, 1,
                       Dict::LetterEn,
                       en_dat.CollectPrefixMatchesExact(start.en_state,
                                                       1, 512));
        }
    }

    const std::size_t total = sentence_cache_.input.size();
    for (std::size_t s = 0; s < total; ++s) {
        sentence_cache_.expansion_edges[s].clear();
        if (sentence_cache_.input[s] == '\'') continue;
        auto suffix = std::string_view(sentence_cache_.input).substr(s);
        if (Dict::IsKnownPinyin(std::string(suffix))) continue;
        const auto& start = sentence_cache_.starts[s];
        emit_expand(sentence_cache_.expansion_edges[s], s, total, suffix.size(),
                    Dict::LetterPinyin,
                    py_dat.CollectCompletionsPinyin(start.py_states,
                                                    suffix.size(), 512));
        emit_expand(sentence_cache_.expansion_edges[s], s, total, suffix.size(),
                    Dict::LetterEn,
                    en_dat.CollectCompletionsExact(start.en_state,
                                                   suffix.size(), 1024));
    }
}

std::vector<DecodeResult> Sime::DecodeStr(
    std::string_view input,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || input.empty()) return results;

    std::string lower = NormalizeInput(input);
    if (lower.empty()) return results;

    std::vector<Node> net;
    InitNet(lower, net, /*expansion=*/true);
    ComputeEdgePenalties(net, lower, PinyinMatchPenalty, lower.size() + 1);

    const std::size_t max_top = num == 0 ? 1 : num;
    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    const auto tail_states = net.back().states.GetStates();
    std::unordered_set<std::string> dedup;
    for (std::size_t rank = 0;
         rank < tail_states.size() && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], net.size() - 1);
        if (path.empty()) continue;
        std::string text = ExtractText(path);
        if (text.empty() || !dedup.insert(text).second) continue;
        std::string py = ExtractUnits(path, lower);
        results.push_back({std::move(text), std::move(py),
                           ExtractTokens(path),
                           -tail_states[rank].score, input.size()});
    }
    return results;
}

void Sime::InitNet(std::string_view input,
                      std::vector<Node>& net,
                      bool expansion) const {
    const std::size_t total = input.size();
    net.clear();
    net.resize(total + 2);

    auto emit = [&](std::size_t s, std::size_t new_col,
                    TokenID tid, const char* pieces) {
        net[s].es.push_back({s, new_col, tid, pieces});
    };

    for (std::size_t s = 0; s < total; ++s) {
        if (input[s] == '\'') {
            net[s].es.push_back({s, s + 1, NotToken});
            continue;
        }

        // Pinyin: PrefixSearchPinyin handles separator skipping
        // and syllable abbreviation at query time.
        // English: plain PrefixSearch (no abbreviation).
        std::string_view suffix = input.substr(s);

        auto emit_results = [&](const std::vector<trie::SearchResult>& results,
                                Dict::DatType type) {
            for (const auto& r : results) {
                auto entry = dict_.GetEntry(type, r.value);
                for (uint32_t i = 0; i < entry.count; ++i) {
                    emit(s, s + r.length, entry.items[i].id,
                         entry.items[i].pieces);
                }
            }
        };

        emit_results(
            dict_.Dat(Dict::LetterPinyin).PrefixSearchPinyin(suffix, 512),
            Dict::LetterPinyin);
        emit_results(
            dict_.Dat(Dict::LetterEn).PrefixSearch(suffix, 512),
            Dict::LetterEn);

        // Tail expansion: use suffix as-is for both pinyin and English.
        // PrefixSearchPinyin handles '\'' natively; English trie keys
        // contain literal '\'' (don't, it's) so passing it through
        // naturally rejects pinyin-separator patterns like g'b'd (no
        // English word starts with "g'") while still matching don't.
        if (expansion && !Dict::IsKnownPinyin(std::string(suffix))) {
            auto expand_results = [&](
                const std::vector<trie::SearchResult>& results,
                Dict::DatType type, std::size_t tail_len) {
                for (const auto& r : results) {
                    if (r.length <= tail_len) continue;
                    auto entry = dict_.GetEntry(type, r.value);
                    for (uint32_t i = 0; i < entry.count; ++i) {
                        emit(s, total, entry.items[i].id,
                             entry.items[i].pieces);
                    }
                }
            };
            expand_results(
                dict_.Dat(Dict::LetterPinyin).FindWordsWithPrefixPinyin(
                    suffix, 512),
                Dict::LetterPinyin, suffix.size());
            expand_results(
                dict_.Dat(Dict::LetterEn).FindWordsWithPrefix(
                    suffix, 1024),
                Dict::LetterEn, suffix.size());
        }
    }

    for (std::size_t i = 0; i < total; ++i) {
        PruneNode(net[i].es);
    }

    net[total].es.push_back({total, total + 1, NotToken});
}

void Sime::PruneNode(std::vector<Link>& edges,
                     std::unordered_map<TokenID, float_t>* score_cache) const {
    if (edges.size() <= NodeSize) return;

    std::unordered_map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        groups[edges[i].end - edges[i].start].push_back(i);
    }

    auto get_score = [&](TokenID id) -> float_t {
        if (id == NotToken) return 0.0;
        if (score_cache) {
            auto it = score_cache->find(id);
            if (it != score_cache->end()) return it->second;
            Scorer::Pos ppos{};
            Scorer::Pos pnext{};
            float_t s = scorer_.ScoreMove(ppos, id, pnext);
            score_cache->emplace(id, s);
            return s;
        }
        Scorer::Pos ppos{};
        Scorer::Pos pnext{};
        return scorer_.ScoreMove(ppos, id, pnext);
    };

    std::vector<Link> pruned;
    pruned.reserve(edges.size());

    for (auto& [span, indices] : groups) {
        if (indices.size() <= NodeSize) {
            for (auto idx : indices) {
                pruned.push_back(edges[idx]);
            }
            continue;
        }

        std::vector<std::pair<float_t, std::size_t>> scored;
        scored.reserve(indices.size());
        for (auto idx : indices) {
            scored.push_back({get_score(edges[idx].id), idx});
        }

        std::partial_sort(
            scored.begin(),
            scored.begin() + static_cast<std::ptrdiff_t>(NodeSize),
            scored.end(),
            [&edges](const auto& a, const auto& b) {
                if (a.first != b.first) return a.first < b.first;
                return edges[a.second].id < edges[b.second].id;
            });

        for (std::size_t i = 0; i < NodeSize; ++i) {
            pruned.push_back(edges[scored[i].second]);
        }
    }

    edges = std::move(pruned);
}

void Sime::Process(std::vector<Node>& net) const {
    for (std::size_t col = 0; col < net.size(); ++col) {
        auto& column = net[col];
        for (auto it = column.states.begin(); it != column.states.end(); ++it) {
            const auto& value = *it;
            for (const auto& word : column.es) {
                Scorer::Pos cur_pos = value.pos;
                Scorer::Pos next_pos{};
                float_t step = scorer_.ScoreMove(cur_pos, word.id, next_pos);
                scorer_.Back(next_pos);
                cur_pos = next_pos;

                float_t next_cost = value.score + step + word.penalty;
                State next(next_cost, word.end, cur_pos, &value, word.id,
                           word.pieces);
                net[word.end].states.Insert(next);
            }
        }
    }
}

std::vector<Sime::Link> Sime::Backtrace(
    const State& tail_state,
    std::size_t end) {
    std::vector<Link> path;
    const State* state = &tail_state;
    while (state != nullptr && state->backtrace_state != nullptr) {
        const State* prev = state->backtrace_state;
        path.push_back({prev->now, state->now,
                        state->backtrace_token,
                        state->backtrace_pieces});
        state = prev;
    }
    std::reverse(path.begin(), path.end());
    if (!path.empty() && path.back().end == end) {
        path.pop_back();
    }
    return path;
}

std::u32string Sime::ToText(const Link& n) const {
    if (n.id == NotToken) {
        return {};
    }
    std::u32string buffer;
    const char32_t* chars = dict_.TokenAt(n.id);
    if (chars) {
        for (std::size_t i = 0; chars[i] != 0 && i < 64; ++i) {
            buffer.push_back(chars[i]);
        }
    }
    return buffer;
}

std::vector<DecodeResult> Sime::DecodeSentence(
    std::string_view input,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_ || input.empty()) return results;

    std::string lower = NormalizeInput(input);
    if (lower.empty()) return results;

    const std::size_t total = lower.size();

    std::vector<Node> net;
    InitNet(lower, net, /*expansion=*/true);
    ComputeEdgePenalties(net, lower, PinyinMatchPenalty, total + 1);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    // `dedup` tracks texts already emitted in `results`; Layer 2 checks
    // against it to avoid duplicating a candidate that made it into Layer 1.
    // Layer 1 uses its own local dedup during beam scan so that beam members
    // that don't make the final 1+extra cut remain eligible for Layer 2.
    std::unordered_set<std::string> dedup;
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;

    // === Layer 1: Full sentence N-best ===
    // Scan the whole beam, re-score with distance penalty,
    // sort by the combined score, then take top (1 + extra).
    // (frag_penalty is already in beam scores via edge.penalty)
    {
        const auto tail = net.back().states.GetStates();
        const std::size_t scan =
            std::min<std::size_t>(BeamSize, tail.size());
        std::vector<DecodeResult> l1;
        l1.reserve(scan);
        std::unordered_set<std::string> l1_seen;
        for (std::size_t rank = 0; rank < scan; ++rank) {
            auto path = Backtrace(tail[rank], net.size() - 1);
            if (path.empty()) continue;
            std::string text = ExtractText(path);
            if (text.empty() || !l1_seen.insert(text).second) continue;
            std::string py = ExtractUnits(path, lower);

            std::size_t piece_len = 0;
            for (char c : py) {
                if (c != '\'') ++piece_len;
            }
            std::size_t distance = (piece_len > total) ? (piece_len - total) : 0;
            float_t dist_penalty =
                static_cast<float_t>(distance) * penalty_per_unit;

            l1.push_back({std::move(text), std::move(py),
                          ExtractTokens(path),
                          -tail[rank].score - dist_penalty,
                          input.size()});
        }
        std::sort(l1.begin(), l1.end(),
                  [](const DecodeResult& a, const DecodeResult& b) {
                      return a.score > b.score;
                  });
        const std::size_t full_limit = 1 + extra;
        for (std::size_t i = 0; i < l1.size() && results.size() < full_limit;
             ++i) {
            dedup.insert(l1[i].text);
            results.push_back(std::move(l1[i]));
        }
    }

    // === Layer 2: word/char alternatives at position 0 ===
    // Split into two tiers: full-pinyin prefix matches first (mismatch==0),
    // then abbreviated prefix matches (mismatch>0), each sorted by score.
    std::vector<Layer2Entry> best_l2;
    std::unordered_map<std::string, std::size_t> l2_index_by_text;
    std::vector<DecodeResult> l2_full;
    std::vector<DecodeResult> l2_abbrev;

    for (const auto& edge : net[0].es) {
        if (edge.id == NotToken) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;
        std::string text_utf8 = TextFromU32(text_u32);
        if (dedup.contains(text_utf8)) continue;

        std::size_t distance = (total > edge.end) ? (total - edge.end) : 0;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;

        auto slice = std::string_view(lower).substr(
            edge.start, edge.end - edge.start);
        std::size_t mismatch = CountSyllableMismatch(edge.pieces, slice, false);

        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty;

        std::string edge_py = edge.pieces
            ? AbbreviatePieces(edge.pieces, slice)
            : "";
        PushBestLayer2Entry(
            best_l2,
            l2_index_by_text,
            {{std::move(text_utf8), std::move(edge_py),
              ExtractTokens({edge}), score, edge.end},
             mismatch == 0});
    }

    for (auto& entry : best_l2) {
        if (entry.exact) {
            l2_full.push_back(std::move(entry.result));
        } else {
            l2_abbrev.push_back(std::move(entry.result));
        }
    }

    auto by_score = [](const DecodeResult& a, const DecodeResult& b) {
        return a.score > b.score;
    };
    std::sort(l2_full.begin(), l2_full.end(), by_score);
    std::sort(l2_abbrev.begin(), l2_abbrev.end(), by_score);
    for (auto& r : l2_full) results.push_back(std::move(r));
    for (auto& r : l2_abbrev) results.push_back(std::move(r));

    return results;
}

std::vector<DecodeResult> Sime::DecodeSentenceCache(
    std::string_view input,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_ || input.empty()) return results;

    std::string lower = NormalizeInput(input);
    if (lower.empty()) return results;

    if (sentence_cache_.input.empty() ||
        !IsAppendOnly(sentence_cache_.input, lower)) {
        ResetSentenceDecodeCache(lower);
    } else if (sentence_cache_.input != lower) {
        AppendSentenceDecodeCache(
            std::string_view(lower).substr(sentence_cache_.input.size()));
    }

    const std::size_t total = lower.size();
    std::vector<Node> net;
    BuildSentenceNetFromCache(net);
    ComputeEdgePenalties(net, lower, PinyinMatchPenalty, total + 1);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    std::unordered_set<std::string> dedup;
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;

    {
        const auto tail = net.back().states.GetStates();
        const std::size_t scan =
            std::min<std::size_t>(BeamSize, tail.size());
        std::vector<DecodeResult> l1;
        l1.reserve(scan);
        std::unordered_set<std::string> l1_seen;
        for (std::size_t rank = 0; rank < scan; ++rank) {
            auto path = Backtrace(tail[rank], net.size() - 1);
            if (path.empty()) continue;
            std::string text = ExtractText(path);
            if (text.empty() || !l1_seen.insert(text).second) continue;
            std::string py = ExtractUnits(path, lower);

            std::size_t piece_len = 0;
            for (char c : py) {
                if (c != '\'') ++piece_len;
            }
            std::size_t distance =
                (piece_len > total) ? (piece_len - total) : 0;
            float_t dist_penalty =
                static_cast<float_t>(distance) * penalty_per_unit;

            l1.push_back({std::move(text), std::move(py),
                          ExtractTokens(path),
                          -tail[rank].score - dist_penalty,
                          input.size()});
        }
        std::sort(l1.begin(), l1.end(),
                  [](const DecodeResult& a, const DecodeResult& b) {
                      return a.score > b.score;
                  });
        const std::size_t full_limit = 1 + extra;
        for (std::size_t i = 0; i < l1.size() && results.size() < full_limit;
             ++i) {
            dedup.insert(l1[i].text);
            results.push_back(std::move(l1[i]));
        }
    }

    std::vector<Layer2Entry> best_l2;
    std::unordered_map<std::string, std::size_t> l2_index_by_text;
    std::vector<DecodeResult> l2_full;
    std::vector<DecodeResult> l2_abbrev;

    for (const auto& edge : net[0].es) {
        if (edge.id == NotToken) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;
        std::string text_utf8 = TextFromU32(text_u32);
        if (dedup.contains(text_utf8)) continue;

        std::size_t distance = (total > edge.end) ? (total - edge.end) : 0;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;

        auto slice = std::string_view(lower).substr(
            edge.start, edge.end - edge.start);
        std::size_t mismatch = CountSyllableMismatch(edge.pieces, slice, false);

        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty;

        std::string edge_py = edge.pieces
            ? AbbreviatePieces(edge.pieces, slice)
            : "";
        PushBestLayer2Entry(
            best_l2,
            l2_index_by_text,
            {{std::move(text_utf8), std::move(edge_py),
              ExtractTokens({edge}), score, edge.end},
             mismatch == 0});
    }

    for (auto& entry : best_l2) {
        if (entry.exact) {
            l2_full.push_back(std::move(entry.result));
        } else {
            l2_abbrev.push_back(std::move(entry.result));
        }
    }

    auto by_score = [](const DecodeResult& a, const DecodeResult& b) {
        return a.score > b.score;
    };
    std::sort(l2_full.begin(), l2_full.end(), by_score);
    std::sort(l2_abbrev.begin(), l2_abbrev.end(), by_score);
    for (auto& r : l2_full) results.push_back(std::move(r));
    for (auto& r : l2_abbrev) results.push_back(std::move(r));

    return results;
}

std::vector<DecodeResult> Sime::NextTokens(
    const std::vector<TokenID>& context,
    std::size_t num,
    bool en) const {
    std::vector<DecodeResult> results;
    if (!ready_ || num == 0) return results;

    // Only the last (num-1) context tokens matter — that's the maximum
    // history a num-gram model can condition on.  Walking from root with
    // at most (num-1) steps also guarantees we never reach leaf level.
    Scorer::Pos pos{};
    const auto tail = static_cast<std::size_t>(std::max(scorer_.Num() - 1, 1));
    std::size_t start = context.size() > tail ? context.size() - tail : 0;
    for (std::size_t i = start; i < context.size(); ++i) {
        Scorer::Pos next{};
        scorer_.ScoreMove(pos, context[i], next);
        pos = next;
    }

    // When filtering to English only, widen the scorer pool — many top
    // predictions will be Chinese and get dropped.
    const std::size_t pool = en ? num * 16 : num * 4;
    auto next_tokens = scorer_.NextTokens(pos, pool);

    // English tokens in Sime's corpora are ASCII-letter words (possibly
    // containing an apostrophe, e.g. don't, 's), or SentencePiece-style
    // pieces prefixed with U+2581 (word boundary). Contraction pieces like
    // "'s" / "'re" start with the apostrophe, so accept it too.
    auto is_english = [](const char32_t* chars) {
        char32_t c = chars[0];
        if (c == 0x2581) return true;
        if (c == U'\'') return true;
        return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
    };

    std::unordered_set<std::string> seen;
    for (const auto& [tid, pro] : next_tokens) {
        if (results.size() >= num) break;
        if (tid < StartToken) continue;

        const char32_t* chars = dict_.TokenAt(tid);
        if (!chars || chars[0] == 0) continue;
        if (en && !is_english(chars)) continue;

        std::u32string u32;
        for (std::size_t i = 0; chars[i] != 0; ++i)
            u32.push_back(chars[i]);
        std::string text = TextFromU32(u32);
        if (!seen.insert(text).second) continue;

        results.push_back({std::move(text), {}, {tid}, -pro, 0});
    }

    return results;
}

std::vector<DecodeResult> Sime::GetTokens(
    std::string_view prefix,
    std::size_t num,
    bool en) const {
    std::vector<DecodeResult> results;
    if (!ready_ || num == 0 || prefix.empty()) return results;

    struct Candidate {
        std::string text;
        TokenID id;
        float_t score;
    };
    std::vector<Candidate> candidates;

    auto collect = [&](Dict::DatType type,
                       const std::vector<trie::SearchResult>& matches) {
        for (const auto& r : matches) {
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                TokenID tid = entry.items[i].id;
                const char32_t* chars = dict_.TokenAt(tid);
                if (!chars || chars[0] == 0) continue;

                std::u32string u32;
                for (std::size_t j = 0; chars[j] != 0; ++j)
                    u32.push_back(chars[j]);
                std::string text = TextFromU32(u32);

                Scorer::Pos pos{};
                Scorer::Pos next{};
                float_t cost = scorer_.ScoreMove(pos, tid, next);

                candidates.push_back({std::move(text), tid, cost});
            }
        }
    };

    collect(Dict::LetterEn,
            dict_.Dat(Dict::LetterEn).FindWordsWithPrefix(prefix, num * 4));
    if (!en) {
        // Mixed mode: also search the pinyin DAT. Plain prefix match is
        // exactly what we want — user typed a literal pinyin prefix (possibly
        // with '\'' syllable separators) and wants every token whose stored
        // pinyin starts with it, including deeper multi-syllable completions.
        collect(Dict::LetterPinyin,
                dict_.Dat(Dict::LetterPinyin)
                    .FindWordsWithPrefix(prefix, num * 4));
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.score < b.score; });

    std::unordered_set<std::string> seen;
    for (auto& c : candidates) {
        if (results.size() >= num) break;
        if (!seen.insert(c.text).second) continue;
        results.push_back({std::move(c.text), {}, {c.id}, -c.score,
                           prefix.size()});
    }

    return results;
}

} // namespace sime
