// JNI bridge for SIME input method engine
//
// Decode methods store results in g_results; Java reads them back
// through typed accessors (nativeResultText, nativeResultConsumed,
// nativeResultTokenIds) instead of marshalling a String[] quad with
// CSV-encoded token IDs.

#include "sime.h"
#include "dict.h"
#include "ustr.h"

#include <jni.h>
#include <android/log.h>
#include <memory>
#include <string>

#define TAG "SimeJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

std::unique_ptr<sime::Sime> g_sime;
std::vector<sime::DecodeResult> g_results;

std::string jstringToString(JNIEnv* env, jstring js) {
    if (!js) return {};
    const char* chars = env->GetStringUTFChars(js, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(js, chars);
    return result;
}

} // namespace

extern "C" {

// ===== Lifecycle =====

JNIEXPORT jboolean JNICALL
Java_com_shiyu_sime_SimeEngine_nativeLoadResources(
    JNIEnv* env, jclass /*clazz*/,
    jstring triePath, jstring modelPath) {

    auto trie = jstringToString(env, triePath);
    auto model = jstringToString(env, modelPath);

    g_sime = std::make_unique<sime::Sime>(trie, model);
    if (!g_sime->Ready()) {
        LOGE("Failed to load resources: trie=%s model=%s", trie.c_str(), model.c_str());
        g_sime.reset();
        return JNI_FALSE;
    }
    LOGI("Resources loaded: trie=%s model=%s", trie.c_str(), model.c_str());
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_shiyu_sime_SimeEngine_nativeIsReady(
    JNIEnv* /*env*/, jclass /*clazz*/) {
    return (g_sime && g_sime->Ready()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_shiyu_sime_SimeEngine_nativeContextSize(
    JNIEnv* /*env*/, jclass /*clazz*/) {
    return (g_sime && g_sime->Ready()) ? g_sime->ContextSize() : 2;
}

JNIEXPORT void JNICALL
Java_com_shiyu_sime_SimeEngine_nativeResetCaches(
    JNIEnv* /*env*/, jclass /*clazz*/) {
    if (g_sime && g_sime->Ready()) g_sime->ResetCaches();
}

// ===== Decode (store results, return count) =====

JNIEXPORT jint JNICALL
Java_com_shiyu_sime_SimeEngine_nativeDecodeSentence(
    JNIEnv* env, jclass /*clazz*/,
    jstring input, jint extra) {

    if (!g_sime || !g_sime->Ready()) { g_results.clear(); return 0; }
    auto input_str = jstringToString(env, input);
    std::size_t e = extra > 0 ? static_cast<std::size_t>(extra) : 0;
    g_results = g_sime->DecodeSentence(input_str, e);
    return static_cast<jint>(g_results.size());
}

JNIEXPORT jint JNICALL
Java_com_shiyu_sime_SimeEngine_nativeDecodeNumSentence(
    JNIEnv* env, jclass /*clazz*/,
    jstring prefixLetters, jstring digits, jint extra) {

    if (!g_sime || !g_sime->Ready()) { g_results.clear(); return 0; }
    auto prefix = jstringToString(env, prefixLetters);
    auto d = jstringToString(env, digits);
    std::size_t e = extra > 0 ? static_cast<std::size_t>(extra) : 0;
    g_results = g_sime->DecodeNumSentence(d, prefix, e);
    return static_cast<jint>(g_results.size());
}

JNIEXPORT jint JNICALL
Java_com_shiyu_sime_SimeEngine_nativeNextTokens(
    JNIEnv* env, jclass /*clazz*/,
    jintArray contextIds, jint limit, jboolean enOnly) {

    if (!g_sime || !g_sime->Ready()) { g_results.clear(); return 0; }
    jsize len = env->GetArrayLength(contextIds);
    std::vector<sime::TokenID> ctx(static_cast<std::size_t>(len));
    env->GetIntArrayRegion(contextIds, 0, len,
        reinterpret_cast<jint*>(ctx.data()));
    g_results = g_sime->NextTokens(ctx,
        static_cast<std::size_t>(limit),
        static_cast<bool>(enOnly));
    return static_cast<jint>(g_results.size());
}

JNIEXPORT jint JNICALL
Java_com_shiyu_sime_SimeEngine_nativeGetTokens(
    JNIEnv* env, jclass /*clazz*/,
    jstring prefix, jint limit, jboolean enOnly) {

    if (!g_sime || !g_sime->Ready()) { g_results.clear(); return 0; }
    auto prefix_str = jstringToString(env, prefix);
    g_results = g_sime->GetTokens(prefix_str,
        static_cast<std::size_t>(limit),
        static_cast<bool>(enOnly));
    return static_cast<jint>(g_results.size());
}

JNIEXPORT jobjectArray JNICALL
Java_com_shiyu_sime_SimeEngine_nativeT9PinyinSyllables(
    JNIEnv* env, jclass /*clazz*/,
    jstring digits, jint limit) {

    auto d = jstringToString(env, digits);
    std::size_t max = limit > 0 ? static_cast<std::size_t>(limit) : 0;
    auto alts = sime::Dict::T9PinyinSyllables(d, max);

    jclass string_class = env->FindClass("java/lang/String");
    jstring empty = env->NewStringUTF("");
    jobjectArray arr = env->NewObjectArray(
        static_cast<jsize>(alts.size()), string_class, empty);
    env->DeleteLocalRef(empty);
    for (std::size_t i = 0; i < alts.size(); ++i) {
        jstring s = env->NewStringUTF(alts[i].c_str());
        env->SetObjectArrayElement(arr, static_cast<jsize>(i), s);
        env->DeleteLocalRef(s);
    }
    return arr;
}

// ===== Result accessors =====

JNIEXPORT jstring JNICALL
Java_com_shiyu_sime_SimeEngine_nativeResultText(
    JNIEnv* env, jclass /*clazz*/, jint index) {
    auto i = static_cast<std::size_t>(index);
    if (i >= g_results.size()) return env->NewStringUTF("");
    return env->NewStringUTF(g_results[i].text.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_shiyu_sime_SimeEngine_nativeResultUnits(
    JNIEnv* env, jclass /*clazz*/, jint index) {
    auto i = static_cast<std::size_t>(index);
    if (i >= g_results.size()) return env->NewStringUTF("");
    return env->NewStringUTF(g_results[i].units.c_str());
}

JNIEXPORT jint JNICALL
Java_com_shiyu_sime_SimeEngine_nativeResultConsumed(
    JNIEnv* /*env*/, jclass /*clazz*/, jint index) {
    auto i = static_cast<std::size_t>(index);
    if (i >= g_results.size()) return 0;
    return static_cast<jint>(g_results[i].cnt);
}

JNIEXPORT jintArray JNICALL
Java_com_shiyu_sime_SimeEngine_nativeResultTokenIds(
    JNIEnv* env, jclass /*clazz*/, jint index) {
    auto i = static_cast<std::size_t>(index);
    if (i >= g_results.size()) return env->NewIntArray(0);
    const auto& tokens = g_results[i].tokens;
    jintArray arr = env->NewIntArray(static_cast<jsize>(tokens.size()));
    if (!tokens.empty()) {
        env->SetIntArrayRegion(arr, 0, static_cast<jsize>(tokens.size()),
            reinterpret_cast<const jint*>(tokens.data()));
    }
    return arr;
}

} // extern "C"
