#ifndef FCITX5_SIME_STATE_H
#define FCITX5_SIME_STATE_H

#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <string>
#include <vector>

namespace fcitx {

// 匹配类型枚举
enum class MatchCategory {
    COMPLETE_MATCH = 0,  // 完整匹配整个拼音
    PREFIX_MATCH = 1     // 前缀匹配
};

// 候选词结构
struct SimeCandidate {
    std::string text;           // 显示文本（UTF-8）
    double score;               // 得分
    int index;                  // 索引（用于选择）
    std::size_t matched_length; // 匹配的拼音长度（用于选词后计算剩余）
    MatchCategory category;     // 匹配类型

    SimeCandidate(std::string t, double s, int i, std::size_t m = 0,
                  MatchCategory c = MatchCategory::COMPLETE_MATCH)
        : text(std::move(t)), score(s), index(i), matched_length(m), category(c) {}
};

// Sime 输入法状态（每个 InputContext 一个实例）
class SimeState : public InputContextProperty {
public:
    // 选择历史记录
    struct Selection {
        std::string text;              // 选择的文字："选"
        std::size_t consumed_length;   // 消耗的拼音字符数：4
        std::string original_pinyin;   // 被消耗的原始拼音："xuan"
    };

    SimeState() = default;

    // 重置状态
    void reset();

    // 拼音缓冲区操作
    void appendPinyin(char c);
    void deleteLast();
    bool isEmpty() const { return pinyinBuffer_.empty(); }

    // 访问器
    const std::string& pinyinBuffer() const { return pinyinBuffer_; }
    const std::vector<SimeCandidate>& candidates() const { return candidates_; }
    void setCandidates(std::vector<SimeCandidate> cands) {
        candidates_ = std::move(cands);
    }

    // 候选词选择
    int selectedIndex() const { return selectedIndex_; }
    void setSelectedIndex(int index) { selectedIndex_ = index; }

    // 缓存管理
    const std::string& cachedPinyin() const { return cachedPinyin_; }
    void setCachedPinyin(const std::string& pinyin) { cachedPinyin_ = pinyin; }

    // 设置拼音缓冲区（用于选词后保留剩余拼音）
    void setPinyinBuffer(const std::string& buffer) { pinyinBuffer_ = buffer; }

    // 清除缓存（强制重新解码）
    void clearCache() {
        cachedPinyin_.clear();
        candidates_.clear();
    }

    // 选择历史管理
    void pushSelection(const std::string& text, std::size_t consumed,
                      const std::string& original) {
        selectionHistory_.push_back({text, consumed, original});
    }

    bool popSelection() {
        if (selectionHistory_.empty()) {
            return false;
        }
        selectionHistory_.pop_back();
        return true;
    }

    std::string getCommittedText() const {
        std::string result;
        for (const auto& sel : selectionHistory_) {
            result += sel.text;
        }
        return result;
    }

    bool hasSelections() const { return !selectionHistory_.empty(); }
    void clearSelections() { selectionHistory_.clear(); }

    const Selection* getLastSelection() const {
        return selectionHistory_.empty() ? nullptr : &selectionHistory_.back();
    }

    // 页码管理
    int getCurrentPage() const { return currentPage_; }
    void setCurrentPage(int page) { currentPage_ = page; }
    void resetPage() { currentPage_ = 0; }

private:
    std::string pinyinBuffer_;          // 当前拼音输入: "nihao"
    std::vector<SimeCandidate> candidates_;  // 候选词列表
    int selectedIndex_ = 0;             // 当前选中的候选（0-based）
    std::string cachedPinyin_;          // 上次解码的拼音（用于缓存）
    std::vector<Selection> selectionHistory_;  // 选择历史栈
    int currentPage_ = 0;               // 当前页码（0-based）
};

} // namespace fcitx

#endif // FCITX5_SIME_STATE_H
