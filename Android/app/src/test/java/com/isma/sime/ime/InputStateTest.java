package com.isma.sime.ime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * Unit tests for {@link InputState}.
 *
 * These mirror the expected semantics described in
 * private/SimeAndroidRefactor.md section 3 and the Linux SimeState it
 * aligns with ({@code Linux/fcitx5/src/sime-state.h}).
 */
public class InputStateTest {

    // ---------- basic selection / remaining / committedText ----------

    @Test
    public void emptyState() {
        InputState s = new InputState();
        assertEquals("", s.buffer);
        assertEquals(0, s.selectedLength());
        assertEquals("", s.committedText());
        assertEquals("", s.remaining());
        assertFalse(s.fullySelected() && !s.buffer.isEmpty()); // vacuously true for empty
    }

    @Test
    public void selectThenRemaining() {
        InputState s = new InputState();
        s.buffer = "nihao";
        s.select("你", "ni", 2);
        assertEquals(2, s.selectedLength());
        assertEquals("你", s.committedText());
        assertEquals("hao", s.remaining());
        assertFalse(s.fullySelected());
    }

    @Test
    public void selectAllThenFullySelected() {
        InputState s = new InputState();
        s.buffer = "nihao";
        s.select("你好", "ni'hao", 5);
        assertTrue(s.fullySelected());
        assertEquals("你好", s.committedText());
        assertEquals("", s.remaining());
    }

    @Test
    public void multipleSelectionsAccumulate() {
        InputState s = new InputState();
        s.buffer = "nihaobuhao";
        s.select("你", "ni", 2);
        s.select("好不好", "hao'bu'hao", 8);
        assertEquals(10, s.selectedLength());
        assertEquals("你好不好", s.committedText());
        assertEquals("", s.remaining());
        assertTrue(s.fullySelected());
    }

    // ---------- cancel (undo stack) ----------

    @Test
    public void cancelEmptyStateReturnsFalse() {
        InputState s = new InputState();
        assertFalse(s.cancel());
    }

    @Test
    public void cancelUndoesHanziPick() {
        InputState s = new InputState();
        s.buffer = "nihao";
        s.select("你", "ni", 2);
        assertEquals("你", s.committedText());

        assertTrue(s.cancel());
        assertEquals("", s.committedText());
        assertEquals("nihao", s.remaining());
        assertEquals(0, s.selections.size());
    }

    @Test
    public void cancelStopsAtEmpty() {
        InputState s = new InputState();
        s.buffer = "nihao";
        s.select("你", "ni", 2);
        assertTrue(s.cancel());
        assertFalse(s.cancel()); // now empty
    }

    // ---------- pinyin / fallback letter pick ----------

    @Test
    public void pinyinPickReplacesDigitsWithLetters() {
        // T9: buffer starts as all digits, e.g. user typed "6426" (ni hao)
        InputState s = new InputState();
        s.buffer = "6426";
        s.lettersEnd = 0;

        // User picks "ni" for the first two digits
        s.applyLetterPick("64", "ni", /*fallback=*/false);

        assertEquals("ni26", s.buffer);
        assertEquals(2, s.lettersEnd);
        assertEquals(1, s.undoStack.size());
    }

    @Test
    public void pinyinPickCanBeUndone() {
        InputState s = new InputState();
        s.buffer = "6426";
        s.lettersEnd = 0;
        s.applyLetterPick("64", "ni", false);

        assertTrue(s.cancel());
        assertEquals("6426", s.buffer);
        assertEquals(0, s.lettersEnd);
        assertTrue(s.undoStack.isEmpty());
    }

    @Test
    public void fallbackPickIsSameRevertAsPinyinPick() {
        InputState s = new InputState();
        s.buffer = "7426";
        s.lettersEnd = 0;
        s.applyLetterPick("7", "q", /*fallback=*/true);

        assertEquals("q426", s.buffer);
        assertEquals(1, s.lettersEnd);

        assertTrue(s.cancel());
        assertEquals("7426", s.buffer);
        assertEquals(0, s.lettersEnd);
    }

    @Test
    public void chainedPicksUndoLifo() {
        InputState s = new InputState();
        s.buffer = "76426";
        s.lettersEnd = 0;

        // First pick fallback 'q' for '7'
        s.applyLetterPick("7", "q", true);
        assertEquals("q6426", s.buffer);
        assertEquals(1, s.lettersEnd);

        // Then pick pinyin 'ni' for "64"
        s.applyLetterPick("64", "ni", false);
        assertEquals("qni26", s.buffer);
        assertEquals(3, s.lettersEnd);

        // Undo pinyin pick
        assertTrue(s.cancel());
        assertEquals("q6426", s.buffer);
        assertEquals(1, s.lettersEnd);

        // Undo fallback pick
        assertTrue(s.cancel());
        assertEquals("76426", s.buffer);
        assertEquals(0, s.lettersEnd);

        assertFalse(s.cancel());
    }

    // ---------- reset ----------

    @Test
    public void resetClearsEverything() {
        InputState s = new InputState();
        s.buffer = "nihao";
        s.lettersEnd = 5;
        s.select("你", "ni", 2);
        s.reset();

        assertEquals("", s.buffer);
        assertEquals(0, s.cursor);
        assertEquals(0, s.lettersEnd);
        assertTrue(s.selections.isEmpty());
        assertTrue(s.undoStack.isEmpty());
    }
}
