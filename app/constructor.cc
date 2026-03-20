#include "construct.h"

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <optional>
#include <sstream>

namespace {

std::vector<std::string> SplitList(const char* arg) {
    std::vector<std::string> result;
    std::stringstream ss(arg);
    std::string item;
    while (std::getline(ss, item, ' ')) {
        if (!item.empty()) {
            if (!item.empty()) {
                result.push_back(item);
            }
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
        {"discount", required_argument, nullptr, 'd'},
        {"wordcount", required_argument, nullptr, 'w'},
        {"prune-cut", required_argument, nullptr, 'p'},
        {"prune-reserve", required_argument, nullptr, 'r'},
        {"log", no_argument, nullptr, 'l'},
        {nullptr, 0, nullptr, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "n:o:c:d:w:p:r:l", long_opts, nullptr)) != -1) {
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
        case 'd': {
            auto parts = SplitList(optarg);
            if (parts.empty()) {
                break;
            }
            const auto& method = parts[0];
            if (method == "ABS") {
                std::optional<double> cval;
                if (parts.size() > 1) {
                    cval = std::stod(parts[1]);
                }
                opts.discounters.push_back(
                    std::make_unique<sime::AbsoluteDiscounter>(cval));
            } else if (method == "LIN") {
                std::optional<double> dval;
                if (parts.size() > 1) {
                    dval = std::stod(parts[1]);
                }
                opts.discounters.push_back(
                    std::make_unique<sime::LinearDiscounter>(dval));
            } else {
                throw std::runtime_error("Unknown discount method: " + method);
            }
            break;
        }
        case 'w':
            opts.token_count = static_cast<std::uint32_t>(std::stoul(optarg));
            break;
        case 'p': {
            auto parts = SplitList(optarg);
            for (const auto& p : parts) {
                opts.prune_cutoffs.push_back(static_cast<int>(std::stoul(p)));
            }
            break;
        }
        case 'r': {
            auto parts = SplitList(optarg);
            for (const auto& p : parts) {
                opts.prune_reserves.push_back(static_cast<int>(std::stoul(p)));
            }
            break;
        }
        case 'l':
            opts.use_log_pr = true;
            break;
        default:
            throw std::runtime_error("Invalid arguments");
        }
    }

    if (optind + 1 != argc) {
        throw std::runtime_error("missing idngram input file");
    }
    opts.input = argv[optind];

    if (opts.num <= 0 || opts.output.empty() || opts.discounters.size() != static_cast<std::size_t>(opts.num)) {
        throw std::runtime_error("invalid arguments: check -n/-o/-d");
    }
    if (!opts.prune_cutoffs.empty() && !opts.prune_reserves.empty()) {
        throw std::runtime_error("prune-cut and prune-reserve cannot be used together");
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
