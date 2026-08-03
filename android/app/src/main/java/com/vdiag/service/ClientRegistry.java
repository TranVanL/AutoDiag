package com.vdiag.service;

import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.IDiagCallback;

import java.util.Map;
import java.util.NoSuchElementException;
import java.util.concurrent.ConcurrentHashMap;


// Class for storage and retrieval of callback objects for clients.
public final class ClientRegistry {
    private static final String TAG = "VDiag.ClientRegistry";

    // Class for clients , information of each client
    static final class ClientEntry {
        // binder object of client , represent client
        final IBinder binder;

        // callback object of client
        final IDiagCallback callback;

        // death recipient when binder object is dead , binder driver will track binder object and call death recipient when binder object is dead (must link to death recipient)

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

    // Storage clients with concurrentHashMap for safety in multi-threading environment
    private final ConcurrentHashMap<IBinder, ClientEntry> mClients = new ConcurrentHashMap<>();

    // Get value is binder object represent for client through unique key that combine from UID + PID
    private final ConcurrentHashMap<String, IBinder> mClientByCaller = new ConcurrentHashMap<>();

    private static String callerKey(int pid, int uid) {
        return uid + ":" + pid;
    }

    private void removeInternal(IBinder binder, boolean fromDeath) {
        // Remove out information clientEntry list
        ClientEntry removed = mClients.remove(binder);
        if (removed == null) {
            return;
        }

        // Erase in map : Unique Key , Value is binder object
        String key = callerKey(removed.callerPid, removed.callerUid);
        mClientByCaller.remove(key, binder);

        if (!fromDeath) {
            try {

                //  don't die , just unregister , careful with memory leak
                // Unlink to death recipient
                // Need to unlink to binder driver don't track binder object anymore , if not , it can be make a lot of issue like memory leak , remove , delete 2 times , ........
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
        // Get binder object (represent client)
        final IBinder callbackBinder = callback.asBinder();
        if (callbackBinder == null) {
            Log.i(TAG, "Callback binder is null");
            return;
        }

        // Link Binder object to death recipient
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


    // Get callback object for client by caller's PID and UID
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
