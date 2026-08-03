package com.vdiag;

// Structure of request , send to service to read property
parcelable DiagRequest {
    int requestId;           // Unique request ID
    int propertyId;          // DID to read (0xF190, 0xFD01, ...)
    byte[] payload;          // Optional payload
}
