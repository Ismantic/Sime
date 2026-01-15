#include "convert.h"

#include <exception>
#include <iostream>
#include <string_view>

namespace {

void Usage() {
    std::cerr << "Usage:\n"
                 "  sime-converter <primitive_slm> <threaded_slm>\n"
                 "  sime-converter --trie <dict.txt> <trie.bin>\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        Usage();
        return 1;
    }

    std::string_view mode = argv[1];
    if (mode == "--trie") {
        if (argc != 4) {
            Usage();
            return 1;
        }
        sime::TrieConverter converter;
        if (!converter.Load(argv[2])) {
            std::cerr << "sime-converter failed: load trie dict\n";
            return 1;
        }
        if (!converter.Write(argv[3])) {
            std::cerr << "sime-converter failed: write trie bin\n";
            return 1;
        }
        return 0;
    }

    if (argc != 3) {
        Usage();
        return 1;
    }

    sime::ConvertOptions opts;
    opts.input = argv[1];
    opts.output = argv[2];

    try {
        sime::ConvertRun(opts);
    } catch (const std::exception& ex) {
        std::cerr << "sime-converter failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
