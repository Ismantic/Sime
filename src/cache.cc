#include "cache.h"
#include "sime.h"

#include <unordered_set>

// =====================================================================
// trie::T9CacheSession
// =====================================================================

namespace trie {

void T9CacheSession::Bind(const DoubleArray* dat, CharExpander expand) {
    dat_ = dat;
    expand_ = expand;
    Reset();
}

void T9CacheSession::Reset() {
    input_len_ = 0;
    if (!dat_ || dat_->Empty()) {
        states_.clear();
        return;
    }
    states_ = {{0, 0}};
}

void T9CacheSession::Reset(std::string_view input) {
    Reset();
    Append(input);
}

void T9CacheSession::Append(std::string_view input) {
    for (char ch : input) {
        Append(static_cast<uint8_t>(ch));
    }
}

void T9CacheSession::Append(uint8_t ch) {
    ++input_len_;
    if (!dat_ || dat_->Empty() || states_.empty()) {
        states_.clear();
        return;
    }
    dat_->AdvanceT9States(states_, ch, expand_);
}

std::vector<SearchResult> T9CacheSession::CollectPrefixMatchesCurrent(
    std::size_t max_per_len) const {
    std::vector<SearchResult> results;
    if (!dat_ || dat_->Empty() || max_per_len == 0 || states_.empty()) {
        return results;
    }

    constexpr std::size_t MaxT9Digits = 24;
    if (input_len_ > MaxT9Digits) return results;

    std::unordered_set<uint64_t> seen;
    auto add_result = [&](uint32_t val) -> bool {
        if (results.size() >= max_per_len) return false;
        uint64_t key = (static_cast<uint64_t>(val) << 32)
                     | static_cast<uint64_t>(input_len_);
        if (!seen.insert(key).second) return false;
        results.push_back({val, input_len_});
        return true;
    };

    for (const auto& s : states_) {
        if (results.size() >= max_per_len) break;
        std::size_t pos = s.pos;
        if (pos >= dat_->size_ || !dat_->array_[pos].eow) continue;
        std::size_t vp = pos ^ dat_->array_[pos].index;
        if (vp >= dat_->size_ || !dat_->array_[vp].HasValue()) continue;
        add_result(static_cast<uint32_t>(dat_->array_[vp].value));
    }

    for (const auto& s : states_) {
        if (results.size() >= max_per_len) break;
        if (s.depth != 1) continue;
        struct Frame { std::size_t pos; int rem; };
        std::vector<Frame> stack = {{s.pos, 6}};
        while (!stack.empty() && results.size() < max_per_len) {
            auto [pos, rem] = stack.back();
            stack.pop_back();
            if (rem <= 0 || pos >= dat_->size_) continue;
            if (dat_->array_[pos].eow) {
                std::size_t vp = pos ^ dat_->array_[pos].index;
                if (vp < dat_->size_ && dat_->array_[vp].HasValue()) {
                    add_result(static_cast<uint32_t>(dat_->array_[vp].value));
                }
            }
            uint32_t base = dat_->array_[pos].index;
            auto try_child = [&](int ch) {
                std::size_t child = pos ^ base ^ static_cast<unsigned>(ch);
                if (child < dat_->size_ && child != pos &&
                    dat_->array_[child].label == ch &&
                    dat_->array_[child].parent == pos) {
                    stack.push_back({child, rem - 1});
                }
            };
            for (int ch = 'a'; ch <= 'z'; ++ch) try_child(ch);
            for (int ch = 'A'; ch <= 'Z'; ++ch) try_child(ch);
        }
    }

    return results;
}

std::vector<SearchResult> T9CacheSession::CollectCompletions(
    std::size_t max_num,
    bool stop_at_sep) const {
    std::vector<SearchResult> results;
    if (!dat_ || dat_->Empty() || max_num == 0) return results;

    std::unordered_set<uint64_t> seen;
    const std::size_t pool = max_num * 8 + 8;
    auto collect_group = [&](bool fuzzy) {
        for (const auto& s : states_) {
            if (results.size() >= max_num) break;
            if (s.fuzzy != fuzzy) continue;
            std::vector<SearchResult> local;
            std::string word;
            dat_->CollectWords(s.pos, word, local, pool, stop_at_sep);
            for (const auto& r : local) {
                uint64_t key = (static_cast<uint64_t>(r.value) << 32)
                             | static_cast<uint64_t>(r.length);
                if (!seen.insert(key).second) continue;
                results.push_back(r);
                if (results.size() >= max_num) break;
            }
        }
    };

    collect_group(false);
    collect_group(true);
    return results;
}

} // namespace trie

// =====================================================================
// sime::Sime cache methods
// =====================================================================

namespace sime {

bool Sime::IsAppendOnly(std::string_view prev, std::string_view next) {
    return next.size() >= prev.size() &&
           next.substr(0, prev.size()) == prev;
}

// --- Num (T9) decode cache ---

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
    num_cache_.dirty.assign(total, true);

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
                               entry.items[i].pieces, 0, true});
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

    std::string combined(num_cache_.start);
    combined += num_cache_.nums;

    for (std::size_t s = 0; s < total; ++s) {
        auto& col = num_cache_.net[s];

        // Skip columns that haven't changed since last rebuild.
        if (s < num_cache_.dirty.size() && !num_cache_.dirty[s]) {
            col.states.Clear();
            continue;
        }

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
        PruneNode(col.es, combined, p, &num_cache_.score_cache);
    }

    // Reset dirty flags after rebuild.
    num_cache_.dirty.assign(total, false);

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
                               entry.items[i].pieces, 0, true});
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

    // Mark dirty columns: all digit columns + cross-active letter columns.
    num_cache_.dirty.resize(total, true);  // new columns default to dirty
    for (std::size_t s = p; s < total; ++s) {
        num_cache_.dirty[s] = true;
    }
    for (std::size_t s = 0; s < p; ++s) {
        if (num_cache_.starts[s].cross_active) {
            num_cache_.dirty[s] = true;
        }
    }

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
        if (tail_len > 6) continue;
        auto& state = num_cache_.starts[s];
        auto py_comp = state.py_t9_session.CollectCompletions(128);
        auto en_comp = state.en_t9_session.CollectCompletions(128);
        emit_expand(num_cache_.dynamic_expansion_edges[s], s, total,
                    tail_len, Dict::LetterPinyin, py_comp);
        emit_expand(num_cache_.dynamic_expansion_edges[s], s, total,
                    tail_len, Dict::LetterEn, en_comp);
    }

    RebuildNumNet();
}

// --- Sentence decode cache ---

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
        PruneNode(net[s].es, input, total + 1);
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
                               entry.items[i].pieces, 0, true});
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
                               entry.items[i].pieces, 0, true});
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

} // namespace sime
