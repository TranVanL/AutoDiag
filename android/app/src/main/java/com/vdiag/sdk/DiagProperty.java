package com.vdiag.sdk;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

/**
 * Domain-level diagnostic properties exposed by SDK.
 *
 * propId: stable SDK ID used by app/service contract.
 * udsId: UDS DID or service opcode used for protocol mapping/logging.
 */
public enum DiagProperty {
    VIN(0xF190, 0x22F190, "VIN"),
    SOC(0x0105, 0x220105, "SOC"),
    RPM(0x010C, 0x22010C, "RPM"),
    SW_VERSION(0xF187, 0x22F187, "SW Version"),
    DTC_LIST(0xF191, 0x1902, "DTC List"),
    DTC_CLEAR(0xF193, 0x14, "Clear DTC");

    private static final Map<Integer, DiagProperty> BY_PROP_ID;

    static {
        Map<Integer, DiagProperty> map = new HashMap<>();
        for (DiagProperty property : values()) {
            map.put(property.propId, property);
        }
        BY_PROP_ID = Collections.unmodifiableMap(map);
    }

    private final int propId;
    private final int udsId;
    private final String displayName;

    DiagProperty(int propId, int udsId, String displayName) {
        this.propId = propId;
        this.udsId = udsId;
        this.displayName = displayName;
    }

    public int getPropId() {
        return propId;
    }

    public int getUdsId() {
        return udsId;
    }

    public String getDisplayName() {
        return displayName;
    }

    public static DiagProperty fromPropId(int propId) {
        return BY_PROP_ID.get(propId);
    }
}
