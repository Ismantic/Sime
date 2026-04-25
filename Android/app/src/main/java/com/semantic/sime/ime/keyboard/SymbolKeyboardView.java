package com.semantic.sime.ime.keyboard;

import android.content.Context;
import android.view.View;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;

import com.semantic.sime.ime.data.Symbols;
import com.semantic.sime.ime.keyboard.framework.KeyDef;
import com.semantic.sime.ime.keyboard.framework.KeyView;
import com.semantic.sime.ime.keyboard.framework.KeyboardContainer;
import com.semantic.sime.ime.keyboard.layouts.SymbolLayout;

/**
 * Symbol keyboard. The grid swaps when the user picks a different
 * category tab in the bottom navigation row; tab keys are wired with
 * a custom listener so the controller can rebuild the grid in place.
 *
 * <p>The bottom row pins ← and ⌫ on the sides and puts the tab strip
 * inside a {@link HorizontalScrollView} — the 8 categories overflow a
 * normal phone width, so the user scrolls horizontally to reach more
 * tabs without each tab shrinking to an unreadable size.
 */
public class SymbolKeyboardView extends KeyboardView {

    /** Width per tab key inside the scrollable strip. */
    private static final int TAB_WIDTH_DP = 52;
    /** Width of the pinned ← and ⌫ side keys. */
    private static final int SIDE_KEY_WIDTH_DP = 48;

    private KeyboardContainer grid;
    private final KeyView[] tabKeys = new KeyView[SymbolLayout.tabCount()];
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

        LinearLayout bottomBar = buildBottomBar();
        LayoutParams brLp = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f);
        bottomBar.setLayoutParams(brLp);
        addView(bottomBar);

        loadTab(currentTab);
    }

    private LinearLayout buildBottomBar() {
        LinearLayout bar = new LinearLayout(getContext());
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setPadding(dp(4), 0, dp(4), 0);

        bar.addView(
                makeKey(KeyDef.accent("←", SimeKey.toBack()).labelSize(16f).build()),
                new LinearLayout.LayoutParams(dp(SIDE_KEY_WIDTH_DP), LinearLayout.LayoutParams.MATCH_PARENT));

        HorizontalScrollView scroll = new HorizontalScrollView(getContext());
        scroll.setHorizontalScrollBarEnabled(false);
        scroll.setOverScrollMode(View.OVER_SCROLL_NEVER);
        LinearLayout strip = new LinearLayout(getContext());
        strip.setOrientation(LinearLayout.HORIZONTAL);
        for (int i = 0; i < SymbolLayout.tabCount(); i++) {
            final int idx = i;
            KeyDef def = KeyDef.normal(Symbols.TAB_NAMES[i], null)
                    .id(SymbolLayout.ID_TAB_PREFIX + i)
                    .labelSize(16f)
                    .build();
            KeyView kv = new KeyView(getContext(), theme, def, 3f);
            kv.setListener((d, action) -> {
                if (action != KeyView.KeyAction.CLICK) return;
                if (idx != currentTab) loadTab(idx);
            });
            tabKeys[i] = kv;
            strip.addView(kv, new LinearLayout.LayoutParams(
                    dp(TAB_WIDTH_DP), LinearLayout.LayoutParams.MATCH_PARENT));
        }
        scroll.addView(strip, new HorizontalScrollView.LayoutParams(
                HorizontalScrollView.LayoutParams.WRAP_CONTENT,
                HorizontalScrollView.LayoutParams.MATCH_PARENT));
        bar.addView(scroll, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1f));

        bar.addView(
                makeKey(KeyDef.function("⌫", SimeKey.backspace()).repeatable(true).build()),
                new LinearLayout.LayoutParams(dp(SIDE_KEY_WIDTH_DP), LinearLayout.LayoutParams.MATCH_PARENT));

        return bar;
    }

    private KeyView makeKey(KeyDef def) {
        KeyView kv = new KeyView(getContext(), theme, def, 3f);
        kv.setListener((d, action) -> {
            if (action == KeyView.KeyAction.CLICK && d.clickAction != null) {
                emit(d.clickAction);
            } else if (action == KeyView.KeyAction.LONG_PRESS && d.longPressAction != null) {
                emit(d.longPressAction);
            }
        });
        return kv;
    }

    private void loadTab(int tab) {
        currentTab = tab;
        grid.setLayout(SymbolLayout.buildPage(tab));
        for (int i = 0; i < tabKeys.length; i++) {
            if (tabKeys[i] != null) tabKeys[i].setHighlighted(i == tab);
        }
    }
}
