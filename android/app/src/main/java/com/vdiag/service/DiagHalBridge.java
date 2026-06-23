package com.vdiag.service;

import android.util.Log;

public class DiagHalBridge {
    private static final String TAG = "VDiag.HalBridge";

    static {
        try {
            System.loadLibrary("vdiag_jni");
            Log.i(TAG, "Native library 'vdiag_jni' loaded");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load native library 'vdiag_jni'", e);
        }
    }

    public static native void nativeInit(String halType);
}
