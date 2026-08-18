#include <android/sharedmem.h>
#include <android/log.h>
#include <errno.h>
#include <jni.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {
    constexpr const char *TAG = "VDiag.Ashmem";
    void throwIOException(JNIEnv *env , const char *msg) {
        jclass ioExceptionClass = env->FindClass("java/io/IOException");
        if (ioExceptionClass != nullptr) env->ThrowNew(ioExceptionClass,msg);
    }

    void throwIllegalArgumentException(JNIEnv *env , const char *msg) {
        jclass ioExceptionClass = env->FindClass("java/lang/IllegalArgumentException");
        if (ioExceptionClass != nullptr) env->ThrowNew(ioExceptionClass,msg);
    }
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_vdiag_ipc_AshmemBridge_nativeCreate(JNIEnv *env, jclass, jstring name ,jint size) {
    if (size <=0) {
        throwIllegalArgumentException(env , "Invalid size");
        return -1;
    }

    const char * regionName = nullptr;
    if (name != nullptr) {
        regionName = env->GetStringUTFChars(name , nullptr);
        if (regionName == nullptr) {
            return -1;
        }
    }

    int fd = ASharedMemory_create(regionName,static_cast<size_t>(size));

    if (regionName != nullptr) {
        env->ReleaseStringUTFChars(name , regionName);
    }

    if (fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,
                            "ASharedMemory_create failed: errno=%d (%s)",
                            errno, strerror(errno));
        throwIOException(env , "Error from creating shared memory");
        return -1;
    }

    __android_log_print(ANDROID_LOG_ERROR, TAG,
                        "ASharedMemory_create succeeded: fd=%d", fd);
    return fd;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_vdiag_ipc_AshmemBridge_nativeWriteBlob(JNIEnv *env, jclass, jint fd ,jbyteArray data){
   if (fd < 0 ) {
       throwIllegalArgumentException(env , "FD number must be >= 0");
       return;
   }

   if (data == nullptr) {
       throwIllegalArgumentException(env , "data is null");
       return;
   }

   const jsize len = env->GetArrayLength(data);
   if (len <= 0) {
       return;
   }

   const size_t RegionSize = ASharedMemory_getSize(fd);
   if (RegionSize < static_cast<size_t>(len)) {
       throwIllegalArgumentException(env ,"data is too long");
       return;
   }

   // Write data to shared memory
   void * mappedData = mmap(nullptr , static_cast<size_t>(len) , PROT_READ | PROT_WRITE , MAP_SHARED , fd , 0);
   if (mappedData == MAP_FAILED) {
       __android_log_print(ANDROID_LOG_ERROR, TAG,
                           "Error from mmap: errno=%d (%s)",
       throwIOException(env , "Error from mmap");
       return;
   }

   env->GetByteArrayRegion(data , 0 , len , static_cast<jbyte*>(mappedData));

   if (munmap(mappedData , static_cast<size_t>(len)) != 0) {
       __android_log_print(ANDROID_LOG_ERROR, TAG,
                           "Error from munmap: errno=%d (%s)",
                           errno, strerror(errno));
       throwIOException(env , "Error from munmap");
       return;
   }

   if (env->ExceptionCheck()) {
       return;
   }

}

extern "C"
JNIEXPORT jint JNICALL
Java_com_vdiag_ipc_AshmemBridge_nativeSetProt(JNIEnv* env , jclass , jint fd , jint prot) {
    if (fd < 0 ) {
        throwIllegalArgumentException(env , "Fd number must be >= 0");
        return -1;
    }
    int result = ASharedMemory_setProt(fd ,prot);
    if (result != 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,
                            "Error from ASharedMemory_setProt: errno=%d (%s)",
                            errno, strerror(errno));
        throwIOException(env , "Error from ASharedMemory_setProt");
        return -1;
    }
    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_vdiag_ipc_AshmemBridge_nativeGetSize(JNIEnv* env , jclass , jint fd) {
    if (fd < 0 ) {
        throwIllegalArgumentException(env , "Fd number must be >= 0");
        return -1;
    }
    const size_t Size = ASharedMemory_getSize(fd);
    return static_cast<jint>(Size);
}


