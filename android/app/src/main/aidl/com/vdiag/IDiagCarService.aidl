package com.vdiag;

import com.vdiag.DiagRequest;
import com.vdiag.IDiagCallback;

import com.vdiag.IDiagPropertyListener;
import com.vdiag.DiagPropertyEvent;
import android.os.ParcelFileDescriptor;

// Expose contract that service offer to client
interface IDiagCarService {
    void registerCallback(in IDiagCallback callback);
    void unregisterCallback(in IDiagCallback callback);
    void getProperty(in DiagRequest request);
    void subscribeProperty(int proId , float rateHz , in IDiagPropertyListener listener);
    void unsubscribeProperty(int proId , in IDiagPropertyListener listener);
    // Area-aware variants. The original methods remain for global properties.
    void subscribePropertyForArea(int proId, int areaId, float rateHz,
                                  in IDiagPropertyListener listener);
    void unsubscribePropertyForArea(int proId, int areaId,
                                    in IDiagPropertyListener listener);
    void flashFirmware(in ParcelFileDescriptor fd , IDiagCallback callback);
    ParcelFileDescriptor getDtcSnapshotShared(out int[] outMeta);
}
