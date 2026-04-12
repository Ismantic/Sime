package com.isma.sime.ime.engine;

import com.isma.sime.SimeEngine;

/**
 * Adapts {@link SimeEngine} (which returns {@link DecodeResult} arrays
 * directly from JNI) to the {@link Decoder} interface consumed by
 * {@link com.isma.sime.ime.InputKernel}.
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
}
