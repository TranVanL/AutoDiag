package com.vdiag;

import com.vdiag.DiagRequest;
import com.vdiag.IDiagCallback;

import com.vdiag.IDiagPropertyListener;
import com.vdiag.DiagPropertyEvent;

interface IDiagCarService {
    void registerCallback(in IDiagCallback callback);
    void unregisterCallback(in IDiagCallback callback);
    void getProperty(in DiagRequest request);
    void subscribeProperty(int proId , float rateHz , in IDiagPropertyListener listener);
    void unsubscribeProperty(int proId , in IDiagPropertyListener listener);
}
