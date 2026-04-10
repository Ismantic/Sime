package com.isma.sime.ime;

import com.isma.sime.ime.engine.Candidate;
import com.isma.sime.ime.engine.DecodeResult;
import com.isma.sime.ime.engine.Decoder;
import com.isma.sime.ime.keyboard.KeyType;
import com.isma.sime.ime.keyboard.SimeKey;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Input logic core. Owns the single {@link InputState} instance, dispatches
 * key and candidate events, calls into the {@link Decoder}, and notifies the
 * IME service via {@link Listener} and the UI via {@link StateObserver}.
 *
 * <p>The behaviour here is the source of truth for Android input semantics
 * and is intentionally aligned with {@code Linux/fcitx5/src/sime.cc}. See
 * {@code private/SimeAndroidRefactor.md} sections 4 and 5 for the rules.
 */
public class InputKernel {

    /** Max candidates to request from the decoder per keystroke. */
    private static final int MAX_CANDIDATES = 36;

    /** Side effects requested by the kernel, forwarded to the IME service. */
    public interface Listener {
        /** Commit finalized text to the input connection. */
        void onCommitText(String text);

        /** Delete {@code count} characters before the cursor (passthrough). */
        void onDeleteBefore(int count);

        /** Ask the host to execute the default enter action. */
        void onSendEnter();

        /** Optional composing text preview (may be empty). */
        void onSetComposingText(String preedit);
    }

    /** Pulled by the UI whenever the state changes. */
    public interface StateObserver {
        void onStateChanged(InputState state, List<Candidate> candidates);
    }

    private final Decoder decoder;
    private final InputState state = new InputState();

    private KeyboardMode mode = KeyboardMode.CHINESE;
    private KeyboardMode previousMode = KeyboardMode.CHINESE;
    private ChineseLayout chineseLayout = ChineseLayout.QWERTY;

    private List<Candidate> candidates = Collections.emptyList();
    private List<PinyinAlt> pinyinAlts = Collections.emptyList();
    /** Segmented pinyin string of the current top candidate, for preedit. */
    private String topUnits = "";

    /**
     * A possible segmentation of the leading digits into pinyin letters.
     * Shown in the T9 left strip while input is active.
     */
    public static final class PinyinAlt {
        /** Display string without separators, e.g. {@code "ni"}. */
        public final String letters;
        /** Segmented string with {@code '} separators, e.g. {@code "ni'hao"}. */
        public final String units;
        /** How many digits this alternative consumes from the digit region. */
        public final int digitCount;

        public PinyinAlt(String letters, String units, int digitCount) {
            this.letters = letters;
            this.units = units;
            this.digitCount = digitCount;
        }
    }

    private Listener listener;
    private StateObserver observer;

    public InputKernel(Decoder decoder) {
        this.decoder = decoder;
    }

    // ===== Lifecycle =====

    public void attach(Listener l, StateObserver o) {
        this.listener = l;
        this.observer = o;
    }

    public void detach() {
        this.listener = null;
        this.observer = null;
    }

    public void onStartInput() {
        resetAll();
    }

    public void onFinishInput() {
        resetAll();
    }

    // ===== State accessors =====

    public InputState getState()             { return state; }
    public KeyboardMode getMode()            { return mode; }
    public ChineseLayout getChineseLayout()  { return chineseLayout; }
    public List<Candidate> getCandidates()   { return candidates; }
    public List<PinyinAlt> getPinyinAlts()   { return pinyinAlts; }
    public String getTopUnits()              { return topUnits; }

    public void setChineseLayout(ChineseLayout l) {
        this.chineseLayout = l;
    }

    // ===== Key dispatch =====

    public void onKey(SimeKey key) {
        if (key == null) return;
        switch (key.type) {
            case LETTER:      handleLetter(key.ch); break;
            case DIGIT:       handleDigit(key.ch); break;
            case SEPARATOR:   handleSeparator(); break;
            case SPACE:       handleSpace(); break;
            case ENTER:       handleEnter(); break;
            case BACKSPACE:   handleBackspace(); break;
            case CLEAR:       resetAll(); break;
            case PUNCTUATION: handlePunctuation(key.text); break;
            case TO_NUMBER:   switchMode(KeyboardMode.NUMBER); break;
            case TO_SYMBOL:   switchMode(KeyboardMode.SYMBOL); break;
            case TO_BACK:     switchMode(previousMode); break;
            case TOGGLE_LANG: handleToggleLang(); break;
        }
    }

    // ===== Letter / digit =====

