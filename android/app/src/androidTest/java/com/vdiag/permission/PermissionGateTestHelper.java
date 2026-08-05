package com.vdiag.permission;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.os.RemoteException;

import androidx.test.platform.app.InstrumentationRegistry;

import com.vdiag.DiagRequest;
import com.vdiag.IDiagCarService;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Helper that binds to DiagCarService and exposes convenience methods 
 */
public final class PermissionGateTestHelper {

    private static final String SERVICE_PACKAGE = "com.vdiag";
    private static final String SERVICE_CLASS   = "com.vdiag.service.DiagCarService";

    private final IDiagCarService mService;

    public PermissionGateTestHelper() throws Exception {
        Context ctx = InstrumentationRegistry.getInstrumentation().getTargetContext();

        Intent intent = new Intent();
        intent.setComponent(new ComponentName(SERVICE_PACKAGE, SERVICE_CLASS));

        CountDownLatch latch = new CountDownLatch(1);
        AtomicReference<IBinder> binderRef = new AtomicReference<>();

        ServiceConnection connection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {
                binderRef.set(service);
                latch.countDown();
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
                // no-op
            }
        };

        boolean bound = ctx.bindService(intent, connection, Context.BIND_AUTO_CREATE);
        if (!bound) {
            throw new IllegalStateException("Failed to bind to " + SERVICE_CLASS);
        }

        if (!latch.await(5, TimeUnit.SECONDS)) {
            throw new IllegalStateException("Timeout waiting for DiagCarService binding");
        }

        mService = IDiagCarService.Stub.asInterface(binderRef.get());
    }

    /**
     * Calls getProperty() on the service for the given property id.
     *
     * @throws SecurityException if the caller lacks the required signature permission.
     */
    public void getProperty(int propId) throws RemoteException {
        DiagRequest request = new DiagRequest();
        request.requestId  = (int) System.currentTimeMillis();
        request.propertyId = propId;
        request.payload    = new byte[0];
        mService.getProperty(request);
    }

    /**
     * Calls subscribeProperty() on the service for the given property id.
     *
     * @throws SecurityException if the caller lacks the required signature permission.
     */
    public void subscribeProperty(int propId) throws RemoteException {
        // Passing a null listener is fine for a permission-only test because
        // the gate runs before any listener bookkeeping.
        mService.subscribeProperty(propId, 1.0f, null);
    }
}
