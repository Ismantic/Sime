#include "compact.h"

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: sime-compact <raw.slm> <output.t3g>\n";
        return 1;
    }

    sime::CompactOptions opts;
    opts.input = argv[1];
    opts.output = argv[2];

    try {
        sime::CompactRun(opts);
    } catch (const std::exception& ex) {
        std::cerr << "sime-compact failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
