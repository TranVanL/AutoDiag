# VDiag HAL service and init.rc
PRODUCT_PACKAGES += \
    android.hardware.vdiag@1.0-service \
    android.hardware.vdiag@1.0-service.rc

# VINTF manifest fragment
PRODUCT_VINTF_MANIFESTS += \
    device/vdiag/vintf/manifest_vdiag.xml

# SELinux policy fragments
BOARD_SEPOLICY_DIRS += \
    device/vdiag/bringup/sepolicy

# Privileged app permission allowlist
PRODUCT_COPY_FILES += \
    device/vdiag/bringup/privapp-permissions-vdiag.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/privapp-permissions-vdiag.xml
