#pragma once

#include <filesystem>

namespace sime {

struct CompressOptions {
    std::filesystem::path input;
    std::filesystem::path output;
};

void CompressRun(const CompressOptions& options);

} // namespace sime
