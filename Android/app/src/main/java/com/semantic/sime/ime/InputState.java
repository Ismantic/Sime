package com.semantic.sime.ime;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;

/**
 * Pure data holder for the IME input state.
 *
 * CONTRACT: This class mirrors Linux/fcitx5/src/sime-state.h. Any change
 * that affects input semantics MUST be mirrored in both implementations.
 * See private/SimeAndroidRefactor.md section 3 and appendix B for the
 * alignment checklist.
 *
 * Field-by-field mapping:
 *   buffer, cursor, selections, Selection — direct copy of SimeState
 *   lettersEnd, undoStack                 — Android-only additions for T9
 *
 * Method-by-method mapping (match signatures and semantics):
 *   selectedLength() / committedText() / remaining() / fullySelected()
 *   cancel() / select() / reset()
 *
 * In the QWERTY-only (non-T9) degenerate case the behaviour collapses to
 * exactly what SimeState does: lettersEnd == buffer.length() always, and
 * the undo stack only holds HANZI_PICK entries which are equivalent to
 * selections.pop_back().
 */
public final class InputState {

    // ===== mirrors SimeState =====

    public String buffer = "";
    public int cursor = 0;
    public final List<Selection> selections = new ArrayList<>();

    public static final class Selection {
        public final String text;    // committed hanzi for this selection
        public final String pinyin;  // associated pinyin segment
        public final int consumed;   // bytes consumed from buffer

        public Selection(String text, String pinyin, int consumed) {
            this.text = text;
            this.pinyin = pinyin;
            this.consumed = consumed;
        }
    }

    // ===== Prediction context (mirrors SimeState::context / context_ids) =====

    /** Recent committed token IDs, persists across composing sessions. */
    public final List<Integer> contextIds = new ArrayList<>();
    /** Whether the candidate bar is currently showing predictions. */
    public boolean predicting = false;

    /** Max context tokens the LM can use (set from engine's contextSize()). */
    public int maxContextIds = 2;

    /**
     * Push committed token IDs into the prediction context.
     * Mirrors {@code SimeState::pushContext}.
     */
    public void pushContext(int[] tokens) {
        for (int tid : tokens) contextIds.add(tid);
        while (contextIds.size() > maxContextIds) {
            contextIds.remove(0);
        }
    }

    public void clearContext() {
        contextIds.clear();
        predicting = false;
    }

    public int[] contextIdsArray() {
        int[] arr = new int[contextIds.size()];
        for (int i = 0; i < arr.length; i++) arr[i] = contextIds.get(i);
        return arr;
    }

    // ===== Android-specific additions =====

    /**
     * For T9: [0, lettersEnd) is confirmed-letter region (pinyin picks and
     * fallback-letter picks extend this), [lettersEnd, buffer.length()) is
     * the undecided digit region. For QWERTY, lettersEnd == buffer.length()
     * at all times.
     */
    public int lettersEnd = 0;

    /**
     * Action-based undo stack. Plain character insertions are NOT pushed;
     * only PINYIN_PICK / FALLBACK_PICK / HANZI_PICK actions are. Stack top
     * is the most recent action.
     */
    public final Deque<InputAction> undoStack = new ArrayDeque<>();

    // ===== methods mirror SimeState =====

    public int selectedLength() {
        int sum = 0;
        for (Selection s : selections) sum += s.consumed;
        return sum;
    }

    public String committedText() {
        StringBuilder sb = new StringBuilder();
        for (Selection s : selections) sb.append(s.text);
        return sb.toString();
    }

    public String remaining() {
        int off = selectedLength();
        if (off >= buffer.length()) return "";
        return buffer.substring(off);
    }

    public boolean fullySelected() {
        return selectedLength() == buffer.length();
    }

