#include <jni.h>
#include <android/log.h>
#include <cstddef>

JavaVM* g_jvm = nullptr;
jclass g_callbackClass = nullptr;
jmethodID g_onResultId = nullptr;
jmethodID g_onErrorId = nullptr;

namespace {
    constexpr const char* TAG = "VDiag.JNI";
    constexpr const char* CALLBACK_CLASS_NAME = "com/vdiag/IDiagCallback";
}

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env) , JNI_VERSION_1_6) != JNI_OK) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "JNI_OnLoad failed");
        return -1;
    }

    jclass localCallbackClass = env->FindClass(CALLBACK_CLASS_NAME);
    if (localCallbackClass == nullptr) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "Failed to find callback class");
        return -1;
    }
    g_callbackClass = reinterpret_cast<jclass>(env->NewGlobalRef(localCallbackClass));
    env->DeleteLocalRef(localCallbackClass);

    g_onResultId = env->GetMethodID(g_callbackClass, "onResult", "(ILjava/lang/String;J)V");
    if (g_onResultId == nullptr) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "Failed to find onResult method");
        return -1;
    }

    g_onErrorId = env->GetMethodID(g_callbackClass, "onError", "(IILjava/lang/String;)V");
    if (g_onErrorId == nullptr) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "Failed to find onError method");
        return -1;
    }

    __android_log_print(ANDROID_LOG_INFO, TAG, "JNI_OnLoad successfully with caching callback class and two methods");
    return JNI_VERSION_1_6;
}

void JNI_OnUnLoad(JavaVM* vm, void* reserved) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "JNI_OnUnLoad");
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        if (g_callbackClass != nullptr) {
            env->DeleteGlobalRef(g_callbackClass);
            g_callbackClass = nullptr;
        }
    }
    g_jvm = nullptr;
}