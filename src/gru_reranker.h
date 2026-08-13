#pragma once

#include "common.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace sime {

class GruReranker {
public:
    GruReranker();
    ~GruReranker();

    GruReranker(const GruReranker&) = delete;
    GruReranker& operator=(const GruReranker&) = delete;

    bool Load(const std::filesystem::path& directory);
    bool Ready() const;
    float_t Score(const std::vector<TokenID>& tokens, bool t9) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sime
