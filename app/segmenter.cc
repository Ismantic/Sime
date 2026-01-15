#include "dict.h"
#include "segment.h"

#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <vector>

namespace {

struct CmdOptions {
    std::filesystem::path dict_path;
    bool text_output = true;
    sime::TokenID stok = sime::SentenceToken;
    std::vector<std::filesystem::path> inputs;
};

void PrintUsage() {
    std::cerr << "sime-segment --dict <dict_file> [--bin] [files...]\n";
}

CmdOptions ParseArgs(int argc, char* argv[]) {
    CmdOptions opts;
    const option long_opts[] = {
        {"dict", required_argument, nullptr, 'd'},
        {"bin", no_argument, nullptr, 'b'},
        {nullptr, 0, nullptr, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "d:b", long_opts, nullptr)) != -1) {
        switch (c) {
            case 'd':
                opts.dict_path = optarg;
                break;
            case 'b':
                opts.text_output = false;
                break;
            default:
                PrintUsage();
                std::exit(1);
        }
    }

    for (int idx = optind; idx < argc; ++idx) {
        opts.inputs.emplace_back(argv[idx]);
    }

    if (opts.dict_path.empty()) {
        PrintUsage();
        std::exit(1);
    }
    return opts;
}

void ProcessFile(const sime::Segmenter& segmenter, 
                 const CmdOptions& cmd, 
                 const std::filesystem::path& file) {
    if (file.empty()) {
        std::cerr << "Processing stdin..." << std::flush;
        segmenter.SegmentStream(std::cin, std::cout,
                                 {.text_output = cmd.text_output,
                                  .sentence_token = cmd.stok});
        std::cerr << "done\n";
        return;
    }    
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        std::cerr << "Failed to open " << file << "\n";
        return;
    }
    std::cerr << "Processing " << file << "..." << std::flush;
    segmenter.SegmentStream(input, std::cout,
                             {.text_output = cmd.text_output,
                              .sentence_token = cmd.stok}); 
    std::cerr << "Done\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    auto cmd = ParseArgs(argc, argv);
    sime::Dict dict;
    std::cerr << "Loading Dict..." << std::flush;
    if (!dict.Load(cmd.dict_path)) {
        std::cerr << "Failed\n";
        return 1;
    }
    std::cerr << "Done\n";
    sime::Segmenter segmenter(std::move(dict));
    if (cmd.inputs.empty()) {
        ProcessFile(segmenter, cmd, {});
    } else {
        for (const auto& file : cmd.inputs) {
            ProcessFile(segmenter, cmd, file);
        }
    }
    return 0;
}
