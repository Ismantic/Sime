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
    std::size_t num = 5;
    bool sentence = false;
    bool nine_mode = false;
    bool stream_mode = false;
};

void PrintUsage() {
    std::cerr << "Usage: ime-interpreter --dict <trie.bin> --cnt <model.bin> "
                 "[--user <user.dict>] [--nine [pinyin_model.bin]] "
                 "[--num N] [--sentence]\n";
}

bool ParseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if ((arg == "--dict" || arg == "-d") && i + 1 < argc) {
            opts.trie = argv[++i];
        } else if ((arg == "--cnt" || arg == "-c") && i + 1 < argc) {
            opts.model = argv[++i];
        } else if ((arg == "--user" || arg == "-u") && i + 1 < argc) {
            opts.userdict = argv[++i];
        } else if (arg == "--num" && i + 1 < argc) {
            opts.num = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--sentence" || arg == "-s") {
            opts.sentence = true;
        } else if (arg == "--nine") {
            opts.nine_mode = true;
        } else if (arg == "--stream") {
            opts.stream_mode = true;
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

    sime::Interpreter interpreter(opts.trie, opts.model);
    if (!interpreter.Ready()) {
        std::cerr << "Load failed: " << opts.trie << ", " << opts.model << "\n";
        return 1;
    }
    std::cout << "Dict: " << opts.trie << "\n"
              << "Model: " << opts.model << "\n";

    if (!opts.userdict.empty()) {
        if (interpreter.LoadDict(opts.userdict)) {
            std::cout << "User: " << opts.userdict << "\n";
        } else {
            std::cerr << "Warning: failed to load dict: " << opts.userdict << "\n";
        }
    }

    if (opts.stream_mode) {
        std::cout << "Mode: stream (digits 2-9, progressive)\n";
    } else if (opts.nine_mode) {
        std::cout << "Mode: nine (digits 2-9)\n";
    }
    bool digit_mode = opts.nine_mode || opts.stream_mode;
    std::cout << "Input " << (digit_mode ? "digits" : "pinyin")
              << ", :quit to exit.\n";

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

        if (opts.stream_mode) {
            auto results = interpreter.DecodeNumSentence(line, {}, opts.num);
            if (results.empty()) {
                std::cout << "  (no candidates)\n";
                continue;
            }
            for (std::size_t idx = 0; idx < results.size(); ++idx) {
                const auto& r = results[idx];
                std::string utf8 = sime::ustr::FromU32(r.text);
                std::cout << "  [" << idx << "] " << utf8
                          << " (score " << std::fixed << std::setprecision(3)
                          << r.score << ", matched " << r.matched_len
                          << "/" << line.size() << ")\n";
            }
        } else if (opts.nine_mode) {
            auto results = interpreter.DecodeNumStr(line, {}, opts.num);
            if (results.empty()) {
                std::cout << "  (no candidates)\n";
                continue;
            }
            for (std::size_t idx = 0; idx < results.size(); ++idx) {
                const auto& r = results[idx];
                std::string utf8 = sime::ustr::FromU32(r.text);
                std::cout << "  [" << idx << "] " << utf8
                          << " (score " << std::fixed << std::setprecision(3)
                          << r.score << ")\n";
            }
        } else if (opts.sentence) {
            auto results = interpreter.DecodeSentence(line, opts.num);
            if (results.empty()) {
                std::cout << "  (no candidates)\n";
                continue;
            }
            for (std::size_t idx = 0; idx < results.size(); ++idx) {
                const auto& r = results[idx];
                std::string utf8 = sime::ustr::FromU32(r.text);
                std::cout << "  [" << idx << "] " << utf8;
                if (!r.pinyin.empty()) std::cout << " [" << r.pinyin << "]";
                std::cout << " (score " << std::fixed << std::setprecision(3)
                          << r.score << ", matched " << r.matched_len
                          << "/" << line.size() << ")\n";
            }
        } else {
            auto results = interpreter.DecodeStr(line, opts.num);
            if (results.empty()) {
                std::cout << "  (no candidates)\n";
                continue;
            }
            for (std::size_t idx = 0; idx < results.size(); ++idx) {
                const auto& r = results[idx];
                std::string utf8 = sime::ustr::FromU32(r.text);
                std::cout << "  [" << idx << "] " << utf8;
                if (!r.pinyin.empty()) std::cout << " [" << r.pinyin << "]";
                std::cout << " (score " << std::fixed << std::setprecision(3)
                          << r.score << ")\n";
            }
        }
    }
    return 0;
}
