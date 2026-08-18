#include <android/sharedmem.h>
#include <android/log.h>
#include <errno.h>
#include <jni.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {
    constexpr const char *TAG = "VDiag.Ashmem";

    void throwIOException(JNIEnv *env, const char *msg) {
        jclass ioExceptionClass = env->FindClass("java/io/IOException");
        if (ioExceptionClass != nullptr) env->ThrowNew(ioExceptionClass, msg);
    }

    void throwIllegalArgumentException(JNIEnv *env, const char *msg) {
        jclass exClass = env->FindClass("java/lang/IllegalArgumentException");
        if (exClass != nullptr) env->ThrowNew(exClass, msg);
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_vdiag_ipc_AshmemBridge_nativeCreate(JNIEnv *env, jclass, jstring name, jint size) {
    if (size <= 0) {
        throwIllegalArgumentException(env, "size must be > 0");
        return -1;
    }

    const char *regionName = nullptr;
    if (name != nullptr) {
        regionName = env->GetStringUTFChars(name, nullptr);
        if (regionName == nullptr) {
            return -1;
        }
    }

    int fd = ASharedMemory_create(regionName, static_cast<size_t>(size));

    if (regionName != nullptr) {
        env->ReleaseStringUTFChars(name, regionName);
    }

    if (fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,
                            "ASharedMemory_create failed: errno=%d (%s)",
                            errno, strerror(errno));
        throwIOException(env, "Failed to create shared memory region");
        return -1;
    }

    __android_log_print(ANDROID_LOG_DEBUG, TAG,
                        "ASharedMemory_create succeeded: fd=%d size=%d", fd, size);
    return fd;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_vdiag_ipc_AshmemBridge_nativeWriteBlob(JNIEnv *env, jclass, jint fd, jbyteArray data) {
    if (fd < 0) {
        throwIllegalArgumentException(env, "fd must be >= 0");
        return;
    }

    if (data == nullptr) {
        throwIllegalArgumentException(env, "data must not be null");
        return;
    }

    const jsize len = env->GetArrayLength(data);
    if (len <= 0) {
        return;
    }

    size_t regionSize = 0;
    int sizeResult = ASharedMemory_getSize(fd, &regionSize);
    if (sizeResult != 0 || regionSize < static_cast<size_t>(len)) {
        throwIllegalArgumentException(env, "data length exceeds shared memory size");
        return;
    }

    void *mappedData = mmap(nullptr, static_cast<size_t>(len),
                            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mappedData == MAP_FAILED) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,
                            "mmap failed: errno=%d (%s)", errno, strerror(errno));
        throwIOException(env, "Failed to mmap shared memory");
        return;
    }

    env->GetByteArrayRegion(data, 0, len, static_cast<jbyte *>(mappedData));

    if (munmap(mappedData, static_cast<size_t>(len)) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,
                            "munmap failed: errno=%d (%s)", errno, strerror(errno));
    }

    if (env->ExceptionCheck()) {
        return;
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_vdiag_ipc_AshmemBridge_nativeSetProt(JNIEnv *env, jclass, jint fd, jint prot) {
    if (fd < 0) {
        throwIllegalArgumentException(env, "fd must be >= 0");
        return -1;
    }
    int result = ASharedMemory_setProt(fd, prot);
    if (result != 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,
                            "ASharedMemory_setProt failed: errno=%d (%s)",
                            errno, strerror(errno));
        throwIOException(env, "Failed to set protection flags");
        return -1;
    }
    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_vdiag_ipc_AshmemBridge_nativeGetSize(JNIEnv *env, jclass, jint fd) {
    if (fd < 0) {
        throwIllegalArgumentException(env, "fd must be >= 0");
        return -1;
    }
    size_t size = 0;
    int result = ASharedMemory_getSize(fd, &size);
    if (result != 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,
                            "ASharedMemory_getSize failed: errno=%d (%s)",
                            errno, strerror(errno));
        throwIOException(env, "Failed to get shared memory size");
        return -1;
    }
    if (size > static_cast<size_t>(INT32_MAX)) {
        throwIOException(env, "Shared memory size too large for jint");
        return -1;
    }
    return static_cast<jint>(size);
}


