#pragma once

#include <jni.h>
#include <cstdint>
#include <string>

// Bridge between Java and C++
class JniCallbackBridge {
public:
    JniCallbackBridge(JNIEnv* env, jobject callback);
    ~JniCallbackBridge() noexcept;

    JniCallbackBridge(const JniCallbackBridge&) = delete;
    JniCallbackBridge& operator=(const JniCallbackBridge&) = delete;


    JniCallbackBridge(JniCallbackBridge &&) noexcept;
    JniCallbackBridge& operator=(JniCallbackBridge &&) = delete;

    void onResult(int requestId, const std::string& value, int64_t latencyUs);
    void onError(int requestId, int errorCode, const std::string& errorMsg);
    void onProgress(int64_t bytesWritten, int64_t totalBytes);

private :
    jobject m_callback;
    JNIEnv* getEnv();
};