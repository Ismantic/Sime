package com.semantic.sime.ime.keyboard;

import android.content.Context;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.semantic.sime.ime.keyboard.framework.KeyboardContainer;
import com.semantic.sime.ime.keyboard.layouts.NumberLayout;

/**
 * Number pad. Layout matches the reference T9 pattern: a tall 换行
 * key in the right column extends from row 3 down through the bottom
 * area, so the bottom row only spans the leftmost 4 columns.
 *
 * <pre>
 *   @  1  2  3  ⌫
 *   %  4  5  6  空格
 *   -  7  8  9
 *   +  ───────  换行
 *   ×  返 0  .
 *   符号
 * </pre>
 *
 * <p>Three vertical blocks side by side: leftBlock (scrollable
 * punctuation strip + 符号), centerBlock (digit grid + center bottom
 * row), rightBlock (⌫ / 空格 / 换行 with row weights 1, 1, 2 to make
 * 换行 two rows tall).
 *
 * <p>The left strip is hand-rolled (ScrollView + LinearLayout of
 * TextViews) so it can hold more entries than fit visibly — first 4
 * are shown, the rest reveal by scrolling. Mirrors the T9 left strip.
 */
public class NumberKeyboardView extends KeyboardView {

    private static final int LEFT_ITEM_HEIGHT_DP = 38;

    public NumberKeyboardView(Context context) {
        super(context);
        // Override the base class's VERTICAL orientation — this view's
        // top-level layout is three columns side by side.
        setOrientation(HORIZONTAL);
        build();
    }

    private void build() {
        // ===== Left block: scrollable strip (3f) + 符号(1f) =====
        LinearLayout leftBlock = new LinearLayout(getContext());
        leftBlock.setOrientation(VERTICAL);
        addView(leftBlock, new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f));

        ScrollView leftScroll = new ScrollView(getContext());
        leftScroll.setVerticalScrollBarEnabled(false);
        leftScroll.setFillViewport(true);
        leftBlock.addView(leftScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 3f));

        LinearLayout leftStrip = new LinearLayout(getContext());
        leftStrip.setOrientation(VERTICAL);
        leftScroll.addView(leftStrip, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));
        for (final String p : NumberLayout.LEFT_STRIP_PUNCS) {
            leftStrip.addView(makePuncCell(p));
        }

        KeyboardContainer fuhao = new KeyboardContainer(getContext(), theme);
        fuhao.setOnKeyEmitListener(this::emit);
        fuhao.setLayout(NumberLayout.buildFuhaoCell());
        leftBlock.addView(fuhao, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        // ===== Center block: mainGrid(top, 3f) + centerBottom(bottom, 1f) =====
        LinearLayout centerBlock = new LinearLayout(getContext());
        centerBlock.setOrientation(VERTICAL);
        addView(centerBlock, new LayoutParams(0, LayoutParams.MATCH_PARENT, 3f));

        KeyboardContainer mainGrid = new KeyboardContainer(getContext(), theme);
        mainGrid.setOnKeyEmitListener(this::emit);
        mainGrid.setLayout(NumberLayout.buildMainGrid());
        centerBlock.addView(mainGrid, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 3f));

        KeyboardContainer centerBottom = new KeyboardContainer(getContext(), theme);
        centerBottom.setOnKeyEmitListener(this::emit);
        centerBottom.setLayout(NumberLayout.buildCenterBottomRow());
        centerBlock.addView(centerBottom, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        // ===== Right block: ⌫ / 空格 / 换行(2 rows tall) =====
        // Row weights inside the container are 1, 1, 2 — total 4 — which
        // matches the (3 + 1) row units of the left and center blocks.
        KeyboardContainer rightCol = new KeyboardContainer(getContext(), theme);
        rightCol.setOnKeyEmitListener(this::emit);
        rightCol.setLayout(NumberLayout.buildRightColumn());
        addView(rightCol, new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f));
    }

    private TextView makePuncCell(final String label) {
        TextView tv = new TextView(getContext());
        tv.setText(label);
        tv.setGravity(Gravity.CENTER);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f);
        tv.setTextColor(theme.keyText);
        tv.setBackground(makeKeySelector(theme.keyBackground, theme.keyBackgroundPressed));
        tv.setClickable(true);
        tv.setFocusable(true);
        tv.setSingleLine(true);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(LEFT_ITEM_HEIGHT_DP));
        int m = dp(3);
        lp.setMargins(m, m, m, m);
        tv.setLayoutParams(lp);
        tv.setOnClickListener(v -> emit(SimeKey.punctuation(label)));
        return tv;
    }
}
