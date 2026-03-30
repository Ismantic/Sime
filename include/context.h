#ifndef SIME_CONTEXT_H
#define SIME_CONTEXT_H

#include <string>
#include <vector>
#include <cstddef>

namespace sime {

// 候选词结构（平台无关）
struct Candidate {
    std::string text;           // 候选文字（UTF-8）
    double score;               // 得分
    std::size_t matched_length; // 匹配的拼音长度

    Candidate(std::string t, double s, std::size_t m)
        : text(std::move(t)), score(s), matched_length(m) {}
};

// 选择历史记录
struct Selection {
    std::string text;            // 选择的文字
    std::size_t consumed_length; // 消耗的拼音字符数
    std::string original_pinyin; // 被消耗的原始拼音

    Selection(std::string t, std::size_t c, std::string p)
        : text(std::move(t)), consumed_length(c), original_pinyin(std::move(p)) {}
};

// 选词结果
struct SelectionResult {
    bool should_commit;          // 是否应该提交
    std::string text_to_commit;  // 需要提交的文字
    std::string remaining_pinyin;// 剩余拼音
};

// 选词管理器（平台无关的核心逻辑）
class SelectionManager {
public:
    SelectionManager() = default;

    // 重置所有状态
    void Reset();

    // 拼音缓冲区操作
    void AppendPinyin(char c);
    void DeleteLastPinyin();
    void SetPinyinBuffer(const std::string& pinyin);
    const std::string& GetPinyinBuffer() const { return pinyin_buffer_; }
    bool IsPinyinEmpty() const { return pinyin_buffer_.empty(); }

    // 候选词管理
    void SetCandidates(std::vector<Candidate> candidates);
    const std::vector<Candidate>& GetCandidates() const { return candidates_; }
    void ClearCandidates() { candidates_.clear(); }

    // 选词操作（核心逻辑）
    // 返回选词结果，包含是否提交、提交文字、剩余拼音
    SelectionResult SelectCandidate(std::size_t index);

    // 回退操作（撤销上一次选择）
    // 返回 true 表示成功回退，false 表示没有可回退的选择
    bool UndoLastSelection();

    // 选择历史查询
    bool HasSelections() const { return !selection_history_.empty(); }
    std::string GetCommittedText() const;
    const Selection* GetLastSelection() const {
        return selection_history_.empty() ? nullptr : &selection_history_.back();
    }

    // 清除缓存
    void ClearCache() {
        cached_pinyin_.clear();
        candidates_.clear();
    }

    // 缓存管理
    const std::string& GetCachedPinyin() const { return cached_pinyin_; }
    void SetCachedPinyin(const std::string& pinyin) { cached_pinyin_ = pinyin; }

private:
    std::string pinyin_buffer_;              // 当前拼音输入
    std::vector<Candidate> candidates_;      // 候选词列表
    std::vector<Selection> selection_history_; // 选择历史栈
    std::string cached_pinyin_;              // 缓存的拼音（用于避免重复解码）
};

} // namespace sime

#endif // SIME_SELECTION_MANAGER_H
