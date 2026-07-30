package com.vdiag;

import com.vdiag.DiagPropertyEvent;

oneway interface IDiagPropertyListener {
    void onPropertyChanged(in DiagPropertyEvent event);
    void onPropertyError(int proId , int areaId , int errorCode);
}