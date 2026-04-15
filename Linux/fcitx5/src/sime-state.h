#ifndef FCITX5_SIME_STATE_H
#define FCITX5_SIME_STATE_H

#include <fcitx/inputcontextproperty.h>
#include "common.h"
#include <string>
#include <vector>

namespace fcitx {

class SimeState : public InputContextProperty {
public:
    // Raw pinyin input buffer (never truncated, only appended/erased)
    std::string buffer;
    std::size_t cursor = 0;

    // Selection tracking
    struct Selection {
        std::string text;       // selected hanzi
        std::string pinyin;     // corresponding pinyin
        std::vector<sime::TokenID> tokens;  // token IDs for LM context
        std::size_t consumed;   // bytes consumed from buffer
    };
    std::vector<Selection> selections;

    // Total bytes consumed by all selections
    std::size_t selectedLength() const {
        std::size_t n = 0;
        for (const auto& s : selections) n += s.consumed;
        return n;
    }

    // Committed text from selections
    std::string committedText() const {
        std::string result;
        for (const auto& s : selections) result += s.text;
        return result;
    }

    // Remaining unselected input
    std::string remaining() const {
        auto sel = selectedLength();
        if (sel >= buffer.size()) return {};
        return buffer.substr(sel);
    }

    // Cancel last selection (undo)
    bool cancel() {
        if (selections.empty()) return false;
        selections.pop_back();
        return true;
    }

    // Select a candidate
    void select(const std::string& text, const std::string& pinyin,
                const std::vector<sime::TokenID>& tokens,
                std::size_t consumed) {
        selections.push_back({text, pinyin, tokens, consumed});
    }

    // Check if fully selected (all input consumed)
    bool fullySelected() const {
        return selectedLength() >= buffer.size() && !buffer.empty();
    }

    void reset() {
        buffer.clear();
        cursor = 0;
        selections.clear();
    }

    bool empty() const { return buffer.empty(); }

    // Prediction context (recent committed words, persists across composing sessions)
    std::vector<std::string> context;
    std::vector<sime::TokenID> context_ids;
    bool predicting = false;

    void pushContext(const std::string& text,
                     const std::vector<sime::TokenID>& tokens,
                     int maxSize) {
        context.push_back(text);
        for (auto tid : tokens) context_ids.push_back(tid);
        while (static_cast<int>(context.size()) > maxSize) {
            context.erase(context.begin());
        }
    }

    void clearContext() {
        context.clear();
        context_ids.clear();
        predicting = false;
    }

    // Punctuation pairing state (persists across composing sessions)
    bool doubleQuoteOpen = false;
    bool singleQuoteOpen = false;

    // Punctuation undo
    bool lastIsPunc = false;
    std::string lastPuncStr;

    void resetPuncState() {
        doubleQuoteOpen = false;
        singleQuoteOpen = false;
        lastIsPunc = false;
        lastPuncStr.clear();
    }
};

} // namespace fcitx

#endif // FCITX5_SIME_STATE_H
