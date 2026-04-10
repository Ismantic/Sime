package com.isma.sime.ime.theme;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Color;

/**
 * Color palette for the IME. Two fixed presets (light / dark) chosen by
 * the system uiMode. No user customisation — users can't pick a theme.
 */
public final class SimeTheme {

    public final int barBackground;
    public final int barForeground;
    public final int keyboardBackground;
    public final int keyBackground;
    public final int keyBackgroundPressed;
    public final int keyText;
    public final int keyTextFunction;
    public final int candidateText;
    public final int candidateHighlight;
    public final int preeditText;
    public final int dividerColor;
    public final int accentColor;

    private SimeTheme(int[] c) {
        barBackground        = c[0];
        barForeground        = c[1];
        keyboardBackground   = c[2];
        keyBackground        = c[3];
        keyBackgroundPressed = c[4];
        keyText              = c[5];
        keyTextFunction      = c[6];
        candidateText        = c[7];
        candidateHighlight   = c[8];
        preeditText          = c[9];
        dividerColor         = c[10];
        accentColor          = c[11];
    }

    public static SimeTheme light() {
        return new SimeTheme(new int[]{
            Color.parseColor("#F5F5F7"),  // barBackground
            Color.parseColor("#222222"),  // barForeground
            Color.parseColor("#E9ECEF"),  // keyboardBackground
            Color.parseColor("#FFFFFF"),  // keyBackground
            Color.parseColor("#D0D4DA"),  // keyBackgroundPressed
            Color.parseColor("#111111"),  // keyText
            Color.parseColor("#555555"),  // keyTextFunction
            Color.parseColor("#111111"),  // candidateText
            Color.parseColor("#2E7D32"),  // candidateHighlight
            Color.parseColor("#666666"),  // preeditText
            Color.parseColor("#D9DCE0"),  // dividerColor
            Color.parseColor("#2E7D32"),  // accentColor
        });
    }

    public static SimeTheme dark() {
        return new SimeTheme(new int[]{
            Color.parseColor("#1C1C1E"),
            Color.parseColor("#ECECEC"),
            Color.parseColor("#121214"),
            Color.parseColor("#2C2C2E"),
            Color.parseColor("#3A3A3C"),
            Color.parseColor("#F2F2F2"),
            Color.parseColor("#AAAAAA"),
            Color.parseColor("#F2F2F2"),
            Color.parseColor("#7FE08A"),
            Color.parseColor("#AAAAAA"),
            Color.parseColor("#333336"),
            Color.parseColor("#7FE08A"),
        });
    }

    public static SimeTheme fromContext(Context ctx) {
        int mode = ctx.getResources().getConfiguration().uiMode
                & Configuration.UI_MODE_NIGHT_MASK;
        return (mode == Configuration.UI_MODE_NIGHT_YES) ? dark() : light();
    }
}
