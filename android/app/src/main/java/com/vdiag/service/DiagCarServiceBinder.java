package com.vdiag.service;

import android.os.Binder;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.IDiagCallback;
import com.vdiag.IDiagCarService;
import com.vdiag.DiagRequest;

import java.util.Map;

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
        mClientRegistry.register(callback);
    }

    @Override
    public void unregisterCallback(IDiagCallback callback) {
        mClientRegistry.unregister(callback);
    }

    @Override
    public void getProperty(DiagRequest request) {
        PermissionGate.enforce(mService);

        if (request == null) {
            Log.e(TAG, "Invalid DiagRequest");
            return;
        }

        Log.d(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Log.d(TAG, "getProperty() called for reqId: " + request.requestId);

        // Tạo một callback trung gian để gửi kết quả tới toàn bộ client đã register
        IDiagCallback proxyCallback = new IDiagCallback.Stub() {
            @Override
            public void onResult(int requestId, String value, long latencyUs) {
                broadcastResult(requestId, value, latencyUs);
            }

            @Override
            public void onError(int requestId, int errorCode, String errorMsg) {
                broadcastError(requestId, errorCode, errorMsg);
            }
        };

        DiagHalBridge.nativeGetProperty(request.requestId, request.propertyId, request.payload, proxyCallback);
        Log.d(TAG, "getProperty() dispatched to HAL");
        Log.d(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    }

    private void broadcastResult(int requestId, String value, long latencyUs) {
        for (Map.Entry<android.os.IBinder, ClientRegistry.ClientEntry> entry : mClientRegistry.mClients.entrySet()) {
            try {
                entry.getValue().callback.onResult(requestId, value, latencyUs);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to notify client", e);
            }
        }
    }

    private void broadcastError(int requestId, int errorCode, String errorMsg) {
        for (Map.Entry<android.os.IBinder, ClientRegistry.ClientEntry> entry : mClientRegistry.mClients.entrySet()) {
            try {
                entry.getValue().callback.onError(requestId, errorCode, errorMsg);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to notify client", e);
            }
        }
    }

    public void cleanup() {
        Log.i(TAG, "🧹 DiagServiceBinder.cleanup()");
        mClientRegistry.cleanup();
    }
}
