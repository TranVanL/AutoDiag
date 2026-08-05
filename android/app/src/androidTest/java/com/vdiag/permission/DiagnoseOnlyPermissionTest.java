package com.vdiag.permission;

import static org.junit.Assert.fail;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.vdiag.sdk.DiagProperty;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * 
 *
 * <p>Expected:
 * <ul>
 *   <li>VIN is allowed because it maps to {@code com.vdiag.permission.DIAGNOSE}.</li>
 *   <li>BATTERY_SOC, TIRE_PRESSURE and RPM are denied because each requires
 *       its own signature permission.</li>
 * </ul>
 *
 * <p><b>APK setup:</b> this test must run in an APK whose manifest declares
 * <b>only</b>:
 * <pre>
 *   &lt;uses-permission android:name="com.vdiag.permission.DIAGNOSE" /&gt;
 * </pre>
 */
@RunWith(AndroidJUnit4.class)
public class DiagnoseOnlyPermissionTest {

    @Test
    public void getProperty_diagnoseOnly_vinAllowed_batteryDenied() throws Exception {
        PermissionGateTestHelper helper = new PermissionGateTestHelper();

        // VIN → DIAGNOSE permission → OK
        helper.getProperty(DiagProperty.VIN.getPropId());

        // BATTERY_SOC → READ_BATTERY permission → SecurityException
        try {
            helper.getProperty(DiagProperty.BATTERY_SOC.getPropId());
            fail("Expected SecurityException for BATTERY_SOC without READ_BATTERY");
        } catch (SecurityException expected) {
            // expected
        }
    }

    @Test
    public void getProperty_diagnoseOnly_tiresAndPowertrainDenied() throws Exception {
        PermissionGateTestHelper helper = new PermissionGateTestHelper();

        try {
            helper.getProperty(DiagProperty.TIRE_PRESSURE.getPropId());
            fail("Expected SecurityException for TIRE_PRESSURE without READ_TIRES");
        } catch (SecurityException expected) {
            // expected
        }

        try {
            helper.getProperty(DiagProperty.RPM.getPropId());
            fail("Expected SecurityException for RPM without READ_POWERTRAIN");
        } catch (SecurityException expected) {
            // expected
        }
    }

    @Test
    public void subscribeProperty_diagnoseOnly_batteryDenied() throws Exception {
        PermissionGateTestHelper helper = new PermissionGateTestHelper();

        try {
            helper.subscribeProperty(DiagProperty.BATTERY_SOC.getPropId());
            fail("Expected SecurityException for subscribe BATTERY_SOC without READ_BATTERY");
        } catch (SecurityException expected) {
            // expected
        }
    }
}
