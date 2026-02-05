#include "sime-state.h"

namespace fcitx {

void SimeState::reset() {
    manager_->Reset();
    candidates_.clear();
    selectedIndex_ = 0;
    currentPage_ = 0;
}

void SimeState::appendPinyin(char c) {
    manager_->AppendPinyin(c);
}

void SimeState::deleteLast() {
    manager_->DeleteLastPinyin();
}

} // namespace fcitx
