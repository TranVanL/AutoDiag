#include <jni.h>
#include <android/log.h>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include "jni_callback.h"
#include "diag_engine.h"
#include "mock_diag_hal.h"

#define JNI_TAG "VDiag.JNI"

static std::unique_ptr<autodiag::DiagEngine> g_engine;

static std::string getDummyResponse(int propertyId);

extern "C"
JNIEXPORT void JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeInit(
        JNIEnv* env, jclass, jstring halType) {

    const char* nativeHalType = env->GetStringUTFChars(halType, nullptr);
    if (nativeHalType == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG, "nativeInit: halType is null");
        return;
    }
    const std::string halTypeStr(nativeHalType);
    env->ReleaseStringUTFChars(halType, nativeHalType);

    if (g_engine != nullptr) {
        __android_log_print(ANDROID_LOG_WARN, JNI_TAG,
                            "nativeInit: engine already running, skipping re-init");
        return;
    }

    __android_log_print(ANDROID_LOG_INFO, JNI_TAG,
                        "nativeInit: creating DiagEngine with halType=%s", halTypeStr.c_str());

    auto hal = std::make_unique<autodiag::MockDiagnosticHal>();
    g_engine = std::make_unique<autodiag::DiagEngine>(std::move(hal));
    g_engine->start();

    __android_log_print(ANDROID_LOG_INFO, JNI_TAG,
                        "nativeInit: DiagEngine started — worker thread alive");
}

extern "C"
JNIEXPORT void JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeShutdown(
        JNIEnv*, jclass) {

    if (g_engine == nullptr) {
        __android_log_print(ANDROID_LOG_WARN, JNI_TAG,
                            "nativeShutdown: engine is null, nothing to do");
        return;
    }

    __android_log_print(ANDROID_LOG_INFO, JNI_TAG, "nativeShutdown: shutting down DiagEngine");
    g_engine->shutdown();
    g_engine.reset();
    __android_log_print(ANDROID_LOG_INFO, JNI_TAG, "nativeShutdown: DiagEngine destroyed");
}

extern "C"
JNIEXPORT void JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeGetProperty(JNIEnv* env, jclass, jint requestId, jint propertyID, jbyteArray payload, jobject callback) {
    (void)payload;
    __android_log_print(ANDROID_LOG_INFO, JNI_TAG,
                        "nativeGetProperty: reqId=%d, propId=0x%X", requestId, propertyID);

    if (callback == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG,
                            "nativeGetProperty: callback is null");
        return;
    }

    auto bridge = std::make_shared<JniCallbackBridge>(env, callback);

    const int reqId = requestId;
    const int proID = propertyID;

    std::thread([bridge, reqId, proID]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const std::string response = getDummyResponse(proID);
        __android_log_print(ANDROID_LOG_INFO, JNI_TAG,
                            "nativeGetProperty(worker): sending result for reqId=%d", reqId);
        bridge->onResult(reqId, response, 1000);
    }).detach();

    __android_log_print(ANDROID_LOG_INFO, JNI_TAG,
                        "nativeGetProperty: async worker spawned");
}

static std::string getDummyResponse(int propertyId) {
    switch (propertyId) {
        case 0xF190: return "VINFAST12345678901";
        case 0xF187: return "SW_V3.2.1_AAOS_HKMC";
        case 0xFD01: return "78";
        case 0xFE01: return "3200";
        case 0x0104: return "92";
        case 0x010C: return "1500";
        default:     return "UNKNOWN_PROPERTY_0x" + std::to_string(propertyId);
    }
}
