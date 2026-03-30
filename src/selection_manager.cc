#include "selection_manager.h"
#include <algorithm>
#include <cctype>

namespace sime {

void SelectionManager::Reset() {
    pinyin_buffer_.clear();
    candidates_.clear();
    selection_history_.clear();
    cached_pinyin_.clear();
}

void SelectionManager::AppendPinyin(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        // 转换为小写
        pinyin_buffer_.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
}

void SelectionManager::DeleteLastPinyin() {
    if (!pinyin_buffer_.empty()) {
        pinyin_buffer_.pop_back();
    }
}

void SelectionManager::SetPinyinBuffer(const std::string& pinyin) {
    pinyin_buffer_ = pinyin;
}

void SelectionManager::SetCandidates(std::vector<Candidate> candidates) {
    candidates_ = std::move(candidates);
}

SelectionResult SelectionManager::SelectCandidate(std::size_t index) {
    SelectionResult result;
    result.should_commit = false;

    // 检查索引有效性
    if (index >= candidates_.size()) {
        return result;
    }

    const auto& selected = candidates_[index];

    // ===== 1. 记录原始消耗的拼音 =====
    std::string consumed_pinyin = pinyin_buffer_.substr(0, selected.matched_length);

    // ===== 2. 推入选择历史（而非立即提交！） =====
    selection_history_.emplace_back(selected.text, selected.matched_length, consumed_pinyin);

    // ===== 3. 计算剩余拼音 =====
    std::string remaining;
    if (selected.matched_length < pinyin_buffer_.size()) {
        remaining = pinyin_buffer_.substr(selected.matched_length);
    }

    // ===== 4. 更新状态并返回结果 =====
    if (!remaining.empty()) {
        // 还有剩余拼音，继续输入
        result.should_commit = false;
        result.remaining_pinyin = remaining;
        pinyin_buffer_ = remaining;
        // 需要清除缓存以便重新生成候选
        ClearCache();
    } else {
        // 拼音全部消耗完毕，提交所有已选文字并重置
        result.should_commit = true;
        result.text_to_commit = GetCommittedText();
        pinyin_buffer_.clear();
        selection_history_.clear();
        ClearCache();
    }

    return result;
}

bool SelectionManager::UndoLastSelection() {
    if (selection_history_.empty()) {
        return false;
    }

    // 获取最后一次选择
    const auto& last_selection = selection_history_.back();
    std::string restored_pinyin = last_selection.original_pinyin;

    // 弹出历史栈
    selection_history_.pop_back();

    // 恢复原拼音
    pinyin_buffer_ = restored_pinyin;
    ClearCache();

    return true;
}

std::string SelectionManager::GetCommittedText() const {
    std::string result;
    for (const auto& sel : selection_history_) {
        result += sel.text;
    }
    return result;
}

} // namespace sime
