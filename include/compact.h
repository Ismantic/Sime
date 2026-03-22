#pragma once

#include <filesystem>

namespace sime {

void RunCompact(const std::filesystem::path& input,
                const std::filesystem::path& output);

} // namespace sime
