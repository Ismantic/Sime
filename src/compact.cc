#include "compact.h"

#include "common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace sime {

namespace {

constexpr std::uint32_t CompactMagic = 0x51434D53;  // "SMCQ"
constexpr int QuantPro = 65536;   // 16-bit for pro
constexpr int QuantBow = 256;     // 8-bit for bow

struct RawNode {
    TokenID id = 0;
    float pro = 0.0f;
    std::uint32_t down = 0;
    float bow = 0.0f;
    std::uint32_t bon = 0;
    std::uint32_t boe = 0;
};

struct RawLeaf {
    TokenID id = 0;
    float pro = 0.0f;
    std::uint32_t bon = 0;
    std::uint32_t boe = 0;
};

template <int N>
std::vector<float> BuildQuantTable(const std::vector<float>& values) {
    std::vector<float> table(static_cast<std::size_t>(N), 0.0f);
    if (values.empty()) return table;
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < N; ++i) {
        auto idx = static_cast<std::size_t>(
            static_cast<std::uint64_t>(i) * (sorted.size() - 1)
            / static_cast<std::uint64_t>(N - 1));
        table[static_cast<std::size_t>(i)] = sorted[idx];
    }
    return table;
}

template <typename T>
T Quantize(float value, const std::vector<float>& table) {
    auto it = std::lower_bound(table.begin(), table.end(), value);
    if (it == table.end()) return static_cast<T>(table.size() - 1);
    if (it == table.begin()) return 0;
    auto prev = it - 1;
    if (std::abs(value - *prev) <= std::abs(value - *it)) {
        return static_cast<T>(prev - table.begin());
    }
    return static_cast<T>(it - table.begin());
}

void PadTo4(std::ofstream& out) {
    auto pos = static_cast<std::uint64_t>(out.tellp());
    auto rem = pos % 4;
    if (rem != 0) {
        char zeros[4] = {};
        out.write(zeros, static_cast<std::streamsize>(4 - rem));
    }
}

void WriteTable(std::ofstream& out, const std::vector<float>& table) {
    out.write(reinterpret_cast<const char*>(table.data()),
              static_cast<std::streamsize>(table.size() * sizeof(float)));
}

} // namespace

