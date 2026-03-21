#include "interpret.h"
#include "ustr.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path pydict;
    std::filesystem::path lm;
    std::size_t nbest = 5;
};

void PrintUsage() {
    std::cerr << "Usage: ime-interpreter --pydict <pydict_sc.ime.bin> --lm <lm_sc.t3g> [--nbest N]\n";
}

bool ParseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if ((arg == "--pydict" || arg == "-d") && i + 1 < argc) {
            opts.pydict = argv[++i];
        } else if ((arg == "--lm" || arg == "-l") && i + 1 < argc) {
            opts.lm = argv[++i];
        } else if (arg == "--nbest" && i + 1 < argc) {
            opts.nbest = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return false;
        }
    }
    if (opts.pydict.empty() || opts.lm.empty()) {
        PrintUsage();
        return false;
    }
    if (opts.nbest == 0) {
        opts.nbest = 1;
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
    if (!interpreter.LoadResources(opts.pydict, opts.lm)) {
        std::cerr << "Load failed: " << opts.pydict << ", " << opts.lm << "\n";
        return 1;
    }

    std::cout << "Dict: " << opts.pydict << "\n"
              << "LanguageModel: " << opts.lm << "\n"
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
        auto results = interpreter.DecodeText(line, opts.nbest);
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
    return 0;
}
