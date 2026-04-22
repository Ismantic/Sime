// Sime Fcitx5 Engine

#include "sime-ime.h"
#include <cstdlib>
#include <fcitx-utils/key.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>


namespace fcitx {

// ===== Candidate word =====

class SimeCandidateWord : public CandidateWord {
public:
    SimeCandidateWord(Sime *engine, std::string text,
                      std::string pinyin,
                      std::vector<sime::TokenID> tokens,
                      std::size_t matchedLen)
        : CandidateWord(Text(text)), engine_(engine),
          text_(std::move(text)), pinyin_(std::move(pinyin)),
          tokens_(std::move(tokens)), matchedLen_(matchedLen) {}

    void select(InputContext *ic) const override {
        engine_->selectCandidate(ic, text_, pinyin_, tokens_, matchedLen_);
    }

private:
    Sime *engine_;
    std::string text_;
    std::string pinyin_;
    std::vector<sime::TokenID> tokens_;
    std::size_t matchedLen_;
};

// ===== Prediction candidate word =====

class SimeNextCandidateWord : public CandidateWord {
public:
    SimeNextCandidateWord(Sime *engine, std::string text,
                          std::vector<sime::TokenID> tokens)
        : CandidateWord(Text(text)), engine_(engine),
          text_(std::move(text)), tokens_(std::move(tokens)) {}

