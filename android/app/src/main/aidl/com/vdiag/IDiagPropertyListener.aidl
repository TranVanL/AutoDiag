package com.vdiag;

import com.vdiag.DiagPropertyEvent;
// Contract that client will provide callback object and service can call through that contract to callback
oneway interface IDiagPropertyListener {
    void onPropertyChanged(in DiagPropertyEvent event);
    void onPropertyError(int proId , int areaId , int errorCode);
}