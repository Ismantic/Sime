#include "sime.h"
#include "ustr.h"
#include <chrono>
#include <filesystem>
#include <map>
#include <unordered_set>
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
    auto sym = key.sym();

    // 获取候选词列表（用于翻页）
    auto &inputPanel = ic->inputPanel();
    auto candidateList = inputPanel.candidateList();
    auto *pageable = candidateList ? candidateList->toPageable() : nullptr;

    // ===== 上下键翻页 =====

    // 下键 → 下一页
    if (!state->isEmpty() && pageable && key.check(FcitxKey_Down)) {
        if (pageable->hasNext()) {
            pageable->next();
            state->setCurrentPage(state->getCurrentPage() + 1);
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            keyEvent.filterAndAccept();
            return;
        }
    }

    // 上键 → 上一页
    if (!state->isEmpty() && pageable && key.check(FcitxKey_Up)) {
        if (pageable->hasPrev()) {
            pageable->prev();
            state->setCurrentPage(state->getCurrentPage() - 1);
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            keyEvent.filterAndAccept();
            return;
        }
    }

    // 处理字母输入 (a-z)
    if (sym >= FcitxKey_a && sym <= FcitxKey_z) {
        state->appendPinyin(static_cast<char>(sym));
        updateCandidates(ic);
        keyEvent.filterAndAccept();
        return;
    }

    // 处理数字选词 (1-9) - 使用页码支持翻页
    if (sym >= FcitxKey_1 && sym <= FcitxKey_9) {
        if (!state->isEmpty()) {
            int keyIdx = sym - FcitxKey_1;  // 0-8 (对应按键 1-9)
            int currentPage = state->getCurrentPage();
            int pageSize = *config_.pageSize;

            // 计算当前页的起始索引
            int pageStart = currentPage * pageSize;

            // 计算要选择的候选的全局索引
            int targetIdx = pageStart + keyIdx;

            // 确保索引有效
            const auto &candidates = state->candidates();
            if (targetIdx >= 0 && targetIdx < static_cast<int>(candidates.size())) {
                selectCandidate(ic, targetIdx);
                keyEvent.filterAndAccept();
                return;
            }
        }
    }

    // 处理空格 - 选择第一个候选词或提交
    if (key.check(FcitxKey_space)) {
        if (!state->isEmpty()) {
            // 有拼音，选择第一个候选
            selectCandidate(ic, 0);
            keyEvent.filterAndAccept();
            return;
        } else if (state->hasSelections()) {
            // 没有拼音但有选择历史，提交所有已选文字
            ic->commitString(state->getCommittedText());
            state->reset();
            clearPreedit(ic);
            keyEvent.filterAndAccept();
            return;
        }
    }

    // 处理退格 - 删除拼音字符或撤销选择
    if (key.check(FcitxKey_BackSpace)) {
        // 优先级 1: 拼音缓冲区不为空，删除最后一个字符
        if (!state->isEmpty()) {
            state->deleteLast();
            if (state->isEmpty() && !state->hasSelections()) {
                clearPreedit(ic);
            } else {
                updateCandidates(ic);
            }
            keyEvent.filterAndAccept();
            return;
        }

        // 优先级 2: 拼音缓冲区为空但有选择历史，撤销最后一次选择
        if (state->hasSelections()) {
            if (state->getManager()->UndoLastSelection()) {
                state->clearCache();
                updateCandidates(ic);
                keyEvent.filterAndAccept();
                return;
            }
        }

        // 优先级 3: 都为空，不处理（传递给应用程序）
    }

    // 处理 Enter - 提交所有内容
    if (key.check(FcitxKey_Return) || key.check(FcitxKey_KP_Enter)) {
        if (!state->isEmpty() || state->hasSelections()) {
            // 提交已选文字 + 当前拼音
            std::string toCommit = state->getCommittedText() + state->pinyinBuffer();
            ic->commitString(toCommit);
            state->reset();
            clearPreedit(ic);
            keyEvent.filterAndAccept();
            return;
        }
    }

    // 处理 Escape - 清空所有输入
    if (key.check(FcitxKey_Escape)) {
        if (!state->isEmpty() || state->hasSelections()) {
            state->reset();  // 清空拼音和选择历史
            clearPreedit(ic);
            keyEvent.filterAndAccept();
            return;
        }
    }
}

