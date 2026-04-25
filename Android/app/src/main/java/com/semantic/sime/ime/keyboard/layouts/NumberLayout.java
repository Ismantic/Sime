package com.semantic.sime.ime.keyboard.layouts;

import com.semantic.sime.ime.keyboard.SimeKey;
import com.semantic.sime.ime.keyboard.framework.KeyDef;
import com.semantic.sime.ime.keyboard.framework.KeyRow;
import com.semantic.sime.ime.keyboard.framework.KeyboardLayout;
import com.semantic.sime.ime.theme.Typography;

/**
 * Number pad layout. Three vertical blocks side by side; 换行 is a
 * tall right-column key that extends from row 3 down through the
 * bottom row, so the bottom row only spans the leftmost 4 columns.
 *
 * <pre>
 *   ┌────┬────┬────┬────┬──────┐
 *   │ @  │ 1  │ 2  │ 3  │  ⌫   │
 *   │ %  ├────┼────┼────┤      │
 *   │ -  │ 4  │ 5  │ 6  │ 空格 │
 *   │ +  ├────┼────┼────┤      │
 *   │ ×  │ 7  │ 8  │ 9  │      │
 *   ├────┼────┼────┼────┤ 换行 │
 *   │符号│返回│  0   │ . │      │
 *   └────┴────┴────┴────┴──────┘
 * </pre>
 *
 * <p>The left strip is scrollable and lives in {@code NumberKeyboardView}
 * directly (built from {@link #LEFT_STRIP_PUNCS}); the rest is exposed
 * here as four layouts:
 * <ul>
 *   <li>{@link #buildFuhaoCell()}    — single 符号 cell, sits below the left strip.
 *   <li>{@link #buildMainGrid()}     — center 3×3 digits.
 *   <li>{@link #buildCenterBottomRow()} — center bottom row: 返回, 0(wider), . .
 *   <li>{@link #buildRightColumn()}  — right column 3 cells (⌫, 空格, 换行 ×2 tall).
 * </ul>
 */
public final class NumberLayout {

    /**
     * Punctuation shown in the scrollable left strip. First 4 are
     * visible without scrolling on a typical phone; the rest reveal
     * by dragging.
     */
    public static final String[] LEFT_STRIP_PUNCS = {
            "@", "%", "-", "+", "×", "/", "=", ":"
    };

    private NumberLayout() {}

    public static KeyboardLayout buildFuhaoCell() {
        return KeyboardLayout.builder()
                .horizontalPadding(0)
                .verticalPadding(0)
                .keyMargin(3)
                .row(KeyRow.builder(1f)
                        .key(KeyDef.function("符号", SimeKey.toSymbol())
                                .labelSize(Typography.SMALL)))
                .build();
    }

    public static KeyboardLayout buildMainGrid() {
        return KeyboardLayout.builder()
                .horizontalPadding(0)
                .verticalPadding(0)
                .keyMargin(3)
                .row(KeyRow.builder(1f)
                        .key(digit("1")).key(digit("2")).key(digit("3")))
                .row(KeyRow.builder(1f)
                        .key(digit("4")).key(digit("5")).key(digit("6")))
                .row(KeyRow.builder(1f)
                        .key(digit("7")).key(digit("8")).key(digit("9")))
                .build();
    }

    /**
     * Bottom row of the center block — sits below the digit grid and
     * spans the same 3 unit widths. Cells are equal width so 0 lands
     * directly under 8.
     */
    public static KeyboardLayout buildCenterBottomRow() {
        return KeyboardLayout.builder()
                .horizontalPadding(0)
                .verticalPadding(0)
                .keyMargin(3)
                .row(KeyRow.builder(1f)
                        .key(KeyDef.function("返回", SimeKey.toBack())
                                .labelSize(Typography.SMALL))
                        .key(digit("0"))
                        .key(punc(".")))
                .build();
    }

    /**
     * Right column. 3 rows with weights 1, 1, 2 — 换行 is tall and
     * extends from the third digit row down through the bottom-row
     * area, so its rendered height equals two main-grid rows.
     */
    public static KeyboardLayout buildRightColumn() {
        return KeyboardLayout.builder()
                .horizontalPadding(0)
                .verticalPadding(0)
                .keyMargin(3)
                .row(KeyRow.builder(1f)
                        .key(KeyDef.function("⌫", SimeKey.backspace())
                                .repeatable(true)))
                .row(KeyRow.builder(1f)
                        .key(KeyDef.function("空格", SimeKey.space())
                                .labelSize(Typography.SMALL)))
                .row(KeyRow.builder(2f)
                        .key(KeyDef.function("换行", SimeKey.enter())
                                .labelSize(Typography.SMALL)))
                .build();
    }

    private static KeyDef.Builder digit(String d) {
        return KeyDef.normal(d, SimeKey.punctuation(d));
    }

    private static KeyDef.Builder punc(String s) {
        return KeyDef.normal(s, SimeKey.punctuation(s)).labelSize(Typography.BODY);
    }
}
