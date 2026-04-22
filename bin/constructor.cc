#include "construct.h"

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <sstream>

namespace {

std::vector<std::string> SplitList(const char* arg, char delim = ',') {
    std::vector<std::string> result;
    std::stringstream ss(arg);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

sime::ConstructOptions ParseArgs(int argc, char* argv[]) {
    sime::ConstructOptions opts;
    const option long_opts[] = {
        {"ngram", required_argument, nullptr, 'n'},
        {"out", required_argument, nullptr, 'o'},
        {"cut", required_argument, nullptr, 'c'},
        {"wordcount", required_argument, nullptr, 'w'},
        {"prune-reserve", required_argument, nullptr, 'r'},
        {"discount", required_argument, nullptr, 'd'},
        {nullptr, 0, nullptr, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "n:o:c:w:r:d:", long_opts, nullptr)) != -1) {
        switch (c) {
        case 'n':
            opts.num = std::stoi(optarg);
            break;
        case 'o':
            opts.output = optarg;
            break;
        case 'c': {
            auto parts = SplitList(optarg);
            for (const auto& p : parts) {
                opts.cutoffs.push_back(static_cast<std::uint32_t>(std::stoul(p)));
            }
            break;
        }
        case 'w':
            opts.token_count = static_cast<std::uint32_t>(std::stoul(optarg));
            break;
        case 'r': {
            auto parts = SplitList(optarg);
            for (const auto& p : parts) {
                opts.prune_reserves.push_back(static_cast<int>(std::stoul(p)));
            }
            break;
        }
        case 'd': {
            auto parts = SplitList(optarg);
            for (const auto& p : parts) {
                opts.discounts.push_back(std::stod(p));
            }
            break;
        }
        default:
            throw std::runtime_error("Invalid arguments");
        }
    }

    if (optind + 1 != argc) {
        throw std::runtime_error("missing idngram input file");
    }
    opts.input = argv[optind];

    if (opts.num < 1 || opts.num > 3 || opts.output.empty()) {
        throw std::runtime_error("invalid arguments: -n must be 1..3, -o required");
    }
    return opts;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        auto opts = ParseArgs(argc, argv);
        sime::RunConstruct(std::move(opts));
    } catch (const std::exception& ex) {
        std::cerr << "sime-construct failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