    private void handleLetter(char c) {
        if (mode != KeyboardMode.CHINESE) {
            if (listener != null) listener.onCommitText(String.valueOf(c));
            return;
        }
        // QWERTY or T9 fallback letter: insert at lettersEnd, extend the
        // letter region. For QWERTY lettersEnd == buffer.length() so this
        // is equivalent to append.
        String buf = state.buffer;
        state.buffer = buf.substring(0, state.lettersEnd)
                + c
                + buf.substring(state.lettersEnd);
        state.lettersEnd += 1;
        state.cursor = state.lettersEnd;
        redecodeAndPublish();
    }

    private void handleDigit(char c) {
        if (mode != KeyboardMode.CHINESE || chineseLayout != ChineseLayout.T9) {
            if (listener != null) listener.onCommitText(String.valueOf(c));
            return;
        }
        // T9 digit: append to the end of the buffer (after any letters).
        state.buffer = state.buffer + c;
        state.cursor = state.buffer.length();
        redecodeAndPublish();
    }

    private void handleSeparator() {
        if (mode != KeyboardMode.CHINESE) {
            if (listener != null) listener.onCommitText("'");
            return;
        }
        // Mirror Linux sime.cc L493: only respond when buffer is non-empty.
        if (state.buffer.isEmpty()) return;
        // Collapse consecutive separators — two '\'' in a row would just
        // grow the decoder net without changing any segmentation.
        if (state.buffer.charAt(state.buffer.length() - 1) == '\'') return;
        // T9: append '\'' to the digit tail as a hard syllable boundary.
        // It stays literal in the buffer and is honored by the C++
        // decoder (InitNumNet / DecodeNumSentence).
        // QWERTY: insert at lettersEnd (which equals buffer length for
        // pure letter input), keeping the old behavior.
        if (chineseLayout == ChineseLayout.T9
                && state.lettersEnd < state.buffer.length()) {
            state.buffer = state.buffer + '\'';
            state.cursor = state.buffer.length();
        } else {
            String buf = state.buffer;
            state.buffer = buf.substring(0, state.lettersEnd)
                    + '\''
                    + buf.substring(state.lettersEnd);
            state.lettersEnd += 1;
            state.cursor = state.lettersEnd;
        }
        redecodeAndPublish();
    }

    // ===== Space / Enter =====

    private void handleSpace() {
        if (state.buffer.isEmpty()) {
            if (listener != null) listener.onCommitText(" ");
            return;
        }
        // Input active: pick the first hanzi candidate (same as clicking
        // the #0 slot in the candidates bar).
        if (!candidates.isEmpty()) {
            onHanziCandidatePick(0);
        }
    }

    private void handleEnter() {
        if (state.buffer.isEmpty()) {
            if (listener != null) listener.onSendEnter();
            return;
        }
        // Commit the raw text: already-picked hanzi + the remaining raw
        // buffer (letters + digits as typed).
        String out = state.committedText() + state.remaining();
        if (listener != null) listener.onCommitText(out);
        resetAll();
    }

    // ===== Backspace =====

    private void handleBackspace() {
        if (state.buffer.isEmpty() && state.selections.isEmpty()) {
            if (listener != null) listener.onDeleteBefore(1);
            return;
        }

        InputAction top = state.undoStack.peek();
        if (top != null) {
            // An action-level undo is always preferred to raw byte delete.
            state.cancel();
        } else if (!state.buffer.isEmpty()) {
            // No action on the stack — drop the last char of buffer. If
            // it was inside the letter region, shrink lettersEnd.
            int len = state.buffer.length();
            state.buffer = state.buffer.substring(0, len - 1);
            if (state.lettersEnd > state.buffer.length()) {
                state.lettersEnd = state.buffer.length();
            }
            if (state.cursor > state.buffer.length()) {
                state.cursor = state.buffer.length();
            }
        } else if (!state.selections.isEmpty()) {
            // Degenerate fallback: pop a selection.
            state.selections.remove(state.selections.size() - 1);
        }

        if (state.isEmpty()) {
            resetAll();
        } else {
            redecodeAndPublish();
        }
    }

    // ===== Punctuation =====

    private void handlePunctuation(String text) {
        if (text == null || text.isEmpty()) return;
        if (mode == KeyboardMode.CHINESE && !state.buffer.isEmpty()
                && !candidates.isEmpty()) {
            // Commit first candidate before the punctuation so the
            // in-flight composition isn't lost.
            commitFirstCandidateInline();
        }
        if (listener != null) listener.onCommitText(text);
    }

