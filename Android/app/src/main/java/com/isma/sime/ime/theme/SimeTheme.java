package com.isma.sime.ime.theme;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Color;

/**
 * Theme palette + sizing for the IME. Two presets (light / dark) chosen
 * by the system uiMode. Includes both colors and a small set of layout
 * dimensions so the framework's {@code KeyView} can read everything
 * from one place.
 */
public final class SimeTheme {

    // ===== Bar / surface =====
    public final int barBackground;
    public final int barForeground;
    public final int keyboardBackground;

    // ===== Normal key =====
    public final int keyBackground;
    public final int keyBackgroundPressed;
    public final int keyText;

    // ===== Function key =====
    public final int functionKeyBackground;
    public final int functionKeyBackgroundPressed;
    public final int keyTextFunction;

    // ===== Misc =====
    public final int candidateText;
    public final int candidateHighlight;
    public final int preeditText;
    public final int dividerColor;
    public final int accentColor;
    public final int hintLabelColor;
    public final int keyShadowColor;

    // ===== Layout dimensions (dp) =====
    public final int keyCornerRadiusDp;
    public final int keyShadowDyDp;
    public final int keyShadowRadiusDp;

    private SimeTheme(int[] c) {
        barBackground                = c[0];
        barForeground                = c[1];
        keyboardBackground           = c[2];
        keyBackground                = c[3];
        keyBackgroundPressed         = c[4];
        keyText                      = c[5];
        functionKeyBackground        = c[6];
        functionKeyBackgroundPressed = c[7];
        keyTextFunction              = c[8];
        candidateText                = c[9];
        candidateHighlight           = c[10];
        preeditText                  = c[11];
        dividerColor                 = c[12];
        accentColor                  = c[13];
        hintLabelColor               = c[14];
        keyShadowColor               = c[15];

        keyCornerRadiusDp = 10;
        keyShadowDyDp     = 1;
        keyShadowRadiusDp = 2;
    }

    public static SimeTheme light() {
        return new SimeTheme(new int[]{
            Color.parseColor("#F2F4F7"),  // barBackground
            Color.parseColor("#1F2933"),  // barForeground
            Color.parseColor("#E6E9EE"),  // keyboardBackground
            Color.parseColor("#FFFFFF"),  // keyBackground
            Color.parseColor("#CFD4DC"),  // keyBackgroundPressed
            Color.parseColor("#1F2933"),  // keyText
            Color.parseColor("#C9CDD4"),  // functionKeyBackground
            Color.parseColor("#A8AEB6"),  // functionKeyBackgroundPressed
            Color.parseColor("#3D4651"),  // keyTextFunction
            Color.parseColor("#1F2933"),  // candidateText
            Color.parseColor("#2E7D32"),  // candidateHighlight
            Color.parseColor("#5C6470"),  // preeditText
            Color.parseColor("#D9DCE0"),  // dividerColor
            Color.parseColor("#2E7D32"),  // accentColor
            Color.parseColor("#8B95A1"),  // hintLabelColor
            Color.parseColor("#33000000"), // keyShadowColor (~20% black)
        });
    }

    public static SimeTheme dark() {
        return new SimeTheme(new int[]{
            Color.parseColor("#1A1B1F"),  // barBackground
            Color.parseColor("#ECECEC"),  // barForeground
            Color.parseColor("#0F1012"),  // keyboardBackground
            Color.parseColor("#2E3036"),  // keyBackground
            Color.parseColor("#42454D"),  // keyBackgroundPressed
            Color.parseColor("#F2F2F2"),  // keyText
            Color.parseColor("#1F2024"),  // functionKeyBackground
            Color.parseColor("#34363D"),  // functionKeyBackgroundPressed
            Color.parseColor("#B5BAC1"),  // keyTextFunction
            Color.parseColor("#F2F2F2"),  // candidateText
            Color.parseColor("#7FE08A"),  // candidateHighlight
            Color.parseColor("#A0A6AE"),  // preeditText
            Color.parseColor("#33363C"),  // dividerColor
            Color.parseColor("#7FE08A"),  // accentColor
            Color.parseColor("#727680"),  // hintLabelColor
            Color.parseColor("#66000000"), // keyShadowColor (~40% black)
        });
    }

    public static SimeTheme fromContext(Context ctx) {
        int mode = ctx.getResources().getConfiguration().uiMode
                & Configuration.UI_MODE_NIGHT_MASK;
        return (mode == Configuration.UI_MODE_NIGHT_YES) ? dark() : light();
    }
}
