package com.isma.sime.ime;

/**
 * Undo-stack element representing a single user-initiated state mutation.
 *
 * Only selection/pick actions are pushed onto the stack; plain character
 * insertions are NOT tracked here. Backspace consults the stack to decide
 * whether to undo a whole action (e.g. revert a pinyin pick) or to delete
 * the last byte of the buffer.
 *
 * See private/SimeAndroidRefactor.md section 4.3 for the full rules.
 */
public final class InputAction {

    public enum Type {
        /** User picked a pinyin candidate from the T9 left strip. */
        PINYIN_PICK,
        /** User picked a fallback letter (e.g. `q` for digit `7`). */
        FALLBACK_PICK,
        /** User picked a hanzi candidate from the candidates bar. */
        HANZI_PICK
    }

    public final Type type;

    // --- PINYIN_PICK / FALLBACK_PICK fields ---
    public final int start;             // offset in buffer where replacement starts
    public final String replacedDigits; // original digit substring
    public final String pickedLetters;  // letters inserted in place of digits
    /**
     * True when {@link InputState#applyLetterPick} inserted a {@code '}
     * separator immediately before {@link #pickedLetters} (because the
     * previous char in the buffer was a letter, marking a syllable
     * boundary). The revert path uses this to remove the extra byte.
     */
    public final boolean leadingSeparator;

    // --- HANZI_PICK fields ---
    public final String hanziText;
    public final String hanziPinyin;
    public final int hanziConsumed;     // bytes consumed from buffer.remaining()
    public final int prevLettersEnd;    // lettersEnd before the pick (for undo)

    private InputAction(Type type,
                        int start,
                        String replacedDigits,
                        String pickedLetters,
                        boolean leadingSeparator,
                        String hanziText,
                        String hanziPinyin,
                        int hanziConsumed,
                        int prevLettersEnd) {
        this.type = type;
        this.start = start;
        this.replacedDigits = replacedDigits;
        this.pickedLetters = pickedLetters;
        this.leadingSeparator = leadingSeparator;
        this.hanziText = hanziText;
        this.hanziPinyin = hanziPinyin;
        this.hanziConsumed = hanziConsumed;
        this.prevLettersEnd = prevLettersEnd;
    }

    public static InputAction pinyinPick(int start, String digits, String letters,
                                          boolean leadingSeparator) {
        return new InputAction(Type.PINYIN_PICK, start, digits, letters,
                leadingSeparator, null, null, 0, 0);
    }

    public static InputAction fallbackPick(int start, String digits, String letters,
                                            boolean leadingSeparator) {
        return new InputAction(Type.FALLBACK_PICK, start, digits, letters,
                leadingSeparator, null, null, 0, 0);
    }

    public static InputAction hanziPick(String text, String pinyin, int consumed,
                                         int prevLettersEnd) {
        return new InputAction(Type.HANZI_PICK, 0, null, null,
                false, text, pinyin, consumed, prevLettersEnd);
    }

    /**
     * Undo this action on the given state. Caller is responsible for also
     * re-triggering decoding after revert.
     */
    public void revert(InputState s) {
        switch (type) {
            case PINYIN_PICK:
            case FALLBACK_PICK: {
                // Replacement spans `start` .. `start + sep + letters`.
                // Restore the original digit substring and roll lettersEnd
                // back to the position right before the (optional) sep.
                int sepLen = leadingSeparator ? 1 : 0;
                int end = start + sepLen + pickedLetters.length();
                s.buffer = s.buffer.substring(0, start)
                        + replacedDigits
                        + s.buffer.substring(end);
                s.lettersEnd = start;
                if (s.cursor > s.buffer.length()) s.cursor = s.buffer.length();
                break;
            }
            case HANZI_PICK: {
                if (!s.selections.isEmpty()) {
                    s.selections.remove(s.selections.size() - 1);
                }
                s.lettersEnd = prevLettersEnd;
                break;
            }
        }
    }
}
