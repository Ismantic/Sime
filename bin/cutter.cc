// sime-cut: segment Chinese text using the Sime LM + dict.
//
//   sime-cut --dict <sime.dict> --cnt <sime.raw.cnt>
//   read lines from stdin, output segmented tokens per line.

#include "cut.h"
#include "dict.h"
#include "score.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::filesystem::path dict_path, cnt_path;
    std::string sep = " ";
    std::string unk_label = "<UNK>";

    for (int i = 1; i < argc; ++i) {
        if ((std::strcmp(argv[i], "--dict") == 0 ||
             std::strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
            dict_path = argv[++i];
        } else if ((std::strcmp(argv[i], "--cnt") == 0 ||
                    std::strcmp(argv[i], "-c") == 0) && i + 1 < argc) {
            cnt_path = argv[++i];
        } else if (std::strcmp(argv[i], "--sep") == 0 && i + 1 < argc) {
            sep = argv[++i];
        } else if (std::strcmp(argv[i], "--unk") == 0 && i + 1 < argc) {
            unk_label = argv[++i];
        } else {
            std::cerr << "Usage: sime-cut --dict <path> --cnt <path> "
                      << "[--sep <str>] [--unk <label>]\n"
                      << "Reads lines from stdin; writes segmentation to stdout.\n";
            return 1;
        }
    }
    if (dict_path.empty() || cnt_path.empty()) {
        std::cerr << "--dict and --cnt are required\n";
        return 1;
    }

    sime::Dict dict;
    if (!dict.Load(dict_path)) {
        std::cerr << "Failed to load dict: " << dict_path << "\n";
        return 1;
    }
    sime::Scorer scorer;
    if (!scorer.Load(cnt_path)) {
        std::cerr << "Failed to load scorer: " << cnt_path << "\n";
        return 1;
    }

    sime::Cutter cutter(dict, scorer);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) { std::cout << "\n"; continue; }
        auto toks = cutter.Cut(line);
        for (std::size_t i = 0; i < toks.size(); ++i) {
            if (i) std::cout << sep;
            std::cout << (toks[i].is_unk ? unk_label : toks[i].text);
        }
        std::cout << "\n";
    }
    return 0;
}
