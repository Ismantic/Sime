package com.isma.sime.ime.keyboard.layouts;

import com.isma.sime.ime.data.Symbols;
import com.isma.sime.ime.keyboard.SimeKey;
import com.isma.sime.ime.keyboard.framework.KeyDef;
import com.isma.sime.ime.keyboard.framework.KeyRow;
import com.isma.sime.ime.keyboard.framework.KeyboardLayout;

/**
 * Symbol keyboard layouts. Two pieces:
 *
 * <ul>
 *   <li>{@link #buildPage(int)} — the symbol grid for a given category
 *       tab. Returns a fresh layout sized to fit the symbols in that
 *       tab (rows = ceil(n / COLS)).
 *   <li>{@link #buildBottomRow(int)} — the navigation row at the bottom:
 *       {@code ← | 中 | EN | 数 | ⌫}. The currently-selected tab is
 *       marked via {@link #ID_TAB_PREFIX} so the controller can flip
 *       its highlighted state.
 * </ul>
 */
public final class SymbolLayout {

    /** Fixed grid: 3 rows × 8 cols = 24 cells per tab. */
    public static final int COLS = 8;
    public static final int ROWS = 3;

    public static final String ID_TAB_PREFIX = "sym.tab.";

    private SymbolLayout() {}

    public static int tabCount() {
        return Symbols.TABS.length;
    }

    public static KeyboardLayout buildPage(int tabIdx) {
        String[] syms = Symbols.TABS[tabIdx];

        KeyboardLayout.Builder b = KeyboardLayout.builder()
                .horizontalPadding(4)
                .verticalPadding(0)
                .keyMargin(3);

        for (int r = 0; r < ROWS; r++) {
            KeyRow.Builder row = KeyRow.builder(1f);
            for (int c = 0; c < COLS; c++) {
                int idx = r * COLS + c;
                if (idx < syms.length) {
                    final String s = syms[idx];
                    row.key(KeyDef.normal(s, SimeKey.punctuation(s))
                            .width(1f)
                            .labelSize(18f));
                } else {
                    row.key(KeyDef.empty(1f));
                }
            }
            b.row(row);
        }
        return b.build();
    }

    public static KeyboardLayout buildBottomRow(int currentTab) {
        KeyRow.Builder row = KeyRow.builder(1f);
        row.key(KeyDef.accent("←", SimeKey.toBack()).width(1.2f).labelSize(16f));
        for (int i = 0; i < Symbols.TAB_NAMES.length; i++) {
            String name = Symbols.TAB_NAMES[i];
            row.key(KeyDef.normal(name, null)
                    .id(ID_TAB_PREFIX + i)
                    .width(2f)
                    .labelSize(14f));
        }
        row.key(KeyDef.function("⌫", SimeKey.backspace())
                .width(1.2f)
                .repeatable(true));
        return KeyboardLayout.builder()
                .horizontalPadding(4)
                .verticalPadding(0)
                .keyMargin(3)
                .row(row)
                .build();
    }
}
