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


public final class SubscriptionManager {

    private static final String TAG = "VDiag.SubManager";

    
    static final long TICK_MS = 100L;

   
    static final long ON_CHANGE_CHECK_TICKS = 10L;

   
    @FunctionalInterface
    public interface PropertyPoller {
        String readProperty(int propId);
    }

   
    record SubscriptionRecord(
            IDiagPropertyListener listener,
            IBinder binder,
            int propId,
            float rateHz,
            long tickInterval,              
            AtomicLong ticksLeft,          
            AtomicReference<String> lastValue, 
            IBinder.DeathRecipient deathRecipient
    ) {}

   
    private final CopyOnWriteArrayList<SubscriptionRecord> mRecords =
            new CopyOnWriteArrayList<>();

   
    private final ScheduledExecutorService mScheduler =
            Executors.newSingleThreadScheduledExecutor(r -> {
                Thread t = new Thread(r, "VDiag-SubTicker");
                t.setDaemon(true);
                return t;
            });

    private final PropertyPoller mPoller;

   
    public SubscriptionManager(PropertyPoller poller) {
        mPoller = poller;
        mScheduler.scheduleAtFixedRate(this::onTick, TICK_MS, TICK_MS, TimeUnit.MILLISECONDS);
        Log.i(TAG, "SubscriptionManager started — tick=" + TICK_MS + "ms");
    }

    public void register(int propId, float rateHz, IDiagPropertyListener listener) {
        if (listener == null) return;

        IBinder binder = listener.asBinder();
        if (binder == null) return;

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

        long initialTicks = (rateHz <= 0f) ? ON_CHANGE_CHECK_TICKS : tickInterval;

        SubscriptionRecord record = new SubscriptionRecord(
                listener,
                binder,
                propId,
                rateHz,
                tickInterval,
                new AtomicLong(initialTicks),
                new AtomicReference<>(null),  
                dr
        );

        mRecords.add(record);
       
        Log.i(TAG, "register propId=0x" + Integer.toHexString(propId)
                + " rateHz=" + rateHz
                + " tickInterval=" + tickInterval
                + " total=" + mRecords.size());
    }

    public void unregister(int propId, IDiagPropertyListener listener) {
        if (listener == null) return;
        IBinder binder = listener.asBinder();

        mRecords.removeIf(r -> {
            if (r.propId() == propId && r.binder() == binder) {
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

    private void onTick() {
        for (SubscriptionRecord r : mRecords) {
            boolean isOnChange = r.rateHz() <= 0f;
            long left = r.ticksLeft().decrementAndGet();

            if (left > 0) continue; 

            r.ticksLeft().set(isOnChange ? ON_CHANGE_CHECK_TICKS : r.tickInterval());

            
            String newValue = mPoller.readProperty(r.propId());
            if (newValue == null) continue;

            String oldValue = r.lastValue().getAndSet(newValue);

           
            if (isOnChange && newValue.equals(oldValue)) continue;

            dispatchEvent(r, newValue);
        }
    }

    private void dispatchEvent(SubscriptionRecord r, String value) {
        DiagPropertyEvent event = new DiagPropertyEvent();
        event.proId      = r.propId();
        event.areaId     = 0;   
        event.timestampNs = (int) (System.nanoTime() & 0x7FFF_FFFFL);
        event.valueString = value;
        event.status     = 0;  

 
        try {
            event.valueInt = Integer.parseInt(value);
        } catch (NumberFormatException ignored) {
            event.valueInt = 0;
        }

        try {
            r.listener().onPropertyChanged(event);
        } catch (RemoteException e) {
            
            Log.w(TAG, "dispatch RemoteException — force-removing propId=0x"
                    + Integer.toHexString(r.propId()));
            removeByBinder(r.binder());
        }
    }


    private void removeByBinder(IBinder binder) {
        mRecords.removeIf(r -> r.binder() == binder);
    }
}
