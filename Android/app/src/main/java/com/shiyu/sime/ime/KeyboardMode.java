package com.shiyu.sime.ime;

/**
 * Top-level keyboard mode. See private/SimeAndroidRefactor.md section 5.
 *
 * CHINESE is the only mode that runs the decoder; ENGLISH commits letters
 * directly; NUMBER and SYMBOL are independent typing keyboards.
 */
public enum KeyboardMode {
    CHINESE,
    ENGLISH,
    NUMBER,
    SYMBOL,
    /** Inline settings panel (opened via the ⚙ gear on the candidates bar). */
    SETTINGS,
    /** Quick-phrase picker (opened from the settings panel). */
    QUICK_PHRASE,
    /** Recent clipboard history picker (opened from the settings panel). */
    CLIPBOARD,
    /** Emoji picker (opened from the settings panel). */
    EMOJI
}
