// 统计分词语料中所有 token 的出现频次
// 输入: 空格分隔的分词文件 (每行一句)
// 输出: token\tfreq (按频次降序)
//
// 用法: count <input> <output>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "usage: count <input> <output>\n";
        return 1;
    }

    std::ifstream fin(argv[1]);
    if (!fin) {
        std::cerr << "cannot open: " << argv[1] << "\n";
        return 1;
    }

    std::unordered_map<std::string, uint64_t> counts;
    std::string line;
    size_t lines = 0;

    while (std::getline(fin, line)) {
        size_t i = 0;
        size_t n = line.size();
        while (i < n) {
            // skip spaces
            while (i < n && line[i] == ' ') ++i;
            if (i >= n) break;
            // find token end
            size_t j = i;
            while (j < n && line[j] != ' ') ++j;
            counts[line.substr(i, j - i)]++;
            i = j;
        }
        if (++lines % 500000 == 0) {
            std::cerr << "read " << lines << " lines, "
                      << counts.size() << " unique tokens\n";
        }
    }

    std::cerr << "read " << lines << " lines, "
              << counts.size() << " unique tokens\n";

    // sort by freq desc, then by token asc
    std::vector<std::pair<std::string, uint64_t>> sorted(counts.begin(),
                                                          counts.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
              });

    std::ofstream fout(argv[2]);
    if (!fout) {
        std::cerr << "cannot open: " << argv[2] << "\n";
        return 1;
    }

    for (const auto& [token, freq] : sorted) {
        fout << token << '\t' << freq << '\n';
    }

    std::cerr << "written " << sorted.size() << " tokens to " << argv[2] << "\n";
    return 0;
}
