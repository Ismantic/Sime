// Sime Fcitx5 Engine
// 结构完全照搬 fcitx5-sime/src/sime_engine.cpp，适配 sime_core API

#include "sime.h"
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

namespace fcitx {

// ===== UTF-32 → UTF-8 (直接从 reference 复制) =====

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

// ===== 候选词类 (照搬 reference) =====

class SimeCandidateWord : public CandidateWord {
public:
    SimeCandidateWord(SimeEngine *engine, const std::string &text, size_t idx)
        : CandidateWord(Text(text)), engine_(engine), idx_(idx) {}

    void select(InputContext *ic) const override;

private:
    SimeEngine *engine_;
    size_t idx_;
};

// ===== 候选词列表类 (照搬 reference，存 DecodeResult) =====

class SimeCandidateList : public CandidateList,
                          public PageableCandidateList {
public:
    SimeCandidateList(SimeEngine *engine, InputContext *ic,
                      const std::vector<sime::DecodeResult> &results,
                      size_t pageSize = 9)
        : engine_(engine), ic_(ic), pageSize_(pageSize), currentPage_(0) {
        for (const auto &r : results) {
            results_.push_back(r);
            words_.emplace_back(std::make_unique<SimeCandidateWord>(
                engine, u32ToUtf8(r.text), results_.size() - 1));
        }
    }

    const Text &label(int idx) const override {
        static const std::vector<Text> labels = []() {
            std::vector<Text> ls;
            const char *nums[] = {"1.", "2.", "3.", "4.", "5.",
                                  "6.", "7.", "8.", "9.", "0."};
            for (int i = 0; i < 10; i++) ls.emplace_back(nums[i]);
            return ls;
        }();
        return labels[idx % 10];
    }

    const CandidateWord &candidate(int idx) const override {
        size_t globalIdx = currentPage_ * pageSize_ + idx;
        if (globalIdx < words_.size()) return *words_[globalIdx];
        static SimeCandidateWord dummy(nullptr, "", 0);
        return dummy;
    }

    int size() const override {
        size_t start = currentPage_ * pageSize_;
        size_t end = std::min(start + pageSize_, results_.size());
        return static_cast<int>(end - start);
    }

    int cursorIndex() const override { return -1; }
    CandidateLayoutHint layoutHint() const override {
        return CandidateLayoutHint::Horizontal;
    }

    bool hasPrev() const override { return currentPage_ > 0; }
    bool hasNext() const override {
        return (currentPage_ + 1) * pageSize_ < results_.size();
    }
    void prev() override { if (hasPrev()) currentPage_--; }
    void next() override { if (hasNext()) currentPage_++; }
    bool usedNextBefore() const override { return false; }

    // 按当前页的 local idx 返回结果 (照搬 reference)
    const sime::DecodeResult *selectedResult(int localIdx) const {
        size_t globalIdx = currentPage_ * pageSize_ + localIdx;
        if (globalIdx < results_.size()) return &results_[globalIdx];
        return nullptr;
    }

private:
    SimeEngine *engine_;
    InputContext *ic_;
    std::vector<sime::DecodeResult> results_;
    std::vector<std::unique_ptr<SimeCandidateWord>> words_;
    size_t pageSize_;
    size_t currentPage_;
};

// ===== 引擎实现 =====

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
    interpreter_ = std::make_unique<sime::Interpreter>();
    if (!interpreter_->LoadResources(*config_.dictPath, *config_.lmPath)) {
        FCITX_ERROR() << "Sime: failed to load resources: dict=" << *config_.dictPath
                      << " lm=" << *config_.lmPath;
        interpreter_.reset();
    } else {
        FCITX_INFO() << "Sime: resources loaded";
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

// 照搬 reference 的 resetState
void SimeEngine::resetState(InputContext *ic) {
    state(ic)->preedit.clear();
    ic->inputPanel().reset();
    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

// 照搬 reference 的 keyEvent
void SimeEngine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
    if (event.isRelease()) return;

    auto *ic = event.inputContext();
    auto *st = state(ic);
    KeySym sym = event.rawKey().sym();

    // 数字 1-9 选词 (照搬 reference：local idx，由 SimeCandidateList 加页偏移)
    if (!st->preedit.empty() && sym >= FcitxKey_1 && sym <= FcitxKey_9) {
        int idx = sym - FcitxKey_1;
        commitCandidate(ic, idx);
        event.filterAndAccept();
        return;
    }

    // 数字 0 选第 10 个
    if (!st->preedit.empty() && sym == FcitxKey_0) {
        commitCandidate(ic, 9);
        event.filterAndAccept();
        return;
    }

    // 空格提交第一个候选
    if (!st->preedit.empty() && sym == FcitxKey_space) {
        commitCandidate(ic, 0);
        event.filterAndAccept();
        return;
    }

    // 回车提交原始拼音
    if (!st->preedit.empty() &&
        (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter)) {
        ic->commitString(st->preedit);
        resetState(ic);
        event.filterAndAccept();
        return;
    }

    // Escape 取消
    if (sym == FcitxKey_Escape) {
        if (!st->preedit.empty()) {
            resetState(ic);
            event.filterAndAccept();
        }
        return;
    }

    // 退格
    if (sym == FcitxKey_BackSpace) {
        if (!st->preedit.empty()) {
            st->preedit.pop_back();
            updateUI(ic);
            event.filterAndAccept();
        }
        return;
    }

    // 翻页 (照搬 reference)
    if (!st->preedit.empty()) {
        auto &panel = ic->inputPanel();
        if (auto *cl = dynamic_cast<SimeCandidateList *>(
                panel.candidateList().get())) {
            if (sym == FcitxKey_Page_Down || sym == FcitxKey_equal ||
                sym == FcitxKey_plus) {
                if (cl->hasNext()) {
                    cl->next();
                    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                }
                event.filterAndAccept();
                return;
            }
            if (sym == FcitxKey_Page_Up || sym == FcitxKey_minus) {
                if (cl->hasPrev()) {
                    cl->prev();
                    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                }
                event.filterAndAccept();
                return;
            }
        }
    }

    // 字母 a-z / A-Z
    if ((sym >= FcitxKey_a && sym <= FcitxKey_z) ||
        (sym >= FcitxKey_A && sym <= FcitxKey_Z)) {
        char c = static_cast<char>(sym);
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        st->preedit.push_back(c);
        updateUI(ic);
        event.filterAndAccept();
        return;
    }

    // 单引号分词符
    if (sym == FcitxKey_apostrophe && !st->preedit.empty()) {
        st->preedit.push_back('\'');
        updateUI(ic);
        event.filterAndAccept();
        return;
    }
}

// 照搬 reference 的 updateUI
void SimeEngine::updateUI(InputContext *ic) {
    auto *st = state(ic);
    auto &panel = ic->inputPanel();
    panel.reset();

    if (st->preedit.empty()) {
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    // 预编辑 (照搬 reference 用 setPreedit)
    Text preeditText(st->preedit);
    preeditText.setCursor(st->preedit.size());
    panel.setClientPreedit(preeditText);

    // 解码
    if (!interpreter_) {
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    auto results = interpreter_->DecodeText(st->preedit, *config_.nbest);

    if (!results.empty()) {
        auto candidateList = std::make_unique<SimeCandidateList>(
            this, ic, results, *config_.pageSize);
        panel.setCandidateList(std::move(candidateList));
    }

    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

// 照搬 reference 的 commitCandidate (local idx)
void SimeEngine::commitCandidate(InputContext *ic, int localIdx) {
    auto &panel = ic->inputPanel();
    auto *cl = dynamic_cast<SimeCandidateList *>(panel.candidateList().get());
    if (!cl) return;

    auto *result = cl->selectedResult(localIdx);
    if (!result) return;

    ic->commitString(u32ToUtf8(result->text));
    resetState(ic);
}

// SimeCandidateWord::select 在 SimeEngine 定义之后实现 (照搬 reference)
void SimeCandidateWord::select(InputContext *ic) const {
    if (engine_) engine_->commitCandidate(ic, idx_);
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::SimeEngineFactory)
