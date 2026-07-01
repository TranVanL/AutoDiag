package com.vdiag.sdk;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.DiagRequest;
import com.vdiag.IDiagCallback;
import com.vdiag.IDiagCarService;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * SDK facade that hides Binder details and exposes diagnostic property APIs.
 */
public final class DiagClient implements AutoCloseable {
    private static final String TAG = "DiagClient";

    public static final int ERR_NOT_CONNECTED = -1001;
    public static final int ERR_BIND_FAILED = -1002;
    public static final int ERR_REMOTE_EXCEPTION = -1003;
    public static final int ERR_UNKNOWN_REQUEST = -1004;

    private static final String SERVICE_CLASS_NAME = "com.vdiag.service.DiagCarService";

    private final Context appContext;
    private final Handler mainHandler;
    private final AtomicInteger nextRequestId;
    private final AtomicBoolean closed;
    private final Map<Integer, DiagProperty> inFlight;

    private volatile IDiagCarService diagService;
    private volatile boolean bound;
    private volatile DiagListener listener;
    private volatile ConnectionListener connectionListener;

    public interface ConnectionListener {
        void onConnectionChanged(boolean connected, String message);
    }

    private final IDiagCallback sdkCallback = new IDiagCallback.Stub() {
        @Override
        public void onResult(int requestId, String value, long latencyUs) {
            final DiagProperty property = inFlight.remove(requestId);
            if (property == null) {
                dispatchError(null, ERR_UNKNOWN_REQUEST,
                        "No in-flight property for requestId=" + requestId, requestId);
                return;
            }
            dispatchSuccess(property, value, latencyUs, requestId);
        }

        @Override
        public void onError(int requestId, int errorCode, String errorMsg) {
            final DiagProperty property = inFlight.remove(requestId);
            dispatchError(property, errorCode, errorMsg, requestId);
        }
    };

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            diagService = IDiagCarService.Stub.asInterface(service);
            try {
                diagService.registerCallback(sdkCallback);
                Log.i(TAG, "Connected and callback registered");
                dispatchConnectionChanged(true, "Connected to DiagCarService");
            } catch (RemoteException e) {
                Log.e(TAG, "registerCallback failed", e);
                diagService = null;
                dispatchConnectionChanged(false, "registerCallback failed");
                dispatchError(null, ERR_REMOTE_EXCEPTION,
                        "registerCallback failed: " + e.getMessage(), -1);
            }
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            Log.w(TAG, "Service disconnected");
            diagService = null;
            dispatchConnectionChanged(false, "Service disconnected");
            dispatchError(null, ERR_NOT_CONNECTED, "Service disconnected", -1);
        }

        @Override
        public void onBindingDied(ComponentName name) {
            Log.e(TAG, "Service binding died");
            diagService = null;
            dispatchConnectionChanged(false, "Service binding died");
            dispatchError(null, ERR_NOT_CONNECTED, "Service binding died", -1);
        }

        @Override
        public void onNullBinding(ComponentName name) {
            Log.e(TAG, "Service returned null binding");
            diagService = null;
            dispatchConnectionChanged(false, "Service returned null binding");
            dispatchError(null, ERR_NOT_CONNECTED, "Service returned null binding", -1);
        }
    };

    private DiagClient(Context context) {
        this.appContext = context.getApplicationContext();
        this.mainHandler = new Handler(Looper.getMainLooper());
        this.nextRequestId = new AtomicInteger(1);
        this.closed = new AtomicBoolean(false);
        this.inFlight = new ConcurrentHashMap<>();
    }

    /**
     * Creates SDK client and starts binding to DiagCarService.
     */
    public static DiagClient create(Context context) {
        DiagClient client = new DiagClient(context);
        client.bind();
        return client;
    }

    /**
     * Receives callbacks on the main thread.
     */
    public void setListener(DiagListener listener) {
        this.listener = listener;
    }

    public void setConnectionListener(ConnectionListener connectionListener) {
        this.connectionListener = connectionListener;
    }

    public boolean isConnected() {
        return diagService != null && !closed.get();
    }

    /**
     * Requests one diagnostic property asynchronously.
     */
    public int getProperty(DiagProperty property) {
        if (property == null) {
            dispatchError(null, ERR_UNKNOWN_REQUEST, "property is null", -1);
            return -1;
        }

        final int requestId = nextRequestId.getAndIncrement();
        final IDiagCarService service = diagService;

        if (closed.get()) {
            dispatchError(property, ERR_NOT_CONNECTED, "client is closed", requestId);
            return requestId;
        }

        if (service == null) {
            dispatchError(property, ERR_NOT_CONNECTED, "service is not connected", requestId);
            return requestId;
        }

        DiagRequest req = new DiagRequest();
        req.requestId = requestId;
        req.propertyId = property.getPropId();

        inFlight.put(requestId, property);
        try {
            service.getProperty(req);
            Log.d(TAG, "getProperty dispatched reqId=" + requestId + " prop=" + property.name());
        } catch (RemoteException e) {
            inFlight.remove(requestId);
            Log.e(TAG, "getProperty failed", e);
            dispatchError(property, ERR_REMOTE_EXCEPTION,
                    "RemoteException: " + e.getMessage(), requestId);
        }
        return requestId;
    }

    private void bind() {
        if (closed.get() || bound) {
            return;
        }

        Intent intent = new Intent();
        intent.setComponent(new ComponentName(appContext.getPackageName(), SERVICE_CLASS_NAME));

        bound = appContext.bindService(intent, connection, Context.BIND_AUTO_CREATE);
        if (!bound) {
            dispatchConnectionChanged(false, "bindService failed");
            dispatchError(null, ERR_BIND_FAILED, "bindService failed", -1);
        }
    }

    @Override
    public void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }

        IDiagCarService service = diagService;
        if (service != null) {
            try {
                service.unregisterCallback(sdkCallback);
            } catch (RemoteException e) {
                Log.w(TAG, "unregisterCallback failed during close", e);
            }
        }

        if (bound) {
            try {
                appContext.unbindService(connection);
            } catch (IllegalArgumentException e) {
                Log.w(TAG, "unbindService ignored", e);
            }
            bound = false;
        }

        diagService = null;
        inFlight.clear();
        listener = null;
        connectionListener = null;
    }

    private void dispatchSuccess(final DiagProperty property,
                                 final String value,
                                 final long latencyUs,
                                 final int requestId) {
        final DiagListener currentListener = listener;
        if (currentListener == null) {
            return;
        }
        mainHandler.post(() -> currentListener.onPropertyReceived(property, value, latencyUs, requestId));
    }

    private void dispatchError(final DiagProperty property,
                               final int code,
                               final String message,
                               final int requestId) {
        final DiagListener currentListener = listener;
        if (currentListener == null) {
            return;
        }
        mainHandler.post(() -> currentListener.onError(property, code, message, requestId));
    }

    private void dispatchConnectionChanged(final boolean connected, final String message) {
        final ConnectionListener currentConnectionListener = connectionListener;
        if (currentConnectionListener == null) {
            return;
        }
        mainHandler.post(() -> currentConnectionListener.onConnectionChanged(connected, message));
    }
}
