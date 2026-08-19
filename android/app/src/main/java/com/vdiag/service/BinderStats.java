package com.vdiag.service;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
public class BinderStats {
    private static final int DEFAULT_POOL_SIZE = 16;
    private static final int SATURATION_THRESHOLD = 12;

    private final AtomicLong totalTx = new AtomicLong(0);
    private final AtomicInteger maxConcurrent = new AtomicInteger(0);
    private final AtomicInteger currentInFlight = new AtomicInteger(0);

    public void enterTx() {
        int c = currentInFlight.incrementAndGet();
        maxConcurrent.updateAndGet(m -> Math.max(m,c));
        totalTx.incrementAndGet();
    }

    public void exitTx() {
        currentInFlight.decrementAndGet();
    }

    public long getTotalTx() {
        return totalTx.get();
    }

    public int getMaxConcurrent() {
        return maxConcurrent.get();
    }

    public int getCurrentInFlight() {
        return currentInFlight.get();
    }

    public boolean  isPoolNearSaturation() {
        return maxConcurrent.get() > SATURATION_THRESHOLD;
    }

    public int getSaturationThreshold() {
        return SATURATION_THRESHOLD;
    }

    public int getPoolSize() {
        return DEFAULT_POOL_SIZE;
    }

    public void resetMaxConcurrent() {
        maxConcurrent.set(0);
    }
}
