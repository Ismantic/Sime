package com.shiyu.sime.ime.keyboard;

import com.shiyu.sime.ime.theme.Typography;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.shiyu.sime.ime.data.EmojiStore;
import com.shiyu.sime.ime.feedback.InputFeedbacks;

import java.util.List;

/**
 * Emoji picker panel — categorized grid. Header has horizontally
 * scrollable category tabs (one tab per emoji group), body has a
 * vertically scrollable grid of emojis for the active category.
 *
 * <p>Tap on an emoji commits it to the host app via
 * {@link OnPickListener}; the panel stays open so the user can pick
 * multiple emojis. ← on the candidates bar exits.
 */
public class EmojiPanelView extends KeyboardView {

    public interface OnPickListener {
        void onPick(String emoji);
    }

    private static final int COLS = 8;

    private final EmojiStore store;
    private OnPickListener pickListener;
    private LinearLayout gridContainer;
    private LinearLayout tabRow;
    private int activeIndex = 0;

    public EmojiPanelView(Context context, EmojiStore store) {
        super(context);
        this.store = store;
        build();
    }

    public void setOnPickListener(OnPickListener l) {
        this.pickListener = l;
    }

    private void build() {
        setPadding(0, 0, 0, 0);

        // ===== Tab row =====
        HorizontalScrollView tabScroll = new HorizontalScrollView(getContext());
        tabScroll.setHorizontalScrollBarEnabled(false);
        addView(tabScroll, new LayoutParams(
                LayoutParams.MATCH_PARENT, dp(40)));

        tabRow = new LinearLayout(getContext());
        tabRow.setOrientation(LinearLayout.HORIZONTAL);
        tabRow.setPadding(dp(8), dp(4), dp(8), dp(4));
        tabScroll.addView(tabRow, new HorizontalScrollView.LayoutParams(
                LayoutParams.WRAP_CONTENT, LayoutParams.MATCH_PARENT));

        renderTabs();

        // ===== Grid body =====
        ScrollView gridScroll = new ScrollView(getContext());
        LayoutParams scrollLp = new LayoutParams(
                LayoutParams.MATCH_PARENT, 0);
        scrollLp.weight = 1f;
        addView(gridScroll, scrollLp);

        gridContainer = new LinearLayout(getContext());
        gridContainer.setOrientation(LinearLayout.VERTICAL);
        gridContainer.setPadding(dp(4), dp(2), dp(4), dp(8));
        gridScroll.addView(gridContainer, new ScrollView.LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));

        renderGrid();
    }

    private void renderTabs() {
        tabRow.removeAllViews();
        List<String> labels = store.labels();
        for (int i = 0; i < labels.size(); i++) {
            final int idx = i;
            String label = labels.get(i);
            TextView tab = new TextView(getContext());
            tab.setText(label);
            tab.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.SMALL);
            tab.setGravity(Gravity.CENTER);
            tab.setPadding(dp(12), dp(4), dp(12), dp(4));
            boolean active = (i == activeIndex);
            if (active) {
                tab.setBackground(roundedRect(theme.keyBackground, dp(14)));
                tab.setTextColor(theme.keyText);
            } else {
                tab.setTextColor(theme.hintLabelColor);
                tab.setBackgroundColor(Color.TRANSPARENT);
                InputFeedbacks.wireClick(tab, () -> {
                    activeIndex = idx;
                    renderTabs();
                    renderGrid();
                });
            }
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    LayoutParams.WRAP_CONTENT, LayoutParams.MATCH_PARENT);
            lp.rightMargin = dp(4);
            tab.setLayoutParams(lp);
            tabRow.addView(tab);
        }
    }

    private void renderGrid() {
        gridContainer.removeAllViews();
        List<String> emojis = store.emojis(activeIndex);
        if (emojis.isEmpty()) {
            TextView empty = new TextView(getContext());
            empty.setText("无表情");
            empty.setTextColor(theme.hintLabelColor);
            empty.setGravity(Gravity.CENTER);
            empty.setPadding(dp(12), dp(20), dp(12), dp(20));
            gridContainer.addView(empty);
            return;
        }
        LinearLayout row = null;
        for (int i = 0; i < emojis.size(); i++) {
            if (i % COLS == 0) {
                row = new LinearLayout(getContext());
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setLayoutParams(new LayoutParams(
                        LayoutParams.MATCH_PARENT, dp(40)));
                gridContainer.addView(row);
            }
            row.addView(buildCell(emojis.get(i)));
        }
        // Pad last row to keep cells aligned (so the final row's emojis
        // don't stretch full-width).
        if (row != null) {
            int filled = emojis.size() % COLS;
            if (filled != 0) {
                for (int j = filled; j < COLS; j++) {
                    View pad = new View(getContext());
                    pad.setLayoutParams(new LinearLayout.LayoutParams(
                            0, LayoutParams.MATCH_PARENT, 1f));
                    row.addView(pad);
                }
            }
        }
    }

    private TextView buildCell(String emoji) {
        TextView tv = new TextView(getContext());
        tv.setText(emoji);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.DISPLAY);
        tv.setGravity(Gravity.CENTER);
        tv.setBackground(makeCellBg());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                0, LayoutParams.MATCH_PARENT, 1f);
        tv.setLayoutParams(lp);
        InputFeedbacks.wireClick(tv, () -> {
            if (pickListener != null) pickListener.onPick(emoji);
        });
        return tv;
    }

    private StateListDrawable makeCellBg() {
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_pressed},
                roundedRect(theme.keyBackgroundPressed, dp(6)));
        sl.addState(new int[]{},
                roundedRect(Color.TRANSPARENT, dp(6)));
        return sl;
    }

    private GradientDrawable roundedRect(int color, int radiusPx) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.RECTANGLE);
        d.setCornerRadius(radiusPx);
        d.setColor(color);
        return d;
    }
}
