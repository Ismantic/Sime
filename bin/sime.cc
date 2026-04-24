#include "sime.h"
#include "ustr.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path dict;
    std::filesystem::path cnt;
    std::size_t n = 5;
    std::size_t extra = 0;  // extra Layer 1 sentences for DecodeNumSentence
    bool sentence = false;
    bool num = false;
    bool next = false;     // prediction mode (Chinese or mixed)
    bool next_en = false;  // prediction mode, filter to English only
    bool en = false;       // pure English prefix completion
    bool mix = false;      // mixed (English + pinyin) prefix completion
    bool cache = false;    // use cache-backed sentence decoders
};

void PrintUsage() {
    std::cerr << "Usage:\n"
              << "  sime --dict <dict> --cnt <model> [options]\n"
              << "\nOptions:\n"
              << "  --dict, -d <path>   Sime dict\n"
              << "  --cnt,  -c <path>   Sime LM model\n"
              << "  -n <N>              Max results to display (default 5)\n"
              << "  -e <N>              Extra Layer 1 sentences for --num -s\n"
              << "                      (top sentence always returned; default 0)\n"
              << "  --sentence, -s      Sentence mode (partial match)\n"
              << "  --num               Num-key mode (digits 2-9)\n"
              << "  --next              Prediction mode (input pinyin, get next-word suggestions)\n"
              << "  --next-en           Prediction mode, filter suggestions to English only\n"
              << "  --en                English-only prefix completion mode\n"
              << "  --mix               Mixed (English + pinyin) prefix completion mode\n"
              << "  --cache             Use cache-backed sentence decoders\n";
}

bool ParseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if ((arg == "--dict" || arg == "-d") && i + 1 < argc) {
            opts.dict = argv[++i];
        } else if ((arg == "--cnt" || arg == "-c") && i + 1 < argc) {
            opts.cnt = argv[++i];
        } else if (arg == "-n" && i + 1 < argc) {
            opts.n = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "-e" && i + 1 < argc) {
            opts.extra = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--sentence" || arg == "-s") {
            opts.sentence = true;
        } else if (arg == "--num") {
            opts.num = true;
        } else if (arg == "--next") {
            opts.next = true;
        } else if (arg == "--next-en") {
            opts.next_en = true;
        } else if (arg == "--en") {
            opts.en = true;
        } else if (arg == "--mix") {
            opts.mix = true;
        } else if (arg == "--cache") {
            opts.cache = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return false;
        }
    }
    if (opts.dict.empty() || opts.cnt.empty()) {
        PrintUsage();
        return false;
    }
    if (opts.next && opts.en) {
        std::cerr << "--next --en combo is no longer supported; "
                  << "use --next-en instead.\n";
        return false;
    }
    if (opts.next && opts.next_en) {
        std::cerr << "--next and --next-en are mutually exclusive.\n";
        return false;
    }
    if (opts.en && opts.mix) {
        std::cerr << "--en and --mix are mutually exclusive.\n";
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
// the engine as-is; incomplete trailing initials (e.g. "niq") are
// handled by InitNumNet's prefix tail expansion.
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

    // Sime mode
    sime::Sime engine(opts.dict, opts.cnt);
    if (!engine.Ready()) {
        std::cerr << "Load failed: " << opts.dict << ", " << opts.cnt << "\n";
        return 1;
    }
    std::cout << "Dict: " << opts.dict << "\n"
              << "Model: " << opts.cnt << "\n";

    if (opts.next || opts.next_en) {
        std::cout << "Mode: next (prediction"
                  << (opts.next_en ? ", English only" : "") << ")\n"
                  << "Input pinyin to decode, top result added to context.\n"
                  << ":reset to clear context, :quit to exit.\n";

        std::string line;
        std::vector<std::string> context_strs;
        std::vector<sime::TokenID> context_ids;
        while (true) {
            std::cout << "> " << std::flush;
            if (!std::getline(std::cin, line)) break;
            if (line == ":quit" || line == ":q") break;
            if (line == ":reset") {
                context_strs.clear();
                context_ids.clear();
                std::cout << "  (context cleared)\n";
                continue;
            }
            if (line.empty()) continue;

            // Decode the input
            auto decoded = engine.DecodeSentence(line, 0);
            if (decoded.empty()) {
                std::cout << "  (no decode result)\n";
                continue;
            }
            std::cout << "  decoded: " << decoded[0].text
                      << " [" << decoded[0].units << "]"
                      << " ids:";
            for (auto tid : decoded[0].tokens) std::cout << " " << tid;
            std::cout << "\n";

            // Add decoded tokens to context
            context_strs.push_back(decoded[0].text);
            for (auto tid : decoded[0].tokens) {
                context_ids.push_back(tid);
            }

            std::cout << "  context:";
            for (auto tid : context_ids) std::cout << " " << tid;
            std::cout << "\n";

            auto nextions = engine.NextTokens(context_ids, opts.n, opts.next_en);
            if (nextions.empty()) {
                std::cout << "  (no nextions)\n";
            } else {
                for (std::size_t idx = 0; idx < nextions.size(); ++idx) {
                    const auto& s = nextions[idx];
                    std::cout << "  [" << idx << "] " << s.text
                              << " (score " << std::fixed
                              << std::setprecision(3) << s.score
                              << ", ids:";
                    for (auto tid : s.tokens) std::cout << " " << tid;
                    std::cout << ")\n";
                }
            }
        }
        return 0;
    }

    if (opts.en || opts.mix) {
        std::cout << "Mode: "
                  << (opts.en ? "en (English-only" : "mix (English + pinyin")
                  << " prefix completion)\n"
                  << "Input prefix, :quit to exit.\n";

        std::string line;
        while (true) {
            std::cout << "> " << std::flush;
            if (!std::getline(std::cin, line)) break;
            if (line == ":quit" || line == ":q") break;
            if (line.empty()) continue;

            auto results = engine.GetTokens(line, opts.n, opts.en);
            if (results.empty()) {
                std::cout << "  (no candidates)\n";
                continue;
            }
            for (std::size_t idx = 0; idx < results.size(); ++idx) {
                const auto& r = results[idx];
                std::cout << "  [" << idx << "] " << r.text
                          << " (score " << std::fixed
                          << std::setprecision(3) << r.score
                          << ", id: " << r.tokens[0] << ")\n";
            }
        }
        return 0;
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
        // Strip tab-separated trailing columns (test case files use
        // <input>\t<pinyin>\t<expected> format).
        if (auto tab = line.find('\t'); tab != std::string::npos)
            line.resize(tab);
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
            results = opts.cache
                ? engine.DecodeNumSentenceCache(digits, prefix, opts.extra)
                : engine.DecodeNumSentence(digits, prefix, opts.extra);
            if (results.size() > opts.n) results.resize(opts.n);
        } else if (opts.num) {
            std::string prefix;
            std::string digits;
            if (!SplitPyDigits(line, prefix, digits)) {
                std::cout << "  (invalid input: expect pinyin prefix + digits 2-9)\n";
                continue;
            }
            results = engine.DecodeNumStr(digits, prefix, opts.n);
        } else if (opts.sentence) {
            results = opts.cache
                ? engine.DecodeSentenceCache(line, opts.extra)
                : engine.DecodeSentence(line, opts.extra);
            if (results.size() > opts.n) results.resize(opts.n);
        } else {
            results = engine.DecodeStr(line, opts.n);
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
            std::cout << ", ids:";
            for (auto tid : r.tokens) std::cout << " " << tid;
            std::cout << ")\n";
        }
    }
    return 0;
}
