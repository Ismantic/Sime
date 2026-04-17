#include "count.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
void FlushCounts(std::map<Item<N>, Cnt>& counts, 
                 std::fstream& swap, 
                 std::vector<RunRange<N>>& runs) {
    if (counts.empty()) {
        return;
    }
    swap.seekp(0, std::ios::end);
    auto start = static_cast<std::uint64_t>(swap.tellp());
    for (const auto& [gram, freq] : counts) {
        Group<N> rec{gram, freq};
        WriteGroup(swap, rec);
    }
    auto end = static_cast<std::uint64_t>(swap.tellp());
    runs.push_back(RunRange<N>{start, end});
    counts.clear();
}

template <std::size_t N>
void ProcessTextFile(const std::filesystem::path& path,
                     const TokenMap& tokens,
                     std::size_t count_max,
                     std::fstream& swap,
                     std::vector<RunRange<N>>& runs) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open input file: " + path.string());
    }
    std::map<Item<N>, Cnt> counts;
    Item<N> gram{};

    auto feed = [&](TokenID id) {
        if constexpr (N == 1) {
            gram[0] = id;
            ++counts[gram];
            if (counts.size() >= count_max) {
                FlushCounts(counts, swap, runs);
            }
        }
    };

    // For N > 1, we need a history buffer
    std::array<TokenID, N> history{};
    std::size_t filled = 0;

    auto feed_ngram = [&](TokenID id) {
        if constexpr (N == 1) {
            feed(id);
        } else {
            if (filled < N - 1) {
                history[filled++] = id;
                return;
            }
            for (std::size_t i = 0; i < N - 1; ++i) {
                gram[i] = history[i];
            }
            gram[N - 1] = id;
            ++counts[gram];
            if (counts.size() >= count_max) {
                FlushCounts(counts, swap, runs);
            }
            for (std::size_t i = 0; i < N - 2; ++i) {
                history[i] = history[i + 1];
            }
            history[N - 2] = id;
        }
    };

    std::string line;
    std::size_t line_count = 0;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        feed_ngram(SentenceStart);

        std::istringstream iss(line);
        std::string token;
        while (iss >> token) {
            // Skip ▁ (U+2581) separator token
            if (token == "\xE2\x96\x81") continue;
            auto it = tokens.ids.find(token);
            if (it != tokens.ids.end()) {
                feed_ngram(it->second);
            } else {
                feed_ngram(UnknownToken);
            }
        }

        feed_ngram(SentenceEnd);
        filled = 0;

        constexpr std::size_t ProgressInterval = 200000;
        if (++line_count % ProgressInterval == 0) {
            std::cerr << "  " << line_count << " lines, "
                      << counts.size() << " ngrams, "
                      << runs.size() << " flushes\n";
        }
    }

    std::cerr << "  " << line_count << " lines, "
              << counts.size() << " ngrams, "
              << runs.size() << " flushes\n";
    FlushCounts(counts, swap, runs);
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
void RunImpl(const CountOptions& options) {
    std::fstream swap(options.swap,
                      std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!swap.is_open()) {
        throw std::runtime_error("Failed to open swap file: " + options.swap.string());
    }
    std::vector<RunRange<N>> runs;
    runs.reserve(16);

    TokenMap tokens;
    if (!LoadTokenMap(options.dict, tokens)) {
        throw std::runtime_error("Failed to load token dict: " + options.dict.string());
    }
    std::cerr << "loaded " << tokens.ids.size() << " tokens from dict\n";

    for (const auto& input : options.inputs) {
        std::cerr << "processing " << input.string() << " ...\n";
        ProcessTextFile<N>(input, tokens, options.count_max, swap, runs);
    }

    swap.close();
    std::cerr << "merging " << runs.size() << " runs ...\n";
    MergeRuns<N>(options.swap, runs, options.output);
    std::cerr << "done\n";
}

} // namespace sime

void RunCount(const CountOptions& options) {
    if (options.num == 1) {
        RunImpl<1>(options);
    } else if (options.num == 2) {
        RunImpl<2>(options);
    } else if (options.num == 3) {
        RunImpl<3>(options);
    } else {
        throw std::invalid_argument("Only 1-Gram, 2-Gram, or 3-Gram are supported");
    }
}

} // namespace sime
