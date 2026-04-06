// Sime Fcitx5 Engine

#include "sime.h"
#include <cstdlib>
#include <fcitx-utils/key.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <set>

namespace fcitx {

// ===== UTF-32 → UTF-8 =====

static std::string u32ToUtf8(const std::u32string &u32str) {
    std::string utf8;
    utf8.reserve(u32str.size() * 4);
    for (char32_t ch : u32str) {
        if (ch < 0x80) {
            utf8.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else if (ch < 0x10000) {
            utf8.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xF0 | (ch >> 18)));
            utf8.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return utf8;
}

// ===== Punctuation mapping =====

static const char *chinesePunc(KeySym sym) {
    switch (sym) {
    case FcitxKey_comma:      return "，";
    case FcitxKey_period:     return "。";
    case FcitxKey_question:   return "？";
    case FcitxKey_exclam:     return "！";
    case FcitxKey_semicolon:  return "；";
    case FcitxKey_colon:      return "：";
    case FcitxKey_parenleft:  return "（";
    case FcitxKey_parenright: return "）";
    case FcitxKey_bracketleft:  return "【";
    case FcitxKey_bracketright: return "】";
    default: return nullptr;
    }
}

// ===== Candidate word =====

class SimeCandidateWord : public CandidateWord {
public:
    SimeCandidateWord(SimeEngine *engine, std::string text,
                      std::string pinyin, std::size_t matchedLen)
        : CandidateWord(Text(text)), engine_(engine),
          text_(std::move(text)), pinyin_(std::move(pinyin)),
          matchedLen_(matchedLen) {}

    void select(InputContext *ic) const override {
        engine_->selectCandidate(ic, text_, pinyin_, matchedLen_);
    }

private:
    SimeEngine *engine_;
    std::string text_;
    std::string pinyin_;
    std::size_t matchedLen_;
};

// ===== Engine =====

SimeEngine::SimeEngine(Instance *instance)
    : instance_(instance),
      factory_([](InputContext &) { return new SimeState(); }) {
    instance_->inputContextManager().registerProperty("simeState", &factory_);
    reloadConfig();
    initInterpreter();
}

SimeEngine::~SimeEngine() {}

void SimeEngine::reloadConfig() {
    readAsIni(config_, "conf/sime.conf");
}

void SimeEngine::initInterpreter() {
    interpreter_ = std::make_unique<sime::Interpreter>(
        *config_.dictPath, *config_.lmPath);
    if (!interpreter_->Ready()) {
        FCITX_ERROR() << "Sime: failed to load resources"
                      << " dict=" << *config_.dictPath
                      << " lm=" << *config_.lmPath;
        interpreter_.reset();
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
        if (!udPath.empty() && interpreter_->LoadDict(udPath)) {
            FCITX_INFO() << "Sime: user dict loaded from " << udPath;
        }
    }
}

SimeState *SimeEngine::state(InputContext *ic) {
    return ic->propertyFor(&factory_);
}

void SimeEngine::activate(const InputMethodEntry &, InputContextEvent &event) {
    if (!interpreter_) initInterpreter();
}

void SimeEngine::deactivate(const InputMethodEntry &, InputContextEvent &event) {
    resetState(event.inputContext());
}

void SimeEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    resetState(event.inputContext());
}

void SimeEngine::resetState(InputContext *ic) {
    state(ic)->reset();
    ic->inputPanel().reset();
    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

// Called when user selects a candidate
void SimeEngine::selectCandidate(InputContext *ic, const std::string& text,
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

void SimeEngine::updateUI(InputContext *ic) {
    auto *st = state(ic);
    auto &panel = ic->inputPanel();
    panel.reset();

    if (st->empty()) {
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    if (!interpreter_) {
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    // Decode only the remaining (unselected) input
    std::string rem = st->remaining();
    std::vector<sime::DecodeResult> results;
    if (!rem.empty()) {
        results = interpreter_->DecodeSentence(
            rem, static_cast<std::size_t>(*config_.nbest));
    }

    // Build preedit: committed hanzi + remaining pinyin
    std::string committed = st->committedText();
    std::string remaining_display;
    if (!results.empty() && !results[0].units.empty()) {
        remaining_display = results[0].units;
        for (auto &ch : remaining_display) {
            if (ch == '\'') ch = ' ';
        }
    } else {
        remaining_display = rem;
    }

    std::string full_display = committed + remaining_display;

    // Cursor position: at the boundary between committed and remaining,
    // adjusted for user's cursor within remaining
    std::size_t sel_len = st->selectedLength();
    int displayCursor = static_cast<int>(full_display.size());
    if (st->cursor >= sel_len) {
        // Map raw cursor (within remaining) to display position
        std::size_t raw_offset = st->cursor - sel_len;
        std::size_t disp_offset = 0;
        std::size_t raw_idx = 0;
        while (raw_idx < rem.size() && disp_offset < remaining_display.size()) {
            if (raw_idx == raw_offset) {
                displayCursor = static_cast<int>(committed.size() + disp_offset);
                break;
            }
            if (remaining_display[disp_offset] == ' ' &&
                (raw_idx >= rem.size() || rem[raw_idx] != ' ')) {
                ++disp_offset;
                continue;
            }
            ++raw_idx;
            ++disp_offset;
        }
        if (raw_idx == raw_offset) {
            displayCursor = static_cast<int>(committed.size() + disp_offset);
        }
    } else {
        displayCursor = static_cast<int>(committed.size());
    }

    Text preeditText;
    if (!committed.empty()) {
        preeditText.append(committed, TextFormatFlags{TextFormatFlag::HighLight});
    }
    if (!remaining_display.empty()) {
        preeditText.append(remaining_display,
                           TextFormatFlags{TextFormatFlag::Underline});
    }
    preeditText.setCursor(displayCursor);

    bool hasPreeditCap = ic->capabilityFlags().test(CapabilityFlag::Preedit);
    if (hasPreeditCap) {
        panel.setClientPreedit(preeditText);
        panel.setPreedit(Text{});
    } else {
        panel.setClientPreedit(Text{});
        panel.setPreedit(preeditText);
    }

    // Build candidate list from remaining input
    auto cl = std::make_unique<CommonCandidateList>();
    cl->setPageSize(*config_.pageSize);
    cl->setLayoutHint(CandidateLayoutHint::Horizontal);
    cl->setSelectionKey(Key::keyListFromString("1 2 3 4 5 6 7 8 9"));
    cl->setCursorPositionAfterPaging(CursorPositionAfterPaging::ResetToFirst);

    std::set<std::string> seen;
    for (const auto &r : results) {
        const auto& text = r.text;
        if (seen.insert(text).second) {
            cl->append<SimeCandidateWord>(this, text, r.units, r.cnt);
        }
    }

    if (!seen.empty()) {
        cl->setCursorIndex(0);
    }
    panel.setCandidateList(std::move(cl));
    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void SimeEngine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
    if (event.isRelease()) return;

    auto *ic = event.inputContext();
    auto *st = state(ic);
    auto sym = event.key().sym();

    auto &panel = ic->inputPanel();
    auto *cl = panel.candidateList().get();

    // Number 1-9: select candidate
    if (!st->empty() && sym >= FcitxKey_1 && sym <= FcitxKey_9) {
        int localIdx = sym - FcitxKey_1;
        if (cl && localIdx < cl->size()) {
            cl->candidate(localIdx).select(ic);
        }
        event.filterAndAccept();
        return;
    }

    // Space: select current highlighted candidate (default first)
    if (!st->empty() && sym == FcitxKey_space) {
        if (cl && cl->size() > 0) {
            int idx = cl->cursorIndex();
            if (idx < 0 || idx >= cl->size()) idx = 0;
            cl->candidate(idx).select(ic);
        }
        event.filterAndAccept();
        return;
    }

    // Enter: commit raw pinyin
    if (!st->empty() &&
        (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter)) {
        ic->commitString(st->buffer);
        resetState(ic);
        event.filterAndAccept();
        return;
    }

    // Escape: cancel
    if (sym == FcitxKey_Escape && !st->empty()) {
        resetState(ic);
        event.filterAndAccept();
        return;
    }

    // Backspace: undo last selection, or delete character
    if (sym == FcitxKey_BackSpace) {
        if (!st->empty()) {
            if (!st->selections.empty() && st->cursor <= st->selectedLength()) {
                // Undo last selection
                st->cancel();
                st->cursor = st->buffer.size();
            } else if (st->cursor > 0) {
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

    // Delete: delete character after cursor
    if (sym == FcitxKey_Delete) {
        if (!st->empty() && st->cursor < st->buffer.size()) {
            st->buffer.erase(st->cursor, 1);
            if (st->empty())
                resetState(ic);
            else
                updateUI(ic);
            event.filterAndAccept();
        }
        return;
    }

    // Tab/Shift+Tab: move highlight among candidates
    if (!st->empty() && cl &&
        (sym == FcitxKey_Tab || sym == FcitxKey_ISO_Left_Tab)) {
        auto *ccl = dynamic_cast<CommonCandidateList *>(cl);
        if (ccl) {
            int cur = ccl->cursorIndex();
            int pageSize = ccl->size();
            if (sym == FcitxKey_Tab) {
                if (cur + 1 < pageSize)
                    ccl->setCursorIndex(cur + 1);
            } else {
                if (cur > 0)
                    ccl->setCursorIndex(cur - 1);
            }
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
        event.filterAndAccept();
        return;
    }

    // Page navigation
    if (!st->empty() && cl) {
        auto *pageable = cl->toPageable();
        if (pageable) {
            if (sym == FcitxKey_Page_Down || sym == FcitxKey_equal ||
                sym == FcitxKey_plus || sym == FcitxKey_Down) {
                if (pageable->hasNext()) {
                    pageable->next();
                    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                }
                event.filterAndAccept();
                return;
            }
            if (sym == FcitxKey_Page_Up || sym == FcitxKey_minus ||
                sym == FcitxKey_Up) {
                if (pageable->hasPrev()) {
                    pageable->prev();
                    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                }
                event.filterAndAccept();
                return;
            }
        }
    }

    // Left/Right: move cursor
    if (!st->empty() &&
        (sym == FcitxKey_Left || sym == FcitxKey_Right)) {
        if (sym == FcitxKey_Left && st->cursor > 0)
            --st->cursor;
        else if (sym == FcitxKey_Right && st->cursor < st->buffer.size())
            ++st->cursor;
        updateUI(ic);
        event.filterAndAccept();
        return;
    }

    // Home/End
    if (!st->empty() &&
        (sym == FcitxKey_Home || sym == FcitxKey_End)) {
        st->cursor = (sym == FcitxKey_Home) ? 0 : st->buffer.size();
        updateUI(ic);
        event.filterAndAccept();
        return;
    }

    // a-z: append to buffer
    if (sym >= FcitxKey_a && sym <= FcitxKey_z) {
        st->buffer.insert(st->cursor, 1, static_cast<char>(sym));
        ++st->cursor;
        updateUI(ic);
        event.filterAndAccept();
        return;
    }

    // Apostrophe: separator
    if (sym == FcitxKey_apostrophe && !st->empty()) {
        st->buffer.insert(st->cursor, 1, '\'');
        ++st->cursor;
        updateUI(ic);
        event.filterAndAccept();
        return;
    }

    // Punctuation: auto-commit first candidate + output punctuation
    if (const char *punc = chinesePunc(sym)) {
        if (!st->empty() && cl && cl->size() > 0) {
            cl->candidate(0).select(ic);
        }
        ic->commitString(punc);
        event.filterAndAccept();
        return;
    }
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::SimeEngineFactory)
