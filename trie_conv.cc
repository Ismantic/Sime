#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::string endian = "le";
};

void PrintUsage() {
    std::cerr << "Usage: ime-convert-pydict --input <pydict_sc.bin> "
                 "--output <pydict_sc.ime.bin> [--endian le|be]\n";
}

bool ParseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            opts.input = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            opts.output = argv[++i];
        } else if (arg == "--endian" && i + 1 < argc) {
            opts.endian = argv[++i];
        } else if (arg == "--help") {
            PrintUsage();
            return false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    if (opts.input.empty() || opts.output.empty()) {
        PrintUsage();
        return false;
    }
    if (opts.endian != "le" && opts.endian != "be") {
        std::cerr << "Invalid --endian value\n";
        return false;
    }
    return true;
}

std::uint32_t ReadU32(const std::vector<char>& data,
                      std::size_t offset,
                      bool be) {
    std::uint32_t v = 0;
    if (!be) {
        std::memcpy(&v, data.data() + offset, sizeof(v));
        return v;
    }
    v = (static_cast<std::uint32_t>(
             static_cast<unsigned char>(data[offset]))
         << 24U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(data[offset + 1]))
         << 16U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(data[offset + 2]))
         << 8U) |
        static_cast<std::uint32_t>(
            static_cast<unsigned char>(data[offset + 3]));
    return v;
}

void WriteU16(std::vector<char>& out, std::size_t offset, std::uint16_t v) {
    std::memcpy(out.data() + offset, &v, sizeof(v));
}

void WriteU32(std::vector<char>& out, std::size_t offset, std::uint32_t v) {
    std::memcpy(out.data() + offset, &v, sizeof(v));
}

struct Transfer {
    std::uint32_t syllable = 0;
    std::uint32_t child = 0;
};

struct Entry {
    std::uint32_t id = 0;
    std::uint8_t cost = 0;
};

struct OldNode {
    std::uint32_t offset = 0;
    std::vector<Transfer> transfers;
    std::vector<Entry> entries;
};

bool Convert(const Options& opts) {
    std::ifstream in(opts.input, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open input: " << opts.input << "\n";
        return false;
    }
    in.seekg(0, std::ios::end);
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        std::cerr << "Input is empty\n";
        return false;
    }
    std::vector<char> data(static_cast<std::size_t>(size));
    if (!in.read(data.data(), size)) {
        std::cerr << "Failed to read input\n";
        return false;
    }

    const bool be = (opts.endian == "be");
    if (data.size() < 12) {
        std::cerr << "Input too small\n";
        return false;
    }
    std::uint32_t str_count = ReadU32(data, 0, be);
    std::uint32_t node_count = ReadU32(data, 4, be);
    std::uint32_t str_offset = ReadU32(data, 8, be);
    if (str_offset > data.size() || str_offset < 12) {
        std::cerr << "Invalid string table offset\n";
        return false;
    }

    std::vector<OldNode> nodes;
    nodes.reserve(node_count);
    std::size_t pos = 12;
    for (std::uint32_t i = 0; i < node_count; ++i) {
        if (pos + 4 > str_offset) {
            std::cerr << "Node section truncated\n";
            return false;
        }
        std::uint32_t header = ReadU32(data, pos, be);
        std::uint32_t word_count = header & 0xFFFU;
        std::uint32_t transfer_count = (header >> 12U) & 0xFFFU;
        std::uint32_t node_start = static_cast<std::uint32_t>(pos);
        pos += 4;

        OldNode node;
        node.offset = node_start;
        node.transfers.reserve(transfer_count);
        node.entries.reserve(word_count);

        for (std::uint32_t t = 0; t < transfer_count; ++t) {
            if (pos + 8 > str_offset) {
                std::cerr << "Transfer section truncated\n";
                return false;
            }
            std::uint32_t syllable = ReadU32(data, pos, be);
            std::uint32_t child = ReadU32(data, pos + 4, be);
            node.transfers.push_back(Transfer{syllable, child});
            pos += 8;
        }

        for (std::uint32_t w = 0; w < word_count; ++w) {
            if (pos + 4 > str_offset) {
                std::cerr << "Entry section truncated\n";
                return false;
            }
            std::uint32_t info = ReadU32(data, pos, be);
            Entry entry;
            entry.id = info & 0xFFFFFFU;
            entry.cost = static_cast<std::uint8_t>((info >> 26U) & 0x1FU);
            node.entries.push_back(entry);
            pos += 4;
        }
        nodes.push_back(std::move(node));
    }

    if (pos != str_offset) {
        std::cerr << "Node section size mismatch\n";
        return false;
    }

    std::unordered_map<std::uint32_t, std::uint32_t> new_offsets;
    new_offsets.reserve(nodes.size());
    std::uint32_t new_pos = 12;
    for (const auto& node : nodes) {
        new_offsets[node.offset] = new_pos;
        new_pos += 4 +
                   static_cast<std::uint32_t>(node.transfers.size() * 8) +
                   static_cast<std::uint32_t>(node.entries.size() * 8);
    }

    std::uint32_t new_str_offset = new_pos;
    std::size_t str_size = data.size() - str_offset;
    std::vector<char> out(static_cast<std::size_t>(new_str_offset) + str_size);

    WriteU32(out, 0, str_count);
    WriteU32(out, 4, node_count);
    WriteU32(out, 8, new_str_offset);

    std::size_t out_pos = 12;
    for (const auto& node : nodes) {
        if (node.entries.size() > 0xFFFF || node.transfers.size() > 0xFFFF) {
            std::cerr << "Node too large for new format\n";
            return false;
        }
        WriteU16(out, out_pos,
                 static_cast<std::uint16_t>(node.entries.size()));
        WriteU16(out, out_pos + 2,
                 static_cast<std::uint16_t>(node.transfers.size()));
        out_pos += 4;

        for (const auto& mv : node.transfers) {
            auto it = new_offsets.find(mv.child);
            if (it == new_offsets.end()) {
                std::cerr << "Missing child offset\n";
                return false;
            }
            WriteU32(out, out_pos, it->second);
            WriteU32(out, out_pos + 4, mv.syllable);
            out_pos += 8;
        }

        for (const auto& entry : node.entries) {
            WriteU32(out, out_pos, entry.id & 0xFFFFFFU);
            out_pos += 4;
            out[out_pos] = static_cast<char>(entry.cost & 0x1FU);
            out[out_pos + 1] = 0;
            out[out_pos + 2] = 0;
            out[out_pos + 3] = 0;
            out_pos += 4;
        }
    }

    std::memcpy(out.data() + new_str_offset,
                data.data() + str_offset,
                str_size);

    std::ofstream out_file(opts.output, std::ios::binary | std::ios::trunc);
    if (!out_file.is_open()) {
        std::cerr << "Failed to open output: " << opts.output << "\n";
        return false;
    }
    out_file.write(out.data(), static_cast<std::streamsize>(out.size()));
    return out_file.good();
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!ParseArgs(argc, argv, opts)) {
        return 1;
    }
    if (!Convert(opts)) {
        return 1;
    }
    std::cout << "Converted " << opts.input << " -> " << opts.output << "\n";
    return 0;
}
