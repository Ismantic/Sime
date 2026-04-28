package com.shiyu.sime.ime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.shiyu.sime.ime.engine.DecodeResult;
import com.shiyu.sime.ime.engine.Decoder;
import com.shiyu.sime.ime.keyboard.SimeKey;

import org.junit.Before;
import org.junit.Test;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Unit tests for {@link InputKernel}. Uses a table-driven fake
 * {@link Decoder} so we don't depend on the native engine.
 */
public class InputKernelTest {

    /** Simple programmable decoder: exact string → canned results. */
    private static final class FakeDecoder implements Decoder {
        final Map<String, DecodeResult[]> sentenceMap = new HashMap<>();
        final Map<String, DecodeResult[]> numSentenceMap = new HashMap<>();
        final Map<String, String[]> t9PinyinMap = new HashMap<>();
        int sentenceCalls = 0;
        int numSentenceCalls = 0;
        int t9PinyinCalls = 0;

        @Override
        public DecodeResult[] decodeSentence(String pinyin, int limit) {
            sentenceCalls++;
            DecodeResult[] r = sentenceMap.get(pinyin);
            return r != null ? r : new DecodeResult[0];
        }

        @Override
        public DecodeResult[] decodeNumSentence(String startLetters, String digits, int limit) {
            numSentenceCalls++;
            DecodeResult[] r = numSentenceMap.get(startLetters + "|" + digits);
            return r != null ? r : new DecodeResult[0];
        }

        void putSentence(String pinyin, DecodeResult... results) {
            sentenceMap.put(pinyin, results);
        }

        void putNumSentence(String startLetters, String digits, DecodeResult... results) {
            numSentenceMap.put(startLetters + "|" + digits, results);
        }

        void putT9PinyinSyllables(String digits, String... syllables) {
            t9PinyinMap.put(digits, syllables);
        }

        @Override
        public DecodeResult[] nextTokens(int[] contextIds, int limit, boolean enOnly) {
            return new DecodeResult[0];
        }

        @Override
        public DecodeResult[] getTokens(String prefix, int limit, boolean enOnly) {
            return new DecodeResult[0];
        }

        @Override
        public String[] t9PinyinSyllables(String digits, int limit) {
            t9PinyinCalls++;
            String[] r = t9PinyinMap.get(digits);
            return r != null ? r : new String[0];
        }

        @Override
        public int contextSize() { return 2; }
    }

    private static final class RecordingListener implements InputKernel.Listener {
        final List<String> committed = new ArrayList<>();
        int deleted = 0;
        int enters = 0;

        @Override public void onCommitText(String text)      { committed.add(text); }
        @Override public void onDeleteBefore(int count)      { deleted += count; }
        @Override public void onSendEnter()                  { enters += 1; }
        @Override public void onSetComposingText(String p)   { /* unused */ }
    }

    private static final class RecordingObserver implements InputKernel.StateObserver {
        InputKernel.Snapshot last;

        @Override
        public void onStateChanged(InputKernel.Snapshot snapshot) {
            last = snapshot;
        }
    }

    private FakeDecoder decoder;
    private RecordingListener listener;
    private RecordingObserver observer;
    private InputKernel kernel;

    @Before
    public void setUp() {
        decoder = new FakeDecoder();
        listener = new RecordingListener();
        observer = new RecordingObserver();
        kernel = new InputKernel(decoder, true);
        kernel.attach(listener, observer);
    }

    private static DecodeResult r(String text, String units, int consumed) {
        return new DecodeResult(text, units, consumed);
    }

    private InputKernel.Snapshot snap() {
        return observer.last != null
                ? observer.last
                : new InputKernel.Snapshot(
                        new InputState(),
                        Collections.emptyList(),
                        Collections.emptyList(),
                        "",
                        KeyboardMode.CHINESE,
                        ChineseLayout.QWERTY,
                        "");
    }

    private InputState state() {
        return snap().state;
    }

    private List<DecodeResult> candidates() {
        return snap().candidates;
    }

    private List<InputKernel.PinyinAlt> pinyinAlts() {
        return snap().pinyinAlts;
    }

    private KeyboardMode mode() {
        return snap().mode;
    }

    // ---------- QWERTY letter typing ----------

