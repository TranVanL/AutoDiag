#include <jni.h>
#include <android/log.h>
#include <string>
#include "jni_callback.h"


static std::string getDummyResponse(int propertyId);

extern "C"
JNIEXPORT void JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeInit(
        JNIEnv* env, jclass, jstring halType) {

    const char* nativeHalType = env->GetStringUTFChars(halType, nullptr);
    if (nativeHalType != nullptr) {
        __android_log_print(ANDROID_LOG_INFO, "VDiag.JNI",
                            "nativeInit called with type: %s", nativeHalType);
        env->ReleaseStringUTFChars(halType, nativeHalType);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeGetProperty(JNIEnv* env, jclass, jint requestId , jint propertyID , jbyteArray payload , jobject callback) {
    (void)payload;
    __android_log_print(ANDROID_LOG_INFO, "VDiag.JNI",
                        "nativeGetProperty: reqId=%d, propId=0x%X", requestId, propertyID);

    if (callback == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "VDiag.JNI",
                            "nativeGetProperty: callback is null");
        return;
    }

    std::string dummyResponse = getDummyResponse(propertyID);
    __android_log_print(ANDROID_LOG_INFO, "VDiag.JNI",
                        "nativeGetProperty: dummyResponse=%s", dummyResponse.c_str());

    // Use JniCallbackBridge to handle the callback logic
    JniCallbackBridge bridge(env, callback);
    bridge.onResult(requestId, dummyResponse, 30000);

    __android_log_print(ANDROID_LOG_INFO, "VDiag.JNI","nativeGetProperty: bridge.onResult called");
}

static std::string getDummyResponse(int propertyId) {
    switch (propertyId) {
        case 0xF190:  // Vehicle Identification Number
            return "VINFAST12345678901";
        case 0xF187:  // Software version
            return "SW_V3.2.1_AAOS_HKMC";
        case 0xFD01:  // Battery SOC %
            return "78";
        case 0xFE01:  // Motor RPM
            return "3200";
        case 0x0104:  // Engine temp
            return "92";
        case 0x010C:  // RPM (OBD mode 01)
            return "1500";
        default:
            return "UNKNOWN_PROPERTY_0x" + std::to_string(propertyId);
    }
}