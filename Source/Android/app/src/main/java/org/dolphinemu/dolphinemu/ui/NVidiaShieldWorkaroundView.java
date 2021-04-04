/*
 * Copyright 2013 Dolphin Emulator Project
 * Licensed under GPLv2+
 * Refer to the license.txt file included.
 */

package org.dolphinemu.dolphinemu.ui;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

/**
 * Work around a bug with the nVidia Shield.
 *
 * Without this View, the emulation SurfaceView acts like it has the
 * highest Z-value, blocking any other View, such as the menu fragments.
 */
public final class NVidiaShieldWorkaroundView extends View
{
  public NVidiaShieldWorkaroundView(Context context, AttributeSet attrs)
  {
    super(context, attrs);

    // Setting this seems to workaround the bug
    setWillNotDraw(false);
  }
}
