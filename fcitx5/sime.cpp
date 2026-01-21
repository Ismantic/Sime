#include "sime.h"
#include "ustr.h"
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

namespace fcitx {

// 候选词类
class SimeCandidateWord : public CandidateWord {
public:
    SimeCandidateWord(SimeEngine *engine, const std::string &text, int index)
        : CandidateWord(Text(text)), engine_(engine), index_(index) {}

    void select(InputContext *ic) const override {
        engine_->selectCandidate(ic, index_);
    }

private:
    SimeEngine *engine_;
    int index_;
};

// 构造函数
SimeEngine::SimeEngine(Instance *instance)
    : instance_(instance), factory_([](InputContext &ic) {
          return new SimeState();
      }) {
    // 注册状态工厂
    instance_->inputContextManager().registerProperty("simeState", &factory_);

    // 加载配置
    reloadConfig();

    // 初始化解释器
    initializeInterpreter();
}

SimeEngine::~SimeEngine() {
    // 清理资源
}

void SimeEngine::reloadConfig() {
    readAsIni(config_, "conf/sime.conf");
}

void SimeEngine::initializeInterpreter() {
    interpreter_ = std::make_unique<sime::Interpreter>();

    // 加载词典和语言模型
    std::filesystem::path dictPath(*config_.dictPath);
    std::filesystem::path lmPath(*config_.lmPath);

    if (!interpreter_->LoadResources(dictPath, lmPath)) {
        FCITX_ERROR() << "Failed to load Sime resources: dict=" << dictPath
                      << ", lm=" << lmPath;
    } else {
        FCITX_INFO() << "Sime resources loaded successfully";
    }
}

SimeState *SimeEngine::state(InputContext *ic) {
    return ic->propertyFor(&factory_);
}

void SimeEngine::activate(const InputMethodEntry &entry,
                          InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *state = this->state(ic);
    state->reset();

    // 如果还没初始化，现在初始化
    if (!interpreter_ || !interpreter_->Ready()) {
        initializeInterpreter();
    }
}

void SimeEngine::deactivate(const InputMethodEntry &entry,
                            InputContextEvent &event) {
    auto *ic = event.inputContext();
    clearPreedit(ic);
}

void SimeEngine::reset(const InputMethodEntry &entry,
                       InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *state = this->state(ic);
    state->reset();
    clearPreedit(ic);
}

void SimeEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) {
    auto *ic = keyEvent.inputContext();
    auto *state = this->state(ic);

    // 只处理按键按下事件
    if (keyEvent.isRelease()) {
        return;
    }

    // 获取按键信息
    auto key = keyEvent.key();

    // 处理字母输入 (a-z)
    if (key.checkKeyList(
            {FcitxKey_a, FcitxKey_b, FcitxKey_c, FcitxKey_d, FcitxKey_e,
             FcitxKey_f, FcitxKey_g, FcitxKey_h, FcitxKey_i, FcitxKey_j,
             FcitxKey_k, FcitxKey_l, FcitxKey_m, FcitxKey_n, FcitxKey_o,
             FcitxKey_p, FcitxKey_q, FcitxKey_r, FcitxKey_s, FcitxKey_t,
             FcitxKey_u, FcitxKey_v, FcitxKey_w, FcitxKey_x, FcitxKey_y,
             FcitxKey_z})) {
        state->appendPinyin(static_cast<char>(key.sym()));
        updateCandidates(ic);
        keyEvent.filterAndAccept();
        return;
    }

    // 处理数字选词 (1-9)
    if (key.checkKeyList({FcitxKey_1, FcitxKey_2, FcitxKey_3, FcitxKey_4,
                          FcitxKey_5, FcitxKey_6, FcitxKey_7, FcitxKey_8,
                          FcitxKey_9})) {
        if (!state->isEmpty()) {
            int index = key.sym() - FcitxKey_1; // 0-based index
            selectCandidate(ic, index);
            keyEvent.filterAndAccept();
            return;
        }
    }

    // 处理空格 - 选择第一个候选词
    if (key.check(FcitxKey_space)) {
        if (!state->isEmpty()) {
            selectCandidate(ic, 0);
            keyEvent.filterAndAccept();
            return;
        }
    }

    // 处理退格 - 删除最后一个字符
    if (key.check(FcitxKey_BackSpace)) {
        if (!state->isEmpty()) {
            state->deleteLast();
            if (state->isEmpty()) {
                clearPreedit(ic);
            } else {
                updateCandidates(ic);
            }
            keyEvent.filterAndAccept();
            return;
        }
    }

    // 处理 Enter - 提交当前拼音
    if (key.check(FcitxKey_Return) || key.check(FcitxKey_KP_Enter)) {
        if (!state->isEmpty()) {
            commitPreedit(ic);
            keyEvent.filterAndAccept();
            return;
        }
    }

    // 处理 Escape - 清空输入
    if (key.check(FcitxKey_Escape)) {
        if (!state->isEmpty()) {
            clearPreedit(ic);
            keyEvent.filterAndAccept();
            return;
        }
    }
}

