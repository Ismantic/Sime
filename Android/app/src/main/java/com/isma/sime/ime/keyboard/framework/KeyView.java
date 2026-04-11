package com.isma.sime.ime.keyboard.framework;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.TypedValue;
import android.view.MotionEvent;
import android.view.View;

import com.isma.sime.ime.theme.SimeTheme;

/**
 * Single self-drawn key. Backed by a {@link KeyDef}; its label can be
 * mutated at runtime via {@link #setLabel(String)} for things like the
 * Qwerty shift state and T9 dual-state keys.
 *
 * <p>Step 1 scope: draws background + label, handles tap, swaps colors
 * on press. Long-press / repeat / preview popup / swipe-up / haptic /
 * sound are wired in later steps but the touch dispatch path is in
 * place so {@link KeyTouchHandler} can plug in without restructuring.
 */
public class KeyView extends View {

    public interface OnKeyAction {
        void onKey(KeyDef def, KeyAction action);
    }

    public enum KeyAction {
        CLICK, LONG_PRESS
    }

    private final SimeTheme theme;
    private final Paint bgPaint;
    private final Paint textPaint;
    private final Paint hintPaint;
    private final RectF bgRect = new RectF();

    private KeyDef def;
    private String label;        // mutable copy of def.label
    private String hintLabel;    // mutable copy of def.hintLabel
    private boolean pressed;
    private boolean highlighted; // for "currently selected" settings entries
    /** True once a long-press has fired during the current touch sequence. */
    private boolean longPressFired;

    private float marginPx;
    private float cornerRadiusPx;
    private float labelSizePx;
    private float hintSizePx;

    private OnKeyAction listener;

    // Step 5: long-press / repeat timing constants. Values borrowed from
    // typical IMEs (yuyansdk uses similar numbers).
    private static final long LONG_PRESS_DELAY_MS = 400L;
    private static final long REPEAT_START_DELAY_MS = 400L;
    private static final long REPEAT_INTERVAL_MS = 50L;

    private final Runnable longPressRunnable = new Runnable() {
        @Override public void run() {
            if (!pressed) return;
            longPressFired = true;
            if (listener != null && def.longPressAction != null) {
                listener.onKey(def, KeyAction.LONG_PRESS);
            }
        }
    };

    private final Runnable repeatRunnable = new Runnable() {
        @Override public void run() {
            if (!pressed || !def.repeatable) return;
            if (listener != null && def.clickAction != null) {
                listener.onKey(def, KeyAction.CLICK);
            }
            postDelayed(this, REPEAT_INTERVAL_MS);
        }
    };

    public KeyView(Context context, SimeTheme theme, KeyDef def, float keyMarginDp) {
        super(context);
        this.theme = theme;
        this.def = def;
        this.label = def.label;
        this.hintLabel = def.hintLabel;
        this.marginPx = dp(keyMarginDp);
        this.cornerRadiusPx = dp(theme.keyCornerRadiusDp);
        this.labelSizePx = sp(def.labelSizeSp > 0 ? def.labelSizeSp : 18f);
        this.hintSizePx = sp(10);

        bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        // Soft drop shadow under each key for visual depth. Requires
        // disabling hardware-accelerated shadow optimisation? No —
        // setShadowLayer works on hw layers since API 28; below 28 it
        // requires SW layer. Force SW layer for shadow correctness on
        // all API levels.
        bgPaint.setShadowLayer(
                dp(theme.keyShadowRadiusDp),
                0f,
                dp(theme.keyShadowDyDp),
                theme.keyShadowColor);
        setLayerType(LAYER_TYPE_SOFTWARE, null);

        textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        textPaint.setTextAlign(Paint.Align.CENTER);
        hintPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        hintPaint.setTextAlign(Paint.Align.RIGHT);

        setClickable(def.appearance != KeyAppearance.EMPTY);
        setFocusable(def.appearance != KeyAppearance.EMPTY);
    }

    public void setListener(OnKeyAction l) { this.listener = l; }

    public void setLabel(String s) {
        this.label = s != null ? s : "";
        invalidate();
    }

