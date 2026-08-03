package com.vdiag;

// Contract that client will provide callback object and service can call through that contract to callback
// oneway (fire-forget) , avoid waiting long time and blocking
oneway interface IDiagCallback {
    void onResult(int requestId, String value, long latencyUs);
    void onError(int requestId, int errorCode, String errorMsg);
}