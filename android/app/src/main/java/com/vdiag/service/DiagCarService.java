package com.vdiag.service;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.util.Log;


public class DiagCarService extends Service {
    private static final String TAG = "DiagCarService";
    private DiagCarServiceBinder mBinder;
    private ClientRegistry mClientRegistry;

    @Override
    public  void onCreate() {
        super.onCreate();
        Log.i(TAG, "DiagCarService onCreate");
        
        // Initialize JNI Bridge
        DiagHalBridge.nativeInit("mock");

        mClientRegistry = new ClientRegistry();
        mBinder = new DiagCarServiceBinder(this, mClientRegistry);
        Log.i(TAG, "DiagCarService Binder created");
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "📍 onBind — client connecting");
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
        if (mBinder != null) {
            mBinder.cleanup();
            mBinder = null;
        }
        mClientRegistry = null;
        super.onDestroy();
    }
}