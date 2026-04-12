// Sime Fcitx5 Engine

#include "sime-ime.h"
#include <cstdlib>
#include <fcitx-utils/key.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>


namespace fcitx {

// ===== Punctuation =====

struct PuncResult {
    std::string text;   // main punctuation
    std::string after;  // text after cursor (for paired brackets)
};

static PuncResult mapPunctuation(KeySym sym, SimeState *st) {
    switch (sym) {
    case FcitxKey_comma:        return {"，", ""};
    case FcitxKey_period:       return {"。", ""};
    case FcitxKey_question:     return {"？", ""};
    case FcitxKey_exclam:       return {"！", ""};
    case FcitxKey_semicolon:    return {"；", ""};
    case FcitxKey_colon:        return {"：", ""};
    case FcitxKey_quotedbl: {
        st->doubleQuoteOpen = !st->doubleQuoteOpen;
        return {st->doubleQuoteOpen ? "\xe2\x80\x9c" : "\xe2\x80\x9d", ""}; // " "
    }
    case FcitxKey_apostrophe: {
        st->singleQuoteOpen = !st->singleQuoteOpen;
        return {st->singleQuoteOpen ? "\xe2\x80\x98" : "\xe2\x80\x99", ""}; // ' '
    }
    case FcitxKey_parenleft:    return {"（", "）"};
    case FcitxKey_parenright:   return {"）", ""};
    case FcitxKey_bracketleft:  return {"【", "】"};
    case FcitxKey_bracketright: return {"】", ""};
    case FcitxKey_less:         return {"《", "》"};
    case FcitxKey_greater:      return {"》", ""};
    default: return {"", ""};
    }
}

static void commitPunctuation(InputContext *ic, SimeState *st,
                               const PuncResult &punc) {
    if (punc.text.empty()) return;

    std::string full = punc.text + punc.after;
    if (!punc.after.empty()) {
        // Paired punctuation: try to place cursor between
        if (ic->capabilityFlags().test(CapabilityFlag::CommitStringWithCursor)) {
            auto len = utf8::lengthValidated(punc.text);
            if (len != utf8::INVALID_LENGTH) {
                ic->commitStringWithCursor(full, len);
            } else {
                ic->commitString(full);
            }
        } else {
            ic->commitString(full);
            auto afterLen = utf8::lengthValidated(punc.after);
            if (afterLen != utf8::INVALID_LENGTH) {
                for (size_t i = 0; i < afterLen; i++) {
                    ic->forwardKey(Key(FcitxKey_Left));
                }
            }
        }
    } else {
        ic->commitString(punc.text);
    }

    st->lastIsPunc = true;
    st->lastPuncStr = full;
}

// ===== Candidate word =====

class SimeCandidateWord : public CandidateWord {
public:
    SimeCandidateWord(Sime *engine, std::string text,
                      std::string pinyin, std::size_t matchedLen)
        : CandidateWord(Text(text)), engine_(engine),
          text_(std::move(text)), pinyin_(std::move(pinyin)),
          matchedLen_(matchedLen) {}

    void select(InputContext *ic) const override {
        engine_->selectCandidate(ic, text_, pinyin_, matchedLen_);
    }

private:
    Sime *engine_;
    std::string text_;
    std::string pinyin_;
    std::size_t matchedLen_;
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
        *config_.triePath, *config_.lmPath);
    if (!sime_->Ready()) {
        FCITX_ERROR() << "Sime: failed to load resources"
                      << " trie=" << *config_.triePath
                      << " lm=" << *config_.lmPath;
        sime_.reset();
    } else {
        FCITX_INFO() << "Sime: resources loaded";
        std::string udPath = *config_.userDictPath;
        if (udPath.empty()) {
            const char* xdg = std::getenv("XDG_DATA_HOME");
            if (xdg && xdg[0]) {
                udPath = std::string(xdg) + "/fcitx5/sime/user.dict";
            } else {
                const char* home = std::getenv("HOME");
                if (home) {
                    udPath = std::string(home) +
                             "/.local/share/fcitx5/sime/user.dict";
                }
            }
        }
        if (!udPath.empty() && sime_->LoadDict(udPath)) {
            FCITX_INFO() << "Sime: user dict loaded from " << udPath;
        }
    }
}

SimeState *Sime::state(InputContext *ic) {
    return ic->propertyFor(&factory_);
}

void Sime::activate(const InputMethodEntry &, InputContextEvent &event) {
    if (!sime_) initSime();
}

void Sime::deactivate(const InputMethodEntry &, InputContextEvent &event) {
    auto *st = state(event.inputContext());
    resetState(event.inputContext());
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

// Called when user selects a candidate
void Sime::selectCandidate(InputContext *ic, const std::string& text,
                                  const std::string& pinyin,
                                  std::size_t matchedLen) {
    auto *st = state(ic);

    // Record selection
    st->select(text, pinyin, matchedLen);

    // If all input consumed, commit everything
    if (st->fullySelected()) {
        ic->commitString(st->committedText());
        resetState(ic);
    } else {
        // Move cursor to end of remaining
        st->cursor = st->buffer.size();
        updateUI(ic);
    }
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
        cl->append<SimeCandidateWord>(this, r.text, r.units, r.cnt);
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

    // a-z / A-Z: append to buffer (like fcitx5-pinyin: isLAZ || isUAZ)
    // Apostrophe: separator (only when composing)
    {
        char ch = 0;
        if ((key.sym() >= FcitxKey_a && key.sym() <= FcitxKey_z && !key.hasModifier()) ||
            (key.sym() >= FcitxKey_A && key.sym() <= FcitxKey_Z &&
             key.states() == KeyState::Shift))
            ch = static_cast<char>(key.sym() | 0x20);
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

    // Punctuation
    auto punc = mapPunctuation(key.sym(), st);
    if (!punc.text.empty()) {
        // Auto-select first candidate if composing
        if (!st->empty() && cl && cl->size() > 0) {
            cl->candidate(0).select(ic);
        }
        commitPunctuation(ic, st, punc);
        event.filterAndAccept();
        return;
    }
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::SimeAddonFactory)
