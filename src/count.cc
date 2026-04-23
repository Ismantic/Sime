#include "count.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>


namespace sime {

namespace {

template <std::size_t N>
constexpr std::size_t GroupSize() {
    return N * sizeof(TokenID) + sizeof(Cnt);
}

template <std::size_t N>
void WriteGroup(std::ostream& out, const Group<N>& rec) {
    out.write(reinterpret_cast<const char*>(rec.item.data()),
              static_cast<std::streamsize>(N * sizeof(TokenID)));
    out.write(reinterpret_cast<const char*>(&rec.cnt),
              static_cast<std::streamsize>(sizeof(Cnt)));
}

template <std::size_t N>
struct RunRange {
    std::uint64_t start = 0;
    std::uint64_t end = 0;
};

template <std::size_t N>
void FlushCounts(std::vector<Group<N>>& counts,
                 std::fstream& swap,
                 std::vector<RunRange<N>>& runs) {
    if (counts.empty()) {
        return;
    }
    std::sort(counts.begin(), counts.end(),
              [](const Group<N>& a, const Group<N>& b) {
                  return a.item < b.item;
              });
    // Merge adjacent duplicates.
    swap.seekp(0, std::ios::end);
    auto start = static_cast<std::uint64_t>(swap.tellp());
    Group<N> cur = counts[0];
    for (std::size_t i = 1; i < counts.size(); ++i) {
        if (counts[i].item == cur.item) {
            cur.cnt += counts[i].cnt;
        } else {
            WriteGroup(swap, cur);
            cur = counts[i];
        }
    }
    WriteGroup(swap, cur);
    auto end = static_cast<std::uint64_t>(swap.tellp());
    runs.push_back(RunRange<N>{start, end});
    counts.clear();
}

template <std::size_t N>
class RunReader {
public:
    RunReader(const std::filesystem::path& path,
              RunRange<N> range)
        : file_(path, std::ios::binary),
          remaining_(range.end >= range.start ? range.end - range.start : 0) {
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open swap file for reading");
        }
        file_.seekg(static_cast<std::streamoff>(range.start), std::ios::beg);
    }

    bool Next() {
        if (remaining_ < GroupSize<N>()) {
            return false;
        }
        if (!file_.read(reinterpret_cast<char*>(current_.item.data()),
                        static_cast<std::streamsize>(N * sizeof(TokenID)))) {
            return false;
        }
        if (!file_.read(reinterpret_cast<char*>(&current_.cnt),
                        static_cast<std::streamsize>(sizeof(Cnt)))) {
            return false;
        }
        remaining_ -= GroupSize<N>();
        return true;
    }

    const Group<N>& Current() const { return current_; }

private:
    std::ifstream file_;
    std::uint64_t remaining_;
    Group<N> current_{};
};

template <std::size_t N>
void MergeRuns(const std::filesystem::path& swap_path,
               const std::vector<RunRange<N>>& runs,
               const std::filesystem::path& output_path) {
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open output file: " + output_path.string());
    }
    if (runs.empty()) {
        return;
    }

    struct HeapEntry {
        Group<N> rec;
        std::size_t run_index;
    };

    struct Compare {
        bool operator()(const HeapEntry& a, const HeapEntry& b) const {
            return a.rec.item > b.rec.item;
        }
    };

    std::vector<std::unique_ptr<RunReader<N>>> readers;
    readers.reserve(runs.size());
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, Compare> heap;

    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto& range = runs[i];
        if (range.end <= range.start) {
            continue;
        }
        auto reader = std::make_unique<RunReader<N>>(swap_path, range);
        if (reader->Next()) {
            heap.push(HeapEntry{reader->Current(), i});
            readers.push_back(std::move(reader));
        } else {
            readers.push_back(nullptr);
        }
    }

    Group<N> current{};
    bool has_current = false;
    while (!heap.empty()) {
        auto node = heap.top();
        heap.pop();
        if (!has_current || node.rec.item != current.item) {
            if (has_current) {
                WriteGroup(out, current);
            }
            current = node.rec;
            has_current = true;
        } else {
            if (current.cnt > std::numeric_limits<Cnt>::max() - node.rec.cnt) {
                throw std::overflow_error("Frequency overflow during merge");
            }
            current.cnt += node.rec.cnt;
        }

        auto& reader = readers[node.run_index];
        if (reader && reader->Next()) {
            heap.push(HeapEntry{reader->Current(), node.run_index});
        }
    }

    if (has_current) {
        WriteGroup(out, current);
    }
}

template <std::size_t N>
struct Bucket {
    std::vector<Group<N>> counts;
    std::fstream swap;
    std::vector<RunRange<N>> runs;
    std::filesystem::path swap_path;
    std::filesystem::path output_path;
    std::size_t count_max = 0;
    bool active = false;

