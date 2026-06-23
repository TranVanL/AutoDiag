package com.vdiag;

import com.vdiag.DiagRequest;
import com.vdiag.IDiagCallback;

interface IDiagCarService {
    void registerCallback(in IDiagCallback callback);
    void unregisterCallback(in IDiagCallback callback);
    void getProperty(in DiagRequest request);
}
