package com.vdiag.service;

import android.os.Binder;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.IDiagCallback;
import com.vdiag.IDiagCarService;
import com.vdiag.DiagRequest;

import com.vdiag.DiagPropertyEvent;
import com.vdiag.IDiagPropertyListener;

// Implement Inheritance Stub class to handle communication between client and service
public class DiagCarServiceBinder extends IDiagCarService.Stub {
    private static final String TAG = "CarService.Binder";
    // Member of DiagCarService
    private final DiagCarService mService;
    private final ClientRegistry mClientRegistry;
    private final SubscriptionManager mSubManager;

    public DiagCarServiceBinder(DiagCarService service, ClientRegistry clientRegistry,
                                SubscriptionManager subManager) {
        mService = service;
        mClientRegistry = clientRegistry;
        mSubManager = subManager;
    }


    // Function that service expose in AIDL for client call through Binder IPC
    @Override
    public void subscribeProperty(int proId, float rateHz, IDiagPropertyListener listener) {
        // Enforce permission before run business logic , only clients have permission that already granted (Defined by manifest) only can bind to service
        PermissionGate.enforceProperty(mService , proId);
        Log.i(TAG, "subscribeProperty() proId=0x" + Integer.toHexString(proId)
                + " rateHz=" + rateHz);
        //  Call real function
        mSubManager.register(proId, rateHz, listener);
    }

    @Override

    public void unsubscribeProperty(int proId, IDiagPropertyListener listener) {
        PermissionGate.enforceProperty(mService , proId);
        Log.i(TAG, "unsubscribeProperty() proId=0x" + Integer.toHexString(proId));
        mSubManager.unregister(proId, listener);
    }

    @Override
    public void subscribePropertyForArea(int proId, int areaId, float rateHz,
                                         IDiagPropertyListener listener) {
        PermissionGate.enforceProperty(mService , proId);
        mSubManager.register(proId, areaId, rateHz, listener);
    }

    @Override
    public void unsubscribePropertyForArea(int proId, int areaId,
                                            IDiagPropertyListener listener) {
        PermissionGate.enforce(mService);
        mSubManager.unregister(proId, areaId, listener);
    }
    
    @Override
    public void registerCallback(IDiagCallback callback) {
        PermissionGate.enforce(mService);
        // Get UID and PID of caller
        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();
        // Register callback to client registry
        mClientRegistry.register(callback, callerPid, callerUid);
    }

    @Override
    public void unregisterCallback(IDiagCallback callback) {
        PermissionGate.enforce(mService);
        mClientRegistry.unregister(callback);
    }

    @Override
    public void getProperty(DiagRequest request) {

        if (request == null) {
            Log.e(TAG, "Invalid DiagRequest");
            return;
        }

        PermissionGate.enforceProperty(mService,request.propertyId);
        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();

        Log.d(TAG, "getProperty() reqId=" + request.requestId + " propId=0x" + Integer.toHexString(request.propertyId));

        // Get callback binder object from client registry by caller's PID and UID
        IDiagCallback callback = mClientRegistry.getCallbackForCaller(callerPid, callerUid);
        if (callback == null) {
            Log.w(TAG, "No registered callback for caller pid=" + callerPid + "; dropping request " + request.requestId);
            return;
        }


        // Use try to ensure that JNI bridge is available and handle exception
        // Avoid error like : Library load failed or native code not found exception ( don't catch it , service will crash)
        // Log for easy to find the reason
        try {
            DiagHalBridge.nativeGetProperty(request.requestId, request.propertyId, request.payload, callback);
            Log.d(TAG, "getProperty() dispatched to JNI, reqId=" + request.requestId);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "JNI bridge unavailable", e);
            try {
                callback.onError(request.requestId, -1, "JNI bridge unavailable");
            } catch (RemoteException re) {
                Log.e(TAG, "Failed to notify callback about JNI error", re);
            }
        }
    }

    // Clean up function to release resources
    public void cleanup() {
        Log.i(TAG, "🧹 DiagServiceBinder.cleanup()");
        mSubManager.shutdown();
        mClientRegistry.cleanup();
    }
}
