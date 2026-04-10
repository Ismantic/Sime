package com.isma.sime.ime.keyboard;

import android.content.Context;
import android.widget.LinearLayout;

/**
 * 4x5 number pad.
 *
 * <pre>
 *   %  1  2  3  ⌫
 *   +  4  5  6  ␣
 *   -  7  8  9  ⏎
 *   *  符 返 0  .
 * </pre>
 *
 * All keys commit their label directly except backspace/space/enter and
 * the two mode keys ({@code 符} → SYMBOL, {@code 返} → previous mode).
 */
public class NumberKeyboardView extends KeyboardView {

    public NumberKeyboardView(Context context) {
        super(context);
        build();
    }

    private void build() {
        addView(buildRow("%", "1", "2", "3", "⌫"));
        addView(buildRow("+", "4", "5", "6", "␣"));
        addView(buildRow("-", "7", "8", "9", "⏎"));
        addView(buildRow("*", "符", "返", "0", "."));
    }

    private LinearLayout buildRow(String... labels) {
        LinearLayout row = makeRow();
        for (String l : labels) {
            final String label = l;
            row.addView(makeKey(l, 1f, 18f, isFunction(l), () -> onKey(label)));
        }
        return row;
    }

    private boolean isFunction(String label) {
        return "⌫".equals(label) || "␣".equals(label) || "⏎".equals(label)
                || "符".equals(label) || "返".equals(label);
    }

    private void onKey(String label) {
        switch (label) {
            case "⌫": emit(SimeKey.backspace()); return;
            case "␣": emit(SimeKey.space()); return;
            case "⏎": emit(SimeKey.enter()); return;
            case "符": emit(SimeKey.toSymbol()); return;
            case "返": emit(SimeKey.toBack()); return;
            default:
                emit(SimeKey.punctuation(label));
        }
    }
}
