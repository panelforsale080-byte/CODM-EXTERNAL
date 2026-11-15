package com.xorel.codm.external;

import android.content.Context;

/** CODM build — menu music disabled (overlay hooks kept for JNI parity). */
final class MusicController {
  private MusicController() {}

  static void attach(Context context) {}

  static void refresh() {}

  static void play(int index) {}

  static void setVolume(int percent) {}

  static void setMuted(boolean on) {}

  static void stop() {}

  static String snapshot() {
    return "-1\t70\t0\t0\t0\tMusic not available\n";
  }
}