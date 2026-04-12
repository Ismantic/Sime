package com.semantic.sime.ime.keyboard.framework;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * A static description of a keyboard: a vertical stack of {@link KeyRow}s
 * plus global spacing parameters. Layouts are pure data — they don't
 * touch View / Context / theme. {@link KeyboardContainer} consumes them
 * and instantiates {@link KeyView}s accordingly.
 *
 * <p>To make a keyboard, write a layout file (e.g. {@code T9Layout}) that
 * exposes a {@code build()} method returning one of these.
 */
public final class KeyboardLayout {

    public final List<KeyRow> rows;
    public final int horizontalPaddingDp;
    public final int verticalPaddingDp;
    public final int keyMarginDp;

    private KeyboardLayout(Builder b) {
        this.rows = Collections.unmodifiableList(b.rows);
        this.horizontalPaddingDp = b.horizontalPaddingDp;
        this.verticalPaddingDp = b.verticalPaddingDp;
        this.keyMarginDp = b.keyMarginDp;
    }

    public static Builder builder() {
        return new Builder();
    }

    public static final class Builder {
        private final List<KeyRow> rows = new ArrayList<>();
        private int horizontalPaddingDp = 4;
        private int verticalPaddingDp = 6;
        private int keyMarginDp = 3;

        public Builder row(KeyRow r) {
            rows.add(r);
            return this;
        }

        public Builder row(KeyRow.Builder b) {
            rows.add(b.build());
            return this;
        }

        public Builder horizontalPadding(int dp) { this.horizontalPaddingDp = dp; return this; }
        public Builder verticalPadding(int dp)   { this.verticalPaddingDp = dp; return this; }
        public Builder keyMargin(int dp)         { this.keyMarginDp = dp; return this; }

        public KeyboardLayout build() {
            return new KeyboardLayout(this);
        }
    }
}
