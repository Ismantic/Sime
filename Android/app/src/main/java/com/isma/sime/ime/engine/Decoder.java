package com.isma.sime.ime.engine;

/**
 * Decoder abstraction consumed by {@link com.isma.sime.ime.InputKernel}.
 *
 * <p>This interface keeps the kernel decoupled from the Android-specific
 * {@link com.isma.sime.SimeEngine} so that kernel logic can be unit tested
 * on a plain JVM with a stub implementation.
 *
 * <p>Both methods return at most {@code limit} extra full-sentence
 * candidates beyond the top one. An implementation that is not yet ready
 * (engine still loading, resources missing) should return an empty array
 * rather than {@code null}.
 */
public interface Decoder {

    /**
     * QWERTY path: decode a pinyin letter string (optionally containing
     * {@code '} separators) to a list of hanzi candidates.
     */
    DecodeResult[] decodeSentence(String pinyin, int limit);

    /**
     * Num-key (T9 / nine-key) path: decode a digit string whose first
     * portion may already have been committed to letters via pinyin /
     * fallback picks. Mirrors {@code sime::Interpreter::DecodeNumSentence}.
     *
     * <p>The result array contains both Layer 1 (full sentence N-best) and
     * Layer 2 (word/char alternatives starting at the first digit) entries
     * as produced by the native engine. Callers that need single-syllable
     * pinyin alternatives extract them from Layer 2 directly without
     * issuing a second decode call.
     *
     * @param startLetters pinyin letters already confirmed for the
     *                     beginning of the input (may be empty). The last
     *                     initial may be incomplete — the native side
     *                     uses {@code ParseWithBoundaries} to expand it.
     * @param digits       undecided digit suffix (may be empty)
     * @param limit        maximum number of extra full-sentence candidates
     *                     to request beyond the top one (Layer 1)
     */
    DecodeResult[] decodeNumSentence(String startLetters, String digits, int limit);
}
