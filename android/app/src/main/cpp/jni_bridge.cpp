#include <jni.h>
#include <android/log.h>
#include <memory>
#include <string>
#include <algorithm>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "jni_callback.h"
#include "diag_engine.h"
#include "diag_type.h"
#include "hal_factory.h"

#define JNI_TAG "VDiag.JNI"

static std::unique_ptr<autodiag::DiagEngine> g_engine;
// Declare JNI function call to native
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

    // Choose HAL type
    auto factory = std::make_unique<autodiag::HalFactory>();
    std::unique_ptr<autodiag::IDiagnosticHal> hal;
    try {
        hal = factory->createHal(halTypeStr);
    } catch (const std::exception& ex) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG,
                            "nativeInit: failed to create HAL: %s", ex.what());
        return;
    }
    // Create DiagEngine with HAL
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
                        static_cast<int>(autodiag::Nrc::EngineNotReady),
                        "Engine not initialized");
        return;
    }

    // Build DiagRequest — map propertyId → UDS service + dataId
    autodiag::DiagRequest req{};
    autodiag::RequestPriority pri = autodiag::RequestPriority::NORMAL;
    req.requestId = static_cast<std::uint32_t>(requestId);

    const auto propId = static_cast<std::uint16_t>(static_cast<unsigned int>(propertyID));
    if (propId == static_cast<std::uint16_t>(autodiag::DiagProperty::DtcList)) {
        req.service      = autodiag::UdsService::ReadDTC;
        req.subFunction  = 0x02;  // reportDTCByStatusMask
        pri = autodiag::RequestPriority::CRITICAL;
    } else if (propId == static_cast<std::uint16_t>(autodiag::DiagProperty::DtcClear)) {
        req.service = autodiag::UdsService::ClearDTC;
        pri = autodiag::RequestPriority::HIGH;
    } else {
        req.service = autodiag::UdsService::ReadDataByIdentifier;
        req.dataId  = propId;
        pri = autodiag::RequestPriority::HIGH;
    }
    // Push to engine diag request
    const bool queued = g_engine->submit(pri,req, [bridge](const autodiag::DiagResponse& r) {
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
                        static_cast<int>(autodiag::Nrc::EngineNotReady),
                        "Engine rejected request");
    }
}


// ── health surface (Day 56 atomics exposed to Java via JNI) ───────────────────

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeIsWorkerAlive(
        JNIEnv*, jclass) {
    if (g_engine == nullptr) {
        return JNI_FALSE;
    }
    return g_engine->isWorkerAlive() ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeGetQueueDepth(
        JNIEnv*, jclass) {
    if (g_engine == nullptr) {
        return 0;
    }
    return static_cast<jint>(g_engine->getQueueDepth());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeReadProperty__II(
        JNIEnv* env, jclass clazz, jint propId, jint areaId);

extern "C"
JNIEXPORT jstring JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeReadProperty__I(
    JNIEnv* env, jclass clazz, jint propId) {
    return Java_com_vdiag_service_DiagHalBridge_nativeReadProperty__II(
        env, clazz, propId, 0);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeReadProperty__II(
    JNIEnv* env, jclass, jint propId, jint areaId) {
    if (g_engine == nullptr) {
        __android_log_print(ANDROID_LOG_WARN, JNI_TAG,
                            "nativeReadProperty: engine not init — propId=0x%X areaId=%d",
                            propId, areaId);
        return nullptr;
    }

    autodiag::IDiagnosticHal* hal = g_engine->getHal();
    if (hal == nullptr) return nullptr;

    autodiag::IDiagnosticHal::Result result =
            hal->readProperty(static_cast<uint32_t>(propId), static_cast<uint32_t>(areaId));

    if (!result.success || result.data.empty()) {
        __android_log_print(ANDROID_LOG_WARN, JNI_TAG,
                            "nativeReadProperty: HAL error propId=0x%X — %s",
                            propId, result.error.c_str());
        return nullptr;
    }


    const uint16_t id = static_cast<uint16_t>(propId);
    std::string out;

    if (id == static_cast<uint16_t>(autodiag::DiagProperty::BatterySoc)) {
        out = std::to_string(result.data[0]);
    } else if (id == static_cast<uint16_t>(autodiag::DiagProperty::RPM) && result.data.size() >= 2) {
        const uint16_t raw = static_cast<uint16_t>(result.data[0] << 8) |
                             static_cast<uint16_t>(result.data[1]);
        out = std::to_string(raw / 4U);
    } else if (id == static_cast<uint16_t>(autodiag::DiagProperty::TirePressure)) {
        out = std::to_string(result.data[0]);
    } else {
        out = std::string(result.data.begin(), result.data.end());
    }

    __android_log_print(ANDROID_LOG_DEBUG, JNI_TAG,
                        "nativeReadProperty: propId=0x%X areaId=%d → \"%s\"",
                        propId, areaId, out.c_str());
    return env->NewStringUTF(out.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_vdiag_service_DiagHalBridge_nativeFlashFirmware(
    JNIEnv* env, jclass, jint fd, jobject callback) {
    if (fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG, "nativeFlashFirmware: invalid fd");
        return;
    }

    if (callback == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG, "nativeFlashFirmware: null callback");
        return;
    }

    auto bridge = std::make_shared<JniCallbackBridge>(env, callback);

    if (g_engine == nullptr || g_engine->getHal() == nullptr) {
        bridge->onError(-1, -1, "Engine not initialized");
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG, "nativeFlashFirmware: engine not ready");
        return;
    }

    // Get file size from the passed fd
    struct stat st;
    if (fstat(fd, &st) != 0) {
        bridge->onError(-1, -1, "Failed to get file size");
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG, "nativeFlashFirmware: fstat failed");
        return;
    }

    const size_t fileSize = static_cast<size_t>(st.st_size);
    if (fileSize == 0) {
        bridge->onError(-1, -1, "File size is zero");
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG, "nativeFlashFirmware: file size is zero");
        return;
    }

    __android_log_print(ANDROID_LOG_INFO, JNI_TAG,
                        "nativeFlashFirmware: fd=%d size=%zu", fd, fileSize);

    // mmap the entire file — zero-copy, no Java byte[] copy
    void* mappedData = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mappedData == MAP_FAILED) {
        bridge->onError(-1, -1, "Failed to mmap file");
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG, "nativeFlashFirmware: mmap failed");
        return;
    }

    // Flash in chunks so we can report progress and avoid blocking too long
    constexpr size_t chunkSize = 256 * 1024; // 256 KB
    const uint8_t* dataPtr = static_cast<const uint8_t*>(mappedData);
    size_t bytesWritten = 0;

    while (bytesWritten < fileSize) {
        size_t bytesToWrite = std::min(chunkSize, fileSize - bytesWritten);
        g_engine->getHal()->flashFirmware(dataPtr + bytesWritten, bytesToWrite);
        bytesWritten += bytesToWrite;
        bridge->onProgress(static_cast<int64_t>(bytesWritten),
                           static_cast<int64_t>(fileSize));
    }

    if (munmap(mappedData, fileSize) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_TAG, "nativeFlashFirmware: munmap failed");
    }

    __android_log_print(ANDROID_LOG_INFO, JNI_TAG,
                        "nativeFlashFirmware: completed %zu bytes", fileSize);
}
