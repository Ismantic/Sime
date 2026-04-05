// Dump n-gram scores from sime.cnt to text files.
// Usage: sime-dump --dict <trie.bin> --cnt <model.bin> --out <prefix>
// Produces: prefix.1 (unigram), prefix.2 (bigram), prefix.3 (trigram)

#include "score.h"
#include "trie.h"
#include "ustr.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string TokenToText(const sime::Trie& trie, sime::TokenID id) {
    if (id == sime::SentenceStart) return "<s>";
    if (id == sime::SentenceEnd) return "</s>";
    if (id == sime::UnknownToken) return "<unk>";
    if (id == sime::NotToken) return "<not>";
    if (id == sime::ScoreNotToken) return "<skip>";
    const char32_t* chars = trie.TokenAt(id);
    if (!chars || chars[0] == 0) return "<?" + std::to_string(id) + ">";
    std::u32string u32;
    for (std::size_t i = 0; chars[i] != 0; ++i) u32.push_back(chars[i]);
    return sime::ustr::FromU32(u32);
}

} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path dict_path, model_path, out_prefix;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dict") == 0 && i + 1 < argc) {
            dict_path = argv[++i];
        } else if (std::strcmp(argv[i], "--cnt") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_prefix = argv[++i];
        }
    }

    if (dict_path.empty() || model_path.empty() || out_prefix.empty()) {
        std::cerr << "Usage: sime-dump --dict <trie.bin> --cnt <model.bin> --out <prefix>\n";
        return 1;
    }

    sime::Trie trie;
    if (!trie.Load(dict_path)) {
        std::cerr << "Failed to load trie: " << dict_path << "\n";
        return 1;
    }

    sime::Scorer scorer;
    if (!scorer.Load(model_path)) {
        std::cerr << "Failed to load model: " << model_path << "\n";
        return 1;
    }

    int num = scorer.Num();
    std::cerr << "Model order: " << num << "\n";

    for (int level = 1; level <= num; ++level) {
        auto ngrams = scorer.DumpLevel(level);
        std::string path = out_prefix.string() + "." + std::to_string(level);
        std::ofstream out(path);
        for (const auto& ng : ngrams) {
            std::string line;
            for (std::size_t i = 0; i < ng.tokens.size(); ++i) {
                if (i > 0) line += ' ';
                line += TokenToText(trie, ng.tokens[i]);
            }
            out << line << "\t" << ng.pro << "\n";
        }
        std::cerr << "level " << level << ": " << ngrams.size()
                  << " entries → " << path << "\n";
    }

    return 0;
}
