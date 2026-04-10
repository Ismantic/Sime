package com.isma.sime.ime.engine;

import com.isma.sime.SimeEngine;

/**
 * Adapts the legacy {@link SimeEngine} (which still returns
 * {@code SimeEngine.Candidate} triplets) to the strongly-typed
 * {@link Decoder} interface consumed by
 * {@link com.isma.sime.ime.InputKernel}.
 *
 * <p>In Phase 3 this is a thin translation layer. A future pass will push
 * {@link DecodeResult} through the JNI boundary directly and this adapter
 * can go away.
 */
public final class SimeEngineDecoder implements Decoder {

    private final SimeEngine engine;

    public SimeEngineDecoder(SimeEngine engine) {
        this.engine = engine;
    }

    @Override
    public DecodeResult[] decodeSentence(String pinyin, int limit) {
        if (!engine.isReady()) return new DecodeResult[0];
        SimeEngine.Candidate[] raw = engine.decodeSentence(pinyin, limit);
        return convert(raw);
    }

    @Override
    public DecodeResult[] decodeT9(String startLetters, String digits, int limit) {
        if (!engine.isReady()) return new DecodeResult[0];
        // Legacy decodeT9 takes String[] prefix syllables; split on '.
        String[] prefix;
        if (startLetters == null || startLetters.isEmpty()) {
            prefix = new String[0];
        } else {
            prefix = startLetters.split("'");
        }
        SimeEngine.Candidate[] raw = engine.decodeT9(prefix, digits, limit);
        return convert(raw);
    }

    private static DecodeResult[] convert(SimeEngine.Candidate[] raw) {
        DecodeResult[] out = new DecodeResult[raw.length];
        for (int i = 0; i < raw.length; i++) {
            out[i] = new DecodeResult(raw[i].text, raw[i].units, 0f, raw[i].matchedLen);
        }
        return out;
    }
}
