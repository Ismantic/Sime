#pragma once

#include <filesystem>

namespace sime {

struct CompactOptions {
    std::filesystem::path input;   // sime.raw.cnt
    std::filesystem::path output;  // sime.ct
};

void RunCompact(const CompactOptions& options);

} // namespace sime
