package com.vdiag.hal;

/**
 * Parcelable response payload for IDiagnosticHal.
 *
 * Must be annotated with @VintfStability because it is returned by a
 * @VintfStability interface. Fields may only be added (with defaults),
 * never removed or reordered, after the first version is frozen.
 */
@VintfStability
parcelable DiagHalResult {
    /** 0 = OK; non-zero = NRC or transport error code. */
    int status;

    /** Raw response payload from the ECU (empty on error). */
    byte[] payload;
}
