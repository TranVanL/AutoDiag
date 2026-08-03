package com.vdiag.sdk;

/**
 * Listener contract for receiving asynchronous diagnostic responses from SDK.
 */

// DiagClient implement that class for handling diagnostic response if subscribing property or getting property
public interface DiagListener {
    void onPropertyReceived(DiagProperty property, String value, long latencyUs, int requestId);
    void onError(DiagProperty property, int code, String message, int requestId);
}
