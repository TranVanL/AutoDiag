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

        // Initialize JNI bridge once service starts.
        // Use 127.0.0.1 with adb reverse tcp:13400 tcp:13400 to connect to host simulator
        if (!DiagHalBridge.init("doip:127.0.0.1:13400")) {
            Log.e(TAG, "JNI init failed, service runs in degraded mode");
        }

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
        if (!DiagHalBridge.shutdown()) {
            Log.w(TAG, "JNI shutdown failed or was skipped");
        }
        mClientRegistry = null;
        super.onDestroy();
    }
}