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

// Pack DecodeResult vector as triplets: [text, units, cnt, text, units, cnt, ...]
jobjectArray packResults(JNIEnv* env, jclass stringClass,
                         const std::vector<sime::DecodeResult>& results) {
    auto arr = env->NewObjectArray(
        static_cast<jsize>(results.size() * 3), stringClass, nullptr);
    for (std::size_t i = 0; i < results.size(); ++i) {
        jsize base = static_cast<jsize>(i * 3);
        env->SetObjectArrayElement(arr, base,
            env->NewStringUTF(results[i].text.c_str()));
        env->SetObjectArrayElement(arr, base + 1,
            env->NewStringUTF(results[i].units.c_str()));
        env->SetObjectArrayElement(arr, base + 2,
            env->NewStringUTF(std::to_string(results[i].cnt).c_str()));
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


// 3. DecodeSentence: returns triplets [text, units, cnt, ...]
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

// 4. DecodeNumSentence: returns triplets [text, units, cnt, ...]
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

} // extern "C"
