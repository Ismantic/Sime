#include "convert.h"

#include <cstring>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: sime-converter <token_dict> <output.dict> [--cn dict.txt]... [--en dict.txt]...\n";
        return 1;
    }

    sime::DictConverter converter;
    if (!converter.LoadTokens(argv[1])) {
        std::cerr << "sime-converter failed: load tokens\n";
        return 1;
    }

    for (int i = 3; i < argc; ++i) {
        bool en = false;
        if (std::strcmp(argv[i], "--cn") == 0) {
            if (++i >= argc) { std::cerr << "--cn requires argument\n"; return 1; }
            en = false;
        } else if (std::strcmp(argv[i], "--en") == 0) {
            if (++i >= argc) { std::cerr << "--en requires argument\n"; return 1; }
            en = true;
        }
        if (!converter.Load(argv[i], en)) {
            std::cerr << "sime-converter failed: load dict " << argv[i] << "\n";
            return 1;
        }
    }

    if (!converter.Write(argv[2])) {
        std::cerr << "sime-converter failed: write dict\n";
        return 1;
    }
    return 0;
}
