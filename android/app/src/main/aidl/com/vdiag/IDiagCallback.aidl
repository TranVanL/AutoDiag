package com.vdiag;

import com.vdiag.DiagRequest;

oneway interface IDiagCallback {
    void onResult(int requestId, String value, long latencyUs);
    void onError(int requestId, int errorCode, String errorMsg);
}