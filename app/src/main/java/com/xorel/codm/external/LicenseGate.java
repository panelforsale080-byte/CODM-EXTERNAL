package com.xorel.codm.external;

/** CODM external — offline license stub (always valid when password non-empty). */
public final class LicenseGate {
    public interface Callback {
        void onResult(boolean ok, String message);
    }

    private LicenseGate() {}

    public static void verifyAsync(String password, Callback cb) {
        if (cb == null) return;
        final boolean ok = password != null && !password.trim().isEmpty();
        LicenseNative.nativeSetLicenseAttested(ok);
        cb.onResult(ok, ok ? "ok" : "password");
    }
}