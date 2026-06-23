package com.vdiag.service;

import android.util.Log;

import com.vdiag.IDiagCallback;

public class DiagHalBridge {
    private static final String TAG = "VDiag.HalBridge";

    static {
        try {
            Log.i(TAG, "Function Onload is called after initing JNI bridge ");
            System.loadLibrary("vdiag_jni");
            Log.i(TAG, "Native library 'vdiag_jni' loaded");
        } catch (UnsatisfiedLinkError e) {
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
}
