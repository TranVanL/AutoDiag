#include <jni.h>
#include <android/log.h>
#include <memory>
#include <string>
#include "jni_callback.h"
#include "diag_engine.h"
#include "diag_type.h"
#include "mock_diag_hal.h"

#define JNI_TAG "VDiag.JNI"

static std::unique_ptr<autodiag::DiagEngine> g_engine;

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

    if (g_engine == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG,
                            "nativeGetProperty: engine not initialized — call nativeInit first");
        bridge->onError(static_cast<int>(requestId),
                        static_cast<int>(autodiag::Nrc::RequestOutOfRange),
                        "Engine not initialized");
        return;
    }

    // Build DiagRequest — map propertyId → UDS service + dataId
    autodiag::DiagRequest req{};
    req.requestId = static_cast<std::uint32_t>(requestId);

    const auto propId = static_cast<std::uint16_t>(static_cast<unsigned int>(propertyID));
    if (propId == static_cast<std::uint16_t>(autodiag::DiagProperty::DtcList)) {
        req.service      = autodiag::UdsService::ReadDTC;
        req.subFunction  = 0x02;  // reportDTCByStatusMask
    } else if (propId == static_cast<std::uint16_t>(autodiag::DiagProperty::DtcClear)) {
        req.service = autodiag::UdsService::ClearDTC;
    } else {
        req.service = autodiag::UdsService::ReadDataByIdentifier;
        req.dataId  = propId;
    }

    const bool queued = g_engine->submit(req, [bridge](const autodiag::DiagResponse& r) {
        if (r.positive) {
            bridge->onResult(r.requestId, r.valueString, r.latencyUs);
        } else {
            bridge->onError(r.requestId,
                            static_cast<int>(r.nrc),
                            autodiag::nrcToString(r.nrc));
        }
    });

    if (queued) {
        __android_log_print(ANDROID_LOG_INFO, JNI_TAG,
                            "nativeGetProperty: submitted to DiagEngine — reqId=%d propId=0x%X",
                            requestId, propertyID);
    } else {
        __android_log_print(ANDROID_LOG_WARN, JNI_TAG,
                            "nativeGetProperty: engine rejected submit (stopped?) — reqId=%d",
                            requestId);
        bridge->onError(static_cast<int>(requestId),
                        static_cast<int>(autodiag::Nrc::RequestOutOfRange),
                        "Engine rejected request");
    }
}