void RunCompact(const CompactOptions& options) {
    std::ifstream in(options.input, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open input: " + options.input.string());
    }

    int num = 0;
    in.read(reinterpret_cast<char*>(&num), sizeof(num));
    if (num <= 0 || num > 3) {
        throw std::runtime_error("Invalid order: " + std::to_string(num));
    }

    std::vector<std::uint32_t> sizes(static_cast<std::size_t>(num + 1));
    in.read(reinterpret_cast<char*>(sizes.data()),
            static_cast<std::streamsize>(sizes.size() * sizeof(std::uint32_t)));

    std::cerr << "order=" << num << "\n";

    // Read all data.
    struct NodeLevel {
        std::vector<TokenID> tokens;
        std::vector<float> pro;
        std::vector<float> bow;
        std::vector<std::uint32_t> down;
    };
    std::vector<NodeLevel> levels(static_cast<std::size_t>(num));

    for (int lvl = 0; lvl < num; ++lvl) {
        auto count = sizes[static_cast<std::size_t>(lvl)];
        auto& nl = levels[static_cast<std::size_t>(lvl)];
        nl.tokens.resize(count);
        nl.pro.resize(count);
        nl.bow.resize(count);
        nl.down.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            RawNode raw;
            in.read(reinterpret_cast<char*>(&raw), sizeof(raw));
            nl.tokens[i] = raw.id;
            nl.pro[i] = raw.pro;
            nl.bow[i] = raw.bow;
            nl.down[i] = raw.down;
        }
        std::cerr << "  level " << lvl << ": " << count << " nodes\n";
    }

    auto leaf_count = sizes[static_cast<std::size_t>(num)];
    std::vector<TokenID> leaf_tokens(leaf_count);
    std::vector<float> leaf_pro_vals(leaf_count);
    for (std::uint32_t i = 0; i < leaf_count; ++i) {
        RawLeaf raw;
        in.read(reinterpret_cast<char*>(&raw), sizeof(raw));
        leaf_tokens[i] = raw.id;
        leaf_pro_vals[i] = raw.pro;
    }
    std::cerr << "  leaves: " << leaf_count << "\n";
    in.close();

    // Build quantization tables.
    std::vector<std::vector<float>> pro_tables(static_cast<std::size_t>(num));
    std::vector<std::vector<float>> bow_tables(static_cast<std::size_t>(num));
    for (int lvl = 0; lvl < num; ++lvl) {
        auto& nl = levels[static_cast<std::size_t>(lvl)];
        pro_tables[static_cast<std::size_t>(lvl)] = BuildQuantTable<QuantPro>(nl.pro);
        bow_tables[static_cast<std::size_t>(lvl)] = BuildQuantTable<QuantBow>(nl.bow);
    }
    auto leaf_pro_table = BuildQuantTable<QuantPro>(leaf_pro_vals);

    // Write output.
    std::ofstream out(options.output, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to open output: " + options.output.string());
    }

    // Header.
    std::uint32_t magic = CompactMagic;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&num), sizeof(num));
    out.write(reinterpret_cast<const char*>(sizes.data()),
              static_cast<std::streamsize>(sizes.size() * sizeof(std::uint32_t)));

    // Quantization tables: per-level (pro16 + bow8), then leaf pro16.
    for (int lvl = 0; lvl < num; ++lvl) {
        WriteTable(out, pro_tables[static_cast<std::size_t>(lvl)]);
        WriteTable(out, bow_tables[static_cast<std::size_t>(lvl)]);
    }
    WriteTable(out, leaf_pro_table);

    // SoA node data: tokens | pro_q16 (pad4) | bow_q8 (pad4) | down.
    for (int lvl = 0; lvl < num; ++lvl) {
        auto& nl = levels[static_cast<std::size_t>(lvl)];
        auto count = sizes[static_cast<std::size_t>(lvl)];
        auto& pt = pro_tables[static_cast<std::size_t>(lvl)];
        auto& bt = bow_tables[static_cast<std::size_t>(lvl)];

        std::vector<std::uint16_t> pro_q(count);
        std::vector<std::uint8_t> bow_q(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            pro_q[i] = Quantize<std::uint16_t>(nl.pro[i], pt);
            bow_q[i] = Quantize<std::uint8_t>(nl.bow[i], bt);
        }

        out.write(reinterpret_cast<const char*>(nl.tokens.data()),
                  static_cast<std::streamsize>(count * sizeof(TokenID)));
        out.write(reinterpret_cast<const char*>(pro_q.data()),
                  static_cast<std::streamsize>(count * sizeof(std::uint16_t)));
        PadTo4(out);
        out.write(reinterpret_cast<const char*>(bow_q.data()),
                  static_cast<std::streamsize>(count));
        PadTo4(out);
        out.write(reinterpret_cast<const char*>(nl.down.data()),
                  static_cast<std::streamsize>(count * sizeof(std::uint32_t)));
    }

    // SoA leaf data: tokens | pro_q16.
    {
        std::vector<std::uint16_t> pro_q(leaf_count);
        for (std::uint32_t i = 0; i < leaf_count; ++i) {
            pro_q[i] = Quantize<std::uint16_t>(leaf_pro_vals[i], leaf_pro_table);
        }
        out.write(reinterpret_cast<const char*>(leaf_tokens.data()),
                  static_cast<std::streamsize>(leaf_count * sizeof(TokenID)));
        out.write(reinterpret_cast<const char*>(pro_q.data()),
                  static_cast<std::streamsize>(leaf_count * sizeof(std::uint16_t)));
    }

    auto file_size = static_cast<std::size_t>(out.tellp());
    std::cerr << "output: " << (file_size / 1024) << " KB ("
              << (file_size / 1024 / 1024) << " MB)\n";
}

} // namespace sime
