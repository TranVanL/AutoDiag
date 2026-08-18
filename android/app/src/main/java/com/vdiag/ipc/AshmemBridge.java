package com.vdiag.ipc;

import android.os.ParcelFileDescriptor;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.io.FileDescriptor;
import java.io.IOException;

public final class AshmemBridge {
    private static final String TAG = "VDiag.AshmemBridge";
    private static final int PROT_READ = 0x1;
    private static final int PROT_WRITE = 0x2;

    static {
        try {
            System.loadLibrary("vdiag_jni");
            Log.i(TAG, "Load library successfully");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load library");
            throw (e);
        }
    }

    private AshmemBridge() {

    }

    public static native int nativeCreate(@Nullable String name, int size) throws IOException;

    public static native void nativeWriteBlob(int fd, @NonNull byte[] data) throws IOException;

    public static native int nativeSetProt(int fd, int prot) throws IOException;

    public static native int nativeGetSize(int fd) throws IOException;

    @NonNull
    public static ParcelFileDescriptor createSharedBlob(@Nullable String name, @NonNull byte[] data) throws IOException {
        if (data == null) {
            throw new IllegalArgumentException("data is null");
        }

        int fd = nativeCreate(name, data.length);
        if (fd < 0) {
            throw new IOException("Error from creating shared memory");
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
            ParcelFileDescriptor.adoptFd(fd).close();
        } catch (IOException e) {
            Log.w(TAG, "Failed to close fd", e);
        }
    }
}