#include "convert.h"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: sime-converter <token_dict.txt> <trie.bin> <dict.txt>...\n";
        return 1;
    }

    sime::TrieConverter converter;
    if (!converter.LoadTokens(argv[1])) {
        std::cerr << "sime-converter failed: load tokens\n";
        return 1;
    }
    for (int i = 3; i < argc; ++i) {
        if (!converter.Load(argv[i])) {
            std::cerr << "sime-converter failed: load dict " << argv[i] << "\n";
            return 1;
        }
    }
    if (!converter.Write(argv[2])) {
        std::cerr << "sime-converter failed: write trie\n";
        return 1;
    }
    return 0;
}
