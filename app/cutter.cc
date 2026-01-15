#include "cut.h"
#include "dict.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct CmdOptions {
    std::filesystem::path dict_path;
    std::filesystem::path model_path;
    bool text_output = false;
    bool show_id = false;
    sime::TokenID stok = sime::kSentenceToken;
    std::vector<std::filesystem::path> inputs;
};

void PrintUsage() {
    std::cerr << "ime-cutter -d <dict_file> -m <lm_sc.t3g> "
                 "[--format text|bin] [--show-id] [--stok N] [files...]\n";
}

CmdOptions ParseArgs(int argc, char* argv[]) {
    CmdOptions opts;
    const option long_opts[] = {
        {"dict", required_argument, nullptr, 'd'},
        {"format", required_argument, nullptr, 'f'},
        {"show-id", no_argument, nullptr, 'i'},
        {"stok", required_argument, nullptr, 's'},
        {"model", required_argument, nullptr, 'm'},
        {nullptr, 0, nullptr, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "d:f:is:m:", long_opts, nullptr)) != -1) {
        switch (c) {
            case 'd':
                opts.dict_path = optarg;
                break;
            case 'f':
                opts.text_output = std::string(optarg) == "text";
                break;
            case 'i':
                opts.show_id = true;
                break;
            case 's':
                opts.stok = static_cast<sime::TokenID>(std::stoul(optarg));
                break;
            case 'm':
                opts.model_path = optarg;
                break;
            default:
                PrintUsage();
                std::exit(1);
        }
    }

    for (int idx = optind; idx < argc; ++idx) {
        opts.inputs.emplace_back(argv[idx]);
    }

    if (opts.dict_path.empty() || opts.model_path.empty()) {
        PrintUsage();
        std::exit(1);
    }
    return opts;
}

void ProcessFile(const sime::Cutter& segmenter,
                 const CmdOptions& cmd,
                 const std::filesystem::path& file) {
    sime::CutOutputOptions options{
        .text_output = cmd.text_output,
        .show_id = cmd.show_id,
        .sentence_token = cmd.stok,
    };

    if (file.empty()) {
        std::cerr << "Processing stdin..." << std::flush;
        segmenter.SegmentStream(std::cin, std::cout, options);
        std::cerr << "done\n";
        return;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        std::cerr << "Failed to open " << file << "\n";
        return;
    }
    std::cerr << "Processing " << file << "..." << std::flush;
    segmenter.SegmentStream(input, std::cout, options);
    std::cerr << "done\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    auto cmd = ParseArgs(argc, argv);

    std::cerr << "Loading lexicon..." << std::flush;
    sime::Dict dict;
    if (!dict.Load(cmd.dict_path)) {
        std::cerr << "failed\n";
        return 1;
    }
    std::cerr << "done\n";

    std::cerr << "Loading LM..." << std::flush;
    sime::Scorer model;
    if (!model.Load(cmd.model_path)) {
        std::cerr << "failed\n";
        return 1;
    }
    std::cerr << "done\n";

    sime::Cutter segmenter(std::move(dict), std::move(model));
    if (cmd.inputs.empty()) {
        ProcessFile(segmenter, cmd, {});
    } else {
        for (const auto& file : cmd.inputs) {
            ProcessFile(segmenter, cmd, file);
        }
    }
    return 0;
}
