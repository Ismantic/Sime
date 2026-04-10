package com.isma.sime.ime.engine;

/**
 * Strongly-typed decode result returned by the native engine.
 * Mirrors {@code sime::DecodeResult} in {@code include/interpret.h}.
 *
 * Fields:
 *   text     — UTF-8 hanzi string
 *   units    — segmented pinyin (e.g. "ni'hao")
 *   score    — larger is better (negative log probability negated)
 *   consumed — bytes of input consumed (the C++ {@code cnt} field)
 */
public final class DecodeResult {
    public final String text;
    public final String units;
    public final float score;
    public final int consumed;

    public DecodeResult(String text, String units, float score, int consumed) {
        this.text = text;
        this.units = units;
        this.score = score;
        this.consumed = consumed;
    }
}
