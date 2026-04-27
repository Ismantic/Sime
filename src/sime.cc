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
// against their T9 digit form instead. Used for L2 exact/abbrev grouping
// (mismatch == 0 means full match).
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
                                std::string_view input) {
    // Short-input bumps:
    // - English: penalty Short>Long so that an English exact word that
    //   coincidentally spell-matches a short input (e.g. "nm"/"up") doesn't
    //   beat a CN 简拼 alternative.
    // - Expansion: penalty Short<Long so that a deliberate short-input
    //   n-initial abbreviation (e.g. 87→他说) can beat a short English
    //   exact, while long inputs keep stricter expansion penalty so
    //   abbrev edges don't crowd out the natural full-pinyin path.
    const bool short_input = input.size() <= 4;
    const float_t english_penalty = short_input
        ? EnglishPenaltyShort : EnglishPenalty;
    const float_t expansion_penalty = short_input
        ? ExpansionPenaltyShort : ExpansionPenalty;
    for (auto& col : net) {
        for (auto& edge : col.es) {
            if (!edge.pieces || edge.id == NotToken) continue;
            edge.penalty =
                (edge.english ? english_penalty : 0.0f)
                + (edge.expansion ? expansion_penalty : 0.0f);
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
                    TokenID tid, const char* pieces,
                    bool en = false, bool fuzzy = false) {
        net[s].es.push_back({s, new_col, tid, pieces, 0, false, en, fuzzy});
    };

    auto emit_dat = [&](std::size_t s, Dict::DatType type,
                        std::string_view suffix, std::size_t max_end,
                        bool pinyin, bool en) {
        auto results = pinyin
            ? dict_.Dat(type).PrefixSearchPinyin(suffix, 512)
            : dict_.Dat(type).PrefixSearch(suffix, 512);
        for (const auto& r : results) {
            std::size_t new_col = s + r.length;
            if (new_col > max_end) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                emit(s, new_col, entry.items[i].id, entry.items[i].pieces,
                     en, r.fuzzy);
            }
        }
    };

    auto expand_dat = [&](std::size_t s, Dict::DatType type,
                          std::string_view tail, std::size_t target_col,
                          std::size_t max_num, bool pinyin, bool en) {
        auto results = pinyin
            ? dict_.Dat(type).FindWordsWithPrefixPinyin(tail, max_num)
            : dict_.Dat(type).FindWordsWithPrefix(tail, max_num);
        for (const auto& r : results) {
            if (r.length <= tail.size()) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                net[s].es.push_back({s, target_col, entry.items[i].id,
                                     entry.items[i].pieces, 0, true, en,
                                     r.fuzzy});
            }
        }
    };

    // Segment letter prefix — inclusive boundary set (every position p
    // where some [j, p) is a known pinyin syllable). Mirrors InitNet.
    std::vector<std::size_t> letter_bounds;
    if (p > 0) {
        std::vector<bool> is_bound(p + 1, false);
        is_bound[0] = true;
        is_bound[p] = true;
        for (std::size_t j = 0; j < p; ++j) {
            if (start[j] == '\'') { is_bound[j] = true; is_bound[j + 1] = true; continue; }
            std::size_t max_len = std::min(p - j, std::size_t(6));
            for (std::size_t len = 2; len <= max_len; ++len) {
                if (Dict::IsKnownPinyin(std::string(start.substr(j, len)))) {
                    is_bound[j + len] = true;
                }
            }
        }
        for (std::size_t i = 0; i <= p; ++i) {
            if (is_bound[i]) letter_bounds.push_back(i);
        }
    }

    // Segment digits via T9 syllable lookup — the T9 analog of letter
    // segmentation. Inclusive boundary set: position p is a boundary iff
    // some [j, p) is a known T9 syllable (or p ∈ {0, d}, or marked by '\'').
    // T9 has segmentation ambiguity (e.g. "744824" parses as both
    // "744+824" = shi+tai and "74+4824" = ri+huai), so we keep ALL
    // possible syllable ends rather than greedy-longest-only — the
    // analog of letter mode's static IsKnownPinyin lookup which is
    // independent at every position.
    std::vector<std::size_t> digit_bounds;
    if (d > 0) {
        std::vector<bool> is_bound(d + 1, false);
        is_bound[0] = true;
        is_bound[d] = true;
        for (std::size_t j = 0; j < d; ++j) {
            if (nums[j] == '\'') { is_bound[j] = true; is_bound[j + 1] = true; continue; }
            std::size_t max_len = std::min(d - j, std::size_t(6));
            for (std::size_t len = 2; len <= max_len; ++len) {
                if (Dict::IsKnownT9Syllable(nums.substr(j, len))) {
                    is_bound[j + len] = true;
                }
            }
        }
        for (std::size_t i = 0; i <= d; ++i) {
            if (is_bound[i]) digit_bounds.push_back(i);
        }
    }

    for (std::size_t s = 0; s < total; ++s) {
        // === Prefix letter columns ===
        if (s < p) {
            if (start[s] == '\'') {
                net[s].es.push_back({s, s + 1, NotToken});
                continue;
            }

            std::string_view suffix = start.substr(s);
            emit_dat(s, Dict::LetterPinyin, suffix, p, true, false);
            emit_dat(s, Dict::LetterEn, suffix, p, false, true);

            // Chinese expansion at every segment boundary > s within 8
            // chars (mirrors InitNet). English expansion: full suffix → p.
            if (expansion) {
                for (std::size_t bi = 0; bi < letter_bounds.size(); ++bi) {
                    if (letter_bounds[bi] <= s) continue;
                    std::size_t target = letter_bounds[bi];
                    if (target - s > 8) break;
                    auto prefix = start.substr(s, target - s);
                    if (Dict::IsKnownPinyin(std::string(prefix))) continue;
                    expand_dat(s, Dict::LetterPinyin, prefix, target, 512, true, false);
                }
                if (!Dict::IsKnownPinyin(std::string(suffix.substr(0, p - s)))) {
                    expand_dat(s, Dict::LetterEn, suffix, p, 1024, false, true);
                }
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
                                      std::size_t max_num, bool en) {
                    auto results = dict_.Dat(type).PrefixSearchT9(
                        cross, expander, max_num);
                    for (const auto& r : results) {
                        std::size_t new_col = s + r.length;
                        if (new_col <= p) continue;  // already covered by letter-only search
                        if (new_col > total) continue;
                        auto entry = dict_.GetEntry(type, r.value);
                        for (uint32_t i = 0; i < entry.count; ++i) {
                            emit(s, new_col, entry.items[i].id,
                                 entry.items[i].pieces, en, r.fuzzy);
                        }
                    }
                };
                cross_emit(Dict::LetterPinyin, Dict::NumToLettersLower, 512, false);
                cross_emit(Dict::LetterEn, Dict::NumToLetters, 512, true);
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
                           std::size_t max_num, bool en) {
            auto results = dict_.Dat(type).PrefixSearchT9(
                num_suffix, expander, max_num);
            for (const auto& r : results) {
                std::size_t new_col = s + r.length;
                if (new_col > total) continue;
                auto entry = dict_.GetEntry(type, r.value);
                for (uint32_t i = 0; i < entry.count; ++i) {
                    emit(s, new_col, entry.items[i].id, entry.items[i].pieces,
                         en, r.fuzzy);
                }
            }
        };
        // Pinyin DAT: lowercase only (no uppercase in pinyin)
        t9_emit(Dict::LetterPinyin, Dict::NumToLettersLower, 512, false);
        // English DAT: both cases
        t9_emit(Dict::LetterEn, Dict::NumToLetters, 512, true);

        // Boundary-driven expansion. Walk AdvanceT9States forward
        // incrementally from s; at each digit_bounds boundary within 8
        // chars, if the segment is NOT a known T9 syllable (the analog
        // of !IsKnownPinyin), emit expansion edges (s, s+k) for
        // completions that extend beyond the boundary. This is the T9
        // mirror of InitNet's segment-boundary FWWPP.
        if (expansion) {
            auto py_states = dict_.Dat(Dict::LetterPinyin).StartPinyinStates();
            auto en_states = dict_.Dat(Dict::LetterEn).StartPinyinStates();
            std::size_t bi = 0;
            while (bi < digit_bounds.size() && digit_bounds[bi] <= dpos) ++bi;
            for (std::size_t k = 1; k <= 8; ++k) {
                std::size_t pos = dpos + k - 1;
                if (pos >= d) break;
                if (nums[pos] == '\'') break;
                auto ch = static_cast<uint8_t>(nums[pos]);
                if (!py_states.empty()) {
                    dict_.Dat(Dict::LetterPinyin).AdvanceT9States(
                        py_states, ch, Dict::NumToLettersLower);
                }
                if (!en_states.empty()) {
                    dict_.Dat(Dict::LetterEn).AdvanceT9States(
                        en_states, ch, Dict::NumToLetters);
                }
                if (py_states.empty() && en_states.empty()) break;

                std::size_t target_dpos = dpos + k;
                if (bi >= digit_bounds.size()
                    || digit_bounds[bi] != target_dpos) continue;
                ++bi;

                if (Dict::IsKnownT9Syllable(nums.substr(dpos, k))) continue;

                auto emit_completions = [&](Dict::DatType type,
                                            const std::vector<trie::DoubleArray::PinyinState>& sts,
                                            std::size_t max_num, bool en) {
                    auto comps = dict_.Dat(type).CollectCompletionsPinyin(
                        sts, k, max_num, /*stop_at_sep=*/true);
                    for (const auto& r : comps) {
                        if (r.length <= k) continue;
                        auto entry = dict_.GetEntry(type, r.value);
                        for (uint32_t i = 0; i < entry.count; ++i) {
                            net[s].es.push_back({s, s + k, entry.items[i].id,
                                                 entry.items[i].pieces, 0, true,
                                                 en, r.fuzzy});
                        }
                    }
                };
                emit_completions(Dict::LetterPinyin, py_states, 64, false);
                emit_completions(Dict::LetterEn, en_states, 64, true);
            }
        }
    }

    // Tier-gated source filter: per (start, end) bucket, keep only the
    // highest-priority tier present. Priority:
    //   0: CN exact   (!expansion, !english)
    //   1: everything else — CN expansion, English exact, English expansion
    // CN exact dominates its bucket; otherwise tier-1 edges all survive
    // and beam search picks the winner by score.
    auto tier_of = [](const Link& e) -> uint8_t {
        if (e.id == NotToken) return 0;
        return (!e.expansion && !e.english) ? 0 : 1;
    };

    // Global fuzzy gate: if the whole input has a CN exact full-coverage
    // edge at (0, total), the user's input is complete and unambiguous —
    // any remaining fuzzy speculations at intermediate buckets are noise.
    // English exact full-coverage doesn't count (T9 of 'key' = 539 should
    // still let CN expansion 可以 survive).
    bool has_full_cover_exact = false;
    for (const auto& e : net[0].es) {
        if (e.id != NotToken && e.end == total
            && !e.fuzzy && !e.expansion && !e.english) {
            has_full_cover_exact = true;
            break;
        }
    }

    // Single-track per-bucket gate: drop everything above the best tier
    // seen per (start, end). With 2 tiers that means: CN exact in bucket
    // → keep only CN exact; otherwise keep all tier-1.
    std::vector<uint8_t> best(total + 2, 0xFF);
    for (std::size_t i = 0; i < total; ++i) {
        auto& edges = net[i].es;
        if (edges.empty()) continue;
        for (const auto& e : edges) {
            uint8_t t = tier_of(e);
            if (t < best[e.end]) best[e.end] = t;
        }
        edges.erase(std::remove_if(edges.begin(), edges.end(),
            [&](const Link& e) {
                if (e.id == NotToken) return false;
                if (has_full_cover_exact && e.fuzzy) return true;
                return tier_of(e) > best[e.end];
            }), edges.end());
        for (const auto& e : edges) best[e.end] = 0xFF;
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
    ComputeEdgePenalties(net, combined_input);
    for (auto& col : net) PruneNode(col.es);

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

    // Skip leading apostrophe-only columns (those carry only a NotToken
    // sentinel edge). Without this, an input that starts with `'` (e.g.
    // a leftover after partial commit) leaves Layer 2 with zero candidates.
    std::size_t l2_col = 0;
    while (l2_col < total && net[l2_col].es.size() == 1 &&
           net[l2_col].es[0].id == NotToken) {
        ++l2_col;
    }
    for (const auto& edge : net[l2_col].es) {
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
        const float_t ep = edge.english
            ? (total <= 4 ? EnglishPenaltyShort : EnglishPenalty)
            : 0.0f;
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty
                        - ep;

        std::string edge_py = edge.pieces
            ? AbbreviatePieces(edge.pieces, slice)
            : "";
        std::size_t cnt = edge.end;
        PushBestLayer2Entry(
            best_l2,
            l2_index_by_text,
            {{std::move(text_utf8), std::move(edge_py),
              ExtractTokens({edge}), score, cnt},
             mismatch == 0 && !edge.english});
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

std::vector<DecodeResult> Sime::DecodeNumStr(  // (DecodeNumSentence: 保持原 l2_full > l2_abbrev 分组顺序)
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
    ComputeEdgePenalties(net, combined_input);
    for (auto& col : net) PruneNode(col.es);

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

std::vector<DecodeResult> Sime::DecodeStr(
    std::string_view input,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || input.empty()) return results;

    std::string lower = NormalizeInput(input);
    if (lower.empty()) return results;

    std::vector<Node> net;
    InitNet(lower, net, /*expansion=*/false);
    ComputeEdgePenalties(net, lower);
    for (auto& col : net) PruneNode(col.es);

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
                    TokenID tid, const char* pieces,
                    bool en = false, bool fuzzy = false) {
        net[s].es.push_back({s, new_col, tid, pieces, 0, false, en, fuzzy});
    };

    // Inclusive boundary set: position p is a boundary iff some [j, p)
    // is a known pinyin syllable (or 0/total/'\'' boundary). Pinyin has
    // segmentation ambiguity (e.g. "xian" = xian or xi+an), so we keep
    // ALL possible syllable ends rather than greedy-longest-only.
    std::vector<std::size_t> seg_bounds;
    {
        std::vector<bool> is_bound(total + 1, false);
        is_bound[0] = true;
        is_bound[total] = true;
        for (std::size_t j = 0; j < total; ++j) {
            if (input[j] == '\'') { is_bound[j] = true; is_bound[j + 1] = true; continue; }
            std::size_t max_len = std::min(total - j, std::size_t(6));
            for (std::size_t len = 2; len <= max_len; ++len) {
                if (Dict::IsKnownPinyin(std::string(input.substr(j, len)))) {
                    is_bound[j + len] = true;
                }
            }
        }
        for (std::size_t i = 0; i <= total; ++i) {
            if (is_bound[i]) seg_bounds.push_back(i);
        }
    }

    for (std::size_t s = 0; s < total; ++s) {
        if (input[s] == '\'') {
            net[s].es.push_back({s, s + 1, NotToken});
            continue;
        }

        // Pinyin: PrefixSearchPinyin is exact-only (fuzzy delegated to FWWPP
        // at segment boundaries). English: plain PrefixSearch.
        std::string_view suffix = input.substr(s);

        auto emit_results = [&](const std::vector<trie::SearchResult>& results,
                                Dict::DatType type, bool en) {
            for (const auto& r : results) {
                auto entry = dict_.GetEntry(type, r.value);
                for (uint32_t i = 0; i < entry.count; ++i) {
                    emit(s, s + r.length, entry.items[i].id,
                         entry.items[i].pieces, en, r.fuzzy);
                }
            }
        };

        emit_results(
            dict_.Dat(Dict::LetterPinyin).PrefixSearchPinyin(suffix, 512),
            Dict::LetterPinyin, false);
        emit_results(
            dict_.Dat(Dict::LetterEn).PrefixSearch(suffix, 512),
            Dict::LetterEn, true);

        // Chinese expansion: FWWPP at every segment boundary > s within 8
        // chars. The old "tail-only to total" expansion is the special case
        // where the only boundary is total. Skip boundaries whose prefix is
        // already a known pinyin syllable (no need to "complete" it).
        // English expansion: full suffix → total only.
        if (expansion) {
            for (std::size_t bi = 0; bi < seg_bounds.size(); ++bi) {
                if (seg_bounds[bi] <= s) continue;
                std::size_t target = seg_bounds[bi];
                if (target - s > 8) break;
                auto prefix = input.substr(s, target - s);
                if (Dict::IsKnownPinyin(std::string(prefix))) continue;
                auto results = dict_.Dat(Dict::LetterPinyin)
                    .FindWordsWithPrefixPinyin(prefix, 512);
                for (const auto& r : results) {
                    if (r.length <= prefix.size()) continue;
                    auto entry = dict_.GetEntry(Dict::LetterPinyin, r.value);
                    for (uint32_t i = 0; i < entry.count; ++i) {
                        net[s].es.push_back({s, target, entry.items[i].id,
                                             entry.items[i].pieces, 0, true,
                                             false, r.fuzzy});
                    }
                }
            }
            if (!Dict::IsKnownPinyin(std::string(suffix))) {
                auto results = dict_.Dat(Dict::LetterEn)
                    .FindWordsWithPrefix(suffix, 1024);
                for (const auto& r : results) {
                    if (r.length <= suffix.size()) continue;
                    auto entry = dict_.GetEntry(Dict::LetterEn, r.value);
                    for (uint32_t i = 0; i < entry.count; ++i) {
                        net[s].es.push_back({s, total, entry.items[i].id,
                                             entry.items[i].pieces, 0, true,
                                             true, r.fuzzy});
                    }
                }
            }
        }
    }

    // Tier-gated source filter:
    //   0: CN exact   (!expansion, !english)
    //   1: everything else — CN expansion, English exact, English expansion
    auto tier_of = [](const Link& e) -> uint8_t {
        if (e.id == NotToken) return 0;
        return (!e.expansion && !e.english) ? 0 : 1;
    };

    // Global fuzzy gate: only CN exact full-coverage triggers global drop.
    // English exact full-coverage (e.g. T9 'key' = 539) doesn't count.
    bool has_full_cover_exact = false;
    for (const auto& e : net[0].es) {
        if (e.id != NotToken && e.end == total
            && !e.fuzzy && !e.expansion && !e.english) {
            has_full_cover_exact = true;
            break;
        }
    }

    // Single-track per-bucket gate: CN exact dominates its bucket;
    // otherwise tier-1 edges (CN expansion + any English) all survive.
    std::vector<uint8_t> best(total + 2, 0xFF);
    for (std::size_t i = 0; i < total; ++i) {
        auto& edges = net[i].es;
        if (edges.empty()) continue;
        for (const auto& e : edges) {
            uint8_t t = tier_of(e);
            if (t < best[e.end]) best[e.end] = t;
        }
        edges.erase(std::remove_if(edges.begin(), edges.end(),
            [&](const Link& e) {
                if (e.id == NotToken) return false;
                if (has_full_cover_exact && e.fuzzy) return true;
                return tier_of(e) > best[e.end];
            }), edges.end());
        for (const auto& e : edges) best[e.end] = 0xFF;
    }

    net[total].es.push_back({total, total + 1, NotToken});
}

void Sime::PruneNode(std::vector<Link>& edges,
                     std::unordered_map<TokenID, float_t>* score_cache) const {
    if (edges.size() <= NodeSize) return;

    // Group by span = end - start (bounded by max word length ≤ ~12).
    std::size_t max_span = 0;
    for (const auto& e : edges) {
        std::size_t span = e.end - e.start;
        if (span > max_span) max_span = span;
    }
    std::vector<std::vector<std::size_t>> groups(max_span + 1);
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

    for (auto& indices : groups) {
        if (indices.empty()) continue;
        if (indices.size() <= NodeSize) {
            for (auto idx : indices) pruned.push_back(edges[idx]);
            continue;
        }

        // Single-key sort: estimate first-pass beam cost as
        //   unigram_score + edge.penalty
        // edge.penalty already encodes mismatch * PinyinMatchPenalty plus
        // the English penalty (set by ComputeEdgePenalties), so all the
        // priority signals — !expansion, !english, low mismatch — fall
        // out naturally from this single number.
        std::vector<std::pair<float_t, std::size_t>> scored;
        scored.reserve(indices.size());
        for (auto idx : indices) {
            float_t cost = get_score(edges[idx].id) + edges[idx].penalty;
            scored.emplace_back(cost, idx);
        }
        std::partial_sort(scored.begin(),
                          scored.begin() + static_cast<std::ptrdiff_t>(NodeSize),
                          scored.end());

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
    ComputeEdgePenalties(net, lower);
    for (auto& col : net) PruneNode(col.es);

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

    // Skip leading apostrophe-only columns (see DecodeNumSentence).
    std::size_t l2_col = 0;
    while (l2_col < total && net[l2_col].es.size() == 1 &&
           net[l2_col].es[0].id == NotToken) {
        ++l2_col;
    }
    for (const auto& edge : net[l2_col].es) {
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
        const float_t ep = edge.english
            ? (lower.size() <= 4 ? EnglishPenaltyShort : EnglishPenalty)
            : 0.0f;
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty
                        - ep;

        std::string edge_py = edge.pieces
            ? AbbreviatePieces(edge.pieces, slice)
            : "";
        PushBestLayer2Entry(
            best_l2,
            l2_index_by_text,
            {{std::move(text_utf8), std::move(edge_py),
              ExtractTokens({edge}), score, edge.end},
             mismatch == 0 && !edge.english});  // exact = 中文全匹配
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
