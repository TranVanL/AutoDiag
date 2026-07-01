package com.vdiag.sdk;

/**
 * Listener contract for receiving asynchronous diagnostic responses from SDK.
 */
public interface DiagListener {
    void onPropertyReceived(DiagProperty property, String value, long latencyUs, int requestId);

    void onError(DiagProperty property, int code, String message, int requestId);
}
