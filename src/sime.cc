#include "sime.h"

#include "ustr.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace sime {

namespace {

bool BetterLayer2Entry(const DecodeResult& lhs, const DecodeResult& rhs) {
    // Dedup tie-break: pure score comparison. Penalty is already in score
    // so the cleaner-provenance entry (exact emit, no expansion penalty)
    // naturally wins.
    if (lhs.score != rhs.score) return lhs.score > rhs.score;
    if (lhs.cnt != rhs.cnt) return lhs.cnt > rhs.cnt;
    return lhs.units.size() > rhs.units.size();
}

void PushBestLayer2Entry(std::vector<DecodeResult>& best_entries,
                         std::unordered_map<std::string, std::size_t>& index_by_text,
                         DecodeResult candidate) {
    auto it = index_by_text.find(candidate.text);
    if (it == index_by_text.end()) {
        index_by_text.emplace(candidate.text, best_entries.size());
        best_entries.push_back(std::move(candidate));
        return;
    }
    auto& current = best_entries[it->second];
    if (BetterLayer2Entry(candidate, current)) {
        current = std::move(candidate);
    }
}

// Vocabulary signature used to detect stale user-sentence files when the
// LM is regenerated (new TokenIDs). LM file size + mtime is a cheap proxy
// for "was the LM regenerated"; when it changes we drop the old file.
std::string MakeVocabSignature(const std::filesystem::path& dict_path,
                               const std::filesystem::path& lm_path) {
    std::error_code ec;
    auto dict_size = std::filesystem::file_size(dict_path, ec);
    if (ec) return {};
    auto lm_size = std::filesystem::file_size(lm_path, ec);
    if (ec) return {};
    auto lm_mtime = std::filesystem::last_write_time(lm_path, ec);
    if (ec) return {};
    auto lm_mtime_secs =
        std::chrono::duration_cast<std::chrono::seconds>(
            lm_mtime.time_since_epoch())
            .count();
    std::ostringstream ss;
    ss << dict_size << ':' << lm_size << ':' << lm_mtime_secs;
    return ss.str();
}

} // namespace

void Sime::MaybeTrimCaches() const {
    if (++decode_count_ % kSepCacheTrimInterval == 0) {
        dict_.ClearSepCaches();
    }
}

Sime::Sime(const std::filesystem::path& dict_path,
                         const std::filesystem::path& model_path) {
    if (!dict_.Load(dict_path)) {
        return;
    }
    if (!scorer_.Load(model_path)) {
        dict_.Clear();
        return;
    }
    vocab_sig_ = MakeVocabSignature(dict_path, model_path);
    ready_ = true;
}

void Sime::SetUserSentenceEnabled(bool enabled) {
    user_sentence_enabled_ = enabled;
}

bool Sime::LoadUserSentence(const std::filesystem::path& path) {
    return user_sentence_.Load(path, vocab_sig_);
}

bool Sime::SaveUserSentence(const std::filesystem::path& path) const {
    return user_sentence_.Save(path, vocab_sig_,
        [this](TokenID id) { return TokenText(id); });
}

void Sime::LearnUserSentence(const std::vector<TokenID>& context,
                             const std::vector<TokenID>& sentence) {
    if (!user_sentence_enabled_) {
        return;
    }
    user_sentence_.Add(context, sentence);
}

std::string Sime::TokenText(TokenID id) const {
    if (id == NotToken || !ready_) {
        return {};
    }
    const char32_t* p = dict_.TokenAt(id);
    if (!p) {
        return {};
    }
    std::u32string u32;
    while (*p) {
        u32.push_back(*p);
        ++p;
    }
    return TextFromU32(u32);
}

