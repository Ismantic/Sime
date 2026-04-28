package com.shiyu.sime.ime;

/**
 * Shared pinyin / buffer string utilities.
 */
public final class PinyinUtil {

    private PinyinUtil() {}

    /**
     * Count non-separator ({@code '}) characters in a raw input string.
     * Used to measure how many real pinyin letters (or digits) a buffer
     * region contains.
     */
    public static int countRealChars(String s) {
        int n = 0;
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) != '\'') n++;
        }
        return n;
    }
}
