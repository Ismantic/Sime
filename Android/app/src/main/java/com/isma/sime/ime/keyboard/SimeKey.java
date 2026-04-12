package com.isma.sime.ime.keyboard;

/**
 * A single key event object passed from a {@link KeyboardView} to the
 * {@link com.isma.sime.ime.InputKernel}. Immutable.
 *
 * <p>The payload semantics depend on {@link #type}:
 * <ul>
 *   <li>LETTER: {@code ch} is the letter</li>
 *   <li>DIGIT: {@code ch} is '2'..'9'</li>
 *   <li>PUNCTUATION: {@code text} is the punctuation to commit</li>
 *   <li>other types: payload unused</li>
 * </ul>
 */
public final class SimeKey {
    public final KeyType type;
    public final char ch;
    public final String text;

    private SimeKey(KeyType type, char ch, String text) {
        this.type = type;
        this.ch = ch;
        this.text = text;
    }

    public static SimeKey letter(char c)     { return new SimeKey(KeyType.LETTER, c, null); }
    public static SimeKey digit(char c)      { return new SimeKey(KeyType.DIGIT, c, null); }
    public static SimeKey separator()        { return new SimeKey(KeyType.SEPARATOR, '\'', null); }
    public static SimeKey space()            { return new SimeKey(KeyType.SPACE, ' ', null); }
    public static SimeKey enter()            { return new SimeKey(KeyType.ENTER, '\n', null); }
    public static SimeKey backspace()        { return new SimeKey(KeyType.BACKSPACE, '\0', null); }
    public static SimeKey clear()            { return new SimeKey(KeyType.CLEAR, '\0', null); }
    public static SimeKey toNumber()         { return new SimeKey(KeyType.TO_NUMBER, '\0', null); }
    public static SimeKey toSymbol()         { return new SimeKey(KeyType.TO_SYMBOL, '\0', null); }
    public static SimeKey toBack()           { return new SimeKey(KeyType.TO_BACK, '\0', null); }
    public static SimeKey toggleLang()       { return new SimeKey(KeyType.TOGGLE_LANG, '\0', null); }
    public static SimeKey punctuation(String s) { return new SimeKey(KeyType.PUNCTUATION, '\0', s); }
    public static SimeKey numPunctuation()      { return new SimeKey(KeyType.NUM_PUNCTUATION, '\0', null); }
}
