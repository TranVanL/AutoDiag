package com.vdiag.hal;

/**
 * Parcelable request payload for IDiagnosticHal.
 *
 * Must be annotated with @VintfStability because it is used as an argument
 * in a @VintfStability interface. All parcelables crossing the vendor
 * interface boundary must be stable.
 */
 // AIDL contract that HAl service expose in Vendor Interface with Hal service run in vendor partition , help it can communicate with system service frame work at system partition through Binder IPC 
 // Stable AIDL ensure interface and parcelable stable , freeze versioning , guarantee for compatibility 
 // Vintf annotation 
@VintfStability
parcelable DiagHalRequest {
    /** UDS/ISO-TP service identifier, e.g. 0x22 for ReadDataByIdentifier. */
    int serviceId;

    /** UDS sub-function / data identifier. */
    int subFunction;

    /** Raw request payload (optional, may be empty). */
    byte[] payload;
}
