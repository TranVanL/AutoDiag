#include <jni.h>
#include <android/log.h>

extern "C"
JNIEXPORT void JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeInit(JNIEnv* env, jclass, jstring halType) {
    const char* nativeHalType = env->GetStringUTFChars(halType, nullptr);
    if (nativeHalType != nullptr) {
        __android_log_print(ANDROID_LOG_INFO, "VDiag.JNI", "nativeInit called with type: %s", nativeHalType);
        env->ReleaseStringUTFChars(halType, nativeHalType);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "VDiag.JNI", "nativeInit called with null or invalid halType");
    }
}
