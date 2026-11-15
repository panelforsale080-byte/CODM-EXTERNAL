package com.xorel.codm.external;

/** CODM external build — license gate disabled (dev). */
public final class LicenseNative {
    private static volatile boolean ok = true;

    private LicenseNative() {}

    public static boolean nativeIsLicenseAttested() {
        return ok;
    }

    public static void nativeSetLicenseAttested(boolean attested) {
        ok = attested;
    }
}