    private void commitFirstCandidateInline() {
        Candidate c = candidates.get(0);
        state.select(c.text, c.pinyin, c.consumed);
        if (state.fullySelected()) {
            String out = state.committedText();
            if (listener != null) listener.onCommitText(out);
            resetAll();
        } else {
            // Flush the committed portion so the app sees it immediately.
            if (listener != null) listener.onCommitText(state.committedText());
            // Keep only the remaining raw tail as a fresh state.
            String rem = state.remaining();
            int letterTail = Math.max(0, state.lettersEnd - state.selectedLength());
            state.reset();
            state.buffer = rem;
            state.lettersEnd = letterTail;
            state.cursor = rem.length();
            redecodeAndPublish();
        }
    }

    // ===== Candidate picks =====

    /**
     * Apply a pinyin letter substitution over the current digit region.
     * {@code letters} will replace a leading chunk of digits of length
     * {@code digits.length()} starting at {@link InputState#lettersEnd}.
     */
    public void onPinyinCandidatePick(String digits, String letters, boolean fallback) {
        if (digits == null || letters == null) return;
        if (mode != KeyboardMode.CHINESE) return;
        state.applyLetterPick(digits, letters, fallback);
        redecodeAndPublish();
    }

    /**
     * Pick one of the pinyin alternatives computed during the last decode
     * (see {@link #getPinyinAlts()}).
     */
    public void onPinyinAltPick(int index) {
        if (index < 0 || index >= pinyinAlts.size()) return;
        if (mode != KeyboardMode.CHINESE) return;
        PinyinAlt alt = pinyinAlts.get(index);
        int start = state.lettersEnd;
        int end = Math.min(start + alt.digitCount, state.buffer.length());
        if (end <= start) return;
        String digits = state.buffer.substring(start, end);
        state.applyLetterPick(digits, alt.letters, false);
        redecodeAndPublish();
    }

    /**
     * Pick a fallback letter for the first digit in the undecided region.
     * Used by the T9 left strip's single-letter buttons.
     */
    public void onFallbackLetterPick(char letter) {
        if (mode != KeyboardMode.CHINESE) return;
        int start = state.lettersEnd;
        if (start >= state.buffer.length()) return;
        String digits = state.buffer.substring(start, start + 1);
        state.applyLetterPick(digits, String.valueOf(letter), true);
        redecodeAndPublish();
    }

    public void onHanziCandidatePick(int index) {
        if (index < 0 || index >= candidates.size()) return;
        Candidate c = candidates.get(index);
        state.select(c.text, c.pinyin, c.consumed);
        if (state.fullySelected()) {
            String out = state.committedText();
            if (listener != null) listener.onCommitText(out);
            resetAll();
        } else {
            redecodeAndPublish();
        }
    }

    // ===== Mode switching =====

    public void switchMode(KeyboardMode next) {
        if (next == mode) return;
        // Only remember "real" input modes as previousMode, so TO_BACK from
        // NUMBER/SYMBOL/SETTINGS returns to CHINESE or ENGLISH.
        if (mode != KeyboardMode.NUMBER
                && mode != KeyboardMode.SYMBOL
                && mode != KeyboardMode.SETTINGS) {
            this.previousMode = mode;
        }
        this.mode = next;
        // Do not touch InputState — resuming CHINESE should continue the
        // in-flight input. ENGLISH / NUMBER / SYMBOL / SETTINGS don't run
        // the decoder.
        if (next == KeyboardMode.CHINESE) {
            redecodeAndPublish();
        } else {
            // Clear candidate list so the bar shows idle state.
            candidates = Collections.emptyList();
            topUnits = "";
            publish();
        }
    }

    public void switchChineseLayout(ChineseLayout layout) {
        if (layout == chineseLayout) return;
        // Switching layout mid-input is allowed but re-runs the decoder
        // because T9 and QWERTY take different code paths.
        this.chineseLayout = layout;
        if (mode == KeyboardMode.CHINESE) redecodeAndPublish();
    }

    private void handleToggleLang() {
        KeyboardMode target = (mode == KeyboardMode.ENGLISH)
                ? KeyboardMode.CHINESE
                : KeyboardMode.ENGLISH;
        // Flush any in-flight composition when leaving CHINESE.
        if (mode == KeyboardMode.CHINESE && !state.buffer.isEmpty()) {
            String out = state.committedText() + state.remaining();
            if (listener != null) listener.onCommitText(out);
            resetAll();
        }
        switchMode(target);
    }

    // ===== Decoding =====

    private void redecodeAndPublish() {
        candidates = decode();
        publish();
    }

