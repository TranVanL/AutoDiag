package com.vdiag.permission;

import static org.junit.Assert.fail;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.vdiag.sdk.DiagProperty;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 *
 *
 * <p>Expected: every property access is denied with a {@link SecurityException}.
 *
 * <p><b>APK setup:</b> this test must run in an APK whose manifest declares
 * <b>none</b> of the following permissions:
 * <pre>
 *   com.vdiag.permission.DIAGNOSE
 *   com.vdiag.permission.READ_BATTERY
 *   com.vdiag.permission.READ_TIRES
 *   com.vdiag.permission.READ_POWERTRAIN
 * </pre>
 */
@RunWith(AndroidJUnit4.class)
public class NoPermissionTest {

    @Test
    public void getProperty_noPermissions_allDenied() throws Exception {
        PermissionGateTestHelper helper = new PermissionGateTestHelper();

        assertGetPropertyDenied(helper, DiagProperty.VIN.getPropId());
        assertGetPropertyDenied(helper, DiagProperty.BATTERY_SOC.getPropId());
        assertGetPropertyDenied(helper, DiagProperty.TIRE_PRESSURE.getPropId());
        assertGetPropertyDenied(helper, DiagProperty.RPM.getPropId());
    }

    @Test
    public void subscribeProperty_noPermissions_allDenied() throws Exception {
        PermissionGateTestHelper helper = new PermissionGateTestHelper();

        assertSubscribePropertyDenied(helper, DiagProperty.VIN.getPropId());
        assertSubscribePropertyDenied(helper, DiagProperty.BATTERY_SOC.getPropId());
        assertSubscribePropertyDenied(helper, DiagProperty.TIRE_PRESSURE.getPropId());
        assertSubscribePropertyDenied(helper, DiagProperty.RPM.getPropId());
    }

    private void assertGetPropertyDenied(PermissionGateTestHelper helper, int propId)
            throws Exception {
        try {
            helper.getProperty(propId);
            fail("Expected SecurityException for getProperty propId=0x"
                    + Integer.toHexString(propId));
        } catch (SecurityException expected) {
            // expected
        }
    }

    private void assertSubscribePropertyDenied(PermissionGateTestHelper helper, int propId)
            throws Exception {
        try {
            helper.subscribeProperty(propId);
            fail("Expected SecurityException for subscribeProperty propId=0x"
                    + Integer.toHexString(propId));
        } catch (SecurityException expected) {
            // expected
        }
    }
}
