package com.isma.sime.ime.keyboard;

import android.content.Context;
import android.graphics.Typeface;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.isma.sime.ime.data.Symbols;

/**
 * 3-tab symbol keyboard (中标点 / 英标点 / 数字符号). One page per tab;
 * the grid is always 6 columns wide. A trailing row provides the back
 * and backspace keys.
 */
public class SymbolKeyboardView extends KeyboardView {

    private static final int COLS = 6;

    private LinearLayout tabRow;
    private LinearLayout grid;
    private int currentTab = 0;
    private TextView[] tabViews;

    public SymbolKeyboardView(Context context) {
        super(context);
        build();
    }

    private void build() {
        tabRow = new LinearLayout(getContext());
        tabRow.setOrientation(HORIZONTAL);
        LayoutParams tabLp = new LayoutParams(
                LayoutParams.MATCH_PARENT, dp(38));
        tabRow.setLayoutParams(tabLp);
        tabViews = new TextView[Symbols.TAB_NAMES.length];
        for (int i = 0; i < Symbols.TAB_NAMES.length; i++) {
            final int idx = i;
            TextView tv = new TextView(getContext());
            tv.setText(Symbols.TAB_NAMES[i]);
            tv.setGravity(Gravity.CENTER);
            tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f);
            tv.setTextColor(theme.barForeground);
            tv.setPadding(dp(12), 0, dp(12), 0);
            tv.setClickable(true);
            tv.setOnClickListener(v -> selectTab(idx));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    0, LayoutParams.MATCH_PARENT, 1f);
            tv.setLayoutParams(lp);
            tabRow.addView(tv);
            tabViews[i] = tv;
        }
        addView(tabRow);

        grid = new LinearLayout(getContext());
        grid.setOrientation(VERTICAL);
        LayoutParams gridLp = new LayoutParams(
                LayoutParams.MATCH_PARENT, 0, 1f);
        grid.setLayoutParams(gridLp);
        addView(grid);

        LinearLayout bottom = makeRow();
        LayoutParams blp = new LayoutParams(LayoutParams.MATCH_PARENT, dp(48));
        bottom.setLayoutParams(blp);
        bottom.addView(makeKey("←", 1.5f, 16f, true, () -> emit(SimeKey.toBack())));
        bottom.addView(makeFiller(3f));
        bottom.addView(makeKey("⌫", 1.5f, 18f, true, () -> emit(SimeKey.backspace())));
        addView(bottom);

        selectTab(0);
    }

    private void selectTab(int idx) {
        currentTab = idx;
        for (int i = 0; i < tabViews.length; i++) {
            tabViews[i].setTypeface(null, i == idx ? Typeface.BOLD : Typeface.NORMAL);
            tabViews[i].setTextColor(
                    i == idx ? theme.accentColor : theme.barForeground);
        }
        populateGrid();
    }

    private void populateGrid() {
        grid.removeAllViews();
        String[] syms = Symbols.TABS[currentTab];
        int rows = (syms.length + COLS - 1) / COLS;
        for (int r = 0; r < rows; r++) {
            LinearLayout row = makeRow();
            for (int c = 0; c < COLS; c++) {
                int idx = r * COLS + c;
                if (idx < syms.length) {
                    final String s = syms[idx];
                    row.addView(makeKey(s, 1f, 16f, false,
                            () -> emit(SimeKey.punctuation(s))));
                } else {
                    row.addView(makeFiller(1f));
                }
            }
            grid.addView(row);
        }
    }
}
