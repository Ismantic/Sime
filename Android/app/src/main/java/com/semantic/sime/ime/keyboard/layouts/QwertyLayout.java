package com.semantic.sime.ime.keyboard.layouts;

import com.semantic.sime.ime.keyboard.SimeKey;
import com.semantic.sime.ime.keyboard.framework.KeyDef;
import com.semantic.sime.ime.keyboard.framework.KeyRow;
import com.semantic.sime.ime.keyboard.framework.KeyboardLayout;

/**
 * QWERTY layout shared by both Chinese (pinyin) and English modes. The
 * controller ({@code QwertyKeyboardView}) flips letter case + dual-state
 * keys at runtime depending on mode/active/shift.
 *
 * <pre>
 *   Row 1: q w e r t y u i o p   (hints: 1 2 3 4 5 6 7 8 9 0)
 *   Row 2:  a s d f g h j k l    (hints: @ # $ % & * - + =)
 *   Row 3: ⇧/分词 z x c v b n m ⌫  (hints: ' " : ; ! ? / on letters)
 *   Row 4: 123 | , | space | 中\n英/EN | 换行/确定
 * </pre>
 */
public final class QwertyLayout {

    public static final String ID_SHIFT = "qwerty.shift";
    public static final String ID_LANG  = "qwerty.lang";
    public static final String ID_ENTER = "qwerty.enter";
    public static final String ID_COMMA = "qwerty.comma";
    public static final String LETTER_ID_PREFIX = "qwerty.letter.";

    public static final String[] ROW1 = {"q","w","e","r","t","y","u","i","o","p"};
    public static final String[] ROW2 = {"a","s","d","f","g","h","j","k","l"};
    public static final String[] ROW3 = {"z","x","c","v","b","n","m"};

    public static final String[] ROW1_HINT = {"1","2","3","4","5","6","7","8","9","0"};
    public static final String[] ROW2_HINT = {"@","#","$","%","&","*","-","+","="};
    public static final String[] ROW3_HINT = {"'","\"",":",";","!","?","/"};

    private QwertyLayout() {}

    public static KeyboardLayout build() {
        KeyboardLayout.Builder b = KeyboardLayout.builder()
                .horizontalPadding(4)
                .verticalPadding(0)
                .keyMargin(3);

        // Row 1: 10 letters
        KeyRow.Builder r1 = KeyRow.builder(1f);
        for (int i = 0; i < ROW1.length; i++) {
            r1.key(letter(ROW1[i], ROW1_HINT[i]));
        }
        b.row(r1);

        // Row 2: 9 letters with 0.5 fillers on each side for the home-row indent
        KeyRow.Builder r2 = KeyRow.builder(1f);
        r2.key(KeyDef.empty(0.5f));
        for (int i = 0; i < ROW2.length; i++) {
            r2.key(letter(ROW2[i], ROW2_HINT[i]));
        }
        r2.key(KeyDef.empty(0.5f));
        b.row(r2);

        // Row 3: shift(1.5) + 7 letters(1) + backspace(1.5) = 10
        KeyRow.Builder r3 = KeyRow.builder(1f);
        r3.key(KeyDef.function("⇧", null).id(ID_SHIFT).width(1.5f).labelSize(16f));
        for (int i = 0; i < ROW3.length; i++) {
            r3.key(letter(ROW3[i], ROW3_HINT[i]));
        }
        r3.key(KeyDef.function("⌫", SimeKey.backspace()).width(1.5f).repeatable(true));
        b.row(r3);

        // Row 4: 符号 | 123 | , | space | 中\n英 | 换行
        // 符号 and 123 are equal-width function keys; , aligns with x;
        // space spans c+v+b; 中\n英 is 1.25× n width; 换行 fills the
        // remaining space on the right. Total: 10 weight units = row 3.
        KeyRow.Builder r4 = KeyRow.builder(0.95f);
        r4.key(KeyDef.function("符号", SimeKey.toSymbol()).width(1.25f).labelSize(14f));
        r4.key(KeyDef.function("123", SimeKey.toNumber()).width(1.25f).labelSize(14f));
        r4.key(KeyDef.normal(",", SimeKey.punctuation(","))
                .id(ID_COMMA).width(1f).labelSize(15f)
                .topLabel(".")
                .longPress(SimeKey.punctuation(".")));
        r4.key(KeyDef.normal("空格", SimeKey.space()).width(3f).labelSize(14f)
                .longPress(SimeKey.toggleLang()));
        r4.key(KeyDef.function("中\n英", SimeKey.toggleLang())
                .id(ID_LANG).width(1.25f).labelSize(14f));
        r4.key(KeyDef.function("换行", SimeKey.enter())
                .id(ID_ENTER).width(2.25f).labelSize(14f));
        b.row(r4);

        return b.build();
    }

    private static KeyDef.Builder letter(String letter, String hint) {
        char c = letter.charAt(0);
        return KeyDef.normal(letter, SimeKey.letter(c))
                .id(LETTER_ID_PREFIX + letter)
                .width(1f)
                .labelSize(18f)
                .hint(hint)
                // Long-press the letter to commit the hint glyph (digit
                // / symbol) directly. Matches the visual cue.
                .longPress(SimeKey.punctuation(hint));
    }
}
