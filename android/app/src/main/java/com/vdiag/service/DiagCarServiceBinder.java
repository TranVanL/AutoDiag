package com.vdiag.service;

import android.os.Binder;
import android.util.Log;

import com.vdiag.IDiagCallback;
import com.vdiag.IDiagCarService;
import com.vdiag.DiagRequest;

public class DiagCarServiceBinder extends IDiagCarService.Stub {
    private static final String TAG = "CarService.Binder";
    private final DiagCarService mService;
    private final ClientRegistry mClientRegistry;

    public DiagCarServiceBinder(DiagCarService service, ClientRegistry clientRegistry) {
        mService = service;
        mClientRegistry = clientRegistry;
    }

    @Override
    public void registerCallback(IDiagCallback callback) {
        PermissionGate.enforce(mService);
        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();
        mClientRegistry.register(callback, callerPid, callerUid);
    }

    @Override
    public void unregisterCallback(IDiagCallback callback) {
        PermissionGate.enforce(mService);
        mClientRegistry.unregister(callback);
    }

    @Override
    public void getProperty(DiagRequest request) {
        PermissionGate.enforce(mService);
        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();

        if (request == null) {
            Log.e(TAG, "Invalid DiagRequest");
            return;
        }

        Log.d(TAG, "getProperty() reqId=" + request.requestId + " propId=0x" + Integer.toHexString(request.propertyId));

        IDiagCallback callback = mClientRegistry.getCallbackForCaller(callerPid, callerUid);
        if (callback == null) {
            Log.w(TAG, "No registered callback for caller pid=" + callerPid + "; dropping request " + request.requestId);
            return;
        }

        DiagHalBridge.nativeGetProperty(request.requestId, request.propertyId, request.payload, callback);
        Log.d(TAG, "getProperty() dispatched to JNI, reqId=" + request.requestId);
    }

    public void cleanup() {
        Log.i(TAG, "🧹 DiagServiceBinder.cleanup()");
        mClientRegistry.cleanup();
    }
}
