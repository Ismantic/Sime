package com.semantic.sime.ime.keyboard.layouts;

import com.semantic.sime.ime.keyboard.SimeKey;
import com.semantic.sime.ime.keyboard.framework.KeyDef;
import com.semantic.sime.ime.keyboard.framework.KeyRow;
import com.semantic.sime.ime.keyboard.framework.KeyboardLayout;

/**
 * T9 (九宫格) layouts. Same three-block structure as
 * {@link NumberLayout}: left strip + 符号, center digit grid + center
 * bottom row, right column with a tall 换行 spanning two rows.
 *
 * <pre>
 *   ┌─────┬─────┬─────┬─────┬──────┐
 *   │ ',  │ @#  │ ABC │ DEF │  ⌫   │
 *   │ 。  ├─────┼─────┼─────┤      │
 *   │ ?   │ GHI │ JKL │ MNO │ 重输 │
 *   │ !   ├─────┼─────┼─────┤      │
 *   │     │PQRS │ TUV │WXYZ │      │
 *   ├─────┼─────┴─────┴─────┤ 换行 │
 *   │符号 │ 123 │ 空格 │中\n英│      │
 *   └─────┴────────────────┴──────┘
 * </pre>
 *
 * <p>Key ids the controller cares about at runtime:
 * <ul>
 *   <li>{@link #ID_TOP_LEFT} — switches between {@code @#} and {@code 分词}.
 *   <li>{@link #ID_ENTER}    — switches between {@code 换行} and {@code 确定}.
 * </ul>
 */
public final class T9Layout {

    public static final String ID_TOP_LEFT = "t9.topLeft";
    public static final String ID_ENTER    = "t9.enter";

    private T9Layout() {}

    /**
     * Single 符号 cell — sits in the left block below the dynamic strip.
     */
    public static KeyboardLayout buildFuhaoCell() {
        return KeyboardLayout.builder()
                .horizontalPadding(4)
                .verticalPadding(0)
                .keyMargin(3)
                .row(KeyRow.builder(1f)
                        .key(KeyDef.function("符号", SimeKey.toSymbol())
                                .width(1f).labelSize(14f)))
                .build();
    }

    /**
     * Center 3×3 digit grid. The first row's leftmost cell is the
     * dual-state {@code @# / 分词} key.
     */
    public static KeyboardLayout buildMainGrid() {
        KeyboardLayout.Builder b = KeyboardLayout.builder()
                .horizontalPadding(4)
                .verticalPadding(0)
                .keyMargin(3);

        b.row(KeyRow.builder(1f)
                .key(KeyDef.function("@#", null).id(ID_TOP_LEFT).width(1f).labelSize(15f)
                        .hint("1"))
                .key(t9digit("2", "ABC"))
                .key(t9digit("3", "DEF")));

        b.row(KeyRow.builder(1f)
                .key(t9digit("4", "GHI"))
                .key(t9digit("5", "JKL"))
                .key(t9digit("6", "MNO")));

        b.row(KeyRow.builder(1f)
                .key(t9digit("7", "PQRS"))
                .key(t9digit("8", "TUV"))
                .key(t9digit("9", "WXYZ")));

        return b.build();
    }

    /**
     * Center bottom row: 123 (narrow) | 空格 (wide) | 中\n英 (narrow).
     * Spans 3 unit widths to match the digit grid above. 空格 is
     * wider because it's the most-typed cell here.
     */
    public static KeyboardLayout buildCenterBottomRow() {
        return KeyboardLayout.builder()
                .horizontalPadding(4)
                .verticalPadding(0)
                .keyMargin(3)
                .row(KeyRow.builder(1f)
                        .key(KeyDef.function("123",  SimeKey.toNumber())
                                .width(0.75f).labelSize(14f))
                        .key(KeyDef.normal  ("空格", SimeKey.space())
                                .width(1.5f).labelSize(14f)
                                .longPress(SimeKey.toggleLang()))
                        .key(KeyDef.function("中\n英", SimeKey.toggleLang())
                                .width(0.75f).labelSize(14f)))
                .build();
    }

    /**
     * Right column. Three rows of weights 1, 1, 2 — 换行 is two
     * grid-rows tall, extending from the third digit row down through
     * the bottom-row area.
     */
    public static KeyboardLayout buildRightColumn() {
        return KeyboardLayout.builder()
                .horizontalPadding(4)
                .verticalPadding(0)
                .keyMargin(3)
                .row(KeyRow.builder(1f)
                        .key(KeyDef.function("⌫", SimeKey.backspace())
                                .width(1f).repeatable(true)))
                .row(KeyRow.builder(1f)
                        .key(KeyDef.function("重输", SimeKey.clear())
                                .width(1f).labelSize(14f)))
                .row(KeyRow.builder(2f)
                        .key(KeyDef.function("换行", SimeKey.enter())
                                .id(ID_ENTER).width(1f).labelSize(14f)))
                .build();
    }

    private static KeyDef.Builder t9digit(String digit, String letters) {
        char c = digit.charAt(0);
        return KeyDef.normal(letters, SimeKey.digit(c))
                .width(1f)
                .labelSize(16f)
                .hint(digit);
    }
}
