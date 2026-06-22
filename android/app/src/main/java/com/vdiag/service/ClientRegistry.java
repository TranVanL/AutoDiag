package com.vdiag.service;

import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.IDiagCallback;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;


public final class ClientRegistry {
    public static final String TAG = "VDiag.ClientRegistry";

    public static final class ClientEntry {
        final IBinder binder;
        final IDiagCallback callback;
        final IBinder.DeathRecipient recipient;

        ClientEntry(IDiagCallback callback, IBinder binder, IBinder.DeathRecipient recipient) {
            this.callback = callback;
            this.binder = binder;
            this.recipient = recipient;
        }
    }

    public final ConcurrentHashMap<IBinder, ClientEntry> mClients = new ConcurrentHashMap<>();

    public void register(IDiagCallback callback) {
        if (callback == null) {
            Log.i(TAG, "Callback is null");
            return;
        }

        final IBinder callbackBinder = callback.asBinder();
        if (callbackBinder == null) {
            Log.i(TAG, "Callback binder is null");
            return;
        }

        if (mClients.containsKey(callbackBinder)) {
            Log.i(TAG, "Callback already registered");
            return;
        }

        final IBinder.DeathRecipient deathRecipient = () -> {
            ClientEntry removed = mClients.remove(callbackBinder);
            if (removed != null) {
                Log.w(TAG, "Client died - auto-removed. active=" + mClients.size());
            }
        };

        try {
            callbackBinder.linkToDeath(deathRecipient, 0);
            ClientEntry newEntry = new ClientEntry(callback, callbackBinder, deathRecipient);
            ClientEntry previous = mClients.putIfAbsent(callbackBinder, newEntry);
            if (previous != null) {
                callbackBinder.unlinkToDeath(deathRecipient, 0);
                Log.i(TAG, "Callback already registered");
                return;
            }
            Log.i(TAG, "Callback registered");
        } catch (RemoteException e) {
            Log.e(TAG, "Error registering callback", e);
        }
    }

    public void unregister(IDiagCallback callback) {
        if (callback == null) {
            return;
        }
        IBinder callbackBinder = callback.asBinder();
        if (callbackBinder == null) return;
        ClientEntry removed = mClients.remove(callbackBinder);
        if (removed == null) return;
        removed.binder.unlinkToDeath(removed.recipient, 0);
        Log.i(TAG, "Callback unregistered");
    }

    public void cleanup() {
        for (Map.Entry<IBinder, ClientEntry> entry : mClients.entrySet()) {
            ClientEntry clientEntry = entry.getValue();
            clientEntry.binder.unlinkToDeath(clientEntry.recipient, 0);
        }
        mClients.clear();
        Log.i(TAG, "Client registry cleanup complete");
    }
}
