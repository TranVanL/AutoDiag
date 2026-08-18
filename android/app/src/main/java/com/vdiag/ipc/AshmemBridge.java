package com.vdiag.ipc;

import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.io.IOException;

public final class AshmemBridge {
    private static final String TAG = "VDiag.AshmemBridge";

    public static final int PROT_READ = 0x1;
    public static final int PROT_WRITE = 0x2;

    static {
        try {
            System.loadLibrary("vdiag_jni");
            Log.i(TAG, "Native library 'vdiag_jni' loaded");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load native library 'vdiag_jni'", e);
            throw e;
        }
    }

    private AshmemBridge() {
        // utility class
    }

    public static native int nativeCreate(@Nullable String name, int size) throws IOException;

    public static native void nativeWriteBlob(int fd, @NonNull byte[] data) throws IOException;

    public static native int nativeSetProt(int fd, int prot) throws IOException;

    public static native int nativeGetSize(int fd) throws IOException;

    @NonNull
    public static ParcelFileDescriptor createSharedBlob(@Nullable String name,
                                                        @NonNull byte[] data) throws IOException {
        if (data == null) {
            throw new IllegalArgumentException("data must not be null");
        }

        int fd = nativeCreate(name, data.length);
        if (fd < 0) {
            throw new IOException("Failed to create shared memory");
        }

        try {
            nativeWriteBlob(fd, data);
            nativeSetProt(fd, PROT_READ);
            return ParcelFileDescriptor.adoptFd(fd);
        } catch (IOException | RuntimeException e) {
            closeFd(fd);
            throw e;
        }
    }

    private static void closeFd(int fd) {
        try {
            Os.close(fd);
        } catch (ErrnoException e) {
            Log.w(TAG, "Failed to close fd", e);
        }
    }
}