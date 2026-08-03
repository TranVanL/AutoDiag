package com.vdiag.service;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

// Class for health status and lifecycle manager
public final class ShimSystemClient implements ISystemLifecycle {
    private static final String TAG = "DiagShimClient";

    static final int MAX_QUEUE_DEPTH = 32;

    private static final long HEARTBEAT_MS = 3_000L;

    
    private final Handler mHandler = new Handler(Looper.getMainLooper());

   
    private final Runnable mHeartbeat = new Runnable() {
        @Override
        public void run() {
            // Run health status check every HEARTBEAT_MS
            performHealthCheck();
            mHandler.postDelayed(this, HEARTBEAT_MS);
        }
    };

   

    @Override
    public void start() {
        Log.i(TAG, "ShimSystemClient.start() — simulating CarWatchdog heartbeat every "
                + HEARTBEAT_MS + " ms");
       
        mHandler.post(mHeartbeat);
    }

    @Override
    public void stop() {
        mHandler.removeCallbacks(mHeartbeat);
        Log.i(TAG, "ShimSystemClient.stop() — heartbeat cancelled");
      
    }

    // Simulate like WatchdogService call health check through Binder IPC every HEARTBEAT_MS
    // Below is function that WatchDogService request
    private void performHealthCheck() {
        final boolean halReady    = DiagHalBridge.isNativeReady();
        final boolean workerAlive = halReady && DiagHalBridge.isWorkerAlive();
        final int     queueDepth  = halReady ? DiagHalBridge.getQueueDepth() : 0;
        final boolean queueOk     = queueDepth < MAX_QUEUE_DEPTH;
        final boolean healthy     = halReady && workerAlive && queueOk;

        if (healthy) {
            Log.d(TAG, "[shim-watchdog] healthy=true"
                    + " workerAlive=true"
                    + " queueDepth=" + queueDepth);
        } else {
            Log.w(TAG, "[shim-watchdog] UNHEALTHY"
                    + " halReady="    + halReady
                    + " workerAlive=" + workerAlive
                    + " queueDepth="  + queueDepth
                    + " queueOk="     + queueOk);
        }
    }
}
