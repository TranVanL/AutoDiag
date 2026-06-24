package com.vdiag.service;

import android.util.Log;

import com.vdiag.IDiagCallback;

public class DiagHalBridge {
    private static final String TAG = "VDiag.HalBridge";
    private static boolean sNativeReady = false;

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

    public static native void nativeGetProperty(
            int reqId,
            int propertyId,
            byte[] payload,
            IDiagCallback callback
    );

    public static native void nativeInit(String halType);

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

    public static boolean isNativeReady() {
        return sNativeReady;
    }
}
