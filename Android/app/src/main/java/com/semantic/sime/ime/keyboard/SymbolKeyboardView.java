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
import com.semantic.sime.ime.theme.Typography;

/**
 * Symbol keyboard. The grid swaps when the user picks a different
 * category tab in the bottom navigation row; tab keys are wired with
 * a custom listener so the controller can rebuild the grid in place.
 *
 * <p>The bottom row pins ← on the left and puts the tab strip inside a
 * {@link HorizontalScrollView} — categories overflow a normal phone
 * width, so the user scrolls horizontally to reach more tabs. ⌫ lives
 * inside the symbol grid (row 3 right) per doubao layout.
 */
public class SymbolKeyboardView extends KeyboardView {

    /** Width per tab key inside the scrollable strip. */
    private static final int TAB_WIDTH_DP = 52;
    /** Width of the pinned ← side key. */
    private static final int SIDE_KEY_WIDTH_DP = 48;

    private KeyboardContainer grid;
    private final KeyView[] tabKeys = new KeyView[SymbolLayout.tabCount()];
    private int currentTab = 0;

    public SymbolKeyboardView(Context context) {
        super(context);
        build();
    }

    private void build() {
        // Outer weights chosen so the grid's 3 rows match Qwerty row
        // 1-3 height (weight 1 each) and the bottom bar matches Qwerty
        // row 4 (weight 0.95).
        grid = new KeyboardContainer(getContext(), theme);
        LayoutParams gLp = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 3f);
        grid.setLayoutParams(gLp);
        grid.setOnKeyEmitListener(this::emit);
        addView(grid);

        LinearLayout bottomBar = buildBottomBar();
        LayoutParams brLp = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 0.95f);
        bottomBar.setLayoutParams(brLp);
        addView(bottomBar);

        loadTab(currentTab);
    }

    private LinearLayout buildBottomBar() {
        LinearLayout bar = new LinearLayout(getContext());
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setPadding(0, 0, 0, 0);

        bar.addView(
                makeKey(KeyDef.accent("←", SimeKey.toBack()).labelSize(Typography.CALLOUT).build()),
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
                    .labelSize(Typography.CALLOUT)
                    .build();
            KeyView kv = new KeyView(getContext(), theme, def, 3f, 5f);
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

        return bar;
    }

    private KeyView makeKey(KeyDef def) {
        KeyView kv = new KeyView(getContext(), theme, def, 3f, 5f);
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
