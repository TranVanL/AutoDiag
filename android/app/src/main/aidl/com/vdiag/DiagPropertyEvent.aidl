package com.vdiag;

/**
 * Property Event structure for communication between service and client.
 */
parcelable DiagPropertyEvent {
    int    propertyId;
    int    areaId;            // AREA_GLOBAL=0, AREA_FL=4, AREA_FR=8, AREA_RL=16, AREA_RR=32
    int    status;            // AVAILABLE=0, UNAVAILABLE=1, ERROR=2
    long   timestampNs;
    int    intValue;
    String stringValue;
    String errorMessage;      // populated only when status=ERROR
}
 