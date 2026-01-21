#ifndef FCITX5_SIME_STATE_H
#define FCITX5_SIME_STATE_H

#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <string>
#include <vector>

namespace fcitx {

// 候选词结构
struct SimeCandidate {
    std::string text;       // 显示文本（UTF-8）
    double score;           // 得分
    int index;              // 索引（用于选择）

    SimeCandidate(std::string t, double s, int i)
        : text(std::move(t)), score(s), index(i) {}
};

// Sime 输入法状态（每个 InputContext 一个实例）
class SimeState : public InputContextProperty {
public:
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

private:
    std::string pinyinBuffer_;          // 当前拼音输入: "nihao"
    std::vector<SimeCandidate> candidates_;  // 候选词列表
    int selectedIndex_ = 0;             // 当前选中的候选（0-based）
    std::string cachedPinyin_;          // 上次解码的拼音（用于缓存）
};

} // namespace fcitx

#endif // FCITX5_SIME_STATE_H
