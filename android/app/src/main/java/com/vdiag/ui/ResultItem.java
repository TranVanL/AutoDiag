package com.vdiag.ui;

public final class ResultItem {
    private final String timestamp;
    private final String property;
    private final String value;
    private final String latency;

    public ResultItem(String timestamp, String property, String value, String latency) {
        this.timestamp = timestamp;
        this.property = property;
        this.value = value;
        this.latency = latency;
    }

    public String getTimestamp() {
        return timestamp;
    }

    public String getProperty() {
        return property;
    }

    public String getValue() {
        return value;
    }

    public String getLatency() {
        return latency;
    }
}