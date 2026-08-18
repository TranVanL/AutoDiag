package com.vdiag.service;

import android.app.Service;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.IBinder;
import android.util.Log;


public class DiagCarService extends Service {

    private static final String TAG = "DiagCarService";

    // Member : Binder for communication with clients, Client Registry for client management , Subscription Manager for client registration and property gathering.
    private DiagCarServiceBinder mBinder;
    private ClientRegistry       mClientRegistry;
    private SubscriptionManager  mSubManager;

   // System Lifecycle client with 3 seconds watchdog and lifecycle follow state machine (Follow PowerManagerService pattern)
    private ISystemLifecycle mSystemClient;

    private DtcStore mDtcStore;

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "DiagCarService onCreate");

        // When create Service , create JNI Hal Bridge and verify HAL works properly
        if (!DiagHalBridge.init("mock")) {
            Log.e(TAG, "JNI init failed — service runs in degraded mode");
        }

        mDtcStore = new DtcStore();
        mDtcStore.seedDemoData();
        // Init members
        // 2. Binder-layer infrastructure.
        mClientRegistry = new ClientRegistry();
        mSubManager     = new SubscriptionManager(
            (SubscriptionManager.AreaPropertyPoller) DiagHalBridge::readProperty);
        mBinder         = new DiagCarServiceBinder(this, mClientRegistry, mSubManager, mDtcStore);
        Log.i(TAG, "DiagCarService Binder created");

        mSystemClient = createSystemClient();
        mSystemClient.start();
    }

    @Override
    public IBinder onBind(Intent intent) {
        // When client bind to service , it calls this method to return Binder
        Log.i(TAG, "onBind — client connecting");
        return mBinder;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        // When client unbind from service , it calls this method to clean up
        // If all of clients unbind , service will be destroyed (Bound Service)
        Log.i(TAG, "onUnbind — client disconnecting");
        return super.onUnbind(intent);
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "DiagCarService onDestroy");
        // Clear resources
        if (mSystemClient != null) {
            mSystemClient.stop();
            mSystemClient = null;
        }

        if (mBinder != null) {
            mBinder.cleanup();
            mBinder = null;
        }

        if (!DiagHalBridge.shutdown()) {
            Log.w(TAG, "JNI shutdown failed or was skipped");
        }
        mClientRegistry = null;
        mSubManager     = null;
        super.onDestroy();
    }

    ISystemLifecycle createSystemClient() {
        return new ShimSystemClient();
    }
}