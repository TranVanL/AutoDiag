package com.vdiag.service;

import android.os.Binder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.IDiagCallback;
import com.vdiag.IDiagCarService;
import com.vdiag.DiagRequest;

import com.vdiag.DiagPropertyEvent;
import com.vdiag.IDiagPropertyListener;

import java.io.IOException;
import com.vdiag.ipc.AshmemBridge;

// Implement Inheritance Stub class to handle communication between client and service
public class DiagCarServiceBinder extends IDiagCarService.Stub {
    private static final String TAG = "CarService.Binder";
    // Member of DiagCarService
    private final DiagCarService mService;
    private final ClientRegistry mClientRegistry;
    private final SubscriptionManager mSubManager;
    private final DtcStore mDtcStore;

    public DiagCarServiceBinder(DiagCarService service, ClientRegistry clientRegistry,
                                SubscriptionManager subManager , DtcStore dtcStore) {
        mService = service;
        mClientRegistry = clientRegistry;
        mSubManager = subManager;
        mDtcStore = dtcStore;
    }

    @Override
    public ParcelFileDescriptor getDtcSnapshotShared(int[] outMeta) {
        PermissionGate.enforce(mService);
        if (outMeta == null || outMeta.length < 2) {
            Log.e(TAG, "getDtcSnapshotShared: outMeta must have length >= 2");
            throw new IllegalArgumentException("outMeta must have length >= 2");
        }

        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();
        Log.i(TAG, "getDtcSnapshotShared() caller pid=" + callerPid
                + " uid=" + callerUid);

        try {
            byte[] dtcSnapshot = mDtcStore.serializeAll();
            if (dtcSnapshot.length == 0) {
                outMeta[0] = 0;
                outMeta[1] = 0;
                Log.i(TAG, "getDtcSnapshotShared: store is empty");
                return null;
            }

            ParcelFileDescriptor pfd = AshmemBridge.createSharedBlob(
                    "vdiag_dtc_snapshot", dtcSnapshot);

            outMeta[0] = dtcSnapshot.length;
            outMeta[1] = mDtcStore.count();

            Log.i(TAG, "getDtcSnapshotShared: returned fd=" + pfd.getFd()
                    + " size=" + dtcSnapshot.length
                    + " count=" + outMeta[1]);
            return pfd;
        } catch (IOException e) {
            Log.e(TAG, "getDtcSnapshotShared failed", e);
            return null;
        }
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

    @Override
    public void flashFirmware(ParcelFileDescriptor fd ,  IDiagCallback callback ) {
        PermissionGate.enforce(mService);

        if (fd == null || callback == null) {
            Log.e(TAG, "Invalid parameters for FlashFirmware");
            return;
        }

        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();
        Log.i(TAG, "flashFirmware() caller pid=" + callerPid + " uid=" + callerUid);

       try {
          DiagHalBridge.flashFirmware(fd.getFd(), callback);
       } catch (UnsatisfiedLinkError e) {
         Log.e(TAG, "JNI bridge unavailable for flashFirmware", e);
          notifyError(callback, -1, "JNI bridge unavailable");
       } catch (Exception e) {
         Log.e(TAG, "flashFirmware failed", e);
         notifyError(callback, -1, e.getMessage());
        } finally {
        try {
            fd.close();  
        } catch (IOException e) {
            Log.w(TAG, "failed to close fd", e);
        }
        }
    }

    private void notifyError(IDiagCallback callback, int errorCode, String errorMessage) {
        try {
            callback.onError(-1, errorCode, errorMessage);
        } catch (RemoteException e) {
            Log.e(TAG, "Failed to notify callback about error", e);
        }
    }

    // Clean up function to release resources
    public void cleanup() {
        Log.i(TAG, "🧹 DiagServiceBinder.cleanup()");
        mSubManager.shutdown();
        mClientRegistry.cleanup();
    }
}
