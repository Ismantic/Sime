#include "interpret.h"
#include "ustr.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path trie;
    std::filesystem::path model;
    std::filesystem::path userdict;
    std::size_t num = 5;
    bool sentence = false;
};

void PrintUsage() {
    std::cerr << "Usage: ime-interpreter --trie <trie.bin> --model <model.bin> [--userdict <user.dict>] [--num N] [--sentence]\n";
}

bool ParseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if ((arg == "--trie" || arg == "-t") && i + 1 < argc) {
            opts.trie = argv[++i];
        } else if ((arg == "--model" || arg == "-m") && i + 1 < argc) {
            opts.model = argv[++i];
        } else if ((arg == "--userdict" || arg == "-u") && i + 1 < argc) {
            opts.userdict = argv[++i];
        } else if (arg == "--num" && i + 1 < argc) {
            opts.num = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--sentence" || arg == "-s") {
            opts.sentence = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return false;
        }
    }
    if (opts.trie.empty() || opts.model.empty()) {
        PrintUsage();
        return false;
    }
    if (opts.num == 0) {
        opts.num = 1;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!ParseArgs(argc, argv, opts)) {
        return 1;
    }

    sime::Interpreter interpreter;
    if (!interpreter.LoadResources(opts.trie, opts.model)) {
        std::cerr << "Load failed: " << opts.trie << ", " << opts.model << "\n";
        return 1;
    }

    if (!opts.userdict.empty()) {
        if (interpreter.LoadUserDict(opts.userdict)) {
            std::cout << "UserDict: " << opts.userdict << "\n";
        } else {
            std::cerr << "Warning: failed to load user dict: " << opts.userdict << "\n";
        }
    }

    std::cout << "Dict: " << opts.trie << "\n"
              << "LanguageModel: " << opts.model << "\n"
              << "输入拼音，使用 :quit 退出。\n";

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line == ":quit" || line == ":q") {
            break;
        }
        if (line.empty()) {
            continue;
        }
        if (opts.sentence) {
            auto results = interpreter.DecodeSentence(line, opts.num);
            if (results.empty()) {
                std::cout << "  (没有候选)\n";
                continue;
            }
            for (std::size_t idx = 0; idx < results.size(); ++idx) {
                const auto& r = results[idx];
                std::string utf8 = sime::ustr::FromU32(r.text);
                std::cout << "  [" << idx << "] " << utf8
                          << " (score " << std::fixed << std::setprecision(3) << r.score
                          << ", matched " << r.matched_len << "/" << line.size() << ")\n";
            }
        } else {
            auto results = interpreter.DecodeText(line, opts.num);
            if (results.empty()) {
                std::cout << "  (没有候选)\n";
                continue;
            }
            for (std::size_t idx = 0; idx < results.size(); ++idx) {
                const auto& result = results[idx];
                std::string utf8 = sime::ustr::FromU32(result.text);
                std::cout << "  [" << idx << "] " << utf8 << " (score "
                          << std::fixed << std::setprecision(3) << result.score << ")\n";
            }
        }
    }
    return 0;
}
