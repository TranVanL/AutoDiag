package com.vdiag;

import com.vdiag.DiagRequest;

oneway interface IDiagCallback {
    void onResponse(in DiagRequest request , int status);
    void onEvent(int eventCode);
}