    @Test
    public void qwertyLetterAppendsAndDecodes() {
        decoder.putSentence("n", r("你", "ni", 1));
        kernel.onKey(SimeKey.letter('n'));

        assertEquals("n", state().buffer);
        assertEquals(1, state().lettersEnd);
        assertEquals(1, candidates().size());
        assertEquals("你", candidates().get(0).text);
    }

    @Test
    public void qwertyFullPickCommitsAndResets() {
        decoder.putSentence("nihao", r("你好", "ni'hao", 5));
        for (char c : "nihao".toCharArray()) kernel.onKey(SimeKey.letter(c));

        kernel.onCandidatePick(0);

        assertEquals(Arrays.asList("你好"), listener.committed);
        assertEquals("", state().buffer);
        assertTrue(state().selections.isEmpty());
    }

    @Test
    public void qwertyPartialPickKeepsRemaining() {
        decoder.putSentence("nihao", r("你", "ni", 2));
        decoder.putSentence("hao",   r("好", "hao", 3));

        for (char c : "nihao".toCharArray()) kernel.onKey(SimeKey.letter(c));
        kernel.onCandidatePick(0);

        // Partial pick does not commit; it holds the selection and
        // updates state so remaining() points at the next segment.
        assertTrue("partial pick must not commit", listener.committed.isEmpty());
        assertEquals("你", state().committedText());
        assertEquals("hao", state().remaining());
    }

    // ---------- Backspace ----------

    @Test
    public void backspaceOnEmptyBufferPassesThrough() {
        kernel.onKey(SimeKey.backspace());
        assertEquals(1, listener.deleted);
    }

    @Test
    public void backspaceDeletesLastLetter() {
        decoder.putSentence("ni", r("你", "ni", 2));
        decoder.putSentence("n",  r("你", "ni", 1));
        kernel.onKey(SimeKey.letter('n'));
        kernel.onKey(SimeKey.letter('i'));

        kernel.onKey(SimeKey.backspace());

        assertEquals("n", state().buffer);
        assertEquals(1, state().lettersEnd);
    }

    @Test
    public void backspaceUndoesHanziPick() {
        decoder.putSentence("nihao", r("你", "ni", 2));
        decoder.putSentence("hao",   r("好", "hao", 3));
        for (char c : "nihao".toCharArray()) kernel.onKey(SimeKey.letter(c));
        kernel.onCandidatePick(0);
        assertEquals("你", state().committedText());

        kernel.onKey(SimeKey.backspace());

        assertEquals("", state().committedText());
        assertEquals("nihao", state().remaining());
    }

    // ---------- Space / Enter ----------

    @Test
    public void spaceOnEmptyBufferCommitsSpace() {
        kernel.onKey(SimeKey.space());
        assertEquals(Collections.singletonList(" "), listener.committed);
    }

    @Test
    public void spaceOnActiveBufferPicksFirst() {
        decoder.putSentence("nihao", r("你好", "ni'hao", 5));
        for (char c : "nihao".toCharArray()) kernel.onKey(SimeKey.letter(c));
        kernel.onKey(SimeKey.space());
        assertEquals(Arrays.asList("你好"), listener.committed);
    }

    @Test
    public void enterOnEmptyBufferSendsEnter() {
        kernel.onKey(SimeKey.enter());
        assertEquals(1, listener.enters);
        assertTrue(listener.committed.isEmpty());
    }

    @Test
    public void enterOnActiveBufferCommitsRaw() {
        kernel.onKey(SimeKey.letter('n'));
        kernel.onKey(SimeKey.letter('i'));
        kernel.onKey(SimeKey.enter());
        assertEquals(Arrays.asList("ni"), listener.committed);
        assertTrue(state().buffer.isEmpty());
    }

    // ---------- English mode ----------

    @Test
    public void englishLetterCommitsDirect() {
        kernel.switchMode(KeyboardMode.ENGLISH);
        kernel.onKey(SimeKey.letter('a'));
        assertTrue(listener.committed.isEmpty());
        assertEquals("a", snap().englishBuffer);
        assertTrue(state().buffer.isEmpty());
    }

    @Test
    public void toggleLangFlushesInFlight() {
        decoder.putSentence("ni", r("你", "ni", 2));
        kernel.onKey(SimeKey.letter('n'));
        kernel.onKey(SimeKey.letter('i'));

        kernel.onKey(SimeKey.toggleLang());

        assertEquals(Arrays.asList("你"), listener.committed);
        assertEquals(KeyboardMode.ENGLISH, mode());
        assertTrue(state().buffer.isEmpty());
    }

