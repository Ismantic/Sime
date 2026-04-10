package com.isma.sime.ime.engine;

import com.isma.sime.SimeEngine;

/**
 * Adapts {@link SimeEngine} (which returns {@code SimeEngine.Candidate}
 * triplets from JNI) to the strongly-typed {@link Decoder} interface
 * consumed by {@link com.isma.sime.ime.InputKernel}.
 */
public final class SimeEngineDecoder implements Decoder {

    private final SimeEngine engine;

    public SimeEngineDecoder(SimeEngine engine) {
        this.engine = engine;
    }

    @Override
    public DecodeResult[] decodeSentence(String pinyin, int limit) {
        if (!engine.isReady()) return new DecodeResult[0];
        return convert(engine.decodeSentence(pinyin, limit));
    }

    @Override
    public DecodeResult[] decodeT9(String startLetters, String digits, int limit) {
        if (!engine.isReady()) return new DecodeResult[0];
        return convert(engine.decodeT9(startLetters, digits, limit));
    }

    private static DecodeResult[] convert(SimeEngine.Candidate[] raw) {
        DecodeResult[] out = new DecodeResult[raw.length];
        for (int i = 0; i < raw.length; i++) {
            out[i] = new DecodeResult(raw[i].text, raw[i].units, raw[i].matchedLen);
        }
        return out;
    }
}
