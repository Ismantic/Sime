package com.shiyu.sime.ime.keyboard.framework;

/**
 * Visual style buckets for a {@link KeyDef}. Drives background color,
 * text color, and (later) shadow / accent treatment in {@link KeyView}.
 *
 * <p>Disjoint from {@link com.shiyu.sime.ime.keyboard.KeyType} which
 * describes the <em>event</em> a key emits — this enum describes the
 * <em>look</em>.
 */
public enum KeyAppearance {
    /** Letter / digit / hanzi-input keys. Lighter bg, primary text color. */
    NORMAL,
    /** Control keys (⌫, ⏎, 中/英, 123, …). Darker bg, secondary text color. */
    FUNCTION,
    /** Emphasised key (selected setting, primary action). Accent bg. */
    ACCENT,
    /** Placeholder slot — invisible, not touchable. Used by settings grid. */
    EMPTY
}
