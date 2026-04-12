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
    std::filesystem::path cnt;
    std::filesystem::path userdict;
    std::filesystem::path nine_model;
    std::size_t n = 5;
    std::size_t extra = 0;  // extra Layer 1 sentences for DecodeNumSentence
    bool sentence = false;
    bool num = false;
    bool nine = false;
};

void PrintUsage() {
    std::cerr << "Usage:\n"
              << "  sime --trie <trie> --cnt <model> [options]\n"
              << "  sime --nine <nine_model> [options]\n"
              << "\nOptions:\n"
              << "  --trie, -d <path>   Interpreter trie\n"
              << "  --cnt,  -c <path>   Interpreter LM model\n"
              << "  --user, -u <path>   User dictionary\n"
              << "  -n <N>              Max results to display (default 5)\n"
              << "  -e <N>              Extra Layer 1 sentences for --num -s\n"
              << "                      (top sentence always returned; default 0)\n"
              << "  --sentence, -s      Sentence mode (partial match)\n"
              << "  --num               Num-key mode (digits 2-9)\n"
              << "  --nine <path>       NineDecoder standalone (digits -> pinyin)\n";
}

bool ParseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if ((arg == "--trie" || arg == "-d") && i + 1 < argc) {
            opts.trie = argv[++i];
        } else if ((arg == "--cnt" || arg == "-c") && i + 1 < argc) {
            opts.cnt = argv[++i];
        } else if ((arg == "--user" || arg == "-u") && i + 1 < argc) {
            opts.userdict = argv[++i];
        } else if (arg == "-n" && i + 1 < argc) {
            opts.n = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "-e" && i + 1 < argc) {
            opts.extra = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--sentence" || arg == "-s") {
            opts.sentence = true;
        } else if (arg == "--num") {
            opts.num = true;
        } else if (arg == "--nine" && i + 1 < argc) {
            opts.nine = true;
            opts.nine_model = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return false;
        }
    }
    if (!opts.nine && (opts.trie.empty() || opts.cnt.empty())) {
        PrintUsage();
        return false;
    }
    if (opts.n == 0) {
        opts.n = 1;
    }
    return true;
}

// Split "ni426" → prefix = "ni", digits = "426".
// Pinyin (letters) must come first, digits 2-9 must come last. Returns false
// if the digit portion has invalid chars. The prefix is passed through to
// the interpreter as-is; incomplete trailing initials (e.g. "niq") are
// handled by ParseWithBoundaries inside DecodeNum*.
bool SplitPyDigits(std::string_view input,
                   std::string& prefix,
                   std::string& digits) {
    prefix.clear();
    digits.clear();
    std::size_t split = 0;
    while (split < input.size() &&
           !(input[split] >= '2' && input[split] <= '9')) {
        ++split;
    }
    digits.assign(input.substr(split));
    // Digits may contain '\'' as a hard syllable boundary hint; the
    // decoder honors it inside DecodeNumSentence / DecodeNumStr.
    for (char c : digits) {
        if (c == '\'') continue;
        if (c < '2' || c > '9') return false;
    }
    prefix.assign(input.substr(0, split));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!ParseArgs(argc, argv, opts)) {
        return 1;
    }

    // --nine: standalone NineDecoder mode
    if (opts.nine) {
        sime::NineDecoder nine_decoder;
        if (!nine_decoder.Load(opts.nine_model)) {
            std::cerr << "Nine model load failed: " << opts.nine_model << "\n";
            return 1;
        }
        std::cout << "Nine: " << opts.nine_model << "\n"
                  << "Mode: nine (NineDecoder, digits 2-9)\n"
                  << "Input digits, :quit to exit.\n";

        std::string line;
        while (true) {
            std::cout << "> " << std::flush;
            if (!std::getline(std::cin, line)) break;
            if (line == ":quit" || line == ":q") break;
            if (line.empty()) continue;

            auto results = nine_decoder.Decode(line, opts.n);
            if (results.empty()) {
                std::cout << "  (no candidates)\n";
                continue;
            }
            for (std::size_t idx = 0; idx < results.size(); ++idx) {
                const auto& r = results[idx];
                std::string pinyin;
                for (const auto& u : r.units) {
                    if (!pinyin.empty()) pinyin += '\'';
                    const char* text = sime::UnitData::Decode(u);
                    if (text) pinyin += text;
                }
                std::cout << "  [" << idx << "] " << pinyin
                          << " (score " << std::fixed << std::setprecision(3)
                          << r.score << ")\n";
            }
        }
        return 0;
    }

    // Interpreter mode
    sime::Interpreter interpreter(opts.trie, opts.cnt);
    if (!interpreter.Ready()) {
        std::cerr << "Load failed: " << opts.trie << ", " << opts.cnt << "\n";
        return 1;
    }
    std::cout << "Trie: " << opts.trie << "\n"
              << "Model: " << opts.cnt << "\n";

    if (!opts.userdict.empty()) {
        if (interpreter.LoadDict(opts.userdict)) {
            std::cout << "User: " << opts.userdict << "\n";
        } else {
            std::cerr << "Warning: failed to load dict: " << opts.userdict << "\n";
        }
    }

    if (opts.num && opts.sentence) {
        std::cout << "Mode: num+sentence (digits 2-9, progressive)\n";
    } else if (opts.num) {
        std::cout << "Mode: num (digits 2-9)\n";
    } else if (opts.sentence) {
        std::cout << "Mode: sentence\n";
    }
    std::cout << "Input " << (opts.num ? "digits" : "pinyin")
              << ", :quit to exit.\n";

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == ":quit" || line == ":q") break;
        if (line.empty()) continue;

        std::vector<sime::DecodeResult> results;
        if (opts.num && opts.sentence) {
            std::string prefix;
            std::string digits;
            if (!SplitPyDigits(line, prefix, digits)) {
                std::cout << "  (invalid input: expect pinyin prefix + digits 2-9)\n";
                continue;
            }
            // Pass `extra` directly (== extra Layer 1 sentences); -n
            // controls only how many results we display.
            results = interpreter.DecodeNumSentence(
                digits, prefix, opts.extra);
            if (results.size() > opts.n) results.resize(opts.n);
        } else if (opts.num) {
            std::string prefix;
            std::string digits;
            if (!SplitPyDigits(line, prefix, digits)) {
                std::cout << "  (invalid input: expect pinyin prefix + digits 2-9)\n";
                continue;
            }
            results = interpreter.DecodeNumStr(digits, prefix, opts.n);
        } else if (opts.sentence) {
            results = interpreter.DecodeSentence(
                line, opts.n > 0 ? opts.n - 1 : 0);
            if (results.size() > opts.n) results.resize(opts.n);
        } else {
            results = interpreter.DecodeStr(line, opts.n);
        }

        if (results.empty()) {
            std::cout << "  (no candidates)\n";
            continue;
        }
        for (std::size_t idx = 0; idx < results.size(); ++idx) {
            const auto& r = results[idx];
            std::cout << "  [" << idx << "] " << r.text;
            if (!r.units.empty()) std::cout << " [" << r.units << "]";
            std::cout << " (score " << std::fixed << std::setprecision(3)
                      << r.score;
            if (opts.sentence || (opts.num && opts.sentence))
                std::cout << ", matched " << r.cnt << "/" << line.size();
            std::cout << ")\n";
        }
    }
    return 0;
}
