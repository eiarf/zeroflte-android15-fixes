/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package com.samsung.exynos7420.displayresolution;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;

public class SummaryProvider extends ContentProvider {

    private static final String KEY_RESOLUTION = "resolution";
    private static final String META_DATA_PREFERENCE_SUMMARY = "com.android.settings.summary";

    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        if (!KEY_RESOLUTION.equals(method)) {
            return null;
        }
        Context context = getContext();
        if (context == null) {
            return null;
        }
        Bundle bundle = new Bundle();
        bundle.putString(META_DATA_PREFERENCE_SUMMARY, ResolutionHelper.getSummaryText(context));
        return bundle;
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
            String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