void Sime::ComputeEdgePenalties(std::vector<Node>& net,
                                std::string_view input) {
    // English penalty: only "long input + full coverage" English gets
    // the lighter EnglishPenalty (e.g. google on 466453, hello on
    // input "hello"). Everything else (partial-coverage like zg in
    // zgkxjsdx, zgkxjs in zgkxjsdx, or short input full-cover like up
    // / nm / key) gets EnglishPenaltyShort — these are likely
    // coincidental English spellings of CN abbreviations.
    // Expansion penalty: short input gets ExpansionPenaltyShort (lets
    // 87→他说 beat short English exact); long input keeps stricter
    // ExpansionPenalty so abbrev edges don't crowd out the natural
    // full-pinyin path.
    const bool short_input = input.size() <= 4;
    const float_t expansion_penalty = short_input
        ? ExpansionPenaltyShort : ExpansionPenalty;
    for (auto& col : net) {
        for (auto& edge : col.es) {
            if (!edge.pieces || edge.id == NotToken) continue;
            float_t en_pen = 0.0f;
            if (edge.english) {
                bool full_cover =
                    edge.start == 0 && edge.end == input.size();
                en_pen = (full_cover && !short_input)
                    ? EnglishPenalty : EnglishPenaltyShort;
            }
            edge.penalty = en_pen
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
                    bool en = false) {
        net[s].es.push_back({s, new_col, tid, pieces, 0, false, en});
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
                emit(s, new_col, entry.items[i].id, entry.items[i].pieces, en);
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
            // For English exact-stem matches PrefixSearch already emits
            // them, so skip duplicates. CN fuzzy walks (q→qi etc.) can
            // hit eow at length == tail.size() — keep those, since
            // PrefixSearchPinyin drops fuzzy states and would miss them.
            if (!pinyin && r.length <= tail.size()) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                net[s].es.push_back({s, target_col, entry.items[i].id,
                                     entry.items[i].pieces, 0, true, en});
            }
        }
    };

    // letter_bounds = greedy syllable walk (mirrors InitNet).
    // letter_is_syllable[i] is true iff segment i is a *terminal*
    // (non-extendable) pinyin syllable.
    std::vector<std::size_t> letter_bounds;
    std::vector<bool> letter_is_syllable;
    if (p > 0) {
        letter_bounds.push_back(0);
        std::size_t pos = 0;
        while (pos < p) {
            if (start[pos] == '\'') {
                ++pos;
                letter_bounds.push_back(pos);
                letter_is_syllable.push_back(false);
                continue;
            }
            bool terminal = false;
            std::size_t best = 1;
            for (std::size_t len = std::min(p - pos, std::size_t(6));
                 len >= 2; --len) {
                std::string ps(start.substr(pos, len));
                if (Dict::IsKnownPinyin(ps)) {
                    best = len;
                    terminal = !Dict::IsExtendablePinyin(ps);
                    break;
                }
            }
            pos += best;
            letter_bounds.push_back(pos);
            letter_is_syllable.push_back(terminal);
        }
    }

    // digit_bounds = greedy T9-syllable walk (mirrors letter_bounds).
    // digit_is_syllable[i] is true iff segment i is a *terminal*
    // (non-extendable) T9 syllable.
    std::vector<std::size_t> digit_bounds;
    std::vector<bool> digit_is_syllable;
    if (d > 0) {
        digit_bounds.push_back(0);
        std::size_t pos = 0;
        while (pos < d) {
            if (nums[pos] == '\'') {
                ++pos;
                digit_bounds.push_back(pos);
                digit_is_syllable.push_back(false);
                continue;
            }
            bool terminal = false;
            std::size_t best = 1;
            for (std::size_t len = std::min(d - pos, std::size_t(6));
                 len >= 2; --len) {
                std::string_view span = nums.substr(pos, len);
                if (Dict::IsKnownT9Syllable(span)) {
                    best = len;
                    terminal = !Dict::IsExtendableT9Syllable(span);
                    break;
                }
            }
            pos += best;
            digit_bounds.push_back(pos);
            digit_is_syllable.push_back(terminal);
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
                                 entry.items[i].pieces, en);
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
                    emit(s, new_col, entry.items[i].id, entry.items[i].pieces, en);
                }
            }
        };
        // Pinyin DAT: lowercase only (no uppercase in pinyin)
        t9_emit(Dict::LetterPinyin, Dict::NumToLettersLower, 512, false);
        // English DAT: both cases
        t9_emit(Dict::LetterEn, Dict::NumToLetters, 512, true);
    }

    // Letter-side boundary expansion (mirrors InitNet): fire FWWPP at
    // every later boundary target whose adjacent left segment is not
    // terminal. English tail completion fires if the suffix contains
    // any non-terminal segment.
    if (expansion && p > 0) {
        for (std::size_t s_idx = 0; s_idx + 1 < letter_bounds.size(); ++s_idx) {
            std::size_t s = letter_bounds[s_idx];
            if (s >= p || start[s] == '\'') continue;

            bool saw_incomplete = false;
            for (std::size_t bi = s_idx + 1; bi < letter_bounds.size(); ++bi) {
                if (!letter_is_syllable[bi - 1]) saw_incomplete = true;
                if (!saw_incomplete) continue;
                std::size_t target = letter_bounds[bi];
                auto prefix = start.substr(s, target - s);
                expand_dat(s, Dict::LetterPinyin, prefix, target, 512, true, false);
            }
            if (saw_incomplete) {
                std::string_view suffix = start.substr(s);
                expand_dat(s, Dict::LetterEn, suffix, p, 1024, false, true);
            }
        }
    }

    // Digit-side boundary expansion (T9 mirror of letter-side). For each
    // digit boundary s, walk AdvanceT9States forward; latch on first
    // non-terminal segment; emit completions at every later boundary
    // target. Drops the old per-column outer loop and 8-char hard cap.
    if (expansion && d > 0) {
        for (std::size_t s_bi = 0; s_bi + 1 < digit_bounds.size(); ++s_bi) {
            std::size_t s_dpos = digit_bounds[s_bi];
            std::size_t s = p + s_dpos;
            if (s_dpos >= d || nums[s_dpos] == '\'') continue;

            auto py_states = dict_.Dat(Dict::LetterPinyin).StartPinyinStates();
            auto en_states = dict_.Dat(Dict::LetterEn).StartPinyinStates();
            bool saw_incomplete = false;
            std::size_t next_bi = s_bi + 1;

            for (std::size_t pos = s_dpos; pos < d; ++pos) {
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

                std::size_t target_dpos = pos + 1;
                if (next_bi >= digit_bounds.size()
                    || digit_bounds[next_bi] != target_dpos) continue;

                std::size_t seg_idx = next_bi - 1;
                if (!digit_is_syllable[seg_idx]) saw_incomplete = true;
                ++next_bi;
                if (!saw_incomplete) continue;

                std::size_t k = target_dpos - s_dpos;
                auto emit_cn = [&](const std::vector<trie::DoubleArray::PinyinState>& sts) {
                    auto comps = dict_.Dat(Dict::LetterPinyin)
                        .CollectCompletionsPinyin(sts, k, 1024, /*stop_at_sep=*/true);
                    for (const auto& r : comps) {
                        auto entry = dict_.GetEntry(Dict::LetterPinyin, r.value);
                        for (uint32_t i = 0; i < entry.count; ++i) {
                            net[s].es.push_back({s, s + k, entry.items[i].id,
                                                 entry.items[i].pieces, 0, true,
                                                 false});
                        }
                    }
                };
                auto emit_en = [&](const std::vector<trie::DoubleArray::PinyinState>& sts) {
                    auto comps = dict_.Dat(Dict::LetterEn)
                        .CollectCompletionsPinyin(sts, k, 1024, /*stop_at_sep=*/true);
                    for (const auto& r : comps) {
                        if (r.length <= k) continue;
                        auto entry = dict_.GetEntry(Dict::LetterEn, r.value);
                        for (uint32_t i = 0; i < entry.count; ++i) {
                            net[s].es.push_back({s, s + k, entry.items[i].id,
                                                 entry.items[i].pieces, 0, true,
                                                 true});
                        }
                    }
                };
                emit_cn(py_states);
                emit_en(en_states);
            }
        }
    }

    // Two-track per-bucket tier filter: CN edges (!english) and English
    // edges (english) are filtered on independent tracks, so an exact
    // edge on one track doesn't suppress edges on the other.
    //   tier 0: !expansion (exact)
    //   tier 1: expansion (abbrev / completion)
    // Keeps both CN exact and English exact alive when they share a
    // bucket (e.g. 466453 → 攻克 + google), and keeps CN expansion
    // alongside English exact (e.g. 539 → 可以 + key).
    auto tier_of = [](const Link& e) -> uint8_t {
        if (e.id == NotToken) return 0;
        return e.expansion ? 1 : 0;
    };
    std::vector<uint8_t> best_py(total + 2, 0xFF);
    std::vector<uint8_t> best_en(total + 2, 0xFF);
    for (std::size_t i = 0; i < total; ++i) {
        auto& edges = net[i].es;
        if (edges.empty()) continue;
        for (const auto& e : edges) {
            uint8_t t = tier_of(e);
            auto& best = e.english ? best_en : best_py;
            if (t < best[e.end]) best[e.end] = t;
        }
        edges.erase(std::remove_if(edges.begin(), edges.end(),
            [&](const Link& e) {
                if (e.id == NotToken) return false;
                const auto& best = e.english ? best_en : best_py;
                return tier_of(e) > best[e.end];
            }), edges.end());
        for (const auto& e : edges) {
            best_py[e.end] = 0xFF;
            best_en[e.end] = 0xFF;
        }
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

std::vector<DecodeResult> Sime::DecodeNumSentence(
    std::string_view nums,
    std::string_view start,
    std::size_t extra) const {
    return DecodeNumSentence(nums, start, {}, extra);
}

std::vector<DecodeResult> Sime::DecodeNumSentence(
    std::string_view nums,
    std::string_view start,
    const std::vector<TokenID>& context,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_) return results;
    MaybeTrimCaches();
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
    InitNumNet(start, nums, net, /*expansion=*/true);
    ComputeEdgePenalties(net, combined_input);
    for (auto& col : net) PruneNode(col.es);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init = InitialState(context);
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
    // All entries (exact + expansion) compete on score; penalty is
    // already in the score so no separate exact/abbrev tiering.
    const std::size_t l2_start = results.size();
    std::vector<DecodeResult> best_l2;
    std::unordered_map<std::string, std::size_t> l2_index_by_text;

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

        auto slice = std::string_view(combined_input).substr(
            edge.start, edge.end - edge.start);

        // Use edge.penalty (English + expansion, set by
        // ComputeEdgePenalties) so L2 score sits on the same coordinate
        // system as L1: -(LM unigram + edge.penalty) - dist_penalty.
        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty
                        - edge.penalty;

        std::string edge_py = edge.pieces
            ? AbbreviatePieces(edge.pieces, slice)
            : "";
        std::size_t cnt = edge.end;
        PushBestLayer2Entry(
            best_l2,
            l2_index_by_text,
            {std::move(text_utf8), std::move(edge_py),
             ExtractTokens({edge}), score, cnt});
    }

    for (auto& entry : best_l2) {
        results.push_back(std::move(entry));
    }
    auto by_score = [](const DecodeResult& a, const DecodeResult& b) {
        return a.score > b.score;
    };
    // Sort the L2 tail (everything after the L1 sentence head) by score —
    // exact and expansion entries compete on equal footing.
    std::sort(results.begin() + l2_start, results.end(), by_score);

    return results;
}