void SimeEngine::updateCandidates(InputContext *ic) {
    auto *state = this->state(ic);
    auto &inputPanel = ic->inputPanel();

    // 如果没有拼音输入且没有选择历史，清空面板
    if (state->isEmpty() && !state->hasSelections()) {
        clearPreedit(ic);
        return;
    }

    const auto& pinyin = state->pinyinBuffer();

    // 重置页码（当拼音变化时）
    state->resetPage();

    // 检查缓存
    if (!pinyin.empty() && pinyin == state->cachedPinyin() &&
        !state->candidates().empty()) {
        goto update_panel;
    }

    // 生成候选词
    if (!pinyin.empty() && interpreter_ && interpreter_->Ready()) {
        auto t_start = std::chrono::high_resolution_clock::now();

        std::vector<std::size_t> prefixes = findAllValidPrefixes(pinyin);

        auto t_prefixes = std::chrono::high_resolution_clock::now();
        auto dur_prefixes = std::chrono::duration_cast<std::chrono::milliseconds>(t_prefixes - t_start).count();

        if (prefixes.empty()) {
            // 无有效拼音前缀
            state->setCandidates({});
            state->setCachedPinyin(pinyin);
            goto update_panel;
        }

        std::vector<SimeCandidate> all_candidates;
        std::unordered_set<std::string> seen_texts;  // 去重

        // 为每个有效前缀生成候选
        // 限制处理时间：如果前缀太多，只处理最重要的几个
        std::size_t max_prefixes_to_process = 3;  // 最多处理3个前缀
        std::size_t processed = 0;
        
        for (std::size_t prefix_len : prefixes) {
            if (processed >= max_prefixes_to_process) {
                break;
            }
            processed++;
            
            std::string prefix = pinyin.substr(0, prefix_len);
            bool is_complete = (prefix_len == pinyin.size());

            sime::DecodeOptions options;
            // 减少候选数量以提高性能
            // 完整匹配请求更多候选，部分匹配请求较少
            options.num = is_complete ? 100 : 30;

            auto t_decode_start = std::chrono::high_resolution_clock::now();
            auto results = interpreter_->DecodeText(prefix, options);
            auto t_decode_end = std::chrono::high_resolution_clock::now();
            auto dur_decode = std::chrono::duration_cast<std::chrono::milliseconds>(t_decode_end - t_decode_start).count();

            // 如果解码时间太长，记录警告
            if (dur_decode > 100) {
                FCITX_INFO() << "DecodeText('" << prefix << "') took " << dur_decode << "ms (slow!)";
            }

            for (const auto& result : results) {
                std::string text = sime::ustr::FromU32(result.text);

                // 去重
                if (seen_texts.count(text)) {
                    continue;
                }
                seen_texts.insert(text);

                all_candidates.emplace_back(
                    text,
                    result.score,
                    0,  // 稍后重新索引
                    prefix_len,
                    is_complete ? MatchCategory::COMPLETE_MATCH
                                : MatchCategory::PREFIX_MATCH
                );
            }
        }

        // 排序：完整匹配优先 -> 词长优先 -> 得分优先
        std::sort(all_candidates.begin(), all_candidates.end(),
            [](const SimeCandidate& a, const SimeCandidate& b) {
                if (a.category != b.category) {
                    return a.category < b.category;  // COMPLETE < PREFIX
                }
                if (a.matched_length != b.matched_length) {
                    return a.matched_length > b.matched_length;  // 更长的在前
                }
                return a.score > b.score;  // 更高分在前
            });

        // 优化候选数量：按前缀长度分组过滤
        // 单字保留全部，多字词每个前缀限制数量
        std::vector<SimeCandidate> filtered_candidates;
        std::size_t single_char_count = 0;
        std::size_t multi_char_count = 0;
        constexpr std::size_t MAX_MULTI_CHAR_PER_PREFIX = 5;  // 每个前缀的多字词最多5个
        constexpr double SCORE_THRESHOLD = 5.0;               // 词频阈值

        // 按 matched_length 分组
        std::map<std::size_t, std::vector<std::size_t>> groups;
        for (std::size_t i = 0; i < all_candidates.size(); ++i) {
            groups[all_candidates[i].matched_length].push_back(i);
        }

        // 处理每个分组
        for (auto& [prefix_len, indices] : groups) {
            // 找到该分组内多字词的最高得分
            double max_score = -1000.0;
            for (std::size_t idx : indices) {
                const auto& cand = all_candidates[idx];
                // 计算字符数
                std::size_t char_count = 0;
                for (std::size_t i = 0; i < cand.text.size(); ) {
                    unsigned char c = cand.text[i];
                    if (c < 0x80) i += 1;
                    else if (c < 0xE0) i += 2;
                    else if (c < 0xF0) i += 3;
                    else i += 4;
                    char_count++;
                }

                if (char_count > 1 && cand.score > max_score) {
                    max_score = cand.score;
                }
            }

            // 过滤该分组的候选
            std::size_t group_multi_count = 0;
            for (std::size_t idx : indices) {
                auto& cand = all_candidates[idx];
                // 计算字符数
                std::size_t char_count = 0;
                for (std::size_t i = 0; i < cand.text.size(); ) {
                    unsigned char c = cand.text[i];
                    if (c < 0x80) i += 1;
                    else if (c < 0xE0) i += 2;
                    else if (c < 0xF0) i += 3;
                    else i += 4;
                    char_count++;
                }

                if (char_count == 1) {
                    // 单字：保留所有
                    filtered_candidates.push_back(std::move(cand));
                    single_char_count++;
                } else {
                    // 多字词：分组内限制数量+词频阈值
                    bool count_ok = group_multi_count < MAX_MULTI_CHAR_PER_PREFIX;
                    bool score_ok = (cand.score >= max_score - SCORE_THRESHOLD);

                    if (count_ok && score_ok) {
                        filtered_candidates.push_back(std::move(cand));
                        multi_char_count++;
                        group_multi_count++;
                    }
                }
            }
        }

        // 重新索引所有候选
        for (size_t i = 0; i < filtered_candidates.size(); ++i) {
            filtered_candidates[i].index = static_cast<int>(i);
        }

        auto t_end = std::chrono::high_resolution_clock::now();
        auto dur_total = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

        // 减少日志输出，避免频繁日志导致性能问题
        // FCITX_INFO() << "updateCandidates('" << pinyin << "') took " << dur_total << "ms";

        state->setCandidates(std::move(filtered_candidates));
        state->setCachedPinyin(pinyin);
    }

update_panel:
    // ===== 更新 Preedit 显示 =====
    Text preedit;

    // 第一部分：已选择的文字（加粗+下划线）
    if (state->hasSelections()) {
        std::string committed = state->getCommittedText();
        preedit.append(committed, TextFormatFlags{TextFormatFlag::Underline, TextFormatFlag::Bold});
    }

    // 第二部分：当前拼音（普通样式）
    if (!state->isEmpty()) {
        preedit.append(state->pinyinBuffer());
    }

    preedit.setCursor(preedit.toString().size());
    inputPanel.setClientPreedit(preedit);

    // ===== 更新候选列表 =====
    auto candidateList = std::make_unique<CommonCandidateList>();
    candidateList->setPageSize(*config_.pageSize);
    candidateList->setLayoutHint(CandidateLayoutHint::Horizontal);

    // 设置选择键（每页从1开始）
    candidateList->setSelectionKey(Key::keyListFromString("1 2 3 4 5 6 7 8 9"));

    const auto &candidates = state->candidates();
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto &cand = candidates[i];
        // 不手动添加标签，让 CandidateList 自动为每页生成
        candidateList->append<SimeCandidateWord>(
            this, cand.text, static_cast<int>(i));
    }

    inputPanel.setCandidateList(std::move(candidateList));
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void SimeEngine::selectCandidate(InputContext *ic, int index) {
    auto *state = this->state(ic);
    const auto &candidates = state->candidates();

    if (index < 0 || index >= static_cast<int>(candidates.size())) {
        return;
    }

    // 使用 SelectionManager 的选词逻辑
    auto result = state->getManager()->SelectCandidate(static_cast<std::size_t>(index));

    if (result.should_commit) {
        // 拼音全部消耗完毕，提交所有已选文字并重置
        ic->commitString(result.text_to_commit);
        state->reset();
        clearPreedit(ic);
    } else {
        // 还有剩余拼音，继续输入
        state->setPinyinBuffer(result.remaining_pinyin);
        state->clearCache();
        updateCandidates(ic);
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

std::vector<std::size_t> SimeEngine::findAllValidPrefixes(const std::string& pinyin) const {
    std::vector<std::size_t> prefixes;
    sime::UnitParser parser;

    // 1. 总是添加完整输入
    prefixes.push_back(pinyin.size());

    // 2. 找到所有有效的分割点
    // 从长到短检查每个前缀，只要前缀和剩余部分都是完整拼音序列，就添加
    for (std::size_t len = pinyin.size() - 1; len > 0; --len) {
        std::string prefix = pinyin.substr(0, len);
        auto prefix_result = parser.ParseTokenEnhanced(prefix, false);

        // 前缀必须是完整的拼音序列
        if (!prefix_result.complete || prefix_result.matched_len != len) {
            // 减少日志输出
        // FCITX_INFO() << "  Checking prefix '" << prefix << "' (len=" << len << "): SKIP";
            continue;
        }

        // 检查剩余部分是否也是完整的拼音序列
        std::string remaining = pinyin.substr(len);
        auto remaining_result = parser.ParseTokenEnhanced(remaining, false);

        // 减少日志输出
        // FCITX_INFO() << "  Prefix '" << prefix << "' valid, remaining '" << remaining << "'";

        // 如果剩余部分也是完整的拼音序列，这就是一个有效的分割点
        if (remaining_result.complete && remaining_result.matched_len == remaining.size()) {
            prefixes.push_back(len);
            // FCITX_INFO() << "  -> Added boundary at " << len;
        }
    }

    // 减少日志输出
    // FCITX_INFO() << "findAllValidPrefixes('" << pinyin << "') found " << prefixes.size() << " prefixes";

    return prefixes;
}

std::string SimeEngine::extractPinyinPrefix(const std::string& pinyin) const {
    // 如果拼音太短，不提取前缀
    if (pinyin.size() <= 2) {
        return "";
    }

    // 使用 UnitParser 的增强解析功能
    sime::UnitParser parser;
    auto parse_result = parser.ParseTokenEnhanced(pinyin, true);

    // 如果完整匹配，不需要前缀
    if (parse_result.complete) {
        return "";
    }

    // 如果有部分匹配，返回匹配的前缀
    if (parse_result.matched_len > 0 && parse_result.matched_len < pinyin.size()) {
        return pinyin.substr(0, parse_result.matched_len);
    }

    return "";
}

} // namespace fcitx

// 注册插件工厂
FCITX_ADDON_FACTORY(fcitx::SimeEngineFactory)
