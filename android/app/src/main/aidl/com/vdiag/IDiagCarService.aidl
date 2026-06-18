package com.vdiag;

import com.vdiag.DiagRequest;
import com.vdiag.IDiagCallback;

interface IDiagCarService {
    void getProperty(in DiagRequest request , in IDiagCallback callback);
}
