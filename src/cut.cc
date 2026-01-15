#include "cut.h"

#include "ustr.h"

#include <algorithm>
#include <iostream>

namespace sime {
namespace {

void WriteWordID(std::ostream& out, TokenID id, bool with_space) {
    if (with_space) {
        out << ' ';
    }
    out << id;
}

} // namespace

Cutter::Cutter(Dict dict, Scorer scorer)
    : dict_(std::move(dict)), scorer_(std::move(scorer)) {}

void Cutter::SegmentStream(std::istream& in,
                           std::ostream& out,
                           CutOutputOptions options) const {
    SentenceReader reader(in);
    std::u32string sentence;
    std::size_t word_count = 0;

    if (!options.text_output) {
        if (options.show_id) {
            WriteWordID(out, options.sentence_token, false);
        } else {
            out.write(reinterpret_cast<const char*>(&options.sentence_token),
                      sizeof(TokenID));
        }
        ++word_count;
    }

    while (reader.NextSentence(sentence)) {
        std::vector<Column> lattice;
        BuildLattice(sentence, options, lattice);
        SearchBest(lattice);
        auto best = ExtractBest(lattice);
        EmitSentence(sentence, best, out, options, word_count);
        if (!options.text_output) {
            if (options.show_id) {
                WriteWordID(out, options.sentence_token, word_count > 0);
            } else {
                out.write(reinterpret_cast<const char*>(&options.sentence_token),
                          sizeof(TokenID));
            }
            ++word_count;
        }
    }
}

void Cutter::BuildLattice(const std::u32string& sentence,
                          CutOutputOptions options,
                          std::vector<Column>& lattice) const {
    lattice.clear();
    lattice.resize(sentence.size() + 2);
    lattice[0].states[Scorer::State{}] =
        StateValue{0.0, nullptr, Scorer::State{}};

    for (std::size_t idx = 0; idx < sentence.size();) {
        auto match = dict_.DoMatch(sentence, idx);
        std::size_t length = match.length;
        TokenID word_id = match.token_id;
        if (length == 0) {
            length = 1;
            word_id = kNotToken;
        }
        auto ambi_len = CalcAmbiguity(dict_, sentence, idx, length);
        if (ambi_len <= length) {
            lattice[idx].words.push_back({idx, idx + length, word_id});
            idx += length;
        } else {
            FullSegmentBlock(sentence, idx, ambi_len, lattice);
            idx += ambi_len;
        }
    }
    lattice[sentence.size()].words.push_back(
        {sentence.size(), sentence.size() + 1, options.sentence_token});
}

void Cutter::FullSegmentBlock(const std::u32string& sentence,
                              std::size_t start,
                              std::size_t length,
                              std::vector<Column>& lattice) const {
    std::size_t limit = start + length;
    for (std::size_t left = start; left < limit; ++left) {
        bool found = false;
        dict_.ForEachMatch(sentence, left, limit - left,
                           [&](std::size_t len, TokenID wid) {
                               found = true;
                               lattice[left].words.push_back({left, left + len, wid});
                           });
        if (!found) {
            lattice[left].words.push_back({left, left + 1, kNotToken});
        }
    }
}

void Cutter::SearchBest(std::vector<Column>& lattice) const {
    for (std::size_t col = 0; col < lattice.size(); ++col) {
        auto& column = lattice[col];
        for (const auto& [state, value] : column.states) {
            for (auto& word : column.words) {
                Scorer::State next_state{};
                double cost = value.cost + scorer_.ScoreMove(state, word.id, next_state);
                scorer_.Back(next_state);
                auto& dest_states = lattice[word.right].states;
                auto it = dest_states.find(next_state);
                if (it == dest_states.end() || it->second.cost > cost) {
                    dest_states[next_state] = StateValue{cost, &word, state};
                }
            }
        }
    }
}

std::vector<Cutter::LatticeWord> Cutter::ExtractBest(
    const std::vector<Column>& lattice) const {
    const auto& tail_states = lattice.back().states;
    if (tail_states.empty()) {
        return {};
    }
    auto best_it = std::min_element(
        tail_states.begin(),
        tail_states.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.second.cost < rhs.second.cost; });

    std::vector<LatticeWord> path;
    const LatticeWord* word = best_it->second.back_word;
    auto prev_state = best_it->second.back_state;
    while (word != nullptr) {
        path.push_back(*word);
        const auto& prev_column = lattice[word->left].states;
        auto it = prev_column.find(prev_state);
        if (it == prev_column.end()) {
            break;
        }
        prev_state = it->second.back_state;
        word = it->second.back_word;
    }
    std::reverse(path.begin(), path.end());
    if (!path.empty() && path.back().right == lattice.size() - 1) {
        path.pop_back();
    }
    return path;
}

void Cutter::EmitSentence(const std::u32string& sentence,
                          const std::vector<LatticeWord>& words,
                          std::ostream& out,
                          CutOutputOptions options,
                          std::size_t& word_count) const {
    TokenID previous_id = options.sentence_token;
    for (const auto& word : words) {
        std::u32string_view slice(sentence.data() + word.left,
                                  word.right - word.left);
        std::string text = ustr::FromU32(slice);
        bool real_gap = (word.id != kNotToken || previous_id != kNotToken);
        if (options.text_output) {
            if (real_gap && previous_id == kNotToken && options.show_id) {
                if (word_count > 0) {
                    out << ' ';
                }
                out << '(' << previous_id << ')';
            }
            if (real_gap && word_count > 0) {
                out << ' ';
            }
            out << text;
            if (options.show_id && word.id != kNotToken) {
                out << '(' << word.id << ')';
            }
        } else if (real_gap) {
            if (options.show_id) {
                WriteWordID(out, word.id, word_count > 0);
            } else {
                out.write(reinterpret_cast<const char*>(&word.id),
                          sizeof(TokenID));
            }
        }
        if (real_gap) {
            ++word_count;
        }
        previous_id = word.id;
    }
}

std::size_t Cutter::CalcAmbiguity(const Dict& dict,
                                  std::u32string_view sentence,
                                  std::size_t start,
                                  std::size_t base_len) {
    std::size_t len = base_len;
    for (std::size_t i = 1; i < len && start + i < sentence.size(); ++i) {
        auto result = dict.DoMatch(sentence, start + i);
        if (len < i + result.length) {
            len = i + result.length;
        }
    }
    return len;
}

} // namespace sime
