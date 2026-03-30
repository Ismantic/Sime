#ifndef FCITX5_SIME_STATE_H
#define FCITX5_SIME_STATE_H

#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <string>
#include <vector>
#include <memory>
#include "context.h"

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
    // 选择历史记录（保持与旧接口兼容）
    struct Selection {
        std::string text;              // 选择的文字："选"
        std::size_t consumed_length;   // 消耗的拼音字符数：4
        std::string original_pinyin;   // 被消耗的原始拼音："xuan"
    };

    SimeState() : manager_(std::make_unique<sime::SelectionManager>()) {}

    // 重置状态
    void reset();

    // 拼音缓冲区操作
    void appendPinyin(char c);
    void deleteLast();
    bool isEmpty() const { return manager_->IsPinyinEmpty(); }

    // 访问器
    const std::string& pinyinBuffer() const { return manager_->GetPinyinBuffer(); }
    const std::vector<SimeCandidate>& candidates() const { return candidates_; }
    void setCandidates(std::vector<SimeCandidate> cands) {
        candidates_ = std::move(cands);
        // 同时更新 manager 中的候选词
        std::vector<sime::Candidate> managerCands;
        for (const auto& c : candidates_) {
            managerCands.emplace_back(c.text, c.score, c.matched_length);
        }
        manager_->SetCandidates(std::move(managerCands));
    }

    // 候选词选择
    int selectedIndex() const { return selectedIndex_; }
    void setSelectedIndex(int index) { selectedIndex_ = index; }

    // 缓存管理
    const std::string& cachedPinyin() const { return manager_->GetCachedPinyin(); }
    void setCachedPinyin(const std::string& pinyin) { manager_->SetCachedPinyin(pinyin); }

    // 设置拼音缓冲区（用于选词后保留剩余拼音）
    void setPinyinBuffer(const std::string& buffer) { manager_->SetPinyinBuffer(buffer); }

    // 清除缓存（强制重新解码）
    void clearCache() {
        manager_->ClearCache();
        candidates_.clear();
    }

    // 选择历史管理
    void pushSelection(const std::string& text, std::size_t consumed,
                      const std::string& original) {
        manager_->SelectCandidate(0);  // 使用 manager 的选词逻辑
    }

    bool popSelection() {
        return manager_->UndoLastSelection();
    }

    std::string getCommittedText() const {
        return manager_->GetCommittedText();
    }

    bool hasSelections() const { return manager_->HasSelections(); }
    void clearSelections() { manager_->Reset(); }

    const Selection* getLastSelection() const {
        const auto* sel = manager_->GetLastSelection();
        if (!sel) return nullptr;
        // 转换为旧的 Selection 结构
        lastSelection_.text = sel->text;
        lastSelection_.consumed_length = sel->consumed_length;
        lastSelection_.original_pinyin = sel->original_pinyin;
        return &lastSelection_;
    }

    // 页码管理
    int getCurrentPage() const { return currentPage_; }
    void setCurrentPage(int page) { currentPage_ = page; }
    void resetPage() { currentPage_ = 0; }

    // 获取底层的 SelectionManager（用于直接访问）
    sime::SelectionManager* getManager() { return manager_.get(); }

private:
    std::unique_ptr<sime::SelectionManager> manager_;  // 底层选词管理器
    std::vector<SimeCandidate> candidates_;  // 候选词列表（fcitx5 特定）
    int selectedIndex_ = 0;             // 当前选中的候选（0-based）
    int currentPage_ = 0;               // 当前页码（0-based）
    mutable Selection lastSelection_;   // 临时存储用于兼容旧接口
};

} // namespace fcitx

#endif // FCITX5_SIME_STATE_H
