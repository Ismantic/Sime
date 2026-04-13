#include "convert.h"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: sime-converter <pinyin_dict.txt> <trie.bin> [en_dict.cut.txt]\n";
        return 1;
    }

    sime::TrieConverter converter;
    if (!converter.Load(argv[1])) {
        std::cerr << "sime-converter failed: load dict\n";
        return 1;
    }
    if (argc == 4) {
        if (!converter.LoadTokens(argv[3])) {
            std::cerr << "sime-converter failed: load tokens\n";
            return 1;
        }
    }
    if (!converter.Write(argv[2])) {
        std::cerr << "sime-converter failed: write trie\n";
        return 1;
    }
    return 0;
}
