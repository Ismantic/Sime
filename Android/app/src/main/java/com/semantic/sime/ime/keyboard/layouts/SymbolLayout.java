package com.semantic.sime.ime.keyboard.layouts;

import com.semantic.sime.ime.data.Symbols;
import com.semantic.sime.ime.keyboard.SimeKey;
import com.semantic.sime.ime.keyboard.framework.KeyDef;
import com.semantic.sime.ime.keyboard.framework.KeyRow;
import com.semantic.sime.ime.keyboard.framework.KeyboardLayout;

/**
 * Symbol grid for one category tab. Returns a fresh layout sized to fit
 * the symbols in that tab (rows = ceil(n / COLS)).
 *
 * <p>The bottom navigation row (← + tab strip + ⌫) is hand-built in
 * {@code SymbolKeyboardView} so the tab strip can live inside a
 * {@link android.widget.HorizontalScrollView} — the 8 tabs overflow a
 * normal phone width and need to scroll. {@link #ID_TAB_PREFIX} is the
 * id prefix used by the view to look its tab keys up.
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
                    row.key(KeyDef.normal(s, SimeKey.punctuation(s)));
                } else {
                    row.key(KeyDef.empty(1f));
                }
            }
            b.row(row);
        }
        return b.build();
    }
}
