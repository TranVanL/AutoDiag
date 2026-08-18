package com.vdiag.service;

import androidx.annotation.NonNull;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public class DtcStore {
    private static final String TAG = "VDiag.DtcStore";
    private static final String SEPARATOR = "\n";

    private final List<String> mDtcList = new ArrayList<>();

    public synchronized void add(@NonNull String dtc) {
        if (dtc == null) {
            throw new IllegalArgumentException("dtc must not be null");
        }
        mDtcList.add(dtc);
    }

    public synchronized void clear() {
        mDtcList.clear();
    }

    public synchronized int count() {
        return mDtcList.size();
    }

    @NonNull
    public synchronized byte[] serializeAll() {
        if (mDtcList.isEmpty()) {
            return new byte[0];
        }
        String joined = String.join(SEPARATOR, mDtcList);
        return joined.getBytes(StandardCharsets.UTF_8);
    }

    public synchronized void seedDemoData() {
        mDtcList.clear();
        mDtcList.add("P0101: Mass Air Flow Circuit Range/Performance");
        mDtcList.add("P0300: Random/Multiple Cylinder Misfire Detected");
        mDtcList.add("P0420: Catalyst System Efficiency Below Threshold");
        mDtcList.add("P0500: Vehicle Speed Sensor Malfunction");
        mDtcList.add("P0705: Transmission Range Sensor Circuit Malfunction");
    }

}
