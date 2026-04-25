package com.semantic.sime.ime.keyboard;

import android.content.Context;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.widget.LinearLayout;

import com.semantic.sime.ime.theme.SimeTheme;

/**
 * Base class for the IME's keyboard layouts. Holds the theme, the
 * outgoing key listener, and a small pair of drawable helpers used by
 * the T9 view's hand-rolled left strip ({@link #makeKeySelector}).
 *
 * <p>The grid layout itself is built by the framework
 * ({@code KeyboardContainer} + {@code KeyboardLayout}); subclasses are
 * thin shells that wire framework containers + any dynamic side-panel
 * children.
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
        setPadding(dp(1), dp(6), dp(1), dp(6));
    }

    public void setOnKeyListener(OnKeyListener l) {
        this.listener = l;
    }

    protected void emit(SimeKey key) {
        if (listener != null) listener.onKey(key);
    }

    // ===== Drawable helpers (used by T9's hand-rolled left strip) =====

    protected int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

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
}
