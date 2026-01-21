#ifndef FCITX5_SIME_ENGINE_H
#define FCITX5_SIME_ENGINE_H

#include "sime-state.h"
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <memory>

// Sime 核心库
#include "interpret.h"

namespace fcitx {

class SimeEngine final : public InputMethodEngineV2 {
public:
    SimeEngine(Instance *instance);
    ~SimeEngine() override;

    // InputMethodEngine 接口实现
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;

    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;

    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;

    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;

    // 获取配置描述（用于 fcitx5-configtool）
    const Configuration *getConfig() const override { return &config_; }

    // 设置配置
    void setConfig(const RawConfig &config) override {
        config_.load(config);
        safeSaveAsIni(config_, "conf/sime.conf");
    }

    // 重新加载配置
    void reloadConfig() override;

    // 获取工厂名称
    std::string addonName() const { return "sime"; }

private:
    // 内部方法
    void initializeInterpreter();
    void updateCandidates(InputContext *ic);
    void selectCandidate(InputContext *ic, int index);
    void commitPreedit(InputContext *ic);
    void clearPreedit(InputContext *ic);
    SimeState *state(InputContext *ic);

    // Fcitx5 实例
    Instance *instance_;

    // 工厂用于创建 SimeState
    FactoryFor<SimeState> factory_;

    // Sime 核心库
    std::unique_ptr<sime::Interpreter> interpreter_;

    // 配置
    struct Config : public Configuration {
        Option<int> numCandidates{this, "NumCandidates", "候选词数量", 5};
        Option<std::string> dictPath{this, "DictPath", "词典路径",
                                     "/usr/share/sime/pydict_sc.ime.bin"};
        Option<std::string> lmPath{this, "LMPath", "语言模型路径",
                                   "/usr/share/sime/lm_sc.t3g"};
    };
    Config config_;
};

class SimeEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new SimeEngine(manager->instance());
    }
};

} // namespace fcitx

#endif // FCITX5_SIME_ENGINE_H