std::vector<DecodeResult> Sime::DecodeNumStr(
    std::string_view nums,
    std::string_view start,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_) return results;
    MaybeTrimCaches();
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
    State init = InitialState();
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
    MaybeTrimCaches();

    std::string lower = NormalizeInput(input);
    if (lower.empty()) return results;

    std::vector<Node> net;
    InitNet(lower, net, /*expansion=*/false);
    ComputeEdgePenalties(net, lower);
    for (auto& col : net) PruneNode(col.es);

    const std::size_t max_top = num == 0 ? 1 : num;
    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init = InitialState();
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
                    bool en = false) {
        net[s].es.push_back({s, new_col, tid, pieces, 0, false, en});
    };

    // Greedy syllable walk. seg_is_syllable[i] is true iff segment i is a
    // *terminal* (non-extendable) known pinyin syllable. Extendable
    // syllables like "xu" (→ xue/xun/xuan) are flagged false so the
    // expansion gate fires through them — the user may have abbreviated a
    // longer syllable to its prefix. best=1 fallback and apostrophe
    // segments are also incomplete.
    std::vector<std::size_t> seg_bounds;
    std::vector<bool> seg_is_syllable;
    {
        seg_bounds.push_back(0);
        std::size_t pos = 0;
        while (pos < total) {
            if (input[pos] == '\'') {
                ++pos;
                seg_bounds.push_back(pos);
                seg_is_syllable.push_back(false);
                continue;
            }
            bool terminal = false;
            std::size_t best = 1;
            for (std::size_t len = std::min(total - pos, std::size_t(6));
                 len >= 2; --len) {
                std::string p(input.substr(pos, len));
                if (Dict::IsKnownPinyin(p)) {
                    best = len;
                    terminal = !Dict::IsExtendablePinyin(p);
                    break;
                }
            }
            pos += best;
            seg_bounds.push_back(pos);
            seg_is_syllable.push_back(terminal);
        }
    }

    for (std::size_t s = 0; s < total; ++s) {
        if (input[s] == '\'') {
            net[s].es.push_back({s, s + 1, NotToken});
            continue;
        }

        std::string_view suffix = input.substr(s);

        auto emit_results = [&](const std::vector<trie::SearchResult>& results,
                                Dict::DatType type, bool en) {
            for (const auto& r : results) {
                auto entry = dict_.GetEntry(type, r.value);
                for (uint32_t i = 0; i < entry.count; ++i) {
                    emit(s, s + r.length, entry.items[i].id,
                         entry.items[i].pieces, en);
                }
            }
        };

        emit_results(
            dict_.Dat(Dict::LetterPinyin).PrefixSearchPinyin(suffix, 512, 2),
            Dict::LetterPinyin, false);
        emit_results(
            dict_.Dat(Dict::LetterEn).PrefixSearch(suffix, 512),
            Dict::LetterEn, true);
    }

    // Boundary-driven expansion: walk forward; once any segment in the
    // span is incomplete (non-terminal pinyin or best=1 fallback), fire
    // FWWPP at every subsequent boundary target. Latching catches
    // "incomplete-then-terminal" spans like "qchuang" → 起床, where the
    // first seg (q) is incomplete but the trailing seg (chuang) is
    // terminal — without the latch, PSPinyin can't reach 起床 because
    // it drops fuzzy states. English tail completion fires per boundary
    // if any non-terminal segment remains in the suffix.
    if (expansion) {
        for (std::size_t s_idx = 0; s_idx + 1 < seg_bounds.size(); ++s_idx) {
            std::size_t s = seg_bounds[s_idx];
            if (s >= total || input[s] == '\'') continue;

            bool saw_incomplete = false;
            for (std::size_t bi = s_idx + 1; bi < seg_bounds.size(); ++bi) {
                if (!seg_is_syllable[bi - 1]) saw_incomplete = true;
                if (!saw_incomplete) continue;
                std::size_t target = seg_bounds[bi];
                auto raw_prefix = input.substr(s, target - s);
                std::string prefix;
                prefix.reserve(raw_prefix.size());
                for (char c : raw_prefix) {
                    if (c != '\'') prefix.push_back(c);
                }
                auto results = dict_.Dat(Dict::LetterPinyin)
                    .FindWordsWithPrefixPinyin(prefix, 512, 2);
                for (const auto& r : results) {
                    auto entry = dict_.GetEntry(Dict::LetterPinyin, r.value);
                    for (uint32_t i = 0; i < entry.count; ++i) {
                        net[s].es.push_back({s, target, entry.items[i].id,
                                             entry.items[i].pieces, 0, true,
                                             false});
                    }
                }
            }
            if (saw_incomplete) {
                std::string_view suffix = input.substr(s);
                auto results = dict_.Dat(Dict::LetterEn)
                    .FindWordsWithPrefix(suffix, 1024);
                for (const auto& r : results) {
                    if (r.length <= suffix.size()) continue;
                    auto entry = dict_.GetEntry(Dict::LetterEn, r.value);
                    for (uint32_t i = 0; i < entry.count; ++i) {
                        net[s].es.push_back({s, total, entry.items[i].id,
                                             entry.items[i].pieces, 0, true,
                                             true});
                    }
                }
            }
        }
    }

    // Two-track per-bucket tier filter: CN edges (!english) and English
    // edges (english) are filtered on independent tracks, so an exact
    // edge on one track doesn't suppress edges on the other.
    //   tier 0: !expansion (exact)
    //   tier 1: expansion (abbrev / completion)
    auto tier_of = [](const Link& e) -> uint8_t {
        if (e.id == NotToken) return 0;
        return e.expansion ? 1 : 0;
    };
    std::vector<uint8_t> best_py(total + 2, 0xFF);
    std::vector<uint8_t> best_en(total + 2, 0xFF);
    for (std::size_t i = 0; i < total; ++i) {
        auto& edges = net[i].es;
        if (edges.empty()) continue;
        for (const auto& e : edges) {
            uint8_t t = tier_of(e);
            auto& best = e.english ? best_en : best_py;
            if (t < best[e.end]) best[e.end] = t;
        }
        edges.erase(std::remove_if(edges.begin(), edges.end(),
            [&](const Link& e) {
                if (e.id == NotToken) return false;
                const auto& best = e.english ? best_en : best_py;
                return tier_of(e) > best[e.end];
            }), edges.end());
        for (const auto& e : edges) {
            best_py[e.end] = 0xFF;
            best_en[e.end] = 0xFF;
        }
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
        // edge.penalty already encodes the English / expansion penalties
        // set by ComputeEdgePenalties.
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

State Sime::InitialState(const std::vector<TokenID>& context) const {
    Scorer::Pos pos = scorer_.StartPos();
    TokenID last = NotToken;
    const std::size_t usable =
        static_cast<std::size_t>(std::max(scorer_.Num() - 1, 1));
    const std::size_t begin = context.size() > usable
                                  ? context.size() - usable
                                  : 0;
    for (std::size_t i = begin; i < context.size(); ++i) {
        TokenID token = context[i];
        if (token == NotToken) {
            continue;
        }
        Scorer::Pos next{};
        scorer_.ScoreMove(pos, token, next);
        scorer_.Back(next);
        pos = next;
        last = token;
    }
    return State(0.0, 0, pos, nullptr, last);
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

                float_t user_adjust = 0.0;
                if (user_sentence_enabled_) {
                    user_adjust = user_sentence_.CostAdjustment(
                        value.backtrace_token, word.id, step);
                }
                float_t next_cost =
                    value.score + step + word.penalty + user_adjust;
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
    return DecodeSentence(input, {}, extra);
}

std::vector<DecodeResult> Sime::DecodeSentence(
    std::string_view input,
    const std::vector<TokenID>& context,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_ || input.empty()) return results;
    MaybeTrimCaches();

    std::string lower = NormalizeInput(input);
    if (lower.empty()) return results;

    const std::size_t total = lower.size();

    std::vector<Node> net;
    InitNet(lower, net, /*expansion=*/true);
    ComputeEdgePenalties(net, lower);
    for (auto& col : net) PruneNode(col.es);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init = InitialState(context);
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
    // Scan the whole beam, sort by score, take top (1 + extra).
    // (penalties are already in beam scores via edge.penalty)
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

            l1.push_back({std::move(text), std::move(py),
                          ExtractTokens(path),
                          -tail[rank].score,
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
    // All entries (exact + expansion) compete on score; penalty is
    // already in the score so no separate exact/abbrev tiering.
    const std::size_t l2_start = results.size();
    std::vector<DecodeResult> best_l2;
    std::unordered_map<std::string, std::size_t> l2_index_by_text;

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

        // Use edge.penalty (English + expansion) so L2 score is on the
        // same coordinate system as L1: -(LM unigram + edge.penalty)
        // - dist_penalty.
        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty
                        - edge.penalty;

        std::string edge_py = edge.pieces
            ? AbbreviatePieces(edge.pieces, slice)
            : "";
        PushBestLayer2Entry(
            best_l2,
            l2_index_by_text,
            {std::move(text_utf8), std::move(edge_py),
             ExtractTokens({edge}), score, edge.end});
    }

    for (auto& entry : best_l2) {
        results.push_back(std::move(entry));
    }
    auto by_score = [](const DecodeResult& a, const DecodeResult& b) {
        return a.score > b.score;
    };
    std::sort(results.begin() + l2_start, results.end(), by_score);

    return results;
}

std::vector<DecodeResult> Sime::NextTokens(
    const std::vector<TokenID>& context,
    std::size_t num,
    bool en) const {
    std::vector<DecodeResult> results;
    if (!ready_ || num == 0) return results;
    MaybeTrimCaches();

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
    MaybeTrimCaches();

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
