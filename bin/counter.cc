#include "count.h"

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <string>

namespace {

void PrintUsage() {
    std::cerr << "sime-count -n <max_order> -d <token_dict> -o <output_prefix> "
                 "-s <swap_prefix> [-c count_size] inputfiles...\n"
                 "Emits <output_prefix>.1gram .. .<N>gram (one file per order).\n";
}

sime::CountOptions ParseArgs(int argc, char* argv[]) {
    sime::CountOptions opts;
    opts.count_max = 1024 * 1024;
    int c;
    const option long_opts[] = {
        {"num", required_argument, nullptr, 'n'},
        {"output", required_argument, nullptr, 'o'},
        {"swap", required_argument, nullptr, 's'},
        {"count", required_argument, nullptr, 'c'},
        {"dict", required_argument, nullptr, 'd'},
        {nullptr, 0, nullptr, 0}
    };

    while ((c = getopt_long(argc, argv, "n:o:s:c:d:", long_opts, nullptr)) != -1) {
        switch (c) {
        case 'n':
            opts.num = std::stoi(optarg);
            break;
        case 'o':
            opts.output = optarg;
            break;
        case 's':
            opts.swap = optarg;
            break;
        case 'c':
            opts.count_max = static_cast<std::size_t>(std::stoul(optarg));
            break;
        case 'd':
            opts.dict = optarg;
            break;
        default:
            PrintUsage();
            std::exit(1);
        }
    }

    for (int idx = optind; idx < argc; ++idx) {
        opts.inputs.emplace_back(argv[idx]);
    }

    if (opts.num < 1 || opts.num > 3 ||
        opts.dict.empty() || opts.output.empty() || opts.swap.empty() ||
        opts.count_max < 1024 || opts.inputs.empty()) {
        PrintUsage();
        std::exit(1);
    }

    return opts;
}

} // namespace 

int main(int argc, char* argv[]) {
    try {
        auto opts = ParseArgs(argc, argv);
        sime::RunCount(opts);
    } catch (const std::exception& ex) {
        std::cerr << "sime_count failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}