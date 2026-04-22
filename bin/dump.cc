// Dump dict token tables and n-gram scores to text files.
// Usage: sime-dump --dict <dict.bin> [--cnt <model.bin>] --out <prefix>

#include "score.h"
#include "dict.h"
#include "ustr.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string TokenToText(const sime::Dict& dict, sime::TokenID id) {
    if (id == sime::NotToken) return "<not>";
    const char32_t* chars = dict.TokenAt(id);
    if (!chars || chars[0] == 0) return "<?" + std::to_string(id) + ">";
    std::u32string u32;
    for (std::size_t i = 0; chars[i] != 0; ++i) u32.push_back(chars[i]);
    return sime::ustr::FromU32(u32);
}

const char* DatName(int t) {
    switch (t) {
    case 0: return "letter_pinyin";
    case 1: return "letter_en";
    default: return "unknown";
    }
}

} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path dict_path, model_path, out_prefix;
    std::string query;
    int query_type = -1;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dict") == 0 && i + 1 < argc) {
            dict_path = argv[++i];
        } else if (std::strcmp(argv[i], "--cnt") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_prefix = argv[++i];
        } else if (std::strcmp(argv[i], "--query") == 0 && i + 2 < argc) {
            query_type = std::atoi(argv[++i]);
            query = argv[++i];
        }
    }

    if (dict_path.empty() || (out_prefix.empty() && query.empty())) {
        std::cerr << "Usage: sime-dump --dict <dict.bin> [--cnt <model.bin>] --out <prefix>\n"
                  << "       sime-dump --dict <dict.bin> --query <type:0-1> <key>\n";
        return 1;
    }

    sime::Dict dict;
    if (!dict.Load(dict_path)) {
        std::cerr << "Failed to load dict: " << dict_path << "\n";
        return 1;
    }

    // --query mode
    if (!query.empty() && query_type >= 0 && query_type < sime::Dict::DatCount) {
        auto type = static_cast<sime::Dict::DatType>(query_type);
        auto results = dict.Dat(type).FindWordsWithPrefix(query, 50);
        std::cerr << "FindWordsWithPrefix(\"" << query << "\", "
                  << DatName(query_type) << "): "
                  << results.size() << " results\n";
        for (std::size_t i = 0; i < results.size(); ++i) {
            auto entry = dict.GetEntry(type, results[i].value);
            for (uint32_t j = 0; j < entry.count; ++j) {
                std::cout << "  [" << i << "] "
                          << TokenToText(dict, entry.items[j].id)
                          << " pieces=" << entry.items[j].pieces
                          << " (id: " << entry.items[j].id << ")\n";
            }
        }
        return 0;
    }

    // Dump token table
    {
        std::string path = out_prefix.string() + ".token";
        std::ofstream out(path);
        std::uint32_t count = dict.TokenCount();
        std::uint32_t n = 0;
        for (std::uint32_t i = sime::StartToken; i < count; ++i) {
            const char32_t* chars = dict.TokenAt(i);
            if (!chars || chars[0] == 0) continue;
            std::u32string u32;
            for (std::size_t j = 0; chars[j] != 0; ++j) u32.push_back(chars[j]);
            out << sime::ustr::FromU32(u32) << "\n";
            ++n;
        }
        std::cerr << "token: " << n << " → " << path << "\n";
    }

    // Dump DAT entries
    for (int t = 0; t < sime::Dict::DatCount; ++t) {
        auto type = static_cast<sime::Dict::DatType>(t);
        std::string path = out_prefix.string() + "." + DatName(t);
        std::ofstream out(path);
        std::size_t n = 0;

        // Iterate through all entries by walking the DAT
        // (we can't easily enumerate all keys, so just report stats)
        // Actually, we can iterate the side table
        for (uint32_t idx = 0; ; ++idx) {
            auto entry = dict.GetEntry(type, idx);
            if (entry.count == 0 && entry.items == nullptr) break;
            for (uint32_t j = 0; j < entry.count; ++j) {
                out << TokenToText(dict, entry.items[j].id) << " "
                    << entry.items[j].pieces << "\n";
                ++n;
            }
        }
        std::cerr << DatName(t) << ": " << n << " → " << path << "\n";
    }

    // Dump model if provided
    if (!model_path.empty()) {
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
                    line += TokenToText(dict, ng.tokens[i]);
                }
                out << line << "\t" << ng.pro << "\n";
            }
            std::cerr << "level " << level << ": " << ngrams.size()
                      << " entries → " << path << "\n";
        }
    }

    return 0;
}
