package com.vdiag.service;

import android.content.Context;
import android.util.Log;


public final class CarApiSystemClient implements ISystemLifecycle {

    private static final String TAG = "DiagCarApiClient";

  
    @SuppressWarnings("FieldCanBeLocal")
    private final Context mContext;

  
    private final ShimSystemClient mShim;

    CarApiSystemClient(Context context) {
        mContext = context;
        mShim    = new ShimSystemClient();
    }

   
    @Override
    public void start() {
        Log.i(TAG, "CarApiSystemClient.start() — android.car not yet linked, "
                + "delegating to ShimSystemClient");
       
        mShim.start();
    }

   
    @Override
    public void stop() {
        Log.i(TAG, "CarApiSystemClient.stop()");
        
        mShim.stop();
    }
}
