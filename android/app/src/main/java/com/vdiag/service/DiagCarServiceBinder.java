package com.vdiag.service;

import android.os.Binder;
import android.util.Log;
import android.os.RemoteException;

import com.vdiag.IDiagCallback;
import com.vdiag.IDiagCarService;
import com.vdiag.DiagRequest;

public class DiagCarServiceBinder extends IDiagCarService.Stub {
    private static final String TAG = "CarService.Binder";
    private final DiagCarService mService;
    private final ClientRegistry mClientRegistry;
    public DiagCarServiceBinder(DiagCarService service , ClientRegistry clientRegistry) {

        mService = service;
        mClientRegistry = clientRegistry;

    }
    @Override
    public void getProperty(DiagRequest request, IDiagCallback callback)  {

        PermissionGate.enforce(mService);

        int callerPid = Binder.getCallingPid();
        int callerUid = Binder.getCallingUid();

        Log.d(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Log.d(TAG, "📥 getProperty() called");
        Log.d(TAG, "  CallerPid: " + callerPid);
        Log.d(TAG, "  CallerUid: " + callerUid);

        if (request == null) {
            Log.e(TAG, "Invalid DiagRequest");
            return;
        }

        if (callback == null) {
            Log.e(TAG, "Invalid DiagCallback");
            return;
        }

        mClientRegistry.register(callback);
        int requestId = request.requestId;
        int propertyId = request.propertyId;

        Log.i(TAG, "Request ID: " + requestId);
        Log.i(TAG, "  PropertyId: 0x" + Integer.toHexString(propertyId));

        String dummyValue = getDummyResponse(propertyId);

        Log.d(TAG, "  DummyResponse: " + dummyValue);
        Log.d(TAG, "  Invoking callback...");

        try {
            callback.onResult(requestId, dummyValue , 0);
            Log.d(TAG, "  Callback invoked successfully");
        } catch (RemoteException e) {
            Log.e(TAG, "Error invoking callback", e);
        }

        Log.d(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    }

    
    private String getDummyResponse(int propertyId) {
        switch (propertyId) {
            case 0xF190:  // Vehicle Identification Number
                return "VINFAST12345678901";
            case 0xF187:  // Software version
                return "SW_V3.2.1_AAOS_HKMC";
            case 0xFD01:  // Battery SOC %
                return "78";
            case 0xFE01:  // Motor RPM
                return "3200";
            case 0x0104:  // Engine temp
                return "92";
            case 0x010C:  // RPM (OBD mode 01)
                return "1500";
            default:
                return "UNKNOWN_PROPERTY_0x" + Integer.toHexString(propertyId);
        }
    }
    public void cleanup() {
        Log.i(TAG, "🧹 DiagServiceBinder.cleanup()");
    }

}