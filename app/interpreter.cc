#include "interpret.h"
#include "nine.h"
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
    std::filesystem::path nine;
    std::size_t num = 5;
    bool sentence = false;
};

void PrintUsage() {
    std::cerr << "Usage: ime-interpreter --trie <trie.bin> --model <model.bin> [--userdict <user.dict>] [--nine <pinyin_model.bin>] [--num N] [--sentence]\n";
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
        } else if (arg == "--nine" && i + 1 < argc) {
            opts.nine = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return false;
        }
    }
    // Need either --nine or trie+model
    if (opts.nine.empty() && (opts.trie.empty() || opts.model.empty())) {
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
    sime::NineDecoder nine;
    bool has_hanzi = false;
    bool has_nine = false;

    if (!opts.trie.empty() && !opts.model.empty()) {
        if (!interpreter.LoadResources(opts.trie, opts.model)) {
            std::cerr << "Load failed: " << opts.trie << ", " << opts.model << "\n";
            return 1;
        }
        has_hanzi = true;
        std::cout << "Trie: " << opts.trie << "\n"
                  << "Model: " << opts.model << "\n";
    }

    if (!opts.userdict.empty()) {
        if (interpreter.LoadDict(opts.userdict)) {
            std::cout << "Dict: " << opts.userdict << "\n";
        } else {
            std::cerr << "Warning: failed to load dict: " << opts.userdict << "\n";
        }
    }

    if (!opts.nine.empty()) {
        if (!nine.Load(opts.nine)) {
            std::cerr << "Failed to load nine model: " << opts.nine << "\n";
            if (!has_hanzi) return 1;
        } else {
            has_nine = true;
            std::cout << "Nine: " << opts.nine << "\n";
            if (has_hanzi) interpreter.LoadNine(opts.nine);
        }
    }

    if (has_nine) {
        std::cout << "模式: 九宫格 (输入数字 2-9)\n";
    }
    std::cout << "输入" << (has_nine ? "数字" : "拼音") << "，使用 :quit 退出。\n";

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
        if (has_nine) {
            if (has_hanzi) {
                // Full pipeline: digits → pinyin + hanzi
                auto nine_result = interpreter.DecodeNine(line, {}, opts.num);
                // Show best pinyin and candidates
                std::cout << "  最佳: " << nine_result.best_pinyin << "\n";
                std::cout << "  拼音:\n";
                for (std::size_t idx = 0; idx < nine_result.pinyin.size(); ++idx) {
                    const auto& p = nine_result.pinyin[idx];
                    std::string pinyin;
                    for (const auto& u : p.units) {
                        if (!pinyin.empty()) pinyin += '\'';
                        const char* syl = sime::UnitData::Decode(u);
                        if (syl) pinyin += syl;
                    }
                    std::cout << "    [" << idx << "] " << pinyin
                              << " (score " << std::fixed << std::setprecision(3) << p.score
                              << ", cnt " << p.cnt << ")\n";
                }
                // Show hanzi candidates
                const auto& results = nine_result.hanzi;
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
                auto parses = nine.Decode(line, opts.num);
                if (parses.empty()) {
                    std::cout << "  (没有候选)\n";
                    continue;
                }
                for (std::size_t idx = 0; idx < parses.size(); ++idx) {
                    const auto& p = parses[idx];
                    std::string pinyin;
                    for (const auto& u : p.units) {
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
