#include <jni.h>
#include <android/log.h>

namespace {
    JavaVM* g_jvm = nullptr;
    constexpr const char* TAG = "VDiag.JNI";
}

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    __android_log_print(ANDROID_LOG_INFO, TAG, "JNI_OnLoad");
    return JNI_VERSION_1_6;
}

void JNI_OnUnLoad(JavaVM* vm, void* reserved) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "JNI_OnUnLoad");
    g_jvm = nullptr;
}