package com.semantic.sime.ime.theme;

/**
 * Single source of truth for text sizes (sp units) used across the IME
 * and the launcher activity. Replaces scattered literals so a tier-wide
 * tweak is one edit instead of dozens.
 *
 * <p>Tiers ordered small → large. Pick the smallest one whose semantic
 * fits the call site; bump up only when truly emphasized.
 */
public final class Typography {

    private Typography() {}

    /** Top-right corner hint glyph on QWERTY / T9 keys. */
    public static final float HINT = 10f;

    /** Smallest text — settings cells, AddPhrase 完成 button, panel empty state. */
    public static final float CAPTION = 13f;

    /** Function key labels — 符号 / 换行 / 重输 / 123 / EmojiPanel tabs / picker tabs. */
    public static final float SMALL = 14f;

    /** Default body — preedit, picker rows, punctuation keys, number-pad keys. */
    public static final float BODY = 15f;

    /** Emphasized body — function buttons, edit content, AddPhrase buffer. */
    public static final float CALLOUT = 16f;

    /** Primary content — candidates, letter keys, settings activity buttons. */
    public static final float TITLE = 18f;

    /** Display glyphs — emoji cells, large action affordances (×, +). */
    public static final float DISPLAY = 22f;

    /** Onboarding / activity headline. */
    public static final float HEADLINE = 28f;
}
