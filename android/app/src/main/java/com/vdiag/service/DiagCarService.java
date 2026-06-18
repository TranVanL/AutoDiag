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
    private final IDiagCarService.Stub binder = new IDiagCarService.Stub() {
        @Override
        public void sendRequest(DiagRequest request, IDiagCallback callback) throws RemoteException {
            Log.d(TAG, "sendRequest from Pid: " + android.os.Binder.getCallingPid());
            if (callback != null) {
                callback.onResponse(request, 0);
            }
        }
        @Override
        public void registerCallback(IDiagCallback callback)  {
            Log.d(TAG, "registerCallback from Pid: " + android.os.Binder.getCallingPid());
            if (callback != null) {
                callbacks.register(callback);
                Log.d(TAG, "registerCallback success");
            }
        }
        @Override
        public void unregisterCallback(IDiagCallback callback)  {
            if (callback != null) {
                callbacks.unregister(callback);
                Log.d(TAG, "unregisterCallback success");
            }
        }

        @Override
        public boolean isConnected()  {
            Log.d(TAG, "Current Status : Not Connected");
            return false;
        }
    };

    @Override
    public  void onCreate() {
        super.onCreate();
        Log.i(TAG, "DiagCarService onCreate");
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "📍 onBind — client connecting");
        return binder;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        Log.i(TAG, "📍 onUnbind — client disconnecting");
        return super.onUnbind(intent);
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "DiagCarService onDestroy");
        callbacks.kill();
        super.onDestroy();
    }
}