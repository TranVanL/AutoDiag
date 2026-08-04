package com.vdiag.hal;

import com.vdiag.hal.DiagHalRequest;
import com.vdiag.hal.DiagHalResult;

/**
 * Stable vendor HAL interface for VDiag diagnostic stack.
 *
 * Annotated with @VintfStability so it becomes part of the Vendor Interface
 * (VINTF) manifest and must remain backward-compatible across OTA updates.
 *
 * This is the vendor-facing counterpart of the app-layer IDiagCarService
 * AIDL interface, which does NOT require @VintfStability.
 */

 // Annotated with keywork @Vinft so it become a part of Vendor Interface (that communication between system partition and vendor partition)
@VintfStability
interface IDiagnosticHal {
    /** Send a diagnostic request and wait for the ECU response. */
    DiagHalResult sendAndReceive(in DiagHalRequest req);

    /** Return true when the underlying transport (DoIP/CAN) is ready. */
    boolean isReady();

    /** Reset the HAL state machine and any pending sessions. */
    void reset();
}
