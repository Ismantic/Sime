package com.shiyu.sime.ime.engine;

/**
 * Decoder abstraction consumed by {@link com.shiyu.sime.ime.InputKernel}.
 *
 * <p>This interface keeps the kernel decoupled from the Android-specific
 * {@link com.shiyu.sime.SimeEngine} so that kernel logic can be unit tested
 * on a plain JVM with a stub implementation.
 *
 * <p>Both decode methods return at most {@code limit} extra full-sentence
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
     * fallback picks.
     */
    DecodeResult[] decodeNumSentence(String startLetters, String digits, int limit);

    /**
     * Prediction: suggest next words based on recently committed token IDs.
     *
     * @param contextIds token IDs of recently committed words
     * @param limit      max number of predictions
     * @param enOnly     when true, only return English tokens
     */
    DecodeResult[] nextTokens(int[] contextIds, int limit, boolean enOnly);

    /**
     * Prefix completion: return tokens starting with {@code prefix},
     * sorted by unigram score.
     *
     * @param prefix  the prefix to search for
     * @param limit   max number of results
     * @param enOnly  when true, only the English DAT is searched
     */
    DecodeResult[] getTokens(String prefix, int limit, boolean enOnly);

    /**
     * Return legal pinyin syllables whose T9 spelling consumes a prefix of
     * {@code digits}. Results are used only for the T9 pinyin-alt strip.
     */
    String[] t9PinyinSyllables(String digits, int limit);

    /** Max context tokens the LM uses for prediction (n-gram order minus 1). */
    int contextSize();

    /**
     * Records a single user pick into the user-history LM. Implementations
     * may throttle disk writes; pair with {@link #flushUserSentence()}
     * at session boundaries. Default no-op so test stubs don't have to
     * implement learning.
     */
    default void learnUserSentence(int[] context, int[] tokens) {}

    /** Persist any pending user-history learns to disk. Default no-op. */
    default void flushUserSentence() {}
}
