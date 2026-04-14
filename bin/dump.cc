// Dump dict token/piece tables and n-gram scores to text files.
// Usage: sime-dump --dict <dict.bin> [--cnt <model.bin>] --out <prefix>
// Produces: prefix.1 (unigram), prefix.2 (bigram), prefix.3 (trigram)

#include "piece.h"
#include "score.h"
#include "dict.h"
#include "unit.h"
#include "ustr.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string TokenToText(const sime::Dict& dict, sime::TokenID id) {
    if (id == sime::SentenceStart) return "<s>";
    if (id == sime::SentenceEnd) return "</s>";
    if (id == sime::UnknownToken) return "<unk>";
    if (id == sime::NotToken) return "<not>";
    if (id == sime::ScoreNotToken) return "<skip>";
    const char32_t* chars = dict.TokenAt(id);
    if (!chars || chars[0] == 0) return "<?" + std::to_string(id) + ">";
    std::u32string u32;
    for (std::size_t i = 0; chars[i] != 0; ++i) u32.push_back(chars[i]);
    return sime::ustr::FromU32(u32);
}

} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path dict_path, model_path, out_prefix;
    std::string groups_query;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dict") == 0 && i + 1 < argc) {
            dict_path = argv[++i];
        } else if (std::strcmp(argv[i], "--cnt") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_prefix = argv[++i];
        } else if (std::strcmp(argv[i], "--groups") == 0 && i + 1 < argc) {
            groups_query = argv[++i];
        }
    }

    if (dict_path.empty() || (out_prefix.empty() && groups_query.empty())) {
        std::cerr << "Usage: sime-dump --dict <dict.bin> [--cnt <model.bin>] --out <prefix>\n"
                  << "       sime-dump --dict <dict.bin> --groups <pieces>\n";
        return 1;
    }

    sime::Dict dict;
    if (!dict.Load(dict_path)) {
        std::cerr << "Failed to load dict: " << dict_path << "\n";
        return 1;
    }

    // --groups mode: query GetGroups and print results
    if (!groups_query.empty()) {
        auto groups = dict.GetGroups(groups_query, 50);
        std::cerr << "GetGroups(\"" << groups_query << "\"): "
                  << groups.size() << " groups\n";
        for (std::size_t i = 0; i < groups.size(); ++i) {
            std::string text;
            for (auto id : groups[i]) {
                text += TokenToText(dict, static_cast<sime::TokenID>(id));
            }
            std::cout << "  [" << i << "] " << text << " (ids:";
            for (auto id : groups[i]) std::cout << " " << id;
            std::cout << ")\n";
        }
        return 0;
    }

    // Dump token table (StartToken onwards, one per line, like sime.token.dict.txt)
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
    // Dump token+pieces mapping (like sime.dict.txt / en_dict.cut.txt)
    // Walk trie to recover token → piece path mappings.
    {
        std::string path = out_prefix.string() + ".dict";
        std::ofstream out(path);
        const auto& pt = dict.GetPieceTable();
        std::size_t n = 0;

        // DFS: collect all (token_id, piece_path) pairs
        struct Frame {
            const sime::Dict::Node* node;
            std::string pieces; // accumulated piece path like "ni'hao"
        };
        std::vector<Frame> stack;
        stack.push_back({dict.Root(), ""});
        while (!stack.empty()) {
            auto [node, pieces] = stack.back();
            stack.pop_back();
            if (!node) continue;
            // Emit tokens at this node
            std::uint32_t count = 0;
            const std::uint32_t* tokens = dict.GetToken(node, count);
            if (count > 0 && !pieces.empty()) {
                std::uint32_t gi = 0;
                while (gi < count) {
                    // Concatenate all tokens in group
                    std::string text;
                    do {
                        auto tid = static_cast<sime::TokenID>(
                            tokens[gi] & sime::GroupTokenMask);
                        text += TokenToText(dict, tid);
                        bool is_end = (tokens[gi] & sime::GroupEnd) != 0;
                        ++gi;
                        if (is_end) break;
                    } while (gi < count);
                    out << text << " " << pieces << "\n";
                    ++n;
                }
            }
            // Recurse into children
            const auto* moves = node->GetMove();
            for (std::uint16_t i = 0; i < node->move_count; ++i) {
                sime::Unit u(moves[i].unit.value);
                const char* piece_text = pt.Decode(u);
                std::string child_pieces = pieces;
                if (!child_pieces.empty()) child_pieces += "'";
                child_pieces += (piece_text ? piece_text : "");
                const auto* child = dict.DoMove(node, u);
                if (child) stack.push_back({child, child_pieces});
            }
        }
        std::cerr << "dict: " << n << " → " << path << "\n";
    }
    {
        const auto& pt = dict.GetPieceTable();
        std::string path = out_prefix.string() + ".pieces";
        std::ofstream out(path);
        for (std::size_t i = 0; i < pt.Size(); ++i) {
            sime::Unit u(static_cast<std::uint32_t>(i));
            const char* text = pt.Decode(u);
            out << i << "\t" << (text ? text : "")
                << "\t" << (pt.IsPinyin(u) ? "py" : "-") << "\n";
        }
        std::cerr << "pieces: " << pt.Size() << " → " << path << "\n";
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