    // ---------- T9 ----------

    @Test
    public void t9DigitsInvokeDecodeNumSentence() {
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "6426", r("你好", "ni'hao", 4));

        kernel.onKey(SimeKey.digit('6'));
        kernel.onKey(SimeKey.digit('4'));
        kernel.onKey(SimeKey.digit('2'));
        kernel.onKey(SimeKey.digit('6'));

        assertEquals("6426", state().buffer);
        assertEquals(0, state().lettersEnd);
        assertEquals(1, candidates().size());
        assertEquals("你好", candidates().get(0).text);
    }

    @Test
    public void t9PinyinPickReplacesDigits() {
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "6426", r("你好", "ni'hao", 4));
        decoder.putNumSentence("ni", "26", r("你饿", "ni'e", 2));
        for (char c : "6426".toCharArray()) kernel.onKey(SimeKey.digit(c));

        kernel.onPinyinCandidatePick("64", "ni", false);

        assertEquals("ni26", state().buffer);
        assertEquals(2, state().lettersEnd);
        assertTrue(state().selections.isEmpty());
    }

    @Test
    public void t9PickHanziAfterPinyinPickFullyCommits() {
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "6426", r("你好", "ni'hao", 4));
        // After pinyin pick "ni" for digits "64", remaining digits are "26".
        // The engine sees decodeNumSentence("ni", "26", ...) and returns "你饿" with
        // cnt = letters.size() + digits.size() = 2 + 2 = 4 (total bytes).
        decoder.putNumSentence("ni", "26", r("你饿", "ni'e", 4));
        for (char c : "6426".toCharArray()) kernel.onKey(SimeKey.digit(c));
        kernel.onPinyinCandidatePick("64", "ni", false);
        // buffer = "ni26", lettersEnd = 2

        // Now pick the hanzi candidate. It should consume the WHOLE buffer
        // ("ni" + "26" = 4 bytes) and commit the text.
        kernel.onCandidatePick(0);

        assertEquals("hanzi pick should commit and clear",
                java.util.Collections.singletonList("你饿"), listener.committed);
        assertEquals("buffer should be empty after full pick",
                "", state().buffer);
        assertTrue("selections should be empty after commit",
                state().selections.isEmpty());
    }

    @Test
    public void t9PinyinPickAfterHanziPickThatCrossedLettersEnd() {
        // Scenario: pinyin pick → partial hanzi pick that consumes more than
        // just the letters → type more digits → pinyin pick again. The second
        // pinyin pick must write to the actual digit region, not the stale
        // bytes still under selectedLength.
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "64267", r("nihao", "ni'hao", 5));
        decoder.putNumSentence("ni", "267", r("你饿", "ni'e", 4));     // covers "ni"+"26"
        // After picking "你饿", remaining digits are "7", then user types "8".
        // We never decode that exact string in this test — only the pinyin
        // pick path matters.

        for (char c : "64267".toCharArray()) kernel.onKey(SimeKey.digit(c));
        kernel.onPinyinCandidatePick("64", "ni", false);
        // buffer = "ni267", lettersEnd = 2

        kernel.onCandidatePick(0);
        // selections = [{你饿, ni'e, 4}], selectedLength = 4
        // buffer = "ni267", lettersEnd = 2 (NOT updated)
        // remaining = "7"

        kernel.onKey(SimeKey.digit('8'));
        // buffer = "ni2678", selectedLength = 4, lettersEnd = 2
        // Actual digit region starts at selectedLength = 4 = "78"

        // Now pinyin pick "ba" for "78".
        kernel.onPinyinCandidatePick("78", "ba", false);

        // Expected: buffer should become "ni26ba" (positions 4-5 replaced),
        // OR semantically: remaining should now reflect the "ba" letters.
        String remaining = state().remaining();
        assertEquals("remaining must contain the picked letters 'ba'",
                "ba", remaining);
    }

    @Test
    public void punctuationFlushesPartialT9PickWithLettersTail() {
        // T9: type digits, pinyin pick, type more digits, then partial hanzi
        // pick that doesn't fully consume — punctuation should flush the
        // committed hanzi and reset state with the leftover digits.
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("ni", "26789", r("你饿", "ni'e", 4));  // covers ni+26
        decoder.putNumSentence("", "789", r("七八九", "qi'ba'jiu", 3));
        for (char c : "64".toCharArray()) kernel.onKey(SimeKey.digit(c));
        kernel.onPinyinCandidatePick("64", "ni", false);
        for (char c : "26789".toCharArray()) kernel.onKey(SimeKey.digit(c));
        // buffer = "ni26789", lettersEnd = 2

        // Now type a punctuation. handlePunctuation should commit first
        // candidate (你饿, consumed=4) inline and then output the punc.
        kernel.onKey(SimeKey.punctuation("，"));

        // Expected: commit "你饿" then ", " — but state should be reset
        // around the leftover "789".
        assertTrue("should have committed 你饿 + punctuation",
                listener.committed.size() >= 1);
        // After commitFirstCandidateInline, state.buffer should be the
        // leftover digits "789" (= remaining after consuming 4 bytes from
        // "ni26789")
        assertEquals("789", state().buffer);
        assertEquals("lettersEnd reset since the consumed pick crossed it",
                0, state().lettersEnd);
    }

    @Test
    public void t9UndoHanziPickRestoresLettersEnd() {
        // After a hanzi pick that crossed lettersEnd, backspace should
        // restore the original lettersEnd from the action.
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("ni", "267", r("你饿", "ni'e", 4)); // covers ni+26
        for (char c : "64267".toCharArray()) kernel.onKey(SimeKey.digit(c));
        kernel.onPinyinCandidatePick("64", "ni", false);
        // buffer = "ni267", lettersEnd = 2

        kernel.onCandidatePick(0);
        // selections=[{你饿,4}], selectedLength=4
        // lettersEnd should be bumped to 4 by the new invariant
        assertEquals(4, state().lettersEnd);
        assertEquals(4, state().selectedLength());

        // Undo the hanzi pick
        kernel.onKey(SimeKey.backspace());
        // Should revert to: selections empty, lettersEnd back to 2
        assertTrue(state().selections.isEmpty());
        assertEquals("lettersEnd should be restored to 2",
                2, state().lettersEnd);
        assertEquals("buffer unchanged", "ni267", state().buffer);
    }

    @Test
    public void t9BackspaceUndoesPinyinPickEvenAfterMoreDigits() {
        // Documented behavior: backspace always prefers action-level undo.
        // Even if the user typed more digits after a pinyin pick, the next
        // backspace reverts the pinyin pick (not the just-typed digit).
        kernel.setChineseLayout(ChineseLayout.T9);
        for (char c : "64".toCharArray()) kernel.onKey(SimeKey.digit(c));
        kernel.onPinyinCandidatePick("64", "ni", false);
        for (char c : "26".toCharArray()) kernel.onKey(SimeKey.digit(c));
        // buffer = "ni26", lettersEnd = 2, undoStack has PINYIN_PICK

        kernel.onKey(SimeKey.backspace());
        assertEquals("backspace reverts the pinyin pick",
                "6426", state().buffer);
        assertEquals(0, state().lettersEnd);
    }

    @Test
    public void t9PinyinPickCanBeUndoneByBackspace() {
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "6426", r("你好", "ni'hao", 4));
        decoder.putNumSentence("ni", "26", r("你饿", "ni'e", 2));
        for (char c : "6426".toCharArray()) kernel.onKey(SimeKey.digit(c));
        kernel.onPinyinCandidatePick("64", "ni", false);

        kernel.onKey(SimeKey.backspace());

        assertEquals("6426", state().buffer);
        assertEquals(0, state().lettersEnd);
    }

    // ---------- Separator ----------

    @Test
    public void separatorOnEmptyBufferNoop() {
        kernel.onKey(SimeKey.separator());
        assertTrue(state().buffer.isEmpty());
        assertTrue(listener.committed.isEmpty());
    }

    @Test
    public void separatorInsertsIntoBuffer() {
        decoder.putSentence("ni", r("你", "ni", 2));
        decoder.putSentence("ni'", r("你", "ni", 3));
        kernel.onKey(SimeKey.letter('n'));
        kernel.onKey(SimeKey.letter('i'));
        kernel.onKey(SimeKey.separator());
        assertEquals("ni'", state().buffer);
    }

    // ---------- Mode switching ----------

    @Test
    public void toNumberAndBackRestores() {
        kernel.onKey(SimeKey.toNumber());
        assertEquals(KeyboardMode.NUMBER, mode());
        kernel.onKey(SimeKey.toBack());
        assertEquals(KeyboardMode.CHINESE, mode());
    }

    @Test
    public void clearResetsEverything() {
        decoder.putSentence("ni", r("你", "ni", 2));
        kernel.onKey(SimeKey.letter('n'));
        kernel.onKey(SimeKey.letter('i'));
        kernel.onKey(SimeKey.clear());
        assertTrue(state().buffer.isEmpty());
        assertTrue(candidates().isEmpty());
    }

    @Test
    public void partialHanziPickDoesNotCommit() {
        // Guard the assumption used in qwertyPartialPickKeepsRemaining.
        decoder.putSentence("nihao", r("你", "ni", 2));
        decoder.putSentence("hao",   r("好", "hao", 3));
        for (char c : "nihao".toCharArray()) kernel.onKey(SimeKey.letter(c));
        kernel.onCandidatePick(0);
        assertFalse("partial pick must not commit",
                listener.committed.contains("你"));
    }

    // ---------- Pinyin alts (extracted from main DecodeNumSentence result) ----------

    @Test
    public void pinyinAltsExtractedFromMainDecodeWithoutLetterPrefix() {
        // Case 1: start="" (typical T9 start). For each raw entry, split
        // units by `'` and take element [0] (= first syllable) as the
        // next-position alt label. Order follows raw[] insertion order
        // with first-occurrence dedup.
        kernel.setChineseLayout(ChineseLayout.T9);
        // raw fixture (in order):
        //   你好 / ni'hao / 4   → first syllable "ni"
        //   米   / mi     / 2   → first syllable "mi"
        //   你   / ni     / 2   → "ni" already seen, dedup
        //   摸   / mo     / 2   → "mo"
        //   能   / n      / 1   → "n"
        decoder.putNumSentence("", "6426",
                r("你好", "ni'hao", 4),
                r("米",   "mi",     2),
                r("你",   "ni",     2),
                r("摸",   "mo",     2),
                r("能",   "n",      1));

        for (char c : "6426".toCharArray()) kernel.onKey(SimeKey.digit(c));

        // Sanity: candidates contain ALL raw entries (no kernel-side
        // filtering). 你好 is deduped against the implicit Layer 1 entry
        // only when shared dedup applies — here all 5 are distinct.
        assertEquals(5, candidates().size());

        List<InputKernel.PinyinAlt> alts = pinyinAlts();
        assertEquals(6, alts.size());
        assertEquals("ni", alts.get(0).letters);
        assertEquals(2,    alts.get(0).digitCount);
        assertEquals("mi", alts.get(1).letters);
        assertEquals("mo", alts.get(2).letters);
        assertEquals("n",  alts.get(3).letters);
        assertEquals(1,    alts.get(3).digitCount);
        assertEquals("m",  alts.get(4).letters);
        assertEquals("o",  alts.get(5).letters);

        // CRITICAL: still only one hanzi decoder call per keystroke; the
        // pinyin strip may use a separate lightweight syllable lookup.
        assertEquals("must not issue a second alt-only decode",
                4, decoder.numSentenceCalls);
    }

    @Test
    public void pinyinAltsExtractedFromMainDecodeWithLetterPrefix() {
        // Case 2: start="ni" (after a pinyin pick). startSyllables=1, so
        // strip 1 leading segment from each raw entry's units; the next
        // segment (index [1]) is the alt label. Layer 2 entries from the
        // C++ side now begin with "ni'" because they walk net[0].es and
        // the first column is fixed to "ni".
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("ni", "26",
                // Layer 1 sentence: split → ["ni", "e"] → next = "e"
                r("你饿", "ni'e",  4),
                // Layer 2 single token covering only "ni": split → ["ni"],
                // length 1 ≤ 1, no next syllable, skip.
                r("你",   "ni",    2),
                // Layer 2 multi-char compound: split → ["ni", "en"] → "en"
                r("你恩", "ni'en", 4));

        for (char c : "64".toCharArray()) kernel.onKey(SimeKey.digit(c));
        kernel.onPinyinCandidatePick("64", "ni", false);
        // buffer="ni", lettersEnd=2
        for (char c : "26".toCharArray()) kernel.onKey(SimeKey.digit(c));
        // buffer="ni26", lettersEnd=2 → triggers ("ni", "26") decode

        List<InputKernel.PinyinAlt> alts = pinyinAlts();
        assertEquals(5, alts.size());
        assertEquals("e",  alts.get(0).letters);
        assertEquals(1,    alts.get(0).digitCount);
        assertEquals("en", alts.get(1).letters);
        assertEquals(2,    alts.get(1).digitCount);
        assertEquals("a",  alts.get(2).letters);
        assertEquals("b",  alts.get(3).letters);
        assertEquals("c",  alts.get(4).letters);
    }

    @Test
    public void pinyinAltsFilledFromNativeSyllablesWhenRawIsSparse() {
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "466453",
                r("攻克", "gong'ke", 6));
        decoder.putT9PinyinSyllables("466453",
                "gong", "hong", "go", "in");

        for (char c : "466453".toCharArray()) kernel.onKey(SimeKey.digit(c));

        List<InputKernel.PinyinAlt> alts = pinyinAlts();
        assertEquals("gong", alts.get(0).letters);
        assertEquals("hong", alts.get(1).letters);
        assertEquals("go",   alts.get(2).letters);
        assertEquals("in",   alts.get(3).letters);
        assertEquals(4, alts.get(1).digitCount);
        assertEquals(2, alts.get(2).digitCount);
        assertTrue("native pinyin lookup should be used",
                decoder.t9PinyinCalls > 0);
    }

    @Test
    public void consecutivePinyinAltPicksDecodeWithJoinedStart() {
        // The user's reported scenario: T9 input, pick "hao" then "de"
        // from the left strip (no hanzi pick yet). The kernel must call
        // decodeNumSentence("hao'de", "985426", ...) so the LM has the
        // full context, not "haode" (which loses the syllable boundary).
        kernel.setChineseLayout(ChineseLayout.T9);
        // Fixture for the post-pick decode call. If start gets passed
        // wrong (e.g. "haode" instead of "hao'de"), this lookup misses
        // and the test sees an empty candidate list.
        decoder.putNumSentence("hao'de", "985426",
                r("好的组件", "hao'de'zu'jian", 12),
                r("组件", "zu'jian", 12),
                r("组",   "zu",      9));

        for (char c : "42633985426".toCharArray()) kernel.onKey(SimeKey.digit(c));
        kernel.onPinyinCandidatePick("426", "hao", false);
        kernel.onPinyinCandidatePick("33",  "de",  false);

        // Buffer should have the separator: "hao'de985426" (12 bytes).
        assertEquals("hao'de985426", state().buffer);
        assertEquals(6, state().lettersEnd);

        // Candidates are decoder.decodeNumSentence("hao'de", "985426", ...)
        // results, pass-through (with consumed normalized).
        java.util.List<DecodeResult> after = candidates();
        assertTrue("must produce candidates from the fixture",
                after.size() >= 1);
        assertEquals("好的组件", after.get(0).text);
        assertEquals(12, after.get(0).consumed);
    }

    @Test
    public void layer1AndLayer2DupesAreDeduped() {
        // Even without a prior selection, Layer 1 top sentence and a
        // Layer 2 multi-char word edge can return the exact same text +
        // consumed. Dedup should keep only the first.
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "98",
                r("组件", "zu'jian", 2),  // Layer 1
                r("组件", "zu'jian", 2),  // Layer 2 dup
                r("组",   "zu",      1)); // Layer 2 single
        kernel.onKey(SimeKey.digit('9'));
        kernel.onKey(SimeKey.digit('8'));
        java.util.List<DecodeResult> after = candidates();
        assertEquals(2, after.size());
        assertEquals("组件", after.get(0).text);
        assertEquals("组",   after.get(1).text);
    }

    @Test
    public void pinyinAltsRejectTailExpansionBeyondBuffer() {
        // Tail-expanded entries (a syllable longer than the available
        // remaining digits) cannot be picked because applyLetterPick
        // would need more digit chars than the buffer has. The kernel
        // filters them out of the alt strip even though they remain in
        // the raw[] (and thus in the candidate bar).
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "5",
                r("看", "kan", 1),   // 3 letters > 1 digit → reject
                r("拉", "la",  1));  // 2 letters > 1 digit → reject
        kernel.onKey(SimeKey.digit('5'));
        assertEquals(3, pinyinAlts().size());
        assertEquals("j", pinyinAlts().get(0).letters);
        assertEquals("k", pinyinAlts().get(1).letters);
        assertEquals("l", pinyinAlts().get(2).letters);
        // Both still appear in the candidate bar (no kernel-side filter
        // there).
        assertEquals(2, candidates().size());
    }

}
