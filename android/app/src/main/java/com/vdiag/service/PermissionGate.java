package com.vdiag.service;

import android.content.Context;
import android.os.Binder;
import android.util.Log;


public final class PermissionGate {
    private static final String TAG = "VDiag.Permission";
    public static final String PERMISSION_DIAGNOSE = "com.vdiag.permission.DIAGNOSE";

    private PermissionGate() {}

    public static void enforce(Context ctx) {
        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();

        Log.i(TAG, "🔐 Permission check — pid=" + callerPid + " uid=" + callerUid);

        ctx.enforceCallingOrSelfPermission(
                PERMISSION_DIAGNOSE,
                "Caller (pid=" + callerPid + " uid=" + callerUid +
                ") lacks " + PERMISSION_DIAGNOSE
        );

        Log.i(TAG, "🔐 Permission check — success");
    }
}
