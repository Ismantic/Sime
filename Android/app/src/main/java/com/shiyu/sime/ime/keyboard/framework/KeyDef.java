package com.shiyu.sime.ime.keyboard.framework;

import com.shiyu.sime.ime.keyboard.SimeKey;

/**
 * Immutable description of a single key. Created via {@link Builder}; no
 * setters. Built by layout files (T9Layout, QwertyLayout, …) and consumed
 * by {@link KeyboardContainer} when it instantiates {@link KeyView}s.
 *
 * <p>{@code clickAction} is the SimeKey emitted on a normal tap.
 * {@code longPressAction} fires after the long-press delay; setting
 * {@code repeatable} makes the key fire {@code clickAction} immediately
 * on press and then repeatedly while held (used for ⌫).
 *
 * <p>{@code id} is optional. When set, it allows controllers
 * (e.g. T9KeyboardView's dual-state keys) to look the KeyView up at
 * runtime via {@link KeyboardContainer#findKeyById(String)} and update
 * its label without rebuilding the whole keyboard.
 */
public final class KeyDef {

    public final String label;
    public final String hintLabel;        // small label drawn top-right
    public final float widthWeight;
    public final KeyAppearance appearance;
    public final SimeKey clickAction;
    public final SimeKey longPressAction;
    public final boolean repeatable;
    public final String id;
    /** Override label text size (sp). 0 means "use container default". */
    public final float labelSizeSp;
    /** Optional vector drawable rendered above the label (settings cells). */
    public final int iconResId;

    private KeyDef(Builder b) {
        this.label = b.label;
        this.hintLabel = b.hintLabel;
        this.widthWeight = b.widthWeight;
        this.appearance = b.appearance;
        this.clickAction = b.clickAction;
        this.longPressAction = b.longPressAction;
        this.repeatable = b.repeatable;
        this.id = b.id;
        this.labelSizeSp = b.labelSizeSp;
        this.iconResId = b.iconResId;
    }

    public boolean isEmpty() {
        return appearance == KeyAppearance.EMPTY;
    }

    public static Builder normal(String label, SimeKey click) {
        return new Builder().label(label).appearance(KeyAppearance.NORMAL).click(click);
    }

    public static Builder function(String label, SimeKey click) {
        return new Builder().label(label).appearance(KeyAppearance.FUNCTION).click(click);
    }

    public static Builder accent(String label, SimeKey click) {
        return new Builder().label(label).appearance(KeyAppearance.ACCENT).click(click);
    }

    public static KeyDef empty(float weight) {
        return new Builder().appearance(KeyAppearance.EMPTY).width(weight).build();
    }

    public static final class Builder {
        private String label = "";
        private String hintLabel = null;
        private float widthWeight = 1f;
        private KeyAppearance appearance = KeyAppearance.NORMAL;
        private SimeKey clickAction = null;
        private SimeKey longPressAction = null;
        private boolean repeatable = false;
        private String id = null;
        private float labelSizeSp = 0f;
        private int iconResId = 0;

        public Builder label(String s)        { this.label = s; return this; }
        public Builder hint(String s)         { this.hintLabel = s; return this; }
        public Builder width(float w)         { this.widthWeight = w; return this; }
        public Builder appearance(KeyAppearance a) { this.appearance = a; return this; }
        public Builder click(SimeKey k)       { this.clickAction = k; return this; }
        public Builder longPress(SimeKey k)   { this.longPressAction = k; return this; }
        public Builder repeatable(boolean r)  { this.repeatable = r; return this; }
        public Builder id(String s)           { this.id = s; return this; }
        public Builder labelSize(float sp)    { this.labelSizeSp = sp; return this; }
        public Builder icon(int resId)        { this.iconResId = resId; return this; }

        public KeyDef build() { return new KeyDef(this); }
    }
}