    void select(InputContext *ic) const override {
        auto *st = ic->propertyFor(engine_->stateFactory());
        int maxCtx = engine_->contextSize();
        st->pushContext(text_, tokens_, maxCtx);
        ic->commitString(text_);
        engine_->showPredictions(ic);
    }

private:
    Sime *engine_;
    std::string text_;
    std::vector<sime::TokenID> tokens_;
};

// ===== Engine =====

Sime::Sime(Instance *instance)
    : instance_(instance),
      factory_([](InputContext &) { return new SimeState(); }) {
    instance_->inputContextManager().registerProperty("simeState", &factory_);
    reloadConfig();
    initSime();
}

Sime::~Sime() {}

void Sime::reloadConfig() {
    readAsIni(config_, "conf/sime.conf");
}

void Sime::initSime() {
    sime_ = std::make_unique<sime::Sime>(
        *config_.dictPath, *config_.lmPath);
    if (!sime_->Ready()) {
        FCITX_ERROR() << "Sime: failed to load resources"
                      << " dict=" << *config_.dictPath
                      << " lm=" << *config_.lmPath;
        sime_.reset();
    } else {
        FCITX_INFO() << "Sime: resources loaded";
    }
}

SimeState *Sime::state(InputContext *ic) {
    return ic->propertyFor(&factory_);
}

void Sime::activate(const InputMethodEntry &, InputContextEvent &event) {
    if (!sime_) initSime();

    // Load optional addon modules
    fullwidth();
    chttrans();

    // Add status area actions (fullwidth, punctuation, chttrans toggles)
    auto *ic = event.inputContext();
    for (const auto *actionName : {"chttrans", "punctuation", "fullwidth"}) {
        if (auto *action =
                instance_->userInterfaceManager().lookupAction(actionName)) {
            ic->statusArea().addAction(StatusGroup::InputMethod, action);
        }
    }
}

void Sime::deactivate(const InputMethodEntry &entry, InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *st = state(ic);

    if (event.type() == EventType::InputContextSwitchInputMethod &&
        !st->empty()) {
        switch (*config_.switchInputMethodBehavior) {
        case SwitchInputMethodBehavior::CommitPreedit:
            ic->commitString(st->committedText() + st->remaining());
            break;
        case SwitchInputMethodBehavior::CommitDefault:
            ic->commitString(commitText(ic));
            break;
        case SwitchInputMethodBehavior::Clear:
            break;
        }
    }

    reset(entry, event);
    st->clearContext();
    st->resetPuncState();
}

void Sime::reset(const InputMethodEntry &, InputContextEvent &event) {
    resetState(event.inputContext());
}

void Sime::resetState(InputContext *ic) {
    state(ic)->reset();
    ic->inputPanel().reset();
    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

// Build the default commit string: selected text + first candidate for remaining
std::string Sime::commitText(InputContext *ic) const {
    auto *st = ic->propertyFor(const_cast<FactoryFor<SimeState>*>(&factory_));
    std::string result = st->committedText();
    if (sime_) {
        std::string rem = st->remaining();
        if (!rem.empty()) {
            auto results = sime_->DecodeSentence(rem, 0);
            if (!results.empty())
                result += results[0].text;
            else
                result += rem;
        }
    } else {
        result += st->remaining();
    }
    return result;
}

// Called when user selects a candidate
void Sime::selectCandidate(InputContext *ic, const std::string& text,
                                  const std::string& pinyin,
                                  const std::vector<sime::TokenID>& tokens,
                                  std::size_t matchedLen) {
    auto *st = state(ic);

    // Record selection
    st->select(text, pinyin, tokens, matchedLen);

    // If all input consumed, commit everything
    if (st->fullySelected()) {
        // Push each selection into context
        for (const auto& sel : st->selections) {
            st->pushContext(sel.text, sel.tokens, contextSize());
        }
        ic->commitString(st->committedText());
        st->reset();
        showPredictions(ic);
    } else {
        // Move cursor to end of remaining
        st->cursor = st->buffer.size();
        updateUI(ic);
    }
}

void Sime::showPredictions(InputContext *ic) {
    auto *st = state(ic);
    auto &panel = ic->inputPanel();
    panel.reset();

    if (!sime_ || st->context.empty() || !*config_.prediction) {
        st->predicting = false;
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    auto results = sime_->NextTokens(st->context_ids,
        static_cast<std::size_t>(*config_.pageSize));

    if (results.empty()) {
        st->predicting = false;
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    st->predicting = true;

    auto cl = std::make_unique<CommonCandidateList>();
    cl->setPageSize(*config_.pageSize);
    cl->setLayoutHint(CandidateLayoutHint::Horizontal);
    cl->setSelectionKey(*config_.selectionKeys);
    cl->setCursorPositionAfterPaging(CursorPositionAfterPaging::ResetToFirst);

    for (const auto& r : results) {
        cl->append<SimeNextCandidateWord>(this, r.text, r.tokens);
    }
    cl->setCursorIndex(0);
    panel.setCandidateList(std::move(cl));

    panel.setClientPreedit(Text{});
    panel.setPreedit(Text{});
    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void Sime::updateUI(InputContext *ic) {
    auto *st = state(ic);
    auto &panel = ic->inputPanel();
    panel.reset();

    if (st->empty() || !sime_) {
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    // Decode only the remaining (unselected) input
    std::string rem = st->remaining();
    std::vector<sime::DecodeResult> results;
    if (!rem.empty()) {
        results = sime_->DecodeSentence(
            rem, static_cast<std::size_t>(*config_.nbest));
    }

    // Build preedit based on mode
    std::string committed = st->committedText();

    // Build remaining_display from rem with spaces at syllable boundaries.
    // Use results[0].units to identify boundaries, then map back to rem.
    std::string remaining_display;
    if (!results.empty() && !results[0].units.empty()) {
        // units is e.g. "bu'zhi'dao", matching first results[0].cnt bytes of rem
        // Insert spaces at apostrophe positions in the original rem
        const auto &units = results[0].units;
        std::size_t ri = 0;  // index into rem
        std::size_t ui = 0;  // index into units
        while (ri < results[0].cnt && ui < units.size()) {
            if (units[ui] == '\'') {
                remaining_display += ' ';
                ++ui;
            } else {
                remaining_display += rem[ri];
                ++ri;
                ++ui;
            }
        }
        // Append unmatched tail (e.g. incomplete syllable 'd' in 'buzhid')
        if (ri < rem.size()) {
            remaining_display += ' ';
            remaining_display += rem.substr(ri);
        }
    } else {
        remaining_display = rem;
    }

    bool hasPreeditCap = ic->capabilityFlags().test(CapabilityFlag::Preedit);
    PreeditMode mode = hasPreeditCap ? *config_.preeditMode : PreeditMode::No;

    // Compute display cursor position (maps raw buffer cursor to display)
    std::size_t sel_len = st->selectedLength();
    int displayCursor;
    if (st->cursor < sel_len) {
        displayCursor = static_cast<int>(committed.size());
    } else if (st->cursor >= st->buffer.size()) {
        displayCursor = static_cast<int>(committed.size() + remaining_display.size());
    } else {
        // Map raw cursor offset to display offset, skipping inserted spaces
        std::size_t raw_offset = st->cursor - sel_len;
        std::size_t disp = 0, raw = 0;
        while (raw < raw_offset && disp < remaining_display.size()) {
            if (remaining_display[disp] == ' ' && (raw >= rem.size() || rem[raw] != ' ')) {
                ++disp;
            } else {
                ++raw;
                ++disp;
            }
        }
        // Skip any trailing spaces at the cursor position
        while (disp < remaining_display.size() && remaining_display[disp] == ' ' &&
               (raw >= rem.size() || rem[raw] != ' '))
            ++disp;
        displayCursor = static_cast<int>(committed.size() + disp);
    }

    // Build composing preedit: [committed](highlight) + [remaining pinyin](underline)
    auto buildComposingPreedit = [&]() {
        Text t;
        if (!committed.empty())
            t.append(committed, TextFormatFlags{TextFormatFlag::HighLight});
        if (!remaining_display.empty())
            t.append(remaining_display, TextFormatFlags{TextFormatFlag::Underline});
        t.setCursor(displayCursor);
        return t;
    };

    Text clientPreedit;
    switch (mode) {
    case PreeditMode::ComposingPinyin:
        clientPreedit = buildComposingPreedit();
        panel.setClientPreedit(clientPreedit);
        panel.setPreedit(Text{});
        break;
    case PreeditMode::CommitPreview: {
        std::string preview = committed;
        if (!results.empty()) preview += results[0].text;
        clientPreedit.append(preview, TextFormatFlags{TextFormatFlag::Underline});
        clientPreedit.setCursor(static_cast<int>(committed.size()));
        panel.setClientPreedit(clientPreedit);
        panel.setPreedit(buildComposingPreedit());
        break;
    }
    case PreeditMode::No:
        panel.setClientPreedit(Text{});
        panel.setPreedit(buildComposingPreedit());
        break;
    }

    // Build candidate list from remaining input
    auto cl = std::make_unique<CommonCandidateList>();
    cl->setPageSize(*config_.pageSize);
    cl->setLayoutHint(CandidateLayoutHint::Horizontal);
    cl->setSelectionKey(*config_.selectionKeys);
    cl->setCursorPositionAfterPaging(CursorPositionAfterPaging::ResetToFirst);

    for (const auto &r : results) {
        cl->append<SimeCandidateWord>(this, r.text, r.units, r.tokens, r.cnt);
    }

    if (!results.empty()) {
        cl->setCursorIndex(0);
    }
    panel.setCandidateList(std::move(cl));
    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void Sime::keyEvent(const InputMethodEntry &, KeyEvent &event) {
    if (event.isRelease()) return;

    auto *ic = event.inputContext();
    auto *st = state(ic);
    auto key = event.key();

    // Save and clear punctuation undo state
    bool lastIsPunc = st->lastIsPunc;
    std::string lastPuncStr = st->lastPuncStr;
    st->lastIsPunc = false;
    st->lastPuncStr.clear();

    auto &panel = ic->inputPanel();
    auto *cl = panel.candidateList().get();

    // Prediction mode: handle selection or exit
    if (st->predicting && cl) {
        // Selection keys: select prediction candidate
        auto idx = key.keyListIndex(*config_.selectionKeys);
        if (idx >= 0 && idx < cl->size()) {
            cl->candidate(idx).select(ic);
            event.filterAndAccept();
            return;
        }
        // Space: select current prediction
        if (key.checkKeyList(*config_.currentCandidate) && cl->size() > 0) {
            int cidx = cl->cursorIndex();
            if (cidx < 0 || cidx >= cl->size()) cidx = 0;
            cl->candidate(cidx).select(ic);
            event.filterAndAccept();
            return;
        }
        // Page navigation in prediction mode
        auto *pageable = cl->toPageable();
        if (pageable) {
            if (key.checkKeyList(*config_.nextPage) && pageable->hasNext()) {
                pageable->next();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                event.filterAndAccept();
                return;
            }
            if (key.checkKeyList(*config_.prevPage) && pageable->hasPrev()) {
                pageable->prev();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                event.filterAndAccept();
                return;
            }
        }
        // Escape: dismiss predictions
        if (key.check(FcitxKey_Escape)) {
            st->predicting = false;
            panel.reset();
            ic->updatePreedit();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return;
        }
        // Any other key: exit prediction, fall through to normal handling
        st->predicting = false;
        panel.reset();
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        cl = nullptr;
    }

    // Selection keys (1-9 by default): select candidate
    if (!st->empty() && cl) {
        auto idx = key.keyListIndex(*config_.selectionKeys);
        if (idx >= 0 && idx < cl->size()) {
            cl->candidate(idx).select(ic);
            event.filterAndAccept();
            return;
        }
    }

    // Current candidate (Space by default): select highlighted
    if (!st->empty() && key.checkKeyList(*config_.currentCandidate)) {
        if (cl && cl->size() > 0) {
            int idx = cl->cursorIndex();
            if (idx < 0 || idx >= cl->size()) idx = 0;
            cl->candidate(idx).select(ic);
        }
        event.filterAndAccept();
        return;
    }

    // Commit raw input (Enter by default)
    // Like fcitx5-pinyin: commit selected hanzi + remaining raw pinyin
    if (!st->empty() && key.checkKeyList(*config_.commitRawInput)) {
        ic->commitString(st->committedText() + st->remaining());
        st->clearContext();
        resetState(ic);
        event.filterAndAccept();
        return;
    }

    // Escape: cancel
    if (key.check(FcitxKey_Escape) && !st->empty()) {
        resetState(ic);
        event.filterAndAccept();
        return;
    }

    // Backspace
    if (key.check(FcitxKey_BackSpace)) {
        // Punctuation undo: buffer empty, last action was punctuation
        if (st->empty() && lastIsPunc && !lastPuncStr.empty()) {
            auto charCount = utf8::lengthValidated(lastPuncStr);
            if (charCount != utf8::INVALID_LENGTH && charCount > 1) {
                for (size_t i = 1; i < charCount; i++) {
                    ic->forwardKey(Key(FcitxKey_BackSpace));
                }
            }
            // Let the original backspace pass through to delete the last char
            return;
        }

        if (!st->empty()) {
            // Like fcitx5-pinyin: if cursor at selection boundary, cancel selection
            if (!st->selections.empty() && st->cursor == st->selectedLength()) {
                st->cancel();
                st->cursor = st->buffer.size();
            } else if (st->cursor > st->selectedLength()) {
                st->buffer.erase(st->cursor - 1, 1);
                --st->cursor;
            }
            if (st->empty())
                resetState(ic);
            else
                updateUI(ic);
            event.filterAndAccept();
        }
        return;
    }

    // Delete: delete character after cursor (only in remaining part)
    if (key.check(FcitxKey_Delete)) {
        if (!st->empty() && st->cursor >= st->selectedLength() &&
            st->cursor < st->buffer.size()) {
            st->buffer.erase(st->cursor, 1);
            if (st->empty())
                resetState(ic);
            else
                updateUI(ic);
            event.filterAndAccept();
        }
        return;
    }

    // Next/prev candidate (Tab/Shift+Tab by default)
    if (!st->empty() && cl) {
        int delta = 0;
        if (key.checkKeyList(*config_.nextCandidate)) delta = 1;
        else if (key.checkKeyList(*config_.prevCandidate)) delta = -1;
        if (delta) {
            auto *ccl = dynamic_cast<CommonCandidateList *>(cl);
            if (ccl) {
                int next = ccl->cursorIndex() + delta;
                if (next >= 0 && next < ccl->size())
                    ccl->setCursorIndex(next);
            }
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return;
        }
    }

    // Page navigation
    if (!st->empty() && cl) {
        auto *pageable = cl->toPageable();
        if (pageable) {
            bool handled = false;
            if (key.checkKeyList(*config_.nextPage)) {
                if (pageable->hasNext()) pageable->next();
                handled = true;
            } else if (key.checkKeyList(*config_.prevPage)) {
                if (pageable->hasPrev()) pageable->prev();
                handled = true;
            }
            if (handled) {
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                event.filterAndAccept();
                return;
            }
        }
    }

    // Left/Right: move cursor (like fcitx5-pinyin)
    if (!st->empty() && (key.check(FcitxKey_Left) || key.check(FcitxKey_Right))) {
        if (key.check(FcitxKey_Left)) {
            // If cursor is at selection boundary, cancel last selection
            // and stay in place (don't move further left).
            if (st->cursor == st->selectedLength() && !st->selections.empty()) {
                st->cancel();
            } else if (st->cursor > st->selectedLength()) {
                --st->cursor;
            }
        } else {
            if (st->cursor < st->buffer.size())
                ++st->cursor;
        }
        updateUI(ic);
        event.filterAndAccept();
        return;
    }

    // Home/End (like fcitx5-pinyin: Home goes to selection boundary, not 0)
    if (!st->empty() && (key.check(FcitxKey_Home) || key.check(FcitxKey_End))) {
        st->cursor = key.check(FcitxKey_Home) ? st->selectedLength() : st->buffer.size();
        updateUI(ic);
        event.filterAndAccept();
        return;
    }

    // a-z / Shift+A-Z: append to buffer (preserving case)
    // Apostrophe: separator (only when composing)
    {
        char ch = 0;
        if (key.isLAZ())
            ch = static_cast<char>(key.sym());
        else if (key.isUAZ())
            ch = static_cast<char>(key.sym());
        else if (key.check(FcitxKey_apostrophe) && !st->empty())
            ch = '\'';
        if (ch) {
            st->buffer.insert(st->cursor, 1, ch);
            ++st->cursor;
            updateUI(ic);
            event.filterAndAccept();
            return;
        }
    }

    // Punctuation (via punctuation addon)
    if (!key.hasModifier() && !key.isKeyPad() && punctuation()) {
        auto chr = Key::keySymToUnicode(key.sym());
        if (chr) {
            auto [punc, puncAfter] =
                punctuation()->call<IPunctuation::pushPunctuationV2>(
                    "zh_CN", ic, chr);

            if (!punc.empty()) {
                // Auto-select first candidate if composing
                if (!st->empty() && cl && cl->size() > 0) {
                    cl->candidate(0).select(ic);
                }

                // Commit punctuation with paired cursor placement
                auto paired = punc + puncAfter;
                if (!puncAfter.empty()) {
                    if (ic->capabilityFlags().test(
                            CapabilityFlag::CommitStringWithCursor)) {
                        auto len = utf8::lengthValidated(punc);
                        if (len != utf8::INVALID_LENGTH) {
                            ic->commitStringWithCursor(paired, len);
                        } else {
                            ic->commitString(paired);
                        }
                    } else {
                        ic->commitString(paired);
                        auto afterLen = utf8::lengthValidated(puncAfter);
                        if (afterLen != utf8::INVALID_LENGTH) {
                            for (size_t i = 0; i < afterLen; i++) {
                                ic->forwardKey(Key(FcitxKey_Left));
                            }
                        }
                    }
                } else {
                    ic->commitString(punc);
                }

                st->lastIsPunc = true;
                st->lastPuncStr = paired;

                // Sentence-ending punctuation clears context
                if (punc == "。" || punc == "？" || punc == "！") {
                    st->clearContext();
                }
                event.filterAndAccept();
                return;
            }
        }
    }
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::SimeAddonFactory)
