#pragma once

#include <jni.h>
#include <cstdint>
#include <string>

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

private :
    jobject m_callback;
    JNIEnv* getEnv();
};