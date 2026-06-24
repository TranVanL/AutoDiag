#include <jni.h>
#include <android/log.h>
#include <string>

extern JavaVM* g_jvm;
extern jclass g_callbackClass;
extern jmethodID g_onResultId;
extern jmethodID g_onErrorId;

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

    if (callback == nullptr || g_onResultId == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "VDiag.JNI",
                            "nativeGetProperty: callback or g_onResultId is null");
        return;
    }

    std::string dummyResponse = getDummyResponse(propertyID);
    __android_log_print(ANDROID_LOG_INFO, "VDiag.JNI",
                        "nativeGetProperty: dummyResponse=%s", dummyResponse.c_str());
    jstring jval = env->NewStringUTF(dummyResponse.c_str());
    if (jval == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "VDiag.JNI",
                            "nativeGetProperty: NewStringUTF returned null");
        return;
    }
    env->CallVoidMethod(callback,g_onResultId , requestId , jval , (jlong)30000);
    if (env->ExceptionCheck()) {
        __android_log_print(ANDROID_LOG_ERROR, "VDiag.JNI",
                            "nativeGetProperty: exception during CallVoidMethod");
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    env->DeleteLocalRef(jval);
    __android_log_print(ANDROID_LOG_INFO, "VDiag.JNI","nativeGetProperty callback fired!!");
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