    private List<Candidate> decode() {
        pinyinAlts = Collections.emptyList();
        if (mode != KeyboardMode.CHINESE) {
            topUnits = "";
            return Collections.emptyList();
        }
        if (state.buffer.isEmpty()) {
            topUnits = "";
            return Collections.emptyList();
        }

        int sel = state.selectedLength();
        int len = state.buffer.length();
        // Un-selected letter region: [max(sel, 0) .. lettersEnd)
        // Un-selected digit region:  [max(sel, lettersEnd) .. len)
        // Both must skip the bytes already consumed by existing selections.
        int letterStart = Math.min(sel, state.lettersEnd);
        int letterEnd = state.lettersEnd;
        String letters = (letterEnd > letterStart)
                ? state.buffer.substring(letterStart, letterEnd)
                : "";
        int digitStart = Math.max(sel, state.lettersEnd);
        String digits = (digitStart < len)
                ? state.buffer.substring(digitStart, len)
                : "";

        DecodeResult[] raw;
        if (chineseLayout == ChineseLayout.T9 && !digits.isEmpty()) {
            raw = decoder.decodeT9(letters, digits, MAX_CANDIDATES);
            pinyinAlts = computePinyinAlts(digits);
        } else if (!letters.isEmpty()) {
            raw = decoder.decodeSentence(letters, MAX_CANDIDATES);
        } else {
            raw = new DecodeResult[0];
        }

        topUnits = (raw.length > 0 && raw[0].units != null) ? raw[0].units : "";
        // Both DecodeSentence and DecodeNumSentence enable tail expansion:
        // a dangling initial like "k" may be completed to "kan", adding
        // letters the user never typed. Clip the preedit pinyin back to
        // the real input-letter count (prefix letters + digits for T9)
        // so the display stays 1:1 with the raw buffer region being
        // decoded.
        if (!topUnits.isEmpty()) {
            int rawLen = countRealChars(letters) + countRealChars(digits);
            topUnits = clipUnitsToLetterCount(topUnits, rawLen);
        }
        List<Candidate> out = new ArrayList<>(raw.length);
        for (DecodeResult r : raw) {
            out.add(Candidate.fromDecode(r));
        }
        return out;
    }

    /** Count real chars (non-separator) in a raw input region. */
    private static int countRealChars(String s) {
        int n = 0;
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) != '\'') n++;
        }
        return n;
    }

    /**
     * Truncate a segmented pinyin string to at most {@code maxLetters}
     * real letters, preserving {@code '} separators but dropping any
     * trailing separator left behind by the clip.
     */
    private static String clipUnitsToLetterCount(String units, int maxLetters) {
        if (maxLetters <= 0) return "";
        int letters = 0;
        int end = 0;
        for (int i = 0; i < units.length(); i++) {
            char c = units.charAt(i);
            if (c == '\'') {
                end = i + 1;
                continue;
            }
            if (letters >= maxLetters) break;
            letters++;
            end = i + 1;
        }
        while (end > 0 && units.charAt(end - 1) == '\'') end--;
        return units.substring(0, end);
    }

    /** Max single-syllable alternatives to show in the T9 left strip. */
    private static final int MAX_PINYIN_ALTS = 12;

    /**
     * Compute single-syllable pinyin alternatives for the first digits in
     * the undecided region. Only results whose {@code units} contain no
     * syllable separator are kept — the left strip is a first-syllable
     * picker, not a full segmentation picker. See
     * {@code private/SimeAndroidRefactor.md} §0.2 (4).
     */
    private List<PinyinAlt> computePinyinAlts(String digits) {
        DecodeResult[] raw = decoder.decodeT9("", digits, MAX_CANDIDATES);
        if (raw.length == 0) return Collections.emptyList();
        // Deduplicate by units string, keeping first occurrence order.
        java.util.LinkedHashMap<String, PinyinAlt> seen = new java.util.LinkedHashMap<>();
        for (DecodeResult r : raw) {
            if (r.units == null || r.units.isEmpty()) continue;
            // Reject multi-syllable: must contain no separator.
            if (r.units.indexOf('\'') >= 0 || r.units.indexOf(' ') >= 0) continue;
            // Reject tail-expanded alts: each T9 digit maps to exactly
            // one letter, so an alt is only faithful when its pinyin
            // letters equal the digits it consumes.
            if (r.units.length() != r.consumed) continue;
            if (seen.containsKey(r.units)) continue;
            seen.put(r.units, new PinyinAlt(r.units, r.units, r.consumed));
            if (seen.size() >= MAX_PINYIN_ALTS) break;
        }
        return new ArrayList<>(seen.values());
    }

    // ===== Helpers =====

    private void resetAll() {
        state.reset();
        candidates = Collections.emptyList();
        pinyinAlts = Collections.emptyList();
        topUnits = "";
        publish();
    }

    private void publish() {
        if (observer != null) observer.onStateChanged(state, candidates);
    }
}
