#include "compact.h"

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: sime-compact <raw.slm> <output.t3g>\n";
        return 1;
    }

    try {
        sime::RunCompact(argv[1], argv[2]);
    } catch (const std::exception& ex) {
        std::cerr << "sime-compact failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
