/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package com.samsung.exynos7420.displayresolution;

import android.content.Context;
import android.graphics.Point;
import android.net.Uri;
import android.os.RemoteException;
import android.os.UserHandle;
import android.util.Log;
import android.view.Display;
import android.view.IWindowManager;
import android.view.WindowManagerGlobal;

public final class ResolutionHelper {

    private static final String TAG = "DisplayResolution";

    public static final Uri SUMMARY_URI =
            Uri.parse("content://com.samsung.exynos7420.displayresolution.summary/resolution");

    public static final int FHD_WIDTH = 1080;
    public static final int FHD_HEIGHT = 1920;
    public static final int FHD_DENSITY = 420;

    public static final int HD_WIDTH = 720;
    public static final int HD_HEIGHT = 1280;
    public static final int HD_DENSITY = 320;

    public static final int NATIVE_FALLBACK_WIDTH = 1440;
    public static final int NATIVE_FALLBACK_HEIGHT = 2560;
    public static final int NATIVE_FALLBACK_DENSITY = 560;

    public enum Mode {
        QHD,
        FHD,
        HD
    }

    private ResolutionHelper() {}

    public static Mode getCurrentMode() {
        Point base = getBaseSize();
        int shortSide = Math.min(base.x, base.y);
        if (shortSide <= 760) {
            return Mode.HD;
        }
        if (shortSide <= 1150) {
            return Mode.FHD;
        }
        return Mode.QHD;
    }

    public static Point getInitialSize() {
        Point size = new Point(NATIVE_FALLBACK_WIDTH, NATIVE_FALLBACK_HEIGHT);
        try {
            IWindowManager wm = WindowManagerGlobal.getWindowManagerService();
            if (wm != null) {
                wm.getInitialDisplaySize(Display.DEFAULT_DISPLAY, size);
            }
        } catch (RemoteException | SecurityException e) {
            Log.w(TAG, "Failed to query initial display size", e);
        }
        return normalizePortrait(size);
    }

    public static int getInitialDensity() {
        try {
            IWindowManager wm = WindowManagerGlobal.getWindowManagerService();
            if (wm != null) {
                int density = wm.getInitialDisplayDensity(Display.DEFAULT_DISPLAY);
                if (density > 0) {
                    return density;
                }
            }
        } catch (RemoteException | SecurityException e) {
            Log.w(TAG, "Failed to query initial display density", e);
        }
        return NATIVE_FALLBACK_DENSITY;
    }

    public static Point getBaseSize() {
        Point size = getInitialSize();
        try {
            IWindowManager wm = WindowManagerGlobal.getWindowManagerService();
            if (wm != null) {
                wm.getBaseDisplaySize(Display.DEFAULT_DISPLAY, size);
            }
        } catch (RemoteException | SecurityException e) {
            Log.w(TAG, "Failed to query base display size", e);
        }
        return normalizePortrait(size);
    }

    public static int getBaseDensity() {
        try {
            IWindowManager wm = WindowManagerGlobal.getWindowManagerService();
            if (wm != null) {
                int density = wm.getBaseDisplayDensity(Display.DEFAULT_DISPLAY);
                if (density > 0) {
                    return density;
                }
            }
        } catch (RemoteException | SecurityException e) {
            Log.w(TAG, "Failed to query base display density", e);
        }
        return getInitialDensity();
    }

    public static String formatDimensions(Point size, int density) {
        return size.x + " × " + size.y + " · " + density + " dpi";
    }

    public static String getSummaryText(Context context) {
        Mode mode = getCurrentMode();
        Point base = getBaseSize();
        switch (mode) {
            case HD:
                return context.getString(R.string.summary_mode_hd, base.x, base.y);
            case FHD:
                return context.getString(R.string.summary_mode_fhd, base.x, base.y);
            case QHD:
            default:
                return context.getString(R.string.summary_mode_qhd, base.x, base.y);
        }
    }

    public static boolean applyMode(Context context, Mode mode) {
        try {
            IWindowManager wm = WindowManagerGlobal.getWindowManagerService();
            if (wm == null) {
                Log.e(TAG, "WindowManagerService unavailable");
                return false;
            }
            int userId = UserHandle.myUserId();
            switch (mode) {
                case HD:
                    wm.setForcedDisplaySize(Display.DEFAULT_DISPLAY, HD_WIDTH, HD_HEIGHT);
                    wm.setForcedDisplayDensityForUser(Display.DEFAULT_DISPLAY, HD_DENSITY, userId);
                    break;
                case FHD:
                    wm.setForcedDisplaySize(Display.DEFAULT_DISPLAY, FHD_WIDTH, FHD_HEIGHT);
                    wm.setForcedDisplayDensityForUser(Display.DEFAULT_DISPLAY, FHD_DENSITY, userId);
                    break;
                case QHD:
                default:
                    wm.clearForcedDisplaySize(Display.DEFAULT_DISPLAY);
                    wm.clearForcedDisplayDensityForUser(Display.DEFAULT_DISPLAY, userId);
                    break;
            }
            context.getContentResolver().notifyChange(SUMMARY_URI, null);
            return true;
        } catch (RemoteException | SecurityException e) {
            Log.e(TAG, "Failed to apply display resolution mode " + mode, e);
            return false;
        }
    }

    private static Point normalizePortrait(Point size) {
        int w = Math.min(size.x, size.y);
        int h = Math.max(size.x, size.y);
        if (w <= 0 || h <= 0) {
            return new Point(NATIVE_FALLBACK_WIDTH, NATIVE_FALLBACK_HEIGHT);
        }
        return new Point(w, h);
    }
}
