package com.semantic.sime.ime.keyboard.layouts;

import com.semantic.sime.ime.data.Symbols;
import com.semantic.sime.ime.keyboard.SimeKey;
import com.semantic.sime.ime.keyboard.framework.KeyDef;
import com.semantic.sime.ime.keyboard.framework.KeyRow;
import com.semantic.sime.ime.keyboard.framework.KeyboardLayout;
import com.semantic.sime.ime.theme.Typography;

/**
 * Symbol grid for one category tab. Doubao-style three-row layout:
 *
 * <pre>
 *  row 1: 10 symbols
 *  row 2: 10 symbols
 *  row 3: [123]  7 symbols  [⌫]
 * </pre>
 *
 * Total 27 symbol cells per tab. The bottom navigation row (← + tab
 * strip) lives in {@code SymbolKeyboardView}.
 */
public final class SymbolLayout {

    public static final String ID_TAB_PREFIX = "sym.tab.";

    private SymbolLayout() {}

    public static int tabCount() {
        return Symbols.TABS.length;
    }

    public static KeyboardLayout buildPage(int tabIdx) {
        String[] syms = Symbols.TABS[tabIdx];

        KeyboardLayout.Builder b = KeyboardLayout.builder()
                .horizontalPadding(0)
                .verticalPadding(0)
                .keyMargin(3)
                .keyMarginVertical(5);

        // Row 1: 10 symbols
        KeyRow.Builder r1 = KeyRow.builder(1f);
        for (int c = 0; c < 10; c++) addSymCell(r1, syms, c);
        b.row(r1);

        // Row 2: 10 symbols
        KeyRow.Builder r2 = KeyRow.builder(1f);
        for (int c = 0; c < 10; c++) addSymCell(r2, syms, 10 + c);
        b.row(r2);

        // Row 3: [123(1.25)] + 0.25 + 7 symbols(1 each) + 0.25 + [⌫(1.25)]
        // = 10 weight. Aligns 1:1 with Qwerty row 3 (shift / 7 letters /
        // backspace) — column edges match across keyboard modes.
        KeyRow.Builder r3 = KeyRow.builder(1f);
        r3.key(KeyDef.function("123", SimeKey.toNumber()).width(1.25f).labelSize(Typography.SMALL));
        r3.key(KeyDef.empty(0.25f));
        for (int c = 0; c < 7; c++) addSymCell(r3, syms, 20 + c);
        r3.key(KeyDef.empty(0.25f));
        r3.key(KeyDef.function("⌫", SimeKey.backspace()).width(1.25f).repeatable(true));
        b.row(r3);

        return b.build();
    }

    private static void addSymCell(KeyRow.Builder row, String[] syms, int idx) {
        if (idx < syms.length) {
            String s = syms[idx];
            row.key(KeyDef.normal(s, SimeKey.punctuation(s)));
        } else {
            row.key(KeyDef.empty(1f));
        }
    }
}
