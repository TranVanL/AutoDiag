package com.vdiag.sdk;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.DiagRequest;
import com.vdiag.IDiagCallback;
import com.vdiag.IDiagCarService;
import com.vdiag.DiagPropertyEvent;
import com.vdiag.IDiagPropertyListener;

import java.io.FileInputStream;
import java.io.IOException;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * SDK facade that hides Binder details and exposes diagnostic property APIs.
 */

// SDK
// Build a Diag Client directly communicate with service , application or other service only use it , don't need to know about how connection or bind to service , don't care about IPC or Binder

public final class DiagClient implements AutoCloseable {
    private static final String TAG = "DiagClient";

    // Error Code
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
    private final Set<SubscriptionToken> activeSubscriptions;

    // Interface binder object to DiagCarService
    private volatile IDiagCarService diagService;
    private volatile boolean bound;
    private volatile DiagListener listener;
    private volatile ConnectionListener connectionListener;

    private final Executor mainExecutor;

    public interface ConnectionListener {
        void onConnectionChanged(boolean connected, String message);
    }

    public interface PropertySubscriptionCallback {
        void onPropertyChanged(DiagProperty property, DiagPropertyEvent event);

        void onPropertyError(DiagProperty property, int areaId, int errorCode);
    }

    // Implement class Token that will return for application to unsubscribe property
    // Token represent for subscription in service
    public final class SubscriptionToken implements AutoCloseable {
        // Member : DiagProperty , Listener callback , atomic variable for check subscription is active or not
        private final DiagProperty property;
        private final int areaId;
        private final IDiagPropertyListener remoteListener;
        private final AtomicBoolean active = new AtomicBoolean(true);

        private SubscriptionToken(DiagProperty property, int areaId,
                                  IDiagPropertyListener remoteListener) {
            this.property = property;
            this.areaId = areaId;
            this.remoteListener = remoteListener;
        }

        public boolean isActive() {
            return active.get();
        }

        public void unsubscribe() {
            if (!active.compareAndSet(true, false)) {
                return;
            }
            unsubscribeInternal(property, areaId, remoteListener);
            activeSubscriptions.remove(this);
        }

        @Override
        public void close() {
            unsubscribe();
        }
    }

    // Implement Stub to handle response when client send request
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

        @Override
        public void onProgress(long bytesWritten, long totalBytes) {
            // Progress is only used for firmware flash; SDK consumers can ignore.
            Log.d(TAG, "onProgress: " + bytesWritten + "/" + totalBytes);
        }
    };

    // Create new connection to connect Service
    private final ServiceConnection connection = new ServiceConnection() {
        // Function will be called after client bind to service and service call onBInd() and return a binder object
        // onServiceConnected() will be called when service return binder object
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            // Get interface to DiagCarService
            diagService = IDiagCarService.Stub.asInterface(service);
            try {
                // Register callback to service
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

        // onServiceDisconnected() will be called when service connection disconnect (service crash or system kill it) , don't run when client unbind service
        // Service no longer connect, maybe crash or kill
        @Override
        public void onServiceDisconnected(ComponentName name) {
            Log.w(TAG, "Service disconnected");
            diagService = null;
            dispatchConnectionChanged(false, "Service disconnected");
            dispatchError(null, ERR_NOT_CONNECTED, "Service disconnected", -1);
        }

        // Binder object died , can retry bind
        @Override
        public void onBindingDied(ComponentName name) {
            Log.e(TAG, "Service binding died");
            diagService = null;
            dispatchConnectionChanged(false, "Service binding died");
            dispatchError(null, ERR_NOT_CONNECTED, "Service binding died", -1);
        }

        // Return null object , service don't permit client to bind

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
        this.activeSubscriptions = ConcurrentHashMap.newKeySet();
        this.mainExecutor = command -> mainHandler.post(command);
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

    
    public SubscriptionToken subscribeProperty(DiagProperty property,
                                               float rateHz,
                                               Executor executor,
                                               PropertySubscriptionCallback callback) {
        return subscribeProperty(property, DiagProperty.AREA_GLOBAL, rateHz, executor, callback);
    }

    public SubscriptionToken subscribeProperty(DiagProperty property, int areaId,
                                               float rateHz, Executor executor,
                                               PropertySubscriptionCallback callback) {
        Objects.requireNonNull(property, "property == null");
        Objects.requireNonNull(callback, "callback == null");

        final IDiagCarService service = diagService;
        if (closed.get() || service == null) {
            throw new IllegalStateException("service is not connected");
        }

        final Executor callbackExecutor = (executor != null) ? executor : mainExecutor;

        final IDiagPropertyListener remoteListener = new IDiagPropertyListener.Stub() {
            @Override
            public void onPropertyChanged(DiagPropertyEvent event) {
                callbackExecutor.execute(() -> callback.onPropertyChanged(property, event));
            }

            @Override
            public void onPropertyError(int proId, int areaId, int errorCode) {
                callbackExecutor.execute(() -> callback.onPropertyError(property, areaId, errorCode));
            }
        };

        try {
            if (areaId == DiagProperty.AREA_GLOBAL) {
                service.subscribeProperty(property.getPropId(), rateHz, remoteListener);
            } else {
                service.subscribePropertyForArea(property.getPropId(), areaId, rateHz, remoteListener);
            }
            SubscriptionToken token = new SubscriptionToken(property, areaId, remoteListener);
            activeSubscriptions.add(token);
            Log.i(TAG, "subscribeProperty ok prop=" + property.name() + " rateHz=" + rateHz);
            return token;
        } catch (RemoteException e) {
            Log.e(TAG, "subscribeProperty failed", e);
            throw new RuntimeException("subscribeProperty failed", e);
        }
    }

    private void unsubscribeInternal(DiagProperty property, int areaId,
                                     IDiagPropertyListener remoteListener) {
        final IDiagCarService service = diagService;
        if (service == null || remoteListener == null) {
            return;
        }
        try {
            if (areaId == DiagProperty.AREA_GLOBAL) {
                service.unsubscribeProperty(property.getPropId(), remoteListener);
            } else {
                service.unsubscribePropertyForArea(property.getPropId(), areaId, remoteListener);
            }
            Log.i(TAG, "unsubscribeProperty ok prop=" + property.name());
        } catch (RemoteException e) {
            Log.w(TAG, "unsubscribeProperty failed prop=" + property.name(), e);
        }
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

        for (SubscriptionToken token : activeSubscriptions.toArray(new SubscriptionToken[0])) {
            token.unsubscribe();
        }
        activeSubscriptions.clear();

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

    
    public String getDtcSnapshotShared() {
        final IDiagCarService service = diagService;
        if (closed.get() || service == null) {
            dispatchError(null, ERR_NOT_CONNECTED, "service is not connected", -1);
            return null;
        }

        try {
            int[] meta = new int[2]; // [0]=size bytes, [1]=record count
            ParcelFileDescriptor pfd = service.getDtcSnapshotShared(meta);
            if (pfd == null) {
                dispatchError(null, ERR_REMOTE_EXCEPTION,
                        "service returned null PFD", -1);
                return null;
            }

            try (FileInputStream fis = new FileInputStream(pfd.getFileDescriptor());
                 FileChannel channel = fis.getChannel()) {

                MappedByteBuffer buffer = channel.map(
                        FileChannel.MapMode.READ_ONLY, 0, meta[0]);

                byte[] bytes = new byte[meta[0]];
                buffer.get(bytes);
                return new String(bytes, StandardCharsets.UTF_8);
            } finally {
                pfd.close();
            }
        } catch (RemoteException e) {
            Log.e(TAG, "getDtcSnapshotShared failed", e);
            dispatchError(null, ERR_REMOTE_EXCEPTION,
                    "getDtcSnapshotShared failed: " + e.getMessage(), -1);
            return null;
        } catch (IOException e) {
            Log.e(TAG, "getDtcSnapshotShared I/O failed", e);
            dispatchError(null, ERR_REMOTE_EXCEPTION,
                    "getDtcSnapshotShared I/O failed: " + e.getMessage(), -1);
            return null;
        }
    }
}
