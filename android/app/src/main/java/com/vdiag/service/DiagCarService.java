package com.vdiag.service;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.RemoteCallbackList;
import android.os.RemoteException;
import android.util.Log;

import com.vdiag.DiagRequest;
import com.vdiag.IDiagCallback;
import com.vdiag.IDiagCarService;


public class DiagCarService extends Service {
    private final RemoteCallbackList<IDiagCallback> callbacks = new RemoteCallbackList<>();
    private static final String TAG = "DiagCarService";
    private DiagCarServiceBinder mBinder;

    @Override
    public  void onCreate() {
        super.onCreate();
        Log.i(TAG, "DiagCarService onCreate");
        mBinder = new DiagCarServiceBinder(this);
        Log.i(TAG, "DiagCarService Binder created");
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "📍 onBind — client connecting");
        return mBinder;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        Log.i(TAG, "📍 onUnbind — client disconnecting");
        return super.onUnbind(intent);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "onStartCommand — startId=" + startId);
        return START_STICKY;  // survive process restart
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "DiagCarService onDestroy");
        callbacks.kill();
        if (mBinder != null) {
            mBinder.cleanup();
            mBinder = null;
        }
        super.onDestroy();
    }
}