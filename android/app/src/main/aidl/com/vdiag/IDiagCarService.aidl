package com.vdiag;

import com.vdiag.DiagRequest;
import com.vdiag.IDiagCallback;

interface IDiagCarService {
    void sendRequest(in DiagRequest request , in IDiagCallback callback);
    void registerCallback(in IDiagCallback callback);
    void unregisterCallback(in IDiagCallback callback);
    boolean isConnected();
}
