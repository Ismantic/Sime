// JNI bridge for SIME input method engine
// ~7 methods mirroring the fcitx5 integration pattern

#include "interpret.h"
#include "ustr.h"

#include <jni.h>
#include <android/log.h>
#include <memory>
#include <string>

#define TAG "SimeJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

std::unique_ptr<sime::Interpreter> g_interpreter;

// UTF-32 → UTF-8
std::string u32ToUtf8(const std::u32string& u32) {
    std::string utf8;
    utf8.reserve(u32.size() * 3);
    for (char32_t ch : u32) {
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

std::string jstringToString(JNIEnv* env, jstring js) {
    if (!js) return {};
    const char* chars = env->GetStringUTFChars(js, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(js, chars);
    return result;
}

} // namespace

extern "C" {

// 1. Load trie + LM resources. Returns true on success.
JNIEXPORT jboolean JNICALL
Java_com_isma_sime_SimeEngine_nativeLoadResources(
    JNIEnv* env, jclass /*clazz*/,
    jstring triePath, jstring modelPath) {

    auto trie = jstringToString(env, triePath);
    auto model = jstringToString(env, modelPath);

    g_interpreter = std::make_unique<sime::Interpreter>();
    if (!g_interpreter->LoadResources(trie, model)) {
        LOGE("Failed to load resources: trie=%s model=%s", trie.c_str(), model.c_str());
        g_interpreter.reset();
        return JNI_FALSE;
    }
    LOGI("Resources loaded: trie=%s model=%s", trie.c_str(), model.c_str());
    return JNI_TRUE;
}

// 2. Load nine-key pinyin model. Returns true on success.
JNIEXPORT jboolean JNICALL
Java_com_isma_sime_SimeEngine_nativeLoadT9(
    JNIEnv* env, jclass /*clazz*/,
    jstring pinyinModelPath) {

    if (!g_interpreter) return JNI_FALSE;
    auto path = jstringToString(env, pinyinModelPath);
    if (!g_interpreter->LoadNine(path)) {
        LOGE("Failed to load nine-key model: %s", path.c_str());
        return JNI_FALSE;
    }
    LOGI("Nine-key model loaded: %s", path.c_str());
    return JNI_TRUE;
}

// 3. Load user dictionary. Returns true on success.
JNIEXPORT jboolean JNICALL
Java_com_isma_sime_SimeEngine_nativeLoadUserDict(
    JNIEnv* env, jclass /*clazz*/,
    jstring userDictPath) {

    if (!g_interpreter) return JNI_FALSE;
    auto path = jstringToString(env, userDictPath);
    if (!g_interpreter->LoadDict(path)) {
        LOGE("Failed to load dict: %s", path.c_str());
        return JNI_FALSE;
    }
    LOGI("Dict loaded: %s", path.c_str());
    return JNI_TRUE;
}

// 4. Decode pinyin → hanzi candidates.
//    Returns String[] where even indices = text, odd indices = matched_len (as string).
JNIEXPORT jobjectArray JNICALL
Java_com_isma_sime_SimeEngine_nativeDecodeSentence(
    JNIEnv* env, jclass /*clazz*/,
    jstring input, jint num) {

    jclass stringClass = env->FindClass("java/lang/String");

    if (!g_interpreter || !g_interpreter->Ready()) {
        return env->NewObjectArray(0, stringClass, nullptr);
    }

    auto pinyin = jstringToString(env, input);
    auto results = g_interpreter->DecodeSentence(
        pinyin, static_cast<std::size_t>(num));

    // Pack as [text0, matchedLen0, text1, matchedLen1, ...]
    auto arr = env->NewObjectArray(
        static_cast<jsize>(results.size() * 2), stringClass, nullptr);
    for (std::size_t i = 0; i < results.size(); ++i) {
        auto text = u32ToUtf8(results[i].text);
        auto lenStr = std::to_string(results[i].matched_len);
        env->SetObjectArrayElement(arr, static_cast<jsize>(i * 2),
                                   env->NewStringUTF(text.c_str()));
        env->SetObjectArrayElement(arr, static_cast<jsize>(i * 2 + 1),
                                   env->NewStringUTF(lenStr.c_str()));
    }
    return arr;
}

// 5. Segment raw pinyin → spaced syllables. e.g. "zenmele" → "zen me le"
JNIEXPORT jstring JNICALL
Java_com_isma_sime_SimeEngine_nativeSegmentPinyin(
    JNIEnv* env, jclass /*clazz*/,
    jstring input) {

    auto raw = jstringToString(env, input);
    auto segmented = sime::Interpreter::SegmentPinyin(raw);
    return env->NewStringUTF(segmented.c_str());
}

// 6. Nine-key: digits → hanzi candidates.
//    Returns String[] same format as DecodeSentence.
JNIEXPORT jobjectArray JNICALL
Java_com_isma_sime_SimeEngine_nativeDecodeT9(
    JNIEnv* env, jclass /*clazz*/,
    jstring digits, jint num) {

    jclass stringClass = env->FindClass("java/lang/String");

    if (!g_interpreter || !g_interpreter->Ready() ||
        !g_interpreter->NineReady()) {
        return env->NewObjectArray(0, stringClass, nullptr);
    }

    auto d = jstringToString(env, digits);
    auto results = g_interpreter->DecodeNine(
        d, static_cast<std::size_t>(num));

    auto arr = env->NewObjectArray(
        static_cast<jsize>(results.size() * 2), stringClass, nullptr);
    for (std::size_t i = 0; i < results.size(); ++i) {
        auto text = u32ToUtf8(results[i].text);
        auto lenStr = std::to_string(results[i].matched_len);
        env->SetObjectArrayElement(arr, static_cast<jsize>(i * 2),
                                   env->NewStringUTF(text.c_str()));
        env->SetObjectArrayElement(arr, static_cast<jsize>(i * 2 + 1),
                                   env->NewStringUTF(lenStr.c_str()));
    }
    return arr;
}

// 7. Nine-key: digits → pinyin candidates.
//    Returns String[] of space-separated pinyin strings, one per interpretation.
JNIEXPORT jobjectArray JNICALL
Java_com_isma_sime_SimeEngine_nativeDecodeT9Pinyin(
    JNIEnv* env, jclass /*clazz*/,
    jstring digits, jint num) {

    jclass stringClass = env->FindClass("java/lang/String");

    if (!g_interpreter || !g_interpreter->Ready() ||
        !g_interpreter->NineReady()) {
        return env->NewObjectArray(0, stringClass, nullptr);
    }

    auto d = jstringToString(env, digits);
    auto results = g_interpreter->DecodeNinePinyin(
        d, static_cast<std::size_t>(num));

    auto arr = env->NewObjectArray(
        static_cast<jsize>(results.size()), stringClass, nullptr);

    for (std::size_t i = 0; i < results.size(); ++i) {
        std::string pinyin;
        for (const auto& unit : results[i].pinyin) {
            const char* py = sime::UnitData::Decode(unit);
            if (py) {
                if (!pinyin.empty()) pinyin += ' ';
                pinyin += py;
            }
        }
        env->SetObjectArrayElement(arr, static_cast<jsize>(i),
                                   env->NewStringUTF(pinyin.c_str()));
    }
    return arr;
}

// 8. Check if engine is ready.
JNIEXPORT jboolean JNICALL
Java_com_isma_sime_SimeEngine_nativeIsReady(
    JNIEnv* /*env*/, jclass /*clazz*/) {
    return (g_interpreter && g_interpreter->Ready()) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
