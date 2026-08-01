package com.vdiag.service;

import android.app.Service;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.IBinder;
import android.util.Log;

public class DiagCarService extends Service {

    private static final String TAG = "DiagCarService";

    private DiagCarServiceBinder mBinder;
    private ClientRegistry       mClientRegistry;
    private SubscriptionManager  mSubManager;

   
    private ISystemLifecycle mSystemClient;

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "DiagCarService onCreate");

        
        if (!DiagHalBridge.init("mock")) {
            Log.e(TAG, "JNI init failed — service runs in degraded mode");
        }

        // 2. Binder-layer infrastructure.
        mClientRegistry = new ClientRegistry();
        mSubManager     = new SubscriptionManager(DiagHalBridge::readProperty);
        mBinder         = new DiagCarServiceBinder(this, mClientRegistry, mSubManager);
        Log.i(TAG, "DiagCarService Binder created");

        mSystemClient = createSystemClient();
        mSystemClient.start();
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "onBind — client connecting");
        return mBinder;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        Log.i(TAG, "onUnbind — client disconnecting");
        return super.onUnbind(intent);
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "DiagCarService onDestroy");

     
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