package com.semantic.sime.ime.keyboard;

import android.content.Context;

import com.semantic.sime.ime.keyboard.framework.KeyView;
import com.semantic.sime.ime.keyboard.framework.KeyboardContainer;
import com.semantic.sime.ime.keyboard.layouts.SymbolLayout;

/**
 * Symbol keyboard. The grid swaps when the user picks a different
 * category tab in the bottom navigation row; tab keys are wired with
 * a custom listener so the controller can rebuild the grid in place.
 */
public class SymbolKeyboardView extends KeyboardView {

    private KeyboardContainer grid;
    private KeyboardContainer bottomRow;
    private int currentTab = 0;

    public SymbolKeyboardView(Context context) {
        super(context);
        build();
    }

    private void build() {
        grid = new KeyboardContainer(getContext(), theme);
        LayoutParams gLp = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 4f);
        grid.setLayoutParams(gLp);
        grid.setOnKeyEmitListener(this::emit);
        addView(grid);

        bottomRow = new KeyboardContainer(getContext(), theme);
        LayoutParams brLp = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f);
        bottomRow.setLayoutParams(brLp);
        bottomRow.setOnKeyEmitListener(this::emit);
        addView(bottomRow);

        bottomRow.setLayout(SymbolLayout.buildBottomRow(currentTab));
        installTabHandlers();

        loadTab(currentTab);
    }

    private void installTabHandlers() {
        for (int i = 0; i < SymbolLayout.tabCount(); i++) {
            final int idx = i;
            KeyView kv = bottomRow.findKeyById(SymbolLayout.ID_TAB_PREFIX + i);
            if (kv != null) {
                kv.setListener((def, action) -> {
                    if (action != KeyView.KeyAction.CLICK) return;
                    if (idx != currentTab) loadTab(idx);
                });
            }
        }
    }

    private void loadTab(int tab) {
        currentTab = tab;
        grid.setLayout(SymbolLayout.buildPage(tab));
        // Refresh tab highlight on the bottom row.
        for (int i = 0; i < SymbolLayout.tabCount(); i++) {
            KeyView kv = bottomRow.findKeyById(SymbolLayout.ID_TAB_PREFIX + i);
            if (kv != null) kv.setHighlighted(i == tab);
        }
    }
}
