#ifndef FCITX5_SIME_ENGINE_H
#define FCITX5_SIME_ENGINE_H

#include "sime-state.h"
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <memory>

#include "interpret.h"

namespace fcitx {

class SimeEngine final : public InputMethodEngineV2 {
public:
    SimeEngine(Instance *instance);
    ~SimeEngine() override;

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
        entry.setLabel("是").setIcon("fcitx-pinyin").setConfigurable(true);
        result.push_back(std::move(entry));
        return result;
    }

private:
    void initInterpreter();
    void updateUI(InputContext *ic);
    void resetState(InputContext *ic);
    SimeState *state(InputContext *ic);

    Instance *instance_;
    FactoryFor<SimeState> factory_;
    std::unique_ptr<sime::Interpreter> interpreter_;

    struct Config : public Configuration {
        Option<int> pageSize{this, "PageSize", "每页候选数", 9};
        Option<int> nbest{this, "NBest", "候选总数", 18};
        Option<std::string> dictPath{this, "DictPath", "词典路径",
                                     "/usr/share/sime/trie.bin"};
        Option<std::string> lmPath{this, "LMPath", "语言模型路径",
                                   "/usr/share/sime/model.bin"};

        const char *typeName() const override { return "SimeConfig"; }
    };
    Config config_;

    friend class SimeCandidateWord;
};

class SimeEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new SimeEngine(manager->instance());
    }
};

} // namespace fcitx

#endif // FCITX5_SIME_ENGINE_H
