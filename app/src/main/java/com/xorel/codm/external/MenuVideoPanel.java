package com.xorel.codm.external;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.FrameLayout;

/** CODM build — no promo video asset; layout hooks kept for overlay parity. */
final class MenuVideoPanel extends FrameLayout {
  private boolean muted;

  MenuVideoPanel(Context context) {
    super(context);
    setVisibility(View.GONE);
  }

  MenuVideoPanel(Context context, AttributeSet attrs) {
    super(context, attrs);
    setVisibility(View.GONE);
  }

  void onMenuVisibleChanged(boolean visible) {
    setVisibility(visible ? VISIBLE : GONE);
  }

  void setMuted(boolean value, boolean fromUser) {
    muted = value;
    if (fromUser) {
      try {
        NativeBridge.nativeSetMenuVideoMuted(muted);
      } catch (Throwable ignored) {
      }
    }
  }

  boolean isMuted() {
    return muted;
  }

  void releaseAll() {}
}