    void Init(std::filesystem::path out, std::filesystem::path swp, std::size_t cap) {
        output_path = std::move(out);
        swap_path = std::move(swp);
        count_max = cap;
        swap.open(swap_path,
                  std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        if (!swap.is_open()) {
            throw std::runtime_error("Failed to open swap file: " + swap_path.string());
        }
        counts.reserve(cap);
        runs.reserve(16);
        active = true;
    }

    void Feed(const Item<N>& gram) {
        counts.push_back(Group<N>{gram, 1});
        if (counts.size() >= count_max) {
            FlushCounts(counts, swap, runs);
        }
    }

    void Finish() {
        if (!active) return;
        FlushCounts(counts, swap, runs);
        swap.close();
        MergeRuns<N>(swap_path, runs, output_path);
    }
};

std::filesystem::path WithSuffix(const std::filesystem::path& base,
                                 const std::string& suffix) {
    return base.string() + suffix;
}

void ProcessOneFile(const std::filesystem::path& path,
                    const TokenMap& tokens,
                    const std::unordered_set<TokenID>& punct_ids,
                    int max_order,
                    Bucket<1>* b1,
                    Bucket<2>* b2,
                    Bucket<3>* b3) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open input file: " + path.string());
    }

    // Per-order sliding windows. No <s>/</s> padding: Sime (libime-style)
    // treats IME input as fragments, not full sentences. Each sentence in
    // the corpus is a standalone segment; we just reset the windows at
    // sentence boundaries so n-grams don't cross sentences.
    std::array<TokenID, 2> b2w{}; std::size_t b2_filled = 0;
    std::array<TokenID, 3> b3w{}; std::size_t b3_filled = 0;

    auto reset_windows = [&]() {
        b2_filled = 0;
        b3_filled = 0;
    };

    auto feed_word = [&](TokenID id) {
        if (max_order >= 1 && b1) {
            b1->Feed(Item<1>{id});
        }
        if (max_order >= 2 && b2) {
            if (b2_filled < 2) {
                b2w[b2_filled++] = id;
            } else {
                b2w[0] = b2w[1];
                b2w[1] = id;
            }
            if (b2_filled == 2) {
                b2->Feed(Item<2>{b2w[0], b2w[1]});
            }
        }
        if (max_order >= 3 && b3) {
            if (b3_filled < 3) {
                b3w[b3_filled++] = id;
            } else {
                b3w[0] = b3w[1];
                b3w[1] = b3w[2];
                b3w[2] = id;
            }
            if (b3_filled == 3) {
                b3->Feed(Item<3>{b3w[0], b3w[1], b3w[2]});
            }
        }
    };

    std::string line;
    std::size_t line_count = 0;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        reset_windows();

        std::size_t pos = 0;
        const std::size_t len = line.size();
        while (pos < len) {
            // skip spaces
            while (pos < len && line[pos] == ' ') ++pos;
            if (pos >= len) break;
            std::size_t start = pos;
            while (pos < len && line[pos] != ' ') ++pos;
            std::string_view token(line.data() + start, pos - start);
            // Skip ▁ (U+2581) separator token
            if (token == "\xE2\x96\x81") continue;
            auto it = tokens.ids.find(std::string(token));
            if (it != tokens.ids.end()) {
                feed_word(it->second);
                // Punctuation breaks the context: the punct token is
                // counted (appears at n-gram end) but subsequent tokens
                // won't form bigrams/trigrams with it as history.
                if (!punct_ids.empty() && punct_ids.count(it->second)) {
                    reset_windows();
                }
            } else {
                // OOV: corpus/dict mismatch. IME output is closed-vocabulary,
                // so we never emit <unk>. Just break the sliding windows so
                // bigrams/trigrams don't span the dropped token.
                reset_windows();
            }
        }

        constexpr std::size_t ProgressInterval = 200000;
        if (++line_count % ProgressInterval == 0) {
            std::cerr << "  " << line_count << " lines\n";
        }
    }

    std::cerr << "  total " << line_count << " lines\n";
}

} // namespace

void RunCount(const CountOptions& options) {
    if (options.num < 1 || options.num > 3) {
        throw std::invalid_argument("num must be in [1, 3]");
    }

    TokenMap tokens;
    if (!LoadTokenMap(options.dict, tokens)) {
        throw std::runtime_error("Failed to load token dict: " + options.dict.string());
    }
    std::cerr << "loaded " << tokens.ids.size() << " tokens from dict\n";

    // Load optional punctuation set.
    std::unordered_set<TokenID> punct_ids;
    if (!options.punct.empty()) {
        std::ifstream pf(options.punct);
        if (!pf.is_open()) {
            throw std::runtime_error("Failed to open punct file: " + options.punct.string());
        }
        std::string pline;
        while (std::getline(pf, pline)) {
            if (pline.empty()) continue;
            auto it = tokens.ids.find(pline);
            if (it != tokens.ids.end()) {
                punct_ids.insert(it->second);
            }
        }
        std::cerr << "loaded " << punct_ids.size() << " punctuation tokens\n";
    }

    // Divide the budget across active orders so aggregate memory stays bounded.
    std::size_t per_bucket = std::max<std::size_t>(
        options.count_max / static_cast<std::size_t>(options.num),
        static_cast<std::size_t>(1024));

    Bucket<1> b1;
    Bucket<2> b2;
    Bucket<3> b3;

    if (options.num >= 1) {
        b1.Init(WithSuffix(options.output, ".1gram"),
                WithSuffix(options.swap, ".1"),
                per_bucket);
    }
    if (options.num >= 2) {
        b2.Init(WithSuffix(options.output, ".2gram"),
                WithSuffix(options.swap, ".2"),
                per_bucket);
    }
    if (options.num >= 3) {
        b3.Init(WithSuffix(options.output, ".3gram"),
                WithSuffix(options.swap, ".3"),
                per_bucket);
    }

    for (const auto& input : options.inputs) {
        std::cerr << "processing " << input.string() << " ...\n";
        ProcessOneFile(input, tokens, punct_ids, options.num,
                       options.num >= 1 ? &b1 : nullptr,
                       options.num >= 2 ? &b2 : nullptr,
                       options.num >= 3 ? &b3 : nullptr);
    }

    if (options.num >= 1) {
        std::cerr << "merging 1-grams -> " << b1.output_path.string() << "\n";
        b1.Finish();
    }
    if (options.num >= 2) {
        std::cerr << "merging 2-grams -> " << b2.output_path.string() << "\n";
        b2.Finish();
    }
    if (options.num >= 3) {
        std::cerr << "merging 3-grams -> " << b3.output_path.string() << "\n";
        b3.Finish();
    }
    std::cerr << "done\n";
}

} // namespace sime
