package com.semantic.sime.ime.keyboard.framework;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * One horizontal row of {@link KeyDef}s plus a vertical weight that
 * determines how the row's height compares to its siblings inside a
 * {@link KeyboardLayout}.
 */
public final class KeyRow {

    public final List<KeyDef> keys;
    public final float heightWeight;

    private KeyRow(List<KeyDef> keys, float heightWeight) {
        this.keys = Collections.unmodifiableList(keys);
        this.heightWeight = heightWeight;
    }

    public static Builder builder(float heightWeight) {
        return new Builder(heightWeight);
    }

    /** Default height weight = 1. */
    public static Builder builder() {
        return new Builder(1f);
    }

    public static final class Builder {
        private final float heightWeight;
        private final List<KeyDef> keys = new ArrayList<>();

        private Builder(float heightWeight) {
            this.heightWeight = heightWeight;
        }

        public Builder key(KeyDef k) {
            keys.add(k);
            return this;
        }

        public Builder key(KeyDef.Builder b) {
            keys.add(b.build());
            return this;
        }

        public KeyRow build() {
            return new KeyRow(keys, heightWeight);
        }
    }
}
