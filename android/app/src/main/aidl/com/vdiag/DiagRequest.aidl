package com.vdiag;


parcelable DiagRequest {
    int serviceId;
    int subFunction;
    String payload;
    long timestamp;
    String clientId;
}
