package com.vdiag.hal;

/**
 * Parcelable request payload for IDiagnosticHal.
 *
 * Must be annotated with @VintfStability because it is used as an argument
 * in a @VintfStability interface. All parcelables crossing the vendor
 * interface boundary must be stable.
 */
@VintfStability
parcelable DiagHalRequest {
    /** UDS/ISO-TP service identifier, e.g. 0x22 for ReadDataByIdentifier. */
    int serviceId;

    /** UDS sub-function / data identifier. */
    int subFunction;

    /** Raw request payload (optional, may be empty). */
    byte[] payload;
}
