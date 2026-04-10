package com.isma.sime.ime.engine;

/**
 * A candidate as shown in the candidates bar. For now this is a thin
 * wrapper around {@link DecodeResult} limited to what the UI needs; in
 * Phase 3 the adapter will use it directly.
 */
public final class Candidate {
    public final String text;
    public final String pinyin;
    public final int consumed;

    public Candidate(String text, String pinyin, int consumed) {
        this.text = text;
        this.pinyin = pinyin;
        this.consumed = consumed;
    }

    public static Candidate fromDecode(DecodeResult r) {
        return new Candidate(r.text, r.units, r.consumed);
    }
}