void SimeEngine::updateCandidates(InputContext *ic) {
    auto *state = this->state(ic);
    auto &inputPanel = ic->inputPanel();

    // 如果没有拼音输入，清空面板
    if (state->isEmpty()) {
        clearPreedit(ic);
        return;
    }

    // 检查缓存
    if (state->pinyinBuffer() == state->cachedPinyin() &&
        !state->candidates().empty()) {
        // 使用缓存的候选词
        goto update_panel;
    }

    // 调用 Sime 核心进行解码
    if (interpreter_ && interpreter_->Ready()) {
        sime::DecodeOptions options;
        options.num = static_cast<std::size_t>(*config_.numCandidates);

        auto results =
            interpreter_->DecodeText(state->pinyinBuffer(), options);

        // 转换为候选词
        std::vector<SimeCandidate> candidates;
        candidates.reserve(results.size());

        for (size_t i = 0; i < results.size(); ++i) {
            auto &result = results[i];
            // 将 u32string 转换为 UTF-8
            std::string text = sime::ustr::ToU8(result.text);
            candidates.emplace_back(text, result.score, static_cast<int>(i));
        }

        state->setCandidates(std::move(candidates));
        state->setCachedPinyin(state->pinyinBuffer());
    }

update_panel:
    // 设置 preedit (拼音显示)
    Text preedit;
    preedit.append(state->pinyinBuffer());
    inputPanel.setClientPreedit(preedit);

    // 设置候选词列表
    auto candidateList = std::make_unique<CommonCandidateList>();
    const auto &candidates = state->candidates();

    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto &cand = candidates[i];
        auto label = std::to_string(i + 1) + ". ";
        candidateList->append<SimeCandidateWord>(this, cand.text,
                                                  static_cast<int>(i));
        candidateList->candidateFromLabel(i)->setText(Text(label + cand.text));
    }

    inputPanel.setCandidateList(std::move(candidateList));
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void SimeEngine::selectCandidate(InputContext *ic, int index) {
    auto *state = this->state(ic);
    const auto &candidates = state->candidates();

    if (index >= 0 && index < static_cast<int>(candidates.size())) {
        // 提交选中的候选词
        ic->commitString(candidates[static_cast<size_t>(index)].text);

        // 重置状态
        state->reset();
        clearPreedit(ic);
    }
}

void SimeEngine::commitPreedit(InputContext *ic) {
    auto *state = this->state(ic);

    // 提交当前拼音（如果没有候选词）
    if (!state->isEmpty()) {
        ic->commitString(state->pinyinBuffer());
        state->reset();
        clearPreedit(ic);
    }
}

void SimeEngine::clearPreedit(InputContext *ic) {
    auto &inputPanel = ic->inputPanel();
    inputPanel.reset();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

} // namespace fcitx

// 注册插件工厂
FCITX_ADDON_FACTORY(fcitx::SimeEngineFactory)
