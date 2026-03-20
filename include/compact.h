#pragma once

#include <filesystem>

namespace sime {

struct CompactOptions {
    std::filesystem::path input;
    std::filesystem::path output;
};

void CompactRun(const CompactOptions& options);

} // namespace sime
