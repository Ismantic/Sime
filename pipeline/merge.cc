// K-way merge of sorted binary n-gram files.
// Each record: N x uint32 (token IDs) + 1 x uint32 (count).
// Same-key counts are summed.
//
// Usage: merge -n <ngram_order> -o <output> input1 input2 ...

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

static constexpr std::size_t kMaxN = 3;
static constexpr std::size_t kBufRecords = 65536;

struct Record {
    std::uint32_t ids[kMaxN]{};
    std::uint32_t cnt = 0;
};

class BufferedReader {
public:
    BufferedReader(const char* path, int n)
        : n_(n), rec_size_(static_cast<std::size_t>(n + 1) * sizeof(std::uint32_t)) {
        fp_ = std::fopen(path, "rb");
        if (!fp_) {
            std::cerr << "cannot open: " << path << "\n";
            return;
        }
        buf_.resize(kBufRecords);
        Fill();
    }

    ~BufferedReader() {
        if (fp_) std::fclose(fp_);
    }

    bool Valid() const { return fp_ != nullptr; }
    bool Done() const { return pos_ >= count_ && eof_; }
    const Record& Current() const { return buf_[pos_]; }

    void Advance() {
        ++pos_;
        if (pos_ >= count_ && !eof_) {
            Fill();
        }
    }

private:
    void Fill() {
        pos_ = 0;
        count_ = 0;
        std::uint32_t raw[(kMaxN + 1)];
        while (count_ < kBufRecords) {
            if (std::fread(raw, rec_size_, 1, fp_) != 1) {
                eof_ = true;
                break;
            }
            Record& r = buf_[count_];
            std::memset(r.ids, 0, sizeof(r.ids));
            for (int i = 0; i < n_; ++i) {
                r.ids[i] = raw[i];
            }
            r.cnt = raw[n_];
            ++count_;
        }
    }

    FILE* fp_ = nullptr;
    int n_;
    std::size_t rec_size_;
    std::vector<Record> buf_;
    std::size_t pos_ = 0;
    std::size_t count_ = 0;
    bool eof_ = false;
};

struct HeapEntry {
    std::uint32_t ids[kMaxN]{};
    std::uint32_t cnt = 0;
    int source = 0;

    bool operator>(const HeapEntry& o) const {
        for (std::size_t i = 0; i < kMaxN; ++i) {
            if (ids[i] != o.ids[i]) return ids[i] > o.ids[i];
        }
        return false;
    }
};

static bool KeyEqual(const std::uint32_t* a, const std::uint32_t* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

int main(int argc, char* argv[]) {
    int n = 0;
    const char* output = nullptr;
    std::vector<const char*> inputs;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            n = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else {
            inputs.push_back(argv[i]);
        }
    }

    if (n < 1 || n > 3 || !output || inputs.empty()) {
        std::cerr << "usage: merge -n <1|2|3> -o <output> input1 input2 ...\n";
        return 1;
    }

    const std::size_t rec_size = static_cast<std::size_t>(n + 1) * sizeof(std::uint32_t);

    std::vector<BufferedReader*> readers;
    for (auto* path : inputs) {
        auto* r = new BufferedReader(path, n);
        if (!r->Valid()) return 1;
        readers.push_back(r);
    }

    // Min-heap
    std::vector<HeapEntry> heap;
    for (int i = 0; i < static_cast<int>(readers.size()); ++i) {
        if (!readers[i]->Done()) {
            HeapEntry e;
            std::memcpy(e.ids, readers[i]->Current().ids, sizeof(e.ids));
            e.cnt = readers[i]->Current().cnt;
            e.source = i;
            heap.push_back(e);
        }
    }
    std::make_heap(heap.begin(), heap.end(), std::greater<HeapEntry>{});

    FILE* out = std::fopen(output, "wb");
    if (!out) {
        std::cerr << "cannot open output: " << output << "\n";
        return 1;
    }

    // Write buffer
    std::vector<std::uint32_t> wbuf;
    wbuf.reserve(kBufRecords * (kMaxN + 1));
    std::uint64_t merged = 0;

    auto Flush = [&]() {
        if (!wbuf.empty()) {
            std::fwrite(wbuf.data(), sizeof(std::uint32_t), wbuf.size(), out);
            wbuf.clear();
        }
    };

    auto WriteRecord = [&](const std::uint32_t* ids, std::uint32_t cnt) {
        for (int i = 0; i < n; ++i) wbuf.push_back(ids[i]);
        wbuf.push_back(cnt);
        ++merged;
        if (wbuf.size() >= kBufRecords * static_cast<std::size_t>(n + 1)) {
            Flush();
        }
    };

    std::uint32_t cur_ids[kMaxN]{};
    std::uint64_t cur_cnt = 0;
    bool has_current = false;

    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), std::greater<HeapEntry>{});
        auto top = heap.back();
        heap.pop_back();

        if (!has_current || !KeyEqual(cur_ids, top.ids)) {
            if (has_current) {
                WriteRecord(cur_ids, static_cast<std::uint32_t>(cur_cnt));
            }
            std::memcpy(cur_ids, top.ids, sizeof(cur_ids));
            cur_cnt = top.cnt;
            has_current = true;
        } else {
            cur_cnt += top.cnt;
        }

        // Advance source reader
        readers[top.source]->Advance();
        if (!readers[top.source]->Done()) {
            HeapEntry e;
            std::memcpy(e.ids, readers[top.source]->Current().ids, sizeof(e.ids));
            e.cnt = readers[top.source]->Current().cnt;
            e.source = top.source;
            heap.push_back(e);
            std::push_heap(heap.begin(), heap.end(), std::greater<HeapEntry>{});
        }

        if (merged % 50000000 == 0 && merged > 0) {
            std::cerr << "  " << merged << " records merged\n";
        }
    }

    if (has_current) {
        WriteRecord(cur_ids, static_cast<std::uint32_t>(cur_cnt));
    }
    Flush();
    std::fclose(out);

    for (auto* r : readers) delete r;

    std::cerr << "merged " << inputs.size() << " files -> " << merged << " records\n";
    return 0;
}
