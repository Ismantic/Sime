package com.shiyu.sime.ime.engine;

import com.shiyu.sime.SimeEngine;

/**
 * Adapts {@link SimeEngine} (which returns {@link DecodeResult} arrays
 * directly from JNI) to the {@link Decoder} interface consumed by
 * {@link com.shiyu.sime.ime.InputKernel}.
 */
public final class SimeEngineDecoder implements Decoder {

    private final SimeEngine engine;

    public SimeEngineDecoder(SimeEngine engine) {
        this.engine = engine;
    }

    @Override
    public DecodeResult[] decodeSentence(String pinyin, int limit) {
        if (!engine.isReady()) return new DecodeResult[0];
        return engine.decodeSentence(pinyin, limit);
    }

    @Override
    public DecodeResult[] decodeNumSentence(String startLetters, String digits, int limit) {
        if (!engine.isReady()) return new DecodeResult[0];
        return engine.decodeNumSentence(startLetters, digits, limit);
    }

    @Override
    public DecodeResult[] nextTokens(int[] contextIds, int limit, boolean enOnly) {
        if (!engine.isReady()) return new DecodeResult[0];
        return engine.nextTokens(contextIds, limit, enOnly);
    }

    @Override
    public DecodeResult[] getTokens(String prefix, int limit, boolean enOnly) {
        if (!engine.isReady()) return new DecodeResult[0];
        return engine.getTokens(prefix, limit, enOnly);
    }

    @Override
    public String[] t9PinyinSyllables(String digits, int limit) {
        if (!engine.isReady()) return new String[0];
        return engine.t9PinyinSyllables(digits, limit);
    }

    @Override
    public int contextSize() {
        return engine.contextSize();
    }
}
