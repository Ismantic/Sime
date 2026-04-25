#ifndef FCITX5_SIME_ENGINE_H
#define FCITX5_SIME_ENGINE_H

#include "sime-state.h"
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx-module/punctuation/punctuation_public.h>
#include <memory>
#include <vector>

#include "sime.h"

namespace fcitx {

enum class PreeditMode { No, ComposingPinyin, CommitPreview };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(PreeditMode, N_("不显示"),
                                 N_("拼音组合"), N_("提交预览"))

enum class SwitchInputMethodBehavior { Clear, CommitPreedit, CommitDefault };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(SwitchInputMethodBehavior,
                                 N_("清空"),
                                 N_("提交预编辑"),
                                 N_("提交默认候选"))

class Sime final : public InputMethodEngineV3 {
public:
    Sime(Instance *instance);
    ~Sime() override;

    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void reset(const InputMethodEntry &entry, InputContextEvent &event) override;
    void activate(const InputMethodEntry &entry, InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry, InputContextEvent &event) override;

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override {
        config_.load(config);
        safeSaveAsIni(config_, "conf/sime.conf");
    }
    void reloadConfig() override;

    std::vector<InputMethodEntry> listInputMethods() override {
        std::vector<InputMethodEntry> result;
        InputMethodEntry entry("sime-pinyin", _("Sime"), "zh_CN", "sime");
        entry.setLabel("语").setConfigurable(true);
        result.push_back(std::move(entry));
        return result;
    }

    // Called by SimeCandidateWord on selection
    void selectCandidate(InputContext *ic, const std::string& text,
                         const std::string& pinyin,
                         const std::vector<sime::TokenID>& tokens,
                         std::size_t matchedLen);

    // Called by SimeNextCandidateWord
    void showPredictions(InputContext *ic);
    FactoryFor<SimeState> *stateFactory() { return &factory_; }
    int contextSize() const { return sime_ ? sime_->ContextSize() : 2; }

    struct Config : public Configuration {
        // Candidates
        Option<int> nbest{this, "NBest", _("额外全句数"), 0};
        Option<bool> prediction{this, "Prediction", _("联想"), true};

        // Preedit
        OptionWithAnnotation<PreeditMode, PreeditModeI18NAnnotation> preeditMode{
            this, "PreeditMode", _("预编辑模式"), PreeditMode::ComposingPinyin};

        // Switch IM behavior
        OptionWithAnnotation<SwitchInputMethodBehavior,
                             SwitchInputMethodBehaviorI18NAnnotation>
            switchInputMethodBehavior{this, "SwitchInputMethodBehavior",
                                      _("切换输入法时"),
                                      SwitchInputMethodBehavior::CommitPreedit};

        // Key bindings
        KeyListOption prevPage{
            this, "PrevPage", _("上一页"),
            {Key(FcitxKey_minus), Key(FcitxKey_Up), Key(FcitxKey_Page_Up)},
            KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
        KeyListOption nextPage{
            this, "NextPage", _("下一页"),
            {Key(FcitxKey_equal), Key(FcitxKey_Down), Key(FcitxKey_Page_Down)},
            KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
        KeyListOption prevCandidate{
            this, "PrevCandidate", _("上一候选"),
            {Key("Shift+Tab")},
            KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
        KeyListOption nextCandidate{
            this, "NextCandidate", _("下一候选"),
            {Key("Tab")},
            KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
        KeyListOption currentCandidate{
            this, "CurrentCandidate", _("选择当前候选"),
            {Key(FcitxKey_space)},
            KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
        KeyListOption commitRawInput{
            this, "CommitRawInput", _("提交原始输入"),
            {Key("Return"), Key("KP_Enter")},
            KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};

        const char *typeName() const override { return "SimeConfig"; }
    };

    const Config &config() const { return config_; }

private:
    void initSime();
    void updateUI(InputContext *ic);
    void resetState(InputContext *ic);
    SimeState *state(InputContext *ic);
    std::string commitText(InputContext *ic) const;
    Instance *instance_;
    FactoryFor<SimeState> factory_;
    std::unique_ptr<sime::Sime> sime_;
    Config config_;

    FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());
};

class SimeAddonFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new Sime(manager->instance());
    }
};

} // namespace fcitx

#endif // FCITX5_SIME_ENGINE_H
