package com.semantic.sime.ime;

import com.semantic.sime.ime.engine.DecodeResult;
import com.semantic.sime.ime.engine.Decoder;
import com.semantic.sime.ime.PinyinUtil;
import com.semantic.sime.ime.keyboard.KeyType;
import com.semantic.sime.ime.keyboard.SimeKey;

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

    /**
     * Extra full-sentence alternatives to request from the decoder per
     * keystroke (Layer 1). Layer 2 single-syllable / single-char
     * alternatives are always returned in full. Mirrors Linux fcitx5
     * default {@code nbest=0}.
     */
    private static final int EXTRA_SENTENCES = 0;

    /** Number of predictions / completions to request. */
    private static final int PREDICTION_LIMIT = 10;

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
        void onStateChanged(InputState state, List<DecodeResult> candidates);
    }

    private final Decoder decoder;
    private final InputState state = new InputState();

    private KeyboardMode mode = KeyboardMode.CHINESE;
    private KeyboardMode previousMode = KeyboardMode.CHINESE;
    private ChineseLayout chineseLayout = ChineseLayout.QWERTY;

    private List<DecodeResult> candidates = Collections.emptyList();
    /**
     * True when the candidate list is showing T9 "1 key" punctuation
     * options instead of decoded hanzi. Picking commits the punctuation
     * directly; any other key dismisses the picker first.
     */
    private boolean inPunctuationPicker = false;

    /** Punctuation shown by the T9 "1 key" picker, in display order. */
    private static final String[] T9_NUM_PUNCTUATION = {
            "@", "#", "*", "+", "。", "~", "(", ")", "、"
    };
    private List<PinyinAlt> pinyinAlts = Collections.emptyList();
    /** Segmented pinyin string of the current top candidate, for preedit. */
    private String topUnits = "";

    /** Token IDs of each selection, parallel with state.selections. */
    private final List<int[]> selectionTokenIds = new ArrayList<>();

    /** English keyboard buffer (not yet committed). */
    private final StringBuilder englishBuffer = new StringBuilder();

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
    public List<DecodeResult> getCandidates()   { return candidates; }
    public List<PinyinAlt> getPinyinAlts()   { return pinyinAlts; }
    public String getTopUnits()              { return topUnits; }
    public String getEnglishBuffer()         { return englishBuffer.toString(); }
    public boolean isPredicting()            { return state.predicting; }

    public void setChineseLayout(ChineseLayout l) {
        this.chineseLayout = l;
    }

    // ===== Key dispatch =====

    public void onKey(SimeKey key) {
        if (key == null) return;
        // The T9 "1 key" picker is a modal candidate strip. Re-pressing
        // the same key keeps it open; any other key dismisses it first
        // and then runs its normal handler.
        if (inPunctuationPicker && key.type != KeyType.NUM_PUNCTUATION) {
            dismissPunctuationPicker(/*publish=*/false);
        }
        switch (key.type) {
            case LETTER:          handleLetter(key.ch); break;
            case DIGIT:           handleDigit(key.ch); break;
            case SEPARATOR:       handleSeparator(); break;
            case SPACE:           handleSpace(); break;
            case ENTER:           handleEnter(); break;
            case BACKSPACE:       handleBackspace(); break;
            case CLEAR:           resetAll(); break;
            case PUNCTUATION:     handlePunctuation(key.text); break;
            case NUM_PUNCTUATION: showPunctuationPicker(); break;
            case TO_NUMBER:       switchMode(KeyboardMode.NUMBER); break;
            case TO_SYMBOL:       switchMode(KeyboardMode.SYMBOL); break;
            case TO_BACK:         switchMode(previousMode); break;
            case TOGGLE_LANG:     handleToggleLang(); break;
        }
    }

    private void showPunctuationPicker() {
        if (mode != KeyboardMode.CHINESE) {
            // Outside of Chinese mode the picker has nowhere to render
            // — fall back to committing the first punctuation directly.
            if (listener != null) listener.onCommitText(T9_NUM_PUNCTUATION[0]);
            return;
        }
        // T9: while composing, the 1 key inserts a separator instead
        // of opening the punctuation picker (mirrors Linux sime.cc).
        if (chineseLayout == ChineseLayout.T9 && !state.buffer.isEmpty()) {
            handleSeparator();
            return;
        }
        java.util.ArrayList<DecodeResult> out =
                new java.util.ArrayList<>(T9_NUM_PUNCTUATION.length);
        for (String p : T9_NUM_PUNCTUATION) {
            out.add(new DecodeResult(p, "", 0));
        }
        candidates = out;
        topUnits = "";
        pinyinAlts = Collections.emptyList();
        inPunctuationPicker = true;
        publish();
    }

    private void dismissPunctuationPicker(boolean publish) {
        if (!inPunctuationPicker) return;
        inPunctuationPicker = false;
        candidates = Collections.emptyList();
        if (publish) publish();
    }

    // ===== Letter / digit =====

    private void handleLetter(char c) {
        if (mode == KeyboardMode.ENGLISH) {
            exitPrediction();
            englishBuffer.append(c);
            if (listener != null) listener.onSetComposingText(englishBuffer.toString());
            refreshEnglishCompletions();
            return;
        }
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
        if (mode == KeyboardMode.ENGLISH) {
            // Apostrophe in English mode: append to buffer (e.g. "don't")
            englishBuffer.append('\'');
            if (listener != null) listener.onSetComposingText(englishBuffer.toString());
            refreshEnglishCompletions();
            return;
        }
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
        if (mode == KeyboardMode.ENGLISH) {
            if (englishBuffer.length() > 0) {
                commitEnglishBuffer();
            } else {
                if (listener != null) listener.onCommitText(" ");
            }
            return;
        }
        if (state.predicting) {
            // In prediction mode, space picks the first prediction.
            if (!candidates.isEmpty()) {
                onPredictionPick(0);
            }
            return;
        }
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
        if (mode == KeyboardMode.ENGLISH) {
            if (englishBuffer.length() > 0) {
                commitEnglishBuffer();
            } else {
                if (listener != null) listener.onSendEnter();
            }
            return;
        }
        if (state.predicting) {
            exitPrediction();
            publish();
            if (listener != null) listener.onSendEnter();
            return;
        }
        if (state.buffer.isEmpty()) {
            if (listener != null) listener.onSendEnter();
            return;
        }
        // Commit what the user actually sees in the preedit:
        //   already-picked hanzi + the decoded pinyin letters.
        // For T9 input "743663" this commits "shenme" (the displayed
        // letters) instead of the raw digits, but for "94'26" it
        // commits "xi'an" — the user-typed separator is preserved
        // while decoder-inserted separators are dropped.
        String tail;
        if (chineseLayout == ChineseLayout.T9
                && topUnits != null && !topUnits.isEmpty()) {
            tail = mergeUnitsWithRawSeparators(state.remaining(), topUnits);
        } else {
            tail = state.remaining();
        }
        String out = state.committedText() + tail;
        if (listener != null) listener.onCommitText(out);
        resetAll();
    }

    /**
     * Walk {@code units} (decoder-segmented pinyin, e.g. "shen'me" or
     * "xi'an") in lockstep with {@code raw} (the user's actual buffer
     * — digits + any user-typed separators). Letters from units are
     * always emitted, but a {@code '} from units is kept only when
     * the corresponding position in raw also has a {@code '}. Decoder-
     * inserted separators are dropped.
     */
    private static String mergeUnitsWithRawSeparators(String raw, String units) {
        StringBuilder sb = new StringBuilder(units.length());
        int bi = 0;
        for (int ui = 0; ui < units.length(); ui++) {
            char u = units.charAt(ui);
            if (u == '\'') {
                if (bi < raw.length() && raw.charAt(bi) == '\'') {
                    sb.append('\'');
                    bi++;
                }
            } else {
                sb.append(u);
                if (bi < raw.length() && raw.charAt(bi) != '\'') {
                    bi++;
                }
            }
        }
        return sb.toString();
    }

    // ===== Backspace =====

    private void handleBackspace() {
        if (mode == KeyboardMode.ENGLISH) {
            if (englishBuffer.length() > 0) {
                englishBuffer.deleteCharAt(englishBuffer.length() - 1);
                if (listener != null) listener.onSetComposingText(englishBuffer.toString());
                if (englishBuffer.length() > 0) {
                    refreshEnglishCompletions();
                } else {
                    candidates = Collections.emptyList();
                    publish();
                }
            } else {
                if (listener != null) listener.onDeleteBefore(1);
            }
            return;
        }
        if (state.predicting) {
            exitPrediction();
            publish();
            return;
        }
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
        if (mode == KeyboardMode.ENGLISH && englishBuffer.length() > 0) {
            commitEnglishBuffer();
        }
        if (mode == KeyboardMode.CHINESE && !state.buffer.isEmpty()
                && !candidates.isEmpty()) {
            // Commit first candidate before the punctuation so the
            // in-flight composition isn't lost.
            commitFirstCandidateInline();
        }
        exitPrediction();
        state.clearContext();
        if (listener != null) listener.onCommitText(text);
    }

    private void commitFirstCandidateInline() {
        DecodeResult c = candidates.get(0);
        state.select(c.text, c.units, c.consumed);
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
        if (state.predicting) {
            onPredictionPick(index);
            return;
        }
        DecodeResult c = candidates.get(index);
        if (inPunctuationPicker) {
            // The "1 key" picker commits the punctuation as raw text and
            // immediately collapses the bar. There's no buffer to consume.
            if (listener != null) listener.onCommitText(c.text);
            dismissPunctuationPicker(/*publish=*/true);
            return;
        }
        state.select(c.text, c.units, c.consumed);
        selectionTokenIds.add(c.tokenIds);
        if (state.fullySelected()) {
            String out = state.committedText();
            if (listener != null) listener.onCommitText(out);
            pushSelectionContext();
            resetInput();
            showPredictions(false);
        } else {
            redecodeAndPublish();
        }
    }

    /** Pick a prediction candidate (shown after full selection). */
    public void onPredictionPick(int index) {
        if (index < 0 || index >= candidates.size()) return;
        DecodeResult c = candidates.get(index);
        state.pushContext(c.tokenIds);
        if (listener != null) listener.onCommitText(c.text);
        showPredictions(false);
    }

    /** Pick an English completion candidate. */
    public void onEnglishCompletionPick(int index) {
        if (index < 0 || index >= candidates.size()) return;
        DecodeResult c = candidates.get(index);
        state.pushContext(c.tokenIds);
        englishBuffer.setLength(0);
        if (listener != null) {
            listener.onCommitText(c.text);
            listener.onSetComposingText("");
        }
        showPredictions(true);
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
            resetInput();
        }
        // Flush English buffer when leaving ENGLISH.
        if (mode == KeyboardMode.ENGLISH && englishBuffer.length() > 0) {
            commitEnglishBuffer();
        }
        exitPrediction();
        switchMode(target);
    }

    // ===== Decoding =====

    private void redecodeAndPublish() {
        decode();
        publish();
    }

    /**
     * Recompute candidates / alts / topUnits from a single synchronous
     * decoder call.
     *
     * <p>Model is dead-simple: the kernel slices the buffer into a
     * letter region and a digit region (skipping bytes already covered
     * by hanzi selections), passes them as {@code start} and
     * {@code digits} to the decoder, and shows whatever the decoder
     * returns. No filtering, no synthesis, no prefix stripping. The
     * only post-processing is a {@code (text, consumed)} dedup pass to
     * collapse the case where Layer 1 and a multi-char Layer 2 edge
     * happen to return the exact same hanzi for the exact same span.
     */
    private void decode() {
        // New input dismisses prediction mode
        exitPrediction();
        if (mode != KeyboardMode.CHINESE || state.buffer.isEmpty()) {
            candidates = Collections.emptyList();
            pinyinAlts = Collections.emptyList();
            topUnits = "";
            return;
        }

        // Buffer slicing — start AFTER any hanzi selections, since those
        // bytes are no longer "in flux".
        int sel = state.selectedLength();
        int len = state.buffer.length();
        int letterStart = Math.min(sel, state.lettersEnd);
        int letterEnd = state.lettersEnd;
        String bufferLetters = (letterEnd > letterStart)
                ? state.buffer.substring(letterStart, letterEnd)
                : "";
        int digitStart = Math.max(sel, state.lettersEnd);
        String digits = (digitStart < len)
                ? state.buffer.substring(digitStart, len)
                : "";

        // start IS bufferLetters. Selections.pinyin is NOT included —
        // those syllables are committed (gone from `state.remaining()`)
        // and the next decode is for what comes after them.
        String start = bufferLetters;

        DecodeResult[] raw;
        if (chineseLayout == ChineseLayout.T9 && !digits.isEmpty()) {
            raw = decoder.decodeNumSentence(start, digits, EXTRA_SENTENCES);
        } else if (!start.isEmpty()) {
            raw = decoder.decodeSentence(start, EXTRA_SENTENCES);
        } else {
            raw = new DecodeResult[0];
        }

        // Pass-through with dedup. consumed comes straight from the
        // decoder; since start IS bufferLetters, C++ start.size() ==
        // start.length() and the returned consumed is already in the
        // correct buffer-byte coordinate system.
        List<DecodeResult> out = new ArrayList<>(raw.length);
        java.util.HashSet<String> seenKeys = new java.util.HashSet<>();
        DecodeResult topCandidate = null;
        for (DecodeResult r : raw) {
            String text = r.text != null ? r.text : "";
            if (text.isEmpty()) continue;
            int consumed = r.consumed;
            if (consumed <= 0) continue;
            String key = text + "\u0001" + consumed;
            if (!seenKeys.add(key)) continue;
            DecodeResult c = new DecodeResult(text, r.units != null ? r.units : "", consumed, r.tokenIds);
            out.add(c);
            if (topCandidate == null) topCandidate = c;
        }
        candidates = out;

        // topUnits = the top candidate's pinyin, clipped against tail
        // expansion (the C++ may complete an incomplete initial like
        // "k" → "kan", which we trim back to the user's actual chars).
        String topU = topCandidate != null ? topCandidate.units : "";
        if (!topU.isEmpty()) {
            int rawLen = PinyinUtil.countRealChars(bufferLetters) + PinyinUtil.countRealChars(digits);
            topU = clipUnitsToLetterCount(topU, rawLen);
        }
        topUnits = topU;

        // Pinyin alts for the NEXT syllable position: take raw[]'s units,
        // skip the syllables already fixed by `start`, and the next
        // segment is a candidate next-syllable label. Bounded by the
        // remaining digit count so tail-expanded entries (which would
        // consume past the end of the buffer) are not offered.
        if (chineseLayout == ChineseLayout.T9 && !digits.isEmpty()) {
            pinyinAlts = computePinyinAltsFromRaw(
                    raw, countSyllables(start), digits.length());
        } else {
            pinyinAlts = Collections.emptyList();
        }
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
     * Extract pinyin alternatives for the next syllable position from a
     * {@code raw} array returned by {@code DecodeNumSentence}.
     *
     * <p>The decoder built its lattice with {@code start} syllables fixed
     * in columns {@code [0, p)}, so every result's {@code units} string
     * begins with the same {@code start.split("'")} sequence (the first
     * {@code startSyllables} segments). Stripping those by index gives
     * the next-position syllable directly — no value-level prefix match
     * is needed because the lattice already enforces it.
     *
     * <p>Each pinyin letter maps to exactly one T9 digit, so
     * {@code digitCount = nextSyllable.length()}. A syllable longer than
     * the available digits ({@code maxDigits}) is a tail-expanded
     * candidate (the lattice's {@code tail_expansion} pass invents
     * "what the user might be typing" syllables that span past the
     * existing buffer); the alt strip rejects these because picking
     * one would consume more digits than the buffer actually has.
     */
    private static List<PinyinAlt> computePinyinAltsFromRaw(
            DecodeResult[] raw, int startSyllables, int maxDigits) {
        if (raw.length == 0) return Collections.emptyList();
        java.util.LinkedHashMap<String, PinyinAlt> seen =
                new java.util.LinkedHashMap<>();
        for (DecodeResult r : raw) {
            if (r.units == null || r.units.isEmpty()) continue;
            String[] segs = r.units.split("'");
            if (segs.length <= startSyllables) continue;
            String nextSyl = segs[startSyllables];
            if (nextSyl.isEmpty()) continue;
            if (nextSyl.length() > maxDigits) continue;  // tail-expanded
            if (seen.containsKey(nextSyl)) continue;
            seen.put(nextSyl, new PinyinAlt(nextSyl, nextSyl, nextSyl.length()));
            if (seen.size() >= MAX_PINYIN_ALTS) break;
        }
        return new ArrayList<>(seen.values());
    }

    /** Number of non-empty pinyin syllables in a {@code '}-separated string. */
    private static int countSyllables(String s) {
        if (s == null || s.isEmpty()) return 0;
        int n = 0;
        for (String seg : s.split("'")) {
            if (!seg.isEmpty()) n++;
        }
        return n;
    }

    // ===== Prediction =====

    /**
     * Push all accumulated selection token IDs into the prediction context.
     */
    private void pushSelectionContext() {
        for (int[] ids : selectionTokenIds) {
            state.pushContext(ids);
        }
    }

    /**
     * Show prediction candidates (NextTokens) if context is available.
     *
     * @param enOnly true when in English mode
     */
    private void showPredictions(boolean enOnly) {
        if (state.contextIds.isEmpty()) {
            exitPrediction();
            publish();
            return;
        }
        DecodeResult[] results = decoder.nextTokens(
                state.contextIdsArray(), PREDICTION_LIMIT, enOnly);
        if (results.length == 0) {
            exitPrediction();
            publish();
            return;
        }
        state.predicting = true;
        candidates = new ArrayList<>(results.length);
        for (DecodeResult r : results) candidates.add(r);
        topUnits = "";
        pinyinAlts = Collections.emptyList();
        publish();
    }

    private void exitPrediction() {
        state.predicting = false;
        candidates = Collections.emptyList();
        topUnits = "";
    }

    /**
     * Reset input state but keep prediction context.
     */
    private void resetInput() {
        state.reset();
        selectionTokenIds.clear();
        pinyinAlts = Collections.emptyList();
        topUnits = "";
        inPunctuationPicker = false;
    }

    // ===== English completion =====

    private void refreshEnglishCompletions() {
        DecodeResult[] results = decoder.getTokens(
                englishBuffer.toString(), PREDICTION_LIMIT, true);
        candidates = new ArrayList<>(results.length);
        for (DecodeResult r : results) candidates.add(r);
        topUnits = "";
        pinyinAlts = Collections.emptyList();
        publish();
    }

    /**
     * Commit the English buffer as-is (user pressed space without
     * picking a completion).
     */
    private void commitEnglishBuffer() {
        String text = englishBuffer.toString();
        englishBuffer.setLength(0);
        if (listener != null) {
            listener.onCommitText(text);
            listener.onSetComposingText("");
        }
        candidates = Collections.emptyList();
        // No context push for raw text (no token IDs)
        publish();
    }

    // ===== Helpers =====

    private void resetAll() {
        state.reset();
        selectionTokenIds.clear();
        candidates = Collections.emptyList();
        pinyinAlts = Collections.emptyList();
        topUnits = "";
        inPunctuationPicker = false;
        englishBuffer.setLength(0);
        publish();
    }

    private void publish() {
        if (observer != null) observer.onStateChanged(state, candidates);
    }
}