    /**
     * Undo the most recent state-changing operation.
     *
     * @return {@code true} if something was undone; {@code false} if the
     *         stack and selections were both empty.
     */
    public boolean cancel() {
        if (!undoStack.isEmpty()) {
            InputAction a = undoStack.pop();
            a.revert(this);
            return true;
        }
        if (!selections.isEmpty()) {
            selections.remove(selections.size() - 1);
            return true;
        }
        return false;
    }

    /**
     * Add a hanzi selection and push the corresponding undo action.
     * Signature and order mirror {@code SimeState::select}.
     *
     * <p>Maintains the invariant {@code lettersEnd >= selectedLength()}: a
     * hanzi pick whose consumed bytes extend past the current letter region
     * effectively turns those bytes into "letters" too (they're now part of
     * the committed hanzi context, not the unconfirmed digit region).
     * Without this push, a subsequent {@link #applyLetterPick} would write
     * into the stale bytes still under {@code selectedLength}.
     */
    public void select(String text, String pinyin, int consumed) {
        int prevLettersEnd = lettersEnd;
        // Absorb trailing separator: applyLetterPick auto-inserts '\''
        // between syllable picks.  Once a selection consumes the preceding
        // syllable the separator is orphaned — absorb it so the next
        // decode sees a clean start without patching consumed elsewhere.
        int sel = selectedLength() + consumed;
        while (sel < lettersEnd && sel < buffer.length()
                && buffer.charAt(sel) == '\'') {
            consumed++;
            sel++;
        }
        selections.add(new Selection(text, pinyin, consumed));
        sel = selectedLength();
        if (lettersEnd < sel) lettersEnd = sel;
        undoStack.push(InputAction.hanziPick(text, pinyin, consumed, prevLettersEnd));
    }

    /**
     * Apply a pinyin or fallback-letter pick: replace the digit substring
     * starting at {@code lettersEnd} with {@code letters}, extending the
     * letter region. The action is recorded on the undo stack.
     *
     * @param digits   the digit substring being replaced (must match
     *                 {@code buffer.substring(lettersEnd, lettersEnd + digits.length())})
     * @param letters  the letter string to insert
     * @param fallback {@code true} for FALLBACK_PICK, {@code false} for PINYIN_PICK
     */
    public void applyLetterPick(String digits, String letters, boolean fallback) {
        int start = lettersEnd;
        int end = start + digits.length();
        if (end > buffer.length()) {
            throw new IllegalArgumentException(
                    "digit substring exceeds buffer: start=" + start
                            + " digits='" + digits + "' buffer='" + buffer + "'");
        }
        // If the previous buffer char is a letter, the new picked letters
        // would otherwise concatenate into a single ambiguous string. Inject
        // a `'` separator so syllable boundaries are preserved for the
        // decoder (and the preedit display) — e.g. picking "hao" then "de"
        // becomes "hao'de", not "haode".
        boolean leadingSep = start > 0
                && Character.isLetter(buffer.charAt(start - 1));
        String replacement = leadingSep ? ("'" + letters) : letters;
        buffer = buffer.substring(0, start) + replacement + buffer.substring(end);
        lettersEnd = start + replacement.length();
        if (cursor < lettersEnd) cursor = lettersEnd;
        InputAction a = fallback
                ? InputAction.fallbackPick(start, digits, letters, leadingSep)
                : InputAction.pinyinPick(start, digits, letters, leadingSep);
        undoStack.push(a);
    }

    public void reset() {
        buffer = "";
        cursor = 0;
        lettersEnd = 0;
        selections.clear();
        undoStack.clear();
    }

    public boolean isEmpty() {
        return buffer.isEmpty() && selections.isEmpty();
    }

    /** Defensive copy of UI-visible fields (excludes undoStack/contextIds). */
    public InputState copy() {
        InputState c = new InputState();
        c.buffer = this.buffer;
        c.cursor = this.cursor;
        c.lettersEnd = this.lettersEnd;
        c.predicting = this.predicting;
        c.selections.addAll(this.selections);
        return c;
    }
}
