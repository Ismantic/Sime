#include "interpret.h"
#include "t9.h"
#include "unit.h"
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
    std::filesystem::path t9model;
    std::size_t num = 5;
    bool sentence = false;
    bool t9 = false;
};

void PrintUsage() {
    std::cerr << "Usage: ime-interpreter --trie <trie.bin> --model <model.bin> [--userdict <user.dict>] [--t9model <pinyin_model.bin>] [--num N] [--sentence] [--t9]\n";
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
        } else if (arg == "--t9") {
            opts.t9 = true;
        } else if (arg == "--t9model" && i + 1 < argc) {
            opts.t9model = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return false;
        }
    }
    // --t9 alone only needs --t9model
    if (!opts.t9 && (opts.trie.empty() || opts.model.empty())) {
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
    bool has_hanzi = false;
    if (!opts.trie.empty() && !opts.model.empty()) {
        if (!interpreter.LoadResources(opts.trie, opts.model)) {
            std::cerr << "Load failed: " << opts.trie << ", " << opts.model << "\n";
            return 1;
        }
        has_hanzi = true;
    }

    if (!opts.userdict.empty()) {
        if (interpreter.LoadUserDict(opts.userdict)) {
            std::cout << "UserDict: " << opts.userdict << "\n";
        } else {
            std::cerr << "Warning: failed to load user dict: " << opts.userdict << "\n";
        }
    }

    if (!opts.t9model.empty()) {
        if (interpreter.LoadT9(opts.t9model)) {
            std::cout << "T9Model: " << opts.t9model << "\n";
        } else {
            std::cerr << "Warning: failed to load T9 model: " << opts.t9model << "\n";
            opts.t9 = false;
        }
    } else if (opts.t9) {
        std::cerr << "Error: --t9 requires --t9model\n";
        return 1;
    }

    std::cout << "Dict: " << opts.trie << "\n"
              << "LanguageModel: " << opts.model << "\n";
    if (opts.t9) {
        std::cout << "模式: T9 (输入数字 2-9)\n";
    }
    std::cout << "输入" << (opts.t9 ? "数字" : "拼音") << "，使用 :quit 退出。\n";

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
        if (opts.t9) {
            if (has_hanzi) {
                // Full pipeline: digits → pinyin → hanzi
                auto results = interpreter.DecodeT9(line, opts.num);
                if (results.empty()) {
                    std::cout << "  (没有候选)\n";
                    continue;
                }
                for (std::size_t idx = 0; idx < results.size(); ++idx) {
                    const auto& r = results[idx];
                    std::string utf8 = sime::ustr::FromU32(r.text);
                    std::cout << "  [" << idx << "] " << utf8
                              << " (score " << std::fixed << std::setprecision(3) << r.score << ")\n";
                }
            } else {
                // Pinyin only: digits → pinyin
                auto parses = interpreter.DecodeT9Pinyin(line, opts.num);
                if (parses.empty()) {
                    std::cout << "  (没有候选)\n";
                    continue;
                }
                for (std::size_t idx = 0; idx < parses.size(); ++idx) {
                    const auto& p = parses[idx];
                    std::string pinyin;
                    for (const auto& u : p.pinyin) {
                        if (!pinyin.empty()) pinyin += '\'';
                        const char* syl = sime::UnitData::Decode(u);
                        if (syl) pinyin += syl;
                    }
                    std::cout << "  [" << idx << "] " << pinyin
                              << " (score " << std::fixed << std::setprecision(3) << p.score << ")\n";
                }
            }
        } else if (opts.sentence) {
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
