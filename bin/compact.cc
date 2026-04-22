#include "compact.h"

#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    try {
        if (argc != 3) {
            std::cerr << "usage: sime-compact <input.raw.cnt> <output.ct>\n";
            return 1;
        }
        sime::CompactOptions opts;
        opts.input = argv[1];
        opts.output = argv[2];
        sime::RunCompact(opts);
    } catch (const std::exception& ex) {
        std::cerr << "sime-compact failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
