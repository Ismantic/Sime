package com.isma.sime.ime.keyboard;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.isma.sime.ime.theme.SimeTheme;

/**
 * Base class for keyboard layouts. Builds on a {@link LinearLayout} and
 * provides a set of helpers so subclasses can compose a key grid without
 * XML.
 */
public abstract class KeyboardView extends LinearLayout {

    public interface OnKeyListener {
        void onKey(SimeKey key);
    }

    private OnKeyListener listener;
    protected SimeTheme theme;

    public KeyboardView(Context context) {
        super(context);
        init();
    }

    public KeyboardView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        setOrientation(VERTICAL);
        theme = SimeTheme.fromContext(getContext());
        setBackgroundColor(theme.keyboardBackground);
        setPadding(dp(4), dp(6), dp(4), dp(6));
    }

    public void setOnKeyListener(OnKeyListener l) {
        this.listener = l;
    }

    protected void emit(SimeKey key) {
        if (listener != null) listener.onKey(key);
    }

    // ===== Layout helpers =====

    protected int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    protected LinearLayout makeRow() {
        LinearLayout row = new LinearLayout(getContext());
        row.setOrientation(HORIZONTAL);
        LayoutParams lp = new LayoutParams(
                LayoutParams.MATCH_PARENT, 0, 1f);
        row.setLayoutParams(lp);
        return row;
    }

    /** Rounded key background selector keyed off the theme colors. */
    protected GradientDrawable makeKeyBg(int color) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.RECTANGLE);
        d.setCornerRadius(dp(8));
        d.setColor(color);
        return d;
    }

    protected StateListDrawable makeKeySelector(int normal, int pressed) {
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_pressed}, makeKeyBg(pressed));
        sl.addState(new int[]{}, makeKeyBg(normal));
        return sl;
    }

    /**
     * Build a text key with the given label, weight within the row, and
     * click action. Applies theme colors.
     */
    protected TextView makeKey(String label, float weight, float textSp,
                                boolean function, Runnable onClick) {
        TextView tv = new TextView(getContext());
        tv.setText(label);
        tv.setGravity(Gravity.CENTER);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, textSp);
        tv.setTextColor(function ? theme.keyTextFunction : theme.keyText);
        int normalBg = function ? darken(theme.keyBackground, 0.92f)
                                 : theme.keyBackground;
        tv.setBackground(makeKeySelector(normalBg, theme.keyBackgroundPressed));
        tv.setClickable(true);
        tv.setFocusable(true);
        LayoutParams lp = new LayoutParams(0, LayoutParams.MATCH_PARENT, weight);
        int m = dp(3);
        lp.setMargins(m, m, m, m);
        tv.setLayoutParams(lp);
        if (onClick != null) tv.setOnClickListener(v -> onClick.run());
        return tv;
    }

    /**
     * Like {@link #makeKey} but sized to be placed inside a VERTICAL parent
     * with height-based weights.
     */
    protected TextView makeVerticalKey(String label, float weight, float textSp,
                                        boolean function, Runnable onClick) {
        TextView tv = makeKey(label, weight, textSp, function, onClick);
        LayoutParams lp = new LayoutParams(
                LayoutParams.MATCH_PARENT, 0, weight);
        int m = dp(3);
        lp.setMargins(m, m, m, m);
        tv.setLayoutParams(lp);
        return tv;
    }

    /** A filler/blank key (for spacing rows). */
    protected View makeFiller(float weight) {
        View v = new View(getContext());
        LayoutParams lp = new LayoutParams(0, LayoutParams.MATCH_PARENT, weight);
        int m = dp(3);
        lp.setMargins(m, m, m, m);
        v.setLayoutParams(lp);
        return v;
    }

    private static int darken(int color, float factor) {
        int r = (int) (Color.red(color) * factor);
        int g = (int) (Color.green(color) * factor);
        int b = (int) (Color.blue(color) * factor);
        return Color.argb(Color.alpha(color), clamp(r), clamp(g), clamp(b));
    }

    private static int clamp(int v) {
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    }
}
