package com.vdiag.service;

import android.content.Context;
import android.os.Binder;
import android.util.Log;

import com.vdiag.sdk.DiagProperty;

import java.util.Map;


public final class PermissionGate {
    private static final String TAG = "VDiag.Permission";
    public static final String PERMISSION_DIAGNOSE = "com.vdiag.permission.DIAGNOSE";

    public static final Map<Integer,String> PERMISSION_MAP = Map.of(
            DiagProperty.VIN.getPropId(), "com.vdiag.permission.DIAGNOSE" ,
            DiagProperty.BATTERY_SOC.getPropId(), "com.vdiag.permission.READ_BATTERY" ,
            DiagProperty.TIRE_PRESSURE.getPropId(), "com.vdiag.permission.READ_TIRES" ,
            DiagProperty.RPM.getPropId() , "com.vdiag.permission.READ_POWERTRAIN" );

    private PermissionGate() {}

    private static final String default_permission = "com.vdiag.permission.DIAGNOSE";
    public static void enforce(Context ctx) {
        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();

        // Enforce permission for client
        Log.i(TAG, "🔐 Permission check — pid=" + callerPid + " uid=" + callerUid);

        ctx.enforceCallingOrSelfPermission(
                PERMISSION_DIAGNOSE,
                "Caller (pid=" + callerPid + " uid=" + callerUid +
                ") lacks " + PERMISSION_DIAGNOSE
        );

        Log.i(TAG, "🔐 Permission check — success");
    }

    public static void enforceProperty(Context ctx , int proId) {
        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();

        String perm = PERMISSION_MAP.getOrDefault(proId, PERMISSION_DIAGNOSE);

        Log.i(TAG, "🔐 Property permission check — pid=" + callerPid
                + " uid=" + callerUid
                + " propId=0x" + Integer.toHexString(proId)
                + " perm=" + perm);

        ctx.enforceCallingOrSelfPermission(
                perm,
                "Caller (pid=" + callerPid + " uid=" + callerUid +
                        ") lacks " + perm + " for property=0x" + Integer.toHexString(proId)
        );

        Log.i(TAG, "🔐 Property permission check — success");
    }
}
