#include "gru_reranker.h"

#ifdef SIME_ENABLE_NCNN
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <mat.h>
#include <net.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

#include <cstdint>
#include <fstream>
#include <string>

namespace sime {

namespace {

#ifdef SIME_ENABLE_NCNN
constexpr std::uint32_t kEmbeddingVersion = 1;
constexpr std::uint32_t kEmbeddingDimension = 32;

std::uint32_t ReadU32(std::istream& stream) {
    std::uint8_t bytes[4]{};
    stream.read(reinterpret_cast<char*>(bytes), 4);
    return static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8)
        | (static_cast<std::uint32_t>(bytes[2]) << 16)
        | (static_cast<std::uint32_t>(bytes[3]) << 24);
}
#endif

} // namespace

class GruReranker::Impl {
public:
#ifdef SIME_ENABLE_NCNN
    bool Load(const std::filesystem::path& directory) {
        std::ifstream input(directory / "gru.embedding.i8", std::ios::binary);
        char magic[4]{};
        input.read(magic, 4);
        const auto version = ReadU32(input);
        rows_ = ReadU32(input);
        dimension_ = ReadU32(input);
        if (!input || std::string(magic, 4) != "STI8"
            || version != kEmbeddingVersion || rows_ < 2
            || dimension_ != kEmbeddingDimension) {
            Clear();
            return false;
        }

        scales_.resize(rows_);
        values_.resize(static_cast<std::size_t>(rows_) * dimension_);
        input.read(reinterpret_cast<char*>(scales_.data()),
                   static_cast<std::streamsize>(scales_.size() * sizeof(std::uint16_t)));
        input.read(reinterpret_cast<char*>(values_.data()),
                   static_cast<std::streamsize>(values_.size()));
        if (!input) {
            Clear();
            return false;
        }

        pinyin_.opt.num_threads = 1;
        t9_.opt.num_threads = 1;
        if (pinyin_.load_param((directory / "gru.pinyin.ncnn.param").c_str()) != 0
            || pinyin_.load_model((directory / "gru.pinyin.ncnn.bin").c_str()) != 0
            || t9_.load_param((directory / "gru.t9.ncnn.param").c_str()) != 0
            || t9_.load_model((directory / "gru.t9.ncnn.bin").c_str()) != 0) {
            Clear();
            return false;
        }
        ready_ = true;
        return true;
    }

    float_t Score(const std::vector<TokenID>& tokens, bool t9) const {
        if (!ready_ || tokens.empty()) return 0.0;

        ncnn::Mat embedded(static_cast<int>(dimension_),
                           static_cast<int>(tokens.size()));
        for (std::size_t row = 0; row < tokens.size(); ++row) {
            const auto index = 1U + static_cast<std::uint32_t>(tokens[row])
                % (rows_ - 1U);
            const auto scale = ncnn::float16_to_float32(scales_[index]);
            const auto* source = values_.data()
                + static_cast<std::size_t>(index) * dimension_;
            auto* destination = embedded.row(static_cast<int>(row));
            for (std::uint32_t column = 0; column < dimension_; ++column) {
                destination[column] = static_cast<float>(source[column]) * scale;
            }
        }

        auto extractor = (t9 ? t9_ : pinyin_).create_extractor();
        if (extractor.input("in0", embedded) != 0) return 0.0;
        ncnn::Mat output;
        if (extractor.extract("out0", output) != 0 || output.empty()) return 0.0;
        return output[0];
    }

    void Clear() {
        ready_ = false;
        rows_ = 0;
        dimension_ = 0;
        scales_.clear();
        values_.clear();
        pinyin_.clear();
        t9_.clear();
    }

    ncnn::Net pinyin_;
    ncnn::Net t9_;
    std::vector<std::uint16_t> scales_;
    std::vector<std::int8_t> values_;
    std::uint32_t rows_ = 0;
    std::uint32_t dimension_ = 0;
    bool ready_ = false;
#else
    bool Load(const std::filesystem::path&) { return false; }
    float_t Score(const std::vector<TokenID>&, bool) const { return 0.0; }
#endif
};

GruReranker::GruReranker() : impl_(std::make_unique<Impl>()) {}
GruReranker::~GruReranker() = default;

bool GruReranker::Load(const std::filesystem::path& directory) {
    return impl_->Load(directory);
}

bool GruReranker::Ready() const {
#ifdef SIME_ENABLE_NCNN
    return impl_->ready_;
#else
    return false;
#endif
}

float_t GruReranker::Score(const std::vector<TokenID>& tokens, bool t9) const {
    return impl_->Score(tokens, t9);
}

} // namespace sime
