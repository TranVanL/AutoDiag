package com.vdiag.service;

import android.util.Log;

import com.vdiag.IDiagCallback;

// JNI Bridge for communication with HAL
public class DiagHalBridge {
    private static final String TAG = "VDiag.HalBridge";
    private static boolean sNativeReady = false;
    // Load native library that has declared in build.gradle
    static {
        try {
            System.loadLibrary("vdiag_jni");
            sNativeReady = true;
            Log.i(TAG, "Native library 'vdiag_jni' loaded — JNI_OnLoad will fire");
        } catch (UnsatisfiedLinkError e) {
            sNativeReady = false;
            Log.e(TAG, "Failed to load native library 'vdiag_jni'", e);
        }
    }
    // Use native function to notify implementation don't have JavaCode to handle JNI call (Don't have body),
    // Javac will create entry with flag ACC_NATIVE , don't have Java bytes code for that ,
    // When call ,JVM will jump into library to get address of function to call
    public static native void nativeGetProperty(
            int reqId,
            int propertyId,
            byte[] payload,
            IDiagCallback callback
    );

    public static native void nativeInit(String halType);

    public static native void nativeShutdown();


    public static native String nativeReadProperty(int propId);

    public static native String nativeReadProperty(int propId, int areaId);

  
    private static native boolean nativeIsWorkerAlive();

 
    private static native int nativeGetQueueDepth();

   
    public static boolean isWorkerAlive() {
        if (!sNativeReady) return false;
        try {
            return nativeIsWorkerAlive();
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "nativeIsWorkerAlive not linked", e);
            return false;
        }
    }

   
    public static int getQueueDepth() {
        if (!sNativeReady) return 0;
        try {
            return nativeGetQueueDepth();
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "nativeGetQueueDepth not linked", e);
            return 0;
        }
    }

    public static boolean init(String halType) {
        if (!sNativeReady) {
            Log.e(TAG, "nativeInit skipped: JNI library not loaded");
            return false;
        }
        try {
            nativeInit(halType);
            return true;
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "nativeInit failed", e);
            return false;
        }
    }

    public static boolean shutdown() {
        if (!sNativeReady) {
            Log.w(TAG, "nativeShutdown skipped: JNI library not loaded");
            return false;
        }
        try {
            nativeShutdown();
            return true;
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "nativeShutdown failed", e);
            return false;
        }
    }

    public static boolean isNativeReady() {
        return sNativeReady;
    }

    public static String readProperty(int propId) {
        return readProperty(propId, com.vdiag.sdk.DiagProperty.AREA_GLOBAL);
    }

    public static String readProperty(int propId, int areaId) {
        if (!sNativeReady) return null;
        try {
            return nativeReadProperty(propId, areaId);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "nativeReadProperty not linked", e);
            return null;
        }
    }

    public static native void nativeFlashFirmware(int fd, IDiagCallback callback);

    public static boolean flashFirmware(int fd, IDiagCallback callback) {
        if (!sNativeReady) {
            Log.e(TAG, "flashFirmware skipped: JNI library not loaded");
            return false;
        }
        try {
            nativeFlashFirmware(fd, callback);
            return true;
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "nativeFlashFirmware failed", e);
            return false;
        }
    }
}
