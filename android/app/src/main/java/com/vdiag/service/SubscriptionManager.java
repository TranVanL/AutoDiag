package com.vdiag.service;

import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.DiagPropertyEvent;
import com.vdiag.IDiagPropertyListener;

import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

// Class for clients subscribe , don't need to send request manually , register with proID and rateHz to get property frequently
// Use push(subscribe) architecture instead of pull(request) architecture
public final class SubscriptionManager {

    private static final String TAG = "VDiag.SubManager";
    // Transfer time to TICK for easily managing
    static final long TICK_MS = 100L;

    static final long ON_CHANGE_CHECK_TICKS = 10L;

   // Function pointer for read property
    @FunctionalInterface
    public interface PropertyPoller {
        String readProperty(int propId);
    }

    @FunctionalInterface
    public interface AreaPropertyPoller {
        String readProperty(int propId, int areaId);
    }


    // A record for each subscription (Consist of information clients when they register )
    // A client can have one or multiple subscriptions
    record SubscriptionRecord(
            IDiagPropertyListener listener,
            IBinder binder,
            int propId,
            int areaId,
            float rateHz,
            long tickInterval,              
            AtomicLong ticksLeft,          
            AtomicReference<String> lastValue, 
            IBinder.DeathRecipient deathRecipient
    ) {}

   
    private final CopyOnWriteArrayList<SubscriptionRecord> mRecords =
            new CopyOnWriteArrayList<>();

   // Scheduler manage thread for subscription
    private final ScheduledExecutorService mScheduler =
            Executors.newSingleThreadScheduledExecutor(r -> {
                Thread t = new Thread(r, "VDiag-SubTicker");
                t.setDaemon(true);
                return t;
            });

    private final PropertyPoller mPoller;
    private final AreaPropertyPoller mAreaPoller;

   
    public SubscriptionManager(PropertyPoller poller) {
        this(poller, (propId, areaId) -> poller.readProperty(propId));
    }

    public SubscriptionManager(AreaPropertyPoller poller) {
        this(null, poller);
    }

    private SubscriptionManager(PropertyPoller poller, AreaPropertyPoller areaPoller) {
        mPoller = poller;
        mAreaPoller = areaPoller;
        mScheduler.scheduleAtFixedRate(this::onTick, TICK_MS, TICK_MS, TimeUnit.MILLISECONDS);
        Log.i(TAG, "SubscriptionManager started — tick=" + TICK_MS + "ms");
    }

    public void register(int propId, float rateHz, IDiagPropertyListener listener) {
        register(propId, 0, rateHz, listener);
    }

    public void register(int propId, int areaId, float rateHz, IDiagPropertyListener listener) {
        if (listener == null) return;
        // Get binder object represent client
        IBinder binder = listener.asBinder();
        if (binder == null) return;

        // Calculate tick numbers , 1 TICK = TICK_MS
        long tickInterval = (rateHz <= 0f)
                ? Long.MAX_VALUE
                : Math.max(1L, Math.round(1000.0 / (rateHz * TICK_MS)));

        IBinder.DeathRecipient dr = () -> {
            removeByBinder(binder);
            Log.w(TAG, "☠ Subscriber died — propId=0x" + Integer.toHexString(propId)
                    + " auto-removed, active=" + mRecords.size());
        };

        try {
            binder.linkToDeath(dr, 0);
        } catch (RemoteException e) {
            Log.w(TAG, "linkToDeath failed — client already dead propId=0x"
                    + Integer.toHexString(propId));
            return;
        }

        // Init Ticks for first time , use count down method
        // ON_CHANGE_CHECK_TICKS : Only changed data will be sent to client
        long initialTicks = (rateHz <= 0f) ? ON_CHANGE_CHECK_TICKS : tickInterval;


        // Create subscription record for client
        SubscriptionRecord record = new SubscriptionRecord(
                listener,
                binder,
                propId,
                areaId,
                rateHz,
                tickInterval,
                new AtomicLong(initialTicks),
                new AtomicReference<>(null),  
                dr
        );
        // add to list
        mRecords.add(record);
       
        Log.i(TAG, "register propId=0x" + Integer.toHexString(propId)
                + " rateHz=" + rateHz
                + " areaId=" + areaId
                + " tickInterval=" + tickInterval
                + " total=" + mRecords.size());
    }

    public void unregister(int propId, IDiagPropertyListener listener) {
        unregister(propId, 0, listener);
    }

    public void unregister(int propId, int areaId, IDiagPropertyListener listener) {
        if (listener == null) return;
        IBinder binder = listener.asBinder();

        // Remove out of list and unlink to death recipient
        mRecords.removeIf(r -> {
            if (r.propId() == propId && r.areaId() == areaId && r.binder() == binder) {
                try {
                    r.binder().unlinkToDeath(r.deathRecipient(), 0);
                } catch (Exception ignored) {
                   
                }
                Log.i(TAG, "unregister propId=0x" + Integer.toHexString(propId));
                return true;
            }
            return false;
        });
    }

    public void shutdown() {
        mScheduler.shutdown();
        mRecords.clear();
        Log.i(TAG, "SubscriptionManager shutdown — all records cleared");
    }

    // Loop function
    private void onTick() {
        // Iterate lists of subscription record
        for (SubscriptionRecord r : mRecords) {
            boolean isOnChange = r.rateHz() <= 0f;
            // Decrease tick numbers
            long left = r.ticksLeft().decrementAndGet();

            if (left > 0) continue; 

            // If left == 0 , it is moment to send
            r.ticksLeft().set(isOnChange ? ON_CHANGE_CHECK_TICKS : r.tickInterval());

            // Compare new and old value
            String newValue = mAreaPoller.readProperty(r.propId(), r.areaId());
            if (newValue == null) {
                dispatchUnavailable(r);
                continue;
            }

            String oldValue = r.lastValue().getAndSet(newValue);

           
            if (isOnChange && newValue.equals(oldValue)) continue;

            dispatchEvent(r, newValue);

        }
    }

    // Dispatch Event of property to client
    private void dispatchEvent(SubscriptionRecord r, String value) {
        DiagPropertyEvent event = new DiagPropertyEvent();
        event.propertyId = r.propId();
        event.areaId     = r.areaId();
        event.status     = 0;  
        event.timestampNs = System.nanoTime();
        event.stringValue = value;
 
        try {
            event.intValue = Integer.parseInt(value);
        } catch (NumberFormatException ignored) {
            event.intValue = 0;
        }

        try {
            // Call function that client request to handle event through Binder IPC
            r.listener().onPropertyChanged(event);
        } catch (RemoteException e) {
            
            Log.w(TAG, "dispatch RemoteException — force-removing propId=0x"
                    + Integer.toHexString(r.propId()));
            removeByBinder(r.binder());
        }
    }

    private void dispatchUnavailable(SubscriptionRecord r) {
        try {
            // Dedicated error channel; code 1 maps to UNAVAILABLE.
            r.listener().onPropertyError(r.propId(), r.areaId(), 1);
        } catch (RemoteException e) {
            removeByBinder(r.binder());
        }
    }


    private void removeByBinder(IBinder binder) {
        mRecords.removeIf(r -> r.binder() == binder);
    }
}
