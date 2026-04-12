package com.semantic.sime.ime.engine;

/**
 * Strongly-typed decode result returned by the native engine.
 * Mirrors {@code sime::DecodeResult} in {@code include/interpret.h}.
 *
 * Fields:
 *   text     — UTF-8 hanzi string
 *   units    — segmented pinyin (e.g. "ni'hao")
 *   consumed — bytes of input consumed (the C++ {@code cnt} field)
 */
public final class DecodeResult {
    public final String text;
    public final String units;
    public final int consumed;

    public DecodeResult(String text, String units, int consumed) {
        this.text = text;
        this.units = units;
        this.consumed = consumed;
    }
}
