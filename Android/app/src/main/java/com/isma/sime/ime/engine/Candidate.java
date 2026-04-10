package com.isma.sime.ime.engine;

/**
 * A candidate as shown in the candidates bar. A thin UI-facing wrapper
 * around {@link DecodeResult}.
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
