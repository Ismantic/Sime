// Sime Fcitx5 Engine

#include "sime.h"
#include "unit.h"
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

// ===== 标点映射 (特性1) =====

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

// ===== 候选词 =====
// 存 text + matchedLen，select() 提交文字并消耗 matchedLen 个字节的 preedit

class SimeCandidateWord : public CandidateWord {
public:
    SimeCandidateWord(SimeEngine *engine, std::string text, std::size_t matchedLen)
        : CandidateWord(Text(text)), engine_(engine),
          text_(std::move(text)), matchedLen_(matchedLen) {}

    void select(InputContext *ic) const override {
        ic->commitString(text_);
        engine_->consumePreedit(ic, matchedLen_);
    }

private:
    SimeEngine *engine_;
    std::string text_;
    std::size_t matchedLen_;
};

// ===== 引擎 =====

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
        FCITX_ERROR() << "Sime: failed to load resources"
                      << " dict=" << *config_.dictPath
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

void SimeEngine::resetState(InputContext *ic) {
    state(ic)->preedit.clear();
    ic->inputPanel().reset();
    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

// 特性4：消耗 n 个字节的 preedit，剩余部分继续输入
void SimeEngine::consumePreedit(InputContext *ic, std::size_t n) {
    auto *st = state(ic);
    if (n >= st->preedit.size()) {
        resetState(ic);
    } else {
        st->preedit = st->preedit.substr(n);
        updateUI(ic);
    }
}

// 特性4：找出 preedit 所有合法的拼音前缀分割点
// 返回长度列表（从大到小），第一个始终是 preedit.size()
std::vector<std::size_t> SimeEngine::validPrefixLengths(const std::string &preedit) {
    std::vector<std::size_t> result;
    result.push_back(preedit.size());

    if (preedit.size() <= 3) return result;

    static const sime::UnitParser parser;

    for (std::size_t len = preedit.size() - 1; len >= 2; --len) {
        std::string prefix = preedit.substr(0, len);
        std::string suffix = preedit.substr(len);
        std::vector<sime::Unit> units;
        if (parser.ParseStr(prefix, units) && parser.ParseStr(suffix, units)) {
            result.push_back(len);
        }
        if (result.size() >= 4) break; // 最多 3 个前缀 + 全长
    }

    return result;
}

void SimeEngine::updateUI(InputContext *ic) {
    auto *st = state(ic);
    auto &panel = ic->inputPanel();
    panel.reset();

    if (st->preedit.empty()) {
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    // 特性2：preedit 加下划线
    Text preeditText;
    preeditText.append(st->preedit, TextFormatFlags{TextFormatFlag::Underline});
    preeditText.setCursor(static_cast<int>(st->preedit.size()));

    bool hasPreeditCap = ic->capabilityFlags().test(CapabilityFlag::Preedit);
    FCITX_INFO() << "Sime: CapabilityFlag::Preedit = " << hasPreeditCap
                 << " preedit=" << st->preedit;
    if (hasPreeditCap) {
        panel.setClientPreedit(preeditText);
        panel.setPreedit(Text{});
    } else {
        panel.setClientPreedit(Text{});
        panel.setPreedit(preeditText);
    }

    if (!interpreter_) {
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    // 特性4：对完整 preedit 和有效前缀分别解码，合并候选
    auto prefixLens = validPrefixLengths(st->preedit);

    auto cl = std::make_unique<CommonCandidateList>();
    cl->setPageSize(*config_.pageSize);
    cl->setLayoutHint(CandidateLayoutHint::Horizontal);
    cl->setSelectionKey(Key::keyListFromString("1 2 3 4 5 6 7 8 9"));
    cl->setCursorPositionAfterPaging(CursorPositionAfterPaging::ResetToFirst);

    std::set<std::string> seen;

    for (std::size_t len : prefixLens) {
        std::string prefix = st->preedit.substr(0, len);
        bool isFull = (len == st->preedit.size());
        std::size_t num = isFull ? static_cast<std::size_t>(*config_.nbest) : 5;

        auto results = interpreter_->DecodeText(prefix, num);
        for (const auto &r : results) {
            auto text = u32ToUtf8(r.text);
            if (seen.insert(text).second) {
                cl->append<SimeCandidateWord>(this, text, len);
            }
        }
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

    // 数字 1-9 选词
    if (!st->preedit.empty() && sym >= FcitxKey_1 && sym <= FcitxKey_9) {
        int localIdx = sym - FcitxKey_1;
        if (cl && localIdx < cl->size()) {
            cl->candidate(localIdx).select(ic);
        }
        event.filterAndAccept();
        return;
    }

    // 空格选第一个候选
    if (!st->preedit.empty() && sym == FcitxKey_space) {
        if (cl && cl->size() > 0) {
            cl->candidate(0).select(ic);
        }
        event.filterAndAccept();
        return;
    }

    // 回车：提交原始拼音
    if (!st->preedit.empty() &&
        (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter)) {
        ic->commitString(st->preedit);
        resetState(ic);
        event.filterAndAccept();
        return;
    }

    // Escape：取消
    if (sym == FcitxKey_Escape && !st->preedit.empty()) {
        resetState(ic);
        event.filterAndAccept();
        return;
    }

    // 退格
    if (sym == FcitxKey_BackSpace) {
        if (!st->preedit.empty()) {
            st->preedit.pop_back();
            if (st->preedit.empty())
                resetState(ic);
            else
                updateUI(ic);
            event.filterAndAccept();
        }
        return;
    }

    // 翻页
    if (!st->preedit.empty() && cl) {
        auto *pageable = cl->toPageable();
        if (pageable) {
            if (sym == FcitxKey_Page_Down || sym == FcitxKey_equal ||
                sym == FcitxKey_plus) {
                if (pageable->hasNext()) {
                    pageable->next();
                    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                }
                event.filterAndAccept();
                return;
            }
            if (sym == FcitxKey_Page_Up || sym == FcitxKey_minus) {
                if (pageable->hasPrev()) {
                    pageable->prev();
                    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                }
                event.filterAndAccept();
                return;
            }
        }
    }

    // 字母 a-z
    if (sym >= FcitxKey_a && sym <= FcitxKey_z) {
        st->preedit.push_back(static_cast<char>(sym));
        updateUI(ic);
        event.filterAndAccept();
        return;
    }

    // 单引号分词
    if (sym == FcitxKey_apostrophe && !st->preedit.empty()) {
        st->preedit.push_back('\'');
        updateUI(ic);
        event.filterAndAccept();
        return;
    }

    // 特性1：标点符号转中文
    // preedit 为空时直接转换；非空时不拦截（用户先完成选词）
    if (st->preedit.empty()) {
        if (const char *punc = chinesePunc(sym)) {
            ic->commitString(punc);
            event.filterAndAccept();
            return;
        }
    }
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::SimeEngineFactory)
