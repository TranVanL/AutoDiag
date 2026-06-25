#include "jni_callback.h"
#include <pthread.h>
#include <android/log.h>

extern JavaVM* g_jvm;
extern jmethodID g_onResultId;
extern jmethodID g_onErrorId;

namespace {
    constexpr const char* TAG = "VDiag.JNI";
    pthread_key_t g_detachKey;
    pthread_once_t g_detachKeyOnce = PTHREAD_ONCE_INIT;

    void DetachThunk(void* arg) {
        if (g_jvm != nullptr) {
            g_jvm->DetachCurrentThread();
        }
    }

    void CreateDetachKey() {
        const int rc = pthread_key_create(&g_detachKey, DetachThunk);
        if (rc != 0 ) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: pthread_key_create failed");
        }
    }
}

JniCallbackBridge::JniCallbackBridge(JNIEnv* env, jobject callback) {
    if (env == nullptr || callback == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: env or callback is null");
        m_callback = nullptr;
        return;
    }

    m_callback = env->NewGlobalRef(callback);
    if (m_callback == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: NewGlobalRef failed");
        return;
    }

    __android_log_print(ANDROID_LOG_INFO, TAG, "JniCallbackBridge created");
}

JniCallbackBridge::JniCallbackBridge(JniCallbackBridge &&other) noexcept{
    m_callback = other.m_callback;
    other.m_callback = nullptr;
    __android_log_print(ANDROID_LOG_INFO, TAG, "JniCallbackBridge moved");

}

JniCallbackBridge::~JniCallbackBridge() noexcept {
    if (m_callback == nullptr) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "JniCallbackBridge destroyed (already null)");
        return;
    }

    JNIEnv* env = getEnv();
    if (env == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: getEnv failed in destructor");
        return;
    }

    env->DeleteGlobalRef(m_callback);
    m_callback = nullptr;
    __android_log_print(ANDROID_LOG_INFO, TAG, "JniCallbackBridge destroyed");

}


void JniCallbackBridge::onError(int requestId, int errorCode, const std::string& errorMsg) {
    if (m_callback == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: callback is null");
        return;
    }

    JNIEnv* env = getEnv();
    if (env == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: Env is null");
        return;
    }

    jstring jmsg = env->NewStringUTF(errorMsg.c_str());

    if (jmsg == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: NewStringUTF failed");
        return;
    }


    env->CallVoidMethod(m_callback, g_onErrorId, static_cast<jint>(requestId), static_cast<jint>(errorCode), jmsg);
    if (env->ExceptionCheck()) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "onError: exception from callback");
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(jmsg);
}

void JniCallbackBridge::onResult(int requestId, const std::string &value, int64_t latencyUs) {
    if (m_callback == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: callback is null");
        return;
    }

    JNIEnv* env = getEnv();
    if (env == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: Env is null");
        return;
    }

    jstring jval = env->NewStringUTF(value.c_str());

    if (jval == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: NewStringUTF failed");
        return;
    }

    env->CallVoidMethod(m_callback, g_onResultId, static_cast<jint>(requestId), jval, static_cast<jlong>(latencyUs));
    if (env->ExceptionCheck()) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "onResult: exception from callback");
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(jval);
}

JNIEnv* JniCallbackBridge::getEnv() {
    if (g_jvm == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: g_jvm is null");
        return nullptr;
    }

    JNIEnv *env = nullptr;
    const jint status = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_OK) return env;
    if (status == JNI_EDETACHED) {
        const jint attachStatus = g_jvm->AttachCurrentThread(&env, nullptr);
        if (attachStatus != JNI_OK || env == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: AttachCurrentThread failed");
            return nullptr;
        }
        pthread_once(&g_detachKeyOnce, CreateDetachKey);
        pthread_setspecific(g_detachKey,reinterpret_cast<void*>(1));
        __android_log_print(ANDROID_LOG_INFO, TAG, "JniCallbackBridge: Attached to thread and register auto-detach ");
        return env;

    }
    __android_log_print(ANDROID_LOG_ERROR, TAG, "JniCallbackBridge: GetEnv failed");
    return nullptr;
    }