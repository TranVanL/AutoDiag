package com.vdiag.service;

import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.IDiagCallback;

import java.util.Map;
import java.util.NoSuchElementException;
import java.util.concurrent.ConcurrentHashMap;


public final class ClientRegistry {
    private static final String TAG = "VDiag.ClientRegistry";

    static final class ClientEntry {
        final IBinder binder;
        final IDiagCallback callback;
        final IBinder.DeathRecipient recipient;
        final int callerPid;
        final int callerUid;

        ClientEntry(
                IDiagCallback callback,
                IBinder binder,
                IBinder.DeathRecipient recipient,
                int callerPid,
                int callerUid
        ) {
            this.callback = callback;
            this.binder = binder;
            this.recipient = recipient;
            this.callerPid = callerPid;
            this.callerUid = callerUid;
        }
    }

    private final ConcurrentHashMap<IBinder, ClientEntry> mClients = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, IBinder> mClientByCaller = new ConcurrentHashMap<>();

    private static String callerKey(int pid, int uid) {
        return uid + ":" + pid;
    }

    private void removeInternal(IBinder binder, boolean fromDeath) {
        ClientEntry removed = mClients.remove(binder);
        if (removed == null) {
            return;
        }

        String key = callerKey(removed.callerPid, removed.callerUid);
        mClientByCaller.remove(key, binder);

        if (!fromDeath) {
            try {
                removed.binder.unlinkToDeath(removed.recipient, 0);
            } catch (NoSuchElementException ignored) {
                // Binder may already be dead/unlinked; state is already removed from registry.
            }
        }
    }

    public void register(IDiagCallback callback, int callerPid, int callerUid) {
        if (callback == null) {
            Log.i(TAG, "Callback is null");
            return;
        }

        final IBinder callbackBinder = callback.asBinder();
        if (callbackBinder == null) {
            Log.i(TAG, "Callback binder is null");
            return;
        }

        final IBinder.DeathRecipient deathRecipient = () -> {
            removeInternal(callbackBinder, true);
            Log.w(TAG, "Client died - auto-removed. active=" + mClients.size());
        };

        try {
            callbackBinder.linkToDeath(deathRecipient, 0);
            ClientEntry newEntry = new ClientEntry(
                    callback,
                    callbackBinder,
                    deathRecipient,
                    callerPid,
                    callerUid
            );

            ClientEntry existing = mClients.putIfAbsent(callbackBinder, newEntry);
            if (existing != null) {
                // Same callback binder already registered — drop the new deathRecipient we just linked
                callbackBinder.unlinkToDeath(deathRecipient, 0);
                Log.i(TAG, "Callback already registered, ignoring duplicate");
                return;
            }

            String key = callerKey(callerPid, callerUid);
            IBinder oldBinder = mClientByCaller.put(key, callbackBinder);
            if (oldBinder != null && oldBinder != callbackBinder) {
                removeInternal(oldBinder, false);
            }

            Log.i(TAG, "Callback registered for caller " + key + ", active=" + mClients.size());
        } catch (RemoteException e) {
            Log.e(TAG, "Error registering callback", e);
        }
    }

    public void unregister(IDiagCallback callback) {
        if (callback == null) {
            return;
        }
        IBinder callbackBinder = callback.asBinder();
        if (callbackBinder == null) {
            return;
        }
        removeInternal(callbackBinder, false);
        Log.i(TAG, "Callback unregistered");
    }

    public IDiagCallback getCallbackForCaller(int callerPid, int callerUid) {
        IBinder binder = mClientByCaller.get(callerKey(callerPid, callerUid));
        if (binder == null) {
            return null;
        }
        ClientEntry entry = mClients.get(binder);
        return entry != null ? entry.callback : null;
    }

    public int getActiveClientCount() {
        return mClients.size();
    }

    public void cleanup() {
        for (Map.Entry<IBinder, ClientEntry> entry : mClients.entrySet()) {
            ClientEntry clientEntry = entry.getValue();
            try {
                clientEntry.binder.unlinkToDeath(clientEntry.recipient, 0);
            } catch (NoSuchElementException ignored) {
                // Binder may already be dead/unlinked by death recipient path.
            }
        }
        mClients.clear();
        mClientByCaller.clear();
        Log.i(TAG, "Client registry cleanup complete");
    }
}
