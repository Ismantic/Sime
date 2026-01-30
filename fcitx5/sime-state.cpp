#include "sime-state.h"

namespace fcitx {

void SimeState::reset() {
    pinyinBuffer_.clear();
    candidates_.clear();
    selectedIndex_ = 0;
    cachedPinyin_.clear();
    selectionHistory_.clear();
    currentPage_ = 0;
}

void SimeState::appendPinyin(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        // 转换为小写
        pinyinBuffer_.push_back(static_cast<char>(std::tolower(c)));
    }
}

void SimeState::deleteLast() {
    if (!pinyinBuffer_.empty()) {
        pinyinBuffer_.pop_back();
    }
}

} // namespace fcitx
