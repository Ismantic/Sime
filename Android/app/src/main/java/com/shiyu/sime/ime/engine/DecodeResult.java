package com.shiyu.sime.ime.engine;

/**
 * Strongly-typed decode result returned by the native engine.
 * Mirrors {@code sime::DecodeResult} in {@code include/sime.h}.
 *
 * Fields:
 *   text     — UTF-8 hanzi string
 *   units    — segmented pinyin (e.g. "ni'hao")
 *   consumed — bytes of input consumed (the C++ {@code cnt} field)
 *   tokenIds — token IDs for LM context (used by NextTokens prediction)
 */
public final class DecodeResult {
    public final String text;
    public final String units;
    public final int consumed;
    public final int[] tokenIds;

    public DecodeResult(String text, String units, int consumed) {
        this(text, units, consumed, EMPTY_IDS);
    }

    public DecodeResult(String text, String units, int consumed, int[] tokenIds) {
        this.text = text;
        this.units = units;
        this.consumed = consumed;
        this.tokenIds = tokenIds;
    }

    private static final int[] EMPTY_IDS = new int[0];
}
