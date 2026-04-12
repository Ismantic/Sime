// Compare Sime vs libime-pinyin candidates side by side.
// Usage: sime-compare --trie <trie.bin> --model <model.bin>

#include "interpret.h"
#include "ustr.h"

#include <libime/core/languagemodel.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/pinyinime.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

static const char *kLibimeLM = "/usr/lib/libime/zh_CN.lm";
static const char *kLibimeDict = "/usr/share/libime/sc.dict";

int main(int argc, char **argv) {
    // Parse Sime paths
    std::string trie_path, model_path;
    std::size_t num = 10;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--trie" || arg == "-t") && i + 1 < argc)
            trie_path = argv[++i];
        else if ((arg == "--model" || arg == "-m") && i + 1 < argc)
            model_path = argv[++i];
        else if (arg == "--num" && i + 1 < argc)
            num = std::stoul(argv[++i]);
    }
    if (trie_path.empty() || model_path.empty()) {
        std::cerr << "Usage: sime-compare --trie <trie.bin> --model <model.bin> [--num N]\n";
        return 1;
    }

    // Init Sime
    sime::Interpreter sime_interp(trie_path, model_path);
        std::cerr << "Sime: load failed\n";
        return 1;
    }

    // Init libime
    auto dict = std::make_unique<libime::PinyinDictionary>();
    dict->load(libime::PinyinDictionary::SystemDict, kLibimeDict,
               libime::PinyinDictFormat::Binary);
    auto lm = std::make_unique<libime::UserLanguageModel>(kLibimeLM);
    auto ime = std::make_unique<libime::PinyinIME>(std::move(dict), std::move(lm));
    ime->setNBest(3);
    ime->setBeamSize(20);
    ime->setFrameSize(15);

    std::cout << "Sime dict: " << trie_path << "\n"
              << "libime dict: " << kLibimeDict << "\n"
              << "输入拼音对比，:quit 退出\n";

    std::string line;
    while (true) {
        std::cout << "\n> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == ":quit" || line == ":q") break;
        if (line.empty()) continue;

        // Sime DecodeSentence
        auto sime_results = sime_interp.DecodeSentence(line, num);
        // Sime DecodeText (original, for reference)
        auto sime_full = sime_interp.DecodeText(line, num);

        // libime
        libime::PinyinContext ctx(ime.get());
        for (char c : line) {
            char s[2] = {c, 0};
            ctx.type(s, 1);
        }
        const auto &libime_cands = ctx.candidates();

        // Print side by side
        std::size_t max_rows = std::max({sime_results.size(), sime_full.size(),
                                         libime_cands.size()});
        max_rows = std::min(max_rows, num);

        std::cout << std::left
                  << std::setw(30) << "Sime-Sentence"
                  << std::setw(30) << "Sime-Full"
                  << "libime-pinyin\n"
                  << std::string(90, '-') << "\n";

        for (std::size_t i = 0; i < max_rows; ++i) {
            // Sime Sentence
            if (i < sime_results.size()) {
                auto &r = sime_results[i];
                std::string text = sime::ustr::FromU32(r.text);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s (%.1f, %zu/%zu)",
                              text.c_str(), r.score,
                              r.matched_len, line.size());
                std::cout << std::setw(30) << buf;
            } else {
                std::cout << std::setw(30) << "";
            }

            // Sime Full
            if (i < sime_full.size()) {
                auto &r = sime_full[i];
                std::string text = sime::ustr::FromU32(r.text);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s (%.1f)",
                              text.c_str(), r.score);
                std::cout << std::setw(30) << buf;
            } else {
                std::cout << std::setw(30) << "";
            }

            // libime
            if (i < libime_cands.size()) {
                std::string text = libime_cands[i].toString();
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s (%.1f)",
                              text.c_str(), libime_cands[i].score());
                std::cout << buf;
            }
            std::cout << "\n";
        }
    }
    return 0;
}