    public void setHighlighted(boolean h) {
        if (this.highlighted == h) return;
        this.highlighted = h;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (def.appearance == KeyAppearance.EMPTY) return;

        float w = getWidth();
        float h = getHeight();
        bgRect.set(marginPx, marginPx, w - marginPx, h - marginPx);

        int bgColor = currentBgColor();
        bgPaint.setColor(bgColor);
        canvas.drawRoundRect(bgRect, cornerRadiusPx, cornerRadiusPx, bgPaint);

        // Label (supports a single newline as a two-line layout for T9 keys).
        textPaint.setColor(currentTextColor());
        textPaint.setTextSize(labelSizePx);
        textPaint.setTypeface(Typeface.DEFAULT);

        if (label != null && !label.isEmpty()) {
            int nl = label.indexOf('\n');
            if (nl < 0) {
                drawCenteredLine(canvas, label, w / 2f, h / 2f, textPaint);
            } else {
                String top = label.substring(0, nl);
                String bot = label.substring(nl + 1);
                Paint.FontMetrics fm = textPaint.getFontMetrics();
                float lineHeight = fm.descent - fm.ascent;
                float cy = h / 2f;
                drawCenteredLine(canvas, top, w / 2f, cy - lineHeight * 0.05f, textPaint);
                Paint sub = new Paint(textPaint);
                sub.setTextSize(labelSizePx * 0.6f);
                drawCenteredLine(canvas, bot, w / 2f, cy + lineHeight * 0.55f, sub);
            }
        }

        // Hint label (top-right corner). Skip on FUNCTION/ACCENT keys
        // and on EMPTY (already short-circuited above).
        if (hintLabel != null && !hintLabel.isEmpty()
                && def.appearance == KeyAppearance.NORMAL) {
            hintPaint.setColor(theme.hintLabelColor);
            hintPaint.setTextSize(hintSizePx);
            float hx = w - marginPx - dp(6);
            float hy = marginPx + dp(4) - hintPaint.getFontMetrics().ascent;
            canvas.drawText(hintLabel, hx, hy, hintPaint);
        }
    }

    private void drawCenteredLine(Canvas canvas, String text, float cx, float cy, Paint p) {
        Paint.FontMetrics fm = p.getFontMetrics();
        float baseline = cy - (fm.ascent + fm.descent) / 2f;
        canvas.drawText(text, cx, baseline, p);
    }

    private int currentBgColor() {
        switch (def.appearance) {
            case FUNCTION:
                return pressed ? theme.functionKeyBackgroundPressed
                              : theme.functionKeyBackground;
            case ACCENT:
                return pressed ? blend(theme.accentColor, theme.keyBackgroundPressed, 0.5f)
                              : theme.accentColor;
            case NORMAL:
            default:
                if (highlighted) {
                    return pressed ? blend(theme.accentColor, theme.keyBackgroundPressed, 0.5f)
                                  : blend(theme.accentColor, theme.keyBackground, 0.7f);
                }
                return pressed ? theme.keyBackgroundPressed : theme.keyBackground;
        }
    }

    private int currentTextColor() {
        switch (def.appearance) {
            case FUNCTION:
                return theme.keyTextFunction;
            case ACCENT:
                return Color.WHITE;
            case NORMAL:
            default:
                if (highlighted) return theme.accentColor;
                return theme.keyText;
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (def.appearance == KeyAppearance.EMPTY) return false;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pressed = true;
                longPressFired = false;
                invalidate();
                if (def.repeatable) {
                    // Repeatable keys (⌫): fire immediately for snappy
                    // first-tap feedback, then start repeating after a
                    // delay so a normal tap doesn't double-fire.
                    if (listener != null && def.clickAction != null) {
                        listener.onKey(def, KeyAction.CLICK);
                    }
                    postDelayed(repeatRunnable, REPEAT_START_DELAY_MS);
                } else if (def.longPressAction != null) {
                    postDelayed(longPressRunnable, LONG_PRESS_DELAY_MS);
                }
                return true;
            case MotionEvent.ACTION_UP:
                if (pressed) {
                    pressed = false;
                    invalidate();
                    removeCallbacks(longPressRunnable);
                    removeCallbacks(repeatRunnable);
                    // Fire the normal click only if this wasn't a
                    // repeat (already fired on DOWN) and a long-press
                    // didn't take over. Always invoke the listener —
                    // dual-state keys (T9 分词, Qwerty shift) keep
                    // clickAction null and rely on a custom listener.
                    if (!def.repeatable && !longPressFired && listener != null) {
                        listener.onKey(def, KeyAction.CLICK);
                    }
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
                pressed = false;
                longPressFired = false;
                invalidate();
                removeCallbacks(longPressRunnable);
                removeCallbacks(repeatRunnable);
                return true;
            default:
                return super.onTouchEvent(e);
        }
    }

    private float dp(float v) {
        return TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private float sp(float v) {
        return TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_SP, v, getResources().getDisplayMetrics());
    }

    private static int blend(int a, int b, float t) {
        int ar = Color.red(a),   ag = Color.green(a),   ab = Color.blue(a),   aa = Color.alpha(a);
        int br = Color.red(b),   bg = Color.green(b),   bb = Color.blue(b),   ba = Color.alpha(b);
        int rr = clamp((int) (ar * (1 - t) + br * t));
        int rg = clamp((int) (ag * (1 - t) + bg * t));
        int rb = clamp((int) (ab * (1 - t) + bb * t));
        int ra = clamp((int) (aa * (1 - t) + ba * t));
        return Color.argb(ra, rr, rg, rb);
    }

    private static int clamp(int v) {
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    }
}
