package com.vdiag.permission;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.vdiag.sdk.DiagProperty;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 *
 * <p>Expected: every property access is allowed.
 *
 * <p><b>APK setup:</b> this test must run in an APK whose manifest declares
 * all of the following uses-permission entries:
 * <pre>
 *   &lt;uses-permission android:name="com.vdiag.permission.DIAGNOSE" /&gt;
 *   &lt;uses-permission android:name="com.vdiag.permission.READ_BATTERY" /&gt;
 *   &lt;uses-permission android:name="com.vdiag.permission.READ_TIRES" /&gt;
 *   &lt;uses-permission android:name="com.vdiag.permission.READ_POWERTRAIN" /&gt;
 * </pre>
 */
@RunWith(AndroidJUnit4.class)
public class AllPermissionsPermissionTest {

    @Test
    public void getProperty_allFourPermissions_allAllowed() throws Exception {
        PermissionGateTestHelper helper = new PermissionGateTestHelper();

        helper.getProperty(DiagProperty.VIN.getPropId());
        helper.getProperty(DiagProperty.BATTERY_SOC.getPropId());
        helper.getProperty(DiagProperty.TIRE_PRESSURE.getPropId());
        helper.getProperty(DiagProperty.RPM.getPropId());
    }

    @Test
    public void subscribeProperty_allFourPermissions_allAllowed() throws Exception {
        PermissionGateTestHelper helper = new PermissionGateTestHelper();

        helper.subscribeProperty(DiagProperty.VIN.getPropId());
        helper.subscribeProperty(DiagProperty.BATTERY_SOC.getPropId());
        helper.subscribeProperty(DiagProperty.TIRE_PRESSURE.getPropId());
        helper.subscribeProperty(DiagProperty.RPM.getPropId());
    }
}
