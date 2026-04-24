// JNI bridge for SIME input method engine

#include "sime.h"
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

std::string jstringToString(JNIEnv* env, jstring js) {
    if (!js) return {};
    const char* chars = env->GetStringUTFChars(js, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(js, chars);
    return result;
}

// Pack DecodeResult vector as quads: [text, units, cnt, tokenIds, ...]
// tokenIds is a comma-separated string of token IDs, e.g. "92703,12345"
jobjectArray packResults(JNIEnv* env, jclass stringClass,
                         const std::vector<sime::DecodeResult>& results) {
    auto arr = env->NewObjectArray(
        static_cast<jsize>(results.size() * 4), stringClass, nullptr);
    for (std::size_t i = 0; i < results.size(); ++i) {
        jsize base = static_cast<jsize>(i * 4);
        env->SetObjectArrayElement(arr, base,
            env->NewStringUTF(results[i].text.c_str()));
        env->SetObjectArrayElement(arr, base + 1,
            env->NewStringUTF(results[i].units.c_str()));
        env->SetObjectArrayElement(arr, base + 2,
            env->NewStringUTF(std::to_string(results[i].cnt).c_str()));
        // Token IDs
        std::string ids;
        for (std::size_t j = 0; j < results[i].tokens.size(); ++j) {
            if (j > 0) ids += ',';
            ids += std::to_string(results[i].tokens[j]);
        }
        env->SetObjectArrayElement(arr, base + 3,
            env->NewStringUTF(ids.c_str()));
    }
    return arr;
}

} // namespace

extern "C" {

// 1. Load trie + LM resources.
JNIEXPORT jboolean JNICALL
Java_com_semantic_sime_SimeEngine_nativeLoadResources(
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


// 3. DecodeSentence: returns quads [text, units, cnt, tokenIds, ...]
JNIEXPORT jobjectArray JNICALL
Java_com_semantic_sime_SimeEngine_nativeDecodeSentence(
    JNIEnv* env, jclass /*clazz*/,
    jstring input, jint extra) {

    jclass stringClass = env->FindClass("java/lang/String");
    if (!g_sime || !g_sime->Ready())
        return env->NewObjectArray(0, stringClass, nullptr);

    auto input_str = jstringToString(env, input);
    std::size_t e = extra > 0 ? static_cast<std::size_t>(extra) : 0;
    auto results = g_sime->DecodeSentence(input_str, e);

    return packResults(env, stringClass, results);
}

// 4. DecodeNumSentence: returns quads [text, units, cnt, tokenIds, ...]
JNIEXPORT jobjectArray JNICALL
Java_com_semantic_sime_SimeEngine_nativeDecodeNumSentence(
    JNIEnv* env, jclass /*clazz*/,
    jstring prefixLetters, jstring digits, jint extra) {

    jclass stringClass = env->FindClass("java/lang/String");
    if (!g_sime || !g_sime->Ready())
        return env->NewObjectArray(0, stringClass, nullptr);

    auto prefix = jstringToString(env, prefixLetters);
    auto d = jstringToString(env, digits);
    std::size_t e = extra > 0 ? static_cast<std::size_t>(extra) : 0;
    auto results = g_sime->DecodeNumSentence(d, prefix, e);

    return packResults(env, stringClass, results);
}

// 5. Check if engine is ready.
JNIEXPORT jboolean JNICALL
Java_com_semantic_sime_SimeEngine_nativeIsReady(
    JNIEnv* /*env*/, jclass /*clazz*/) {
    return (g_sime && g_sime->Ready()) ? JNI_TRUE : JNI_FALSE;
}

// 6. NextTokens: prediction based on context token IDs.
JNIEXPORT jobjectArray JNICALL
Java_com_semantic_sime_SimeEngine_nativeNextTokens(
    JNIEnv* env, jclass /*clazz*/,
    jintArray contextIds, jint limit, jboolean enOnly) {

    jclass stringClass = env->FindClass("java/lang/String");
    if (!g_sime || !g_sime->Ready())
        return env->NewObjectArray(0, stringClass, nullptr);

    jsize len = env->GetArrayLength(contextIds);
    std::vector<sime::TokenID> ctx(static_cast<std::size_t>(len));
    env->GetIntArrayRegion(contextIds, 0, len,
        reinterpret_cast<jint*>(ctx.data()));

    auto results = g_sime->NextTokens(ctx,
        static_cast<std::size_t>(limit),
        static_cast<bool>(enOnly));

    return packResults(env, stringClass, results);
}

// 7. ContextSize: max context tokens the LM can use (num - 1).
JNIEXPORT jint JNICALL
Java_com_semantic_sime_SimeEngine_nativeContextSize(
    JNIEnv* /*env*/, jclass /*clazz*/) {
    return (g_sime && g_sime->Ready()) ? g_sime->ContextSize() : 2;
}

// 8. GetTokens: prefix completion from the dictionary trie.
JNIEXPORT jobjectArray JNICALL
Java_com_semantic_sime_SimeEngine_nativeGetTokens(
    JNIEnv* env, jclass /*clazz*/,
    jstring prefix, jint limit, jboolean enOnly) {

    jclass stringClass = env->FindClass("java/lang/String");
    if (!g_sime || !g_sime->Ready())
        return env->NewObjectArray(0, stringClass, nullptr);

    auto prefix_str = jstringToString(env, prefix);
    auto results = g_sime->GetTokens(prefix_str,
        static_cast<std::size_t>(limit),
        static_cast<bool>(enOnly));

    return packResults(env, stringClass, results);
}

} // extern "C"
