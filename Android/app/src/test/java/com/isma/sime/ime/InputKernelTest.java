package com.isma.sime.ime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.isma.sime.ime.engine.Candidate;
import com.isma.sime.ime.engine.DecodeResult;
import com.isma.sime.ime.engine.Decoder;
import com.isma.sime.ime.keyboard.SimeKey;

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

        @Override
        public DecodeResult[] decodeSentence(String pinyin, int limit) {
            DecodeResult[] r = sentenceMap.get(pinyin);
            return r != null ? r : new DecodeResult[0];
        }

        @Override
        public DecodeResult[] decodeNumSentence(String startLetters, String digits, int limit) {
            DecodeResult[] r = numSentenceMap.get(startLetters + "|" + digits);
            return r != null ? r : new DecodeResult[0];
        }

        void putSentence(String pinyin, DecodeResult... results) {
            sentenceMap.put(pinyin, results);
        }

        void putNumSentence(String startLetters, String digits, DecodeResult... results) {
            numSentenceMap.put(startLetters + "|" + digits, results);
        }
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

    private FakeDecoder decoder;
    private RecordingListener listener;
    private InputKernel kernel;

    @Before
    public void setUp() {
        decoder = new FakeDecoder();
        listener = new RecordingListener();
        kernel = new InputKernel(decoder);
        kernel.attach(listener, null);
    }

    private static DecodeResult r(String text, String units, int consumed) {
        return new DecodeResult(text, units, consumed);
    }

    // ---------- QWERTY letter typing ----------

    @Test
    public void qwertyLetterAppendsAndDecodes() {
        decoder.putSentence("n", r("你", "ni", 1));
        kernel.onKey(SimeKey.letter('n'));

        assertEquals("n", kernel.getState().buffer);
        assertEquals(1, kernel.getState().lettersEnd);
        assertEquals(1, kernel.getCandidates().size());
        assertEquals("你", kernel.getCandidates().get(0).text);
    }

    @Test
    public void qwertyFullPickCommitsAndResets() {
        decoder.putSentence("nihao", r("你好", "ni'hao", 5));
        for (char c : "nihao".toCharArray()) kernel.onKey(SimeKey.letter(c));

        kernel.onHanziCandidatePick(0);

        assertEquals(Arrays.asList("你好"), listener.committed);
        assertEquals("", kernel.getState().buffer);
        assertTrue(kernel.getState().selections.isEmpty());
    }

    @Test
    public void qwertyPartialPickKeepsRemaining() {
        decoder.putSentence("nihao", r("你", "ni", 2));
        decoder.putSentence("hao",   r("好", "hao", 3));

        for (char c : "nihao".toCharArray()) kernel.onKey(SimeKey.letter(c));
        kernel.onHanziCandidatePick(0);

        // Partial pick does not commit; it holds the selection and
        // updates state so remaining() points at the next segment.
        assertTrue("partial pick must not commit", listener.committed.isEmpty());
        assertEquals("你", kernel.getState().committedText());
        assertEquals("hao", kernel.getState().remaining());
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

        assertEquals("n", kernel.getState().buffer);
        assertEquals(1, kernel.getState().lettersEnd);
    }

    @Test
    public void backspaceUndoesHanziPick() {
        decoder.putSentence("nihao", r("你", "ni", 2));
        decoder.putSentence("hao",   r("好", "hao", 3));
        for (char c : "nihao".toCharArray()) kernel.onKey(SimeKey.letter(c));
        kernel.onHanziCandidatePick(0);
        assertEquals("你", kernel.getState().committedText());

        kernel.onKey(SimeKey.backspace());

        assertEquals("", kernel.getState().committedText());
        assertEquals("nihao", kernel.getState().remaining());
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
        assertTrue(kernel.getState().buffer.isEmpty());
    }

    // ---------- English mode ----------

    @Test
    public void englishLetterCommitsDirect() {
        kernel.switchMode(KeyboardMode.ENGLISH);
        kernel.onKey(SimeKey.letter('a'));
        assertEquals(Collections.singletonList("a"), listener.committed);
        assertTrue(kernel.getState().buffer.isEmpty());
    }

    @Test
    public void toggleLangFlushesInFlight() {
        decoder.putSentence("ni", r("你", "ni", 2));
        kernel.onKey(SimeKey.letter('n'));
        kernel.onKey(SimeKey.letter('i'));

        kernel.onKey(SimeKey.toggleLang());

        assertEquals(Arrays.asList("ni"), listener.committed);
        assertEquals(KeyboardMode.ENGLISH, kernel.getMode());
        assertTrue(kernel.getState().buffer.isEmpty());
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

        assertEquals("6426", kernel.getState().buffer);
        assertEquals(0, kernel.getState().lettersEnd);
        assertEquals(1, kernel.getCandidates().size());
        assertEquals("你好", kernel.getCandidates().get(0).text);
    }

    @Test
    public void t9PinyinPickReplacesDigits() {
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "6426", r("你好", "ni'hao", 4));
        decoder.putNumSentence("ni", "26", r("你饿", "ni'e", 2));
        for (char c : "6426".toCharArray()) kernel.onKey(SimeKey.digit(c));

        kernel.onPinyinCandidatePick("64", "ni", false);

        assertEquals("ni26", kernel.getState().buffer);
        assertEquals(2, kernel.getState().lettersEnd);
        assertEquals(1, kernel.getState().undoStack.size());
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
        kernel.onHanziCandidatePick(0);

        assertEquals("hanzi pick should commit and clear",
                java.util.Collections.singletonList("你饿"), listener.committed);
        assertEquals("buffer should be empty after full pick",
                "", kernel.getState().buffer);
        assertTrue("selections should be empty after commit",
                kernel.getState().selections.isEmpty());
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

        kernel.onHanziCandidatePick(0);
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
        String remaining = kernel.getState().remaining();
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
        assertEquals("789", kernel.getState().buffer);
        assertEquals("lettersEnd reset since the consumed pick crossed it",
                0, kernel.getState().lettersEnd);
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

        kernel.onHanziCandidatePick(0);
        // selections=[{你饿,4}], selectedLength=4
        // lettersEnd should be bumped to 4 by the new invariant
        assertEquals(4, kernel.getState().lettersEnd);
        assertEquals(4, kernel.getState().selectedLength());

        // Undo the hanzi pick
        kernel.onKey(SimeKey.backspace());
        // Should revert to: selections empty, lettersEnd back to 2
        assertTrue(kernel.getState().selections.isEmpty());
        assertEquals("lettersEnd should be restored to 2",
                2, kernel.getState().lettersEnd);
        assertEquals("buffer unchanged", "ni267", kernel.getState().buffer);
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
                "6426", kernel.getState().buffer);
        assertEquals(0, kernel.getState().lettersEnd);
        assertTrue("undoStack should be empty after revert",
                kernel.getState().undoStack.isEmpty());
    }

    @Test
    public void t9PinyinPickCanBeUndoneByBackspace() {
        kernel.setChineseLayout(ChineseLayout.T9);
        decoder.putNumSentence("", "6426", r("你好", "ni'hao", 4));
        decoder.putNumSentence("ni", "26", r("你饿", "ni'e", 2));
        for (char c : "6426".toCharArray()) kernel.onKey(SimeKey.digit(c));
        kernel.onPinyinCandidatePick("64", "ni", false);

        kernel.onKey(SimeKey.backspace());

        assertEquals("6426", kernel.getState().buffer);
        assertEquals(0, kernel.getState().lettersEnd);
        assertTrue(kernel.getState().undoStack.isEmpty());
    }

    // ---------- Separator ----------

    @Test
    public void separatorOnEmptyBufferNoop() {
        kernel.onKey(SimeKey.separator());
        assertTrue(kernel.getState().buffer.isEmpty());
        assertTrue(listener.committed.isEmpty());
    }

    @Test
    public void separatorInsertsIntoBuffer() {
        decoder.putSentence("ni", r("你", "ni", 2));
        decoder.putSentence("ni'", r("你", "ni", 3));
        kernel.onKey(SimeKey.letter('n'));
        kernel.onKey(SimeKey.letter('i'));
        kernel.onKey(SimeKey.separator());
        assertEquals("ni'", kernel.getState().buffer);
    }

    // ---------- Mode switching ----------

    @Test
    public void toNumberAndBackRestores() {
        kernel.onKey(SimeKey.toNumber());
        assertEquals(KeyboardMode.NUMBER, kernel.getMode());
        kernel.onKey(SimeKey.toBack());
        assertEquals(KeyboardMode.CHINESE, kernel.getMode());
    }

    @Test
    public void clearResetsEverything() {
        decoder.putSentence("ni", r("你", "ni", 2));
        kernel.onKey(SimeKey.letter('n'));
        kernel.onKey(SimeKey.letter('i'));
        kernel.onKey(SimeKey.clear());
        assertTrue(kernel.getState().buffer.isEmpty());
        assertTrue(kernel.getCandidates().isEmpty());
    }

    @Test
    public void partialHanziPickDoesNotCommit() {
        // Guard the assumption used in qwertyPartialPickKeepsRemaining.
        decoder.putSentence("nihao", r("你", "ni", 2));
        decoder.putSentence("hao",   r("好", "hao", 3));
        for (char c : "nihao".toCharArray()) kernel.onKey(SimeKey.letter(c));
        kernel.onHanziCandidatePick(0);
        assertFalse("partial pick must not commit",
                listener.committed.contains("你"));
    }
}
