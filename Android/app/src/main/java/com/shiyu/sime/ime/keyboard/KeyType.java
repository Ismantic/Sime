package com.shiyu.sime.ime.keyboard;

/**
 * High-level key event kinds dispatched by {@link com.shiyu.sime.ime.InputKernel}.
 */
public enum KeyType {
    /** A letter (QWERTY mode, also T9 fallback letters). */
    LETTER,
    /** A T9 digit key 2-9. */
    DIGIT,
    /** The pinyin separator key `'` or T9 `分词`. */
    SEPARATOR,
    /** Space bar. */
    SPACE,
    /** Enter / 换行 / 确定. */
    ENTER,
    /** Backspace / ⌫. */
    BACKSPACE,
    /** T9 重输 — full reset of the current input. */
    CLEAR,
    /** Mode-switch: to NUMBER keyboard (符号/数字 key). */
    TO_NUMBER,
    /** Mode-switch: to SYMBOL keyboard. */
    TO_SYMBOL,
    /** Mode-switch: return to the last CHINESE / ENGLISH keyboard. */
    TO_BACK,
    /** Toggle between CHINESE and ENGLISH mode. */
    TOGGLE_LANG,
    /** A punctuation shortcut (T9 left strip in idle state, etc.). */
    PUNCTUATION,
    /** T9 "1 key": opens an inline punctuation picker in the candidate bar. */
    NUM_PUNCTUATION
}
