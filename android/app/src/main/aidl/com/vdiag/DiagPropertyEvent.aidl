package com.vdiag;
// Property Event , help to communicate from client and service , when have information of property , service will wrap data follow this structure and send to client
parcelable DiagPropertyEvent {
    int proId;
    int areaId;
    int timestampNs;
    int valueInt;
    float ValueFloat;
    String valueString;
    int status;
}
