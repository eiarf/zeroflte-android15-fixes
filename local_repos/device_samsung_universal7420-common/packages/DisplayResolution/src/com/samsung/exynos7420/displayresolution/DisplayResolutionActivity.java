/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package com.samsung.exynos7420.displayresolution;

import android.graphics.Color;
import android.graphics.Point;
import android.os.Bundle;
import android.view.View;
import android.widget.RadioButton;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;

import com.google.android.material.appbar.AppBarLayout;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.color.MaterialColors;

public class DisplayResolutionActivity extends AppCompatActivity {

    private MaterialCardView cardQhd;
    private MaterialCardView cardFhd;
    private MaterialCardView cardHd;

    private RadioButton radioQhd;
    private RadioButton radioFhd;
    private RadioButton radioHd;

    private TextView currentStatusView;
    private TextView qhdSpecsView;
    private TextView fhdSpecsView;
    private TextView hdSpecsView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        DynamicColors.applyToActivityIfAvailable(this);
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        setContentView(R.layout.activity_display_resolution);

        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setNavigationOnClickListener(v -> getOnBackPressedDispatcher().onBackPressed());

        setupInsets();

        currentStatusView = findViewById(R.id.text_current_status);

        cardQhd = findViewById(R.id.card_qhd);
        cardFhd = findViewById(R.id.card_fhd);
        cardHd = findViewById(R.id.card_hd);

        radioQhd = findViewById(R.id.radio_qhd);
        radioFhd = findViewById(R.id.radio_fhd);
        radioHd = findViewById(R.id.radio_hd);

        qhdSpecsView = findViewById(R.id.text_qhd_specs);
        fhdSpecsView = findViewById(R.id.text_fhd_specs);
        hdSpecsView = findViewById(R.id.text_hd_specs);

        Point initSize = ResolutionHelper.getInitialSize();
        int initDensity = ResolutionHelper.getInitialDensity();
        qhdSpecsView.setText(ResolutionHelper.formatDimensions(initSize, initDensity));
        fhdSpecsView.setText(ResolutionHelper.formatDimensions(
                new Point(ResolutionHelper.FHD_WIDTH, ResolutionHelper.FHD_HEIGHT),
                ResolutionHelper.FHD_DENSITY));
        hdSpecsView.setText(ResolutionHelper.formatDimensions(
                new Point(ResolutionHelper.HD_WIDTH, ResolutionHelper.HD_HEIGHT),
                ResolutionHelper.HD_DENSITY));

        cardQhd.setOnClickListener(v -> onModeSelected(ResolutionHelper.Mode.QHD));
        cardFhd.setOnClickListener(v -> onModeSelected(ResolutionHelper.Mode.FHD));
        cardHd.setOnClickListener(v -> onModeSelected(ResolutionHelper.Mode.HD));

        updateSelectionUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateSelectionUi();
    }

    private void setupInsets() {
        AppBarLayout appBar = findViewById(R.id.app_bar);
        View scrollView = findViewById(R.id.scroll_content);
        final int baseScrollLeft = scrollView.getPaddingLeft();
        final int baseScrollTop = scrollView.getPaddingTop();
        final int baseScrollRight = scrollView.getPaddingRight();
        final int baseScrollBottom = scrollView.getPaddingBottom();

        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.root_container), (v, insets) -> {
            Insets sysBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            appBar.setPadding(sysBars.left, sysBars.top, sysBars.right, 0);
            scrollView.setPadding(
                    baseScrollLeft + sysBars.left,
                    baseScrollTop,
                    baseScrollRight + sysBars.right,
                    baseScrollBottom + sysBars.bottom);
            return insets;
        });
    }

    private void onModeSelected(ResolutionHelper.Mode targetMode) {
        if (ResolutionHelper.getCurrentMode() == targetMode) {
            return;
        }
        if (!ResolutionHelper.applyMode(this, targetMode)) {
            Toast.makeText(this, R.string.resolution_apply_failed, Toast.LENGTH_SHORT).show();
            return;
        }
        updateSelectionUi();
    }

    private void updateSelectionUi() {
        ResolutionHelper.Mode active = ResolutionHelper.getCurrentMode();
        Point baseSize = ResolutionHelper.getBaseSize();
        int baseDensity = ResolutionHelper.getBaseDensity();

        currentStatusView.setText(getString(
                R.string.current_resolution_value,
                ResolutionHelper.formatDimensions(baseSize, baseDensity)));

        styleOptionCard(cardQhd, radioQhd, active == ResolutionHelper.Mode.QHD);
        styleOptionCard(cardFhd, radioFhd, active == ResolutionHelper.Mode.FHD);
        styleOptionCard(cardHd, radioHd, active == ResolutionHelper.Mode.HD);
    }

    private void styleOptionCard(MaterialCardView card, RadioButton radio, boolean selected) {
        radio.setChecked(selected);
        int strokeWidthPx = Math.round(
                (selected ? 2f : 1f) * getResources().getDisplayMetrics().density);
        card.setStrokeWidth(strokeWidthPx);

        int bgSelected = MaterialColors.getColor(
                card, R.attr.colorPrimaryContainer, Color.TRANSPARENT);
        int bgUnselected = MaterialColors.getColor(
                card, R.attr.colorSurface, Color.TRANSPARENT);
        int strokeSelected = MaterialColors.getColor(
                card, R.attr.colorPrimary, Color.GRAY);
        int strokeUnselected = MaterialColors.getColor(
                card, R.attr.colorOutline, Color.LTGRAY);

        card.setCardBackgroundColor(selected ? bgSelected : bgUnselected);
        card.setStrokeColor(selected ? strokeSelected : strokeUnselected);
    }
}
