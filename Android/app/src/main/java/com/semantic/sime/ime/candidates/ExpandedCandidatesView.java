package com.semantic.sime.ime.candidates;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.semantic.sime.ime.InputKernel;
import com.semantic.sime.ime.engine.DecodeResult;
import com.semantic.sime.ime.feedback.InputFeedbacks;
import com.semantic.sime.ime.keyboard.StripHelper;
import com.semantic.sime.ime.theme.SimeTheme;

import java.util.Collections;
import java.util.List;

/**
 * Full-page candidate panel shown in place of the keyboard when the
 * user taps {@code ∨} on the candidates bar. Three columns side by side:
 *
 * <ul>
 *   <li><b>Left strip</b> — pinyin alternatives + T9 fallback letters
 *       (hidden in Qwerty mode when empty).
 *   <li><b>Main grid</b> — hanzi candidates in a scrollable 4-column
 *       grid; paging via the right column's ∧/∨ buttons.
 *   <li><b>Right control column</b> — 返回 (collapse), ∧ (page up),
 *       ∨ (page down), ⌫ (backspace).
 * </ul>
 */
public class ExpandedCandidatesView extends LinearLayout {

    /** Single listener for all panel events with no-op defaults. */
    public interface Listener {
        default void onCandidatePick(int index) {}
        default void onPinyinAltPick(int index) {}
        default void onBackspace() {}
        default void onFallbackLetter(char letter) {}
        default void onCollapse() {}
    }

    private static final int LEFT_ITEM_HEIGHT_DP = 42;

    /**
     * Each row in the grid is fixed at this height so that 5 rows fit
     * inside the typical 240dp keyboard viewport. Extra rows scroll
     * vertically.
     */
    private static final int GRID_ROW_HEIGHT_DP = 46;

    private final SimeTheme theme;

    private ScrollView leftScroll;
    private LinearLayout leftStrip;
    private ScrollView mainScroll;
    private LinearLayout grid;

    private Listener listener = new Listener() {};

    // View pools — grow on demand, never shrink.
    private final java.util.List<TextView> gridCellPool = new java.util.ArrayList<>();
    private final java.util.List<android.view.View> gridFillerPool = new java.util.ArrayList<>();
    private final java.util.List<LinearLayout> rowPool = new java.util.ArrayList<>();
    private final java.util.List<android.view.View> leftItemPool = new java.util.ArrayList<>();
    private int cellsUsed, fillersUsed, rowsUsed, leftItemsUsed;
    private StateListDrawable sharedCellBg;
    private StateListDrawable sharedFunctionBg;

    public ExpandedCandidatesView(Context ctx) {
        super(ctx);
        theme = SimeTheme.fromContext(ctx);
        setOrientation(HORIZONTAL);
        setBackgroundColor(theme.keyboardBackground);
        setPadding(dp(4), dp(6), dp(4), dp(6));

        // ===== Left strip: pinyin alts (vertical, narrow column) =====
        // Hidden when there are no alts (e.g. Qwerty mode), letting the
        // grid take the full width.
        leftScroll = new ScrollView(ctx);
        leftScroll.setVerticalScrollBarEnabled(false);
        leftScroll.setFillViewport(true);
        addView(leftScroll, new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f));

        leftStrip = new LinearLayout(ctx);
        leftStrip.setOrientation(VERTICAL);
        leftScroll.addView(leftStrip, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        // ===== Middle: scrollable hanzi grid =====
        mainScroll = new ScrollView(ctx);
        mainScroll.setVerticalScrollBarEnabled(false);
        mainScroll.setFillViewport(true);
        addView(mainScroll, new LayoutParams(0, LayoutParams.MATCH_PARENT, 5f));

        grid = new LinearLayout(ctx);
        grid.setOrientation(VERTICAL);
        grid.setPadding(dp(4), 0, dp(4), 0);
        mainScroll.addView(grid, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        // ===== Right control column: 返回 / 上 / 下 / ⌫ =====
        // Vertical strip of 4 function buttons (each 1/4 of column
        // height). 返回 collapses the panel; 上/下 page-scroll the
        // grid; ⌫ deletes one char from the buffer.
        LinearLayout rightCol = new LinearLayout(ctx);
        rightCol.setOrientation(VERTICAL);
        addView(rightCol, new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f));

        rightCol.addView(makeControlButton("返回", () -> listener.onCollapse()));
        rightCol.addView(makeControlButton("∧", () -> {
            int dy = -mainScroll.getHeight() / 2;
            if (dy != 0) mainScroll.smoothScrollBy(0, dy);
        }));
        rightCol.addView(makeControlButton("∨", () -> {
            int dy = mainScroll.getHeight() / 2;
            if (dy != 0) mainScroll.smoothScrollBy(0, dy);
        }));
        rightCol.addView(makeControlButton("⌫", () -> listener.onBackspace()));
    }

    private TextView makeControlButton(String label, Runnable onClick) {
        TextView tv = new TextView(getContext());
        tv.setText(label);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f);
        tv.setGravity(Gravity.CENTER);
        tv.setTextColor(theme.keyTextFunction);
        tv.setBackground(makeFunctionCellBg());
        tv.setClickable(true);
        tv.setFocusable(true);
        InputFeedbacks.wireClick(tv, onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        int m = dp(2);
        lp.setMargins(m, m, m, m);
        tv.setLayoutParams(lp);
        return tv;
    }

    public void setListener(Listener l) {
        this.listener = (l != null) ? l : new Listener() {};
    }

    /**
     * @param gridStartIndex number of leading candidates to skip in
     *        the grid (already visible in the bar above). Pass
     *        {@code CandidatesBar.getVisibleCandidateCount()} for a
     *        dynamic value so bar and grid never show duplicates.
     */
    public void render(List<DecodeResult> candidates,
                        List<InputKernel.PinyinAlt> alts,
                        String fallbackLetters,
                        int gridStartIndex) {
        List<InputKernel.PinyinAlt> safeAlts =
                alts != null ? alts : Collections.<InputKernel.PinyinAlt>emptyList();
        String safeFallback = fallbackLetters != null ? fallbackLetters : "";
        leftScroll.setVisibility(
                (safeAlts.isEmpty() && safeFallback.isEmpty()) ? GONE : VISIBLE);
        renderLeftStrip(safeAlts, safeFallback);
        renderGrid(candidates, gridStartIndex);
    }

    private void renderLeftStrip(List<InputKernel.PinyinAlt> alts, String fallbackLetters) {
        leftItemsUsed = 0;
        java.util.HashSet<String> shown = new java.util.HashSet<>();
        for (int i = 0; i < alts.size(); i++) {
            final int idx = i;
            String label = alts.get(i).letters;
            android.view.View item = getOrCreateLeftItem(label,
                    () -> listener.onPinyinAltPick(idx));
            item.setVisibility(VISIBLE);
            shown.add(label);
        }
        for (int i = 0; i < fallbackLetters.length(); i++) {
            final char ch = fallbackLetters.charAt(i);
            String label = String.valueOf(ch);
            if (shown.contains(label)) continue;
            android.view.View item = getOrCreateLeftItem(label,
                    () -> listener.onFallbackLetter(ch));
            item.setVisibility(VISIBLE);
            shown.add(label);
        }
        // Hide unused pool items.
        for (int i = leftItemsUsed; i < leftItemPool.size(); i++) {
            leftItemPool.get(i).setVisibility(GONE);
        }
    }

    private android.view.View getOrCreateLeftItem(String label, Runnable onClick) {
        android.view.View item;
        if (leftItemsUsed < leftItemPool.size()) {
            item = leftItemPool.get(leftItemsUsed);
            if (item instanceof TextView) {
                ((TextView) item).setText(label);
            }
            InputFeedbacks.wireClick(item, onClick);
        } else {
            item = StripHelper.makeStripCell(
                    getContext(), theme, label, true, onClick, LEFT_ITEM_HEIGHT_DP);
            leftItemPool.add(item);
            leftStrip.addView(item);
        }
        leftItemsUsed++;
        return item;
    }

    private StateListDrawable makeFunctionCellBg() {
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_pressed},
                roundedRect(theme.functionKeyBackgroundPressed));
        sl.addState(new int[]{}, roundedRect(theme.functionKeyBackground));
        return sl;
    }

    /** Virtual column count for the candidate grid. */
    private static final int GRID_COLS = 4;

    /**
     * Lay out candidates into a {@link #GRID_COLS}-column grid with
     * fixed row height, skipping the first {@code startIdx} entries
     * (already visible in the candidates bar above). Each cell's
     * column span depends on its char length: 1-2 chars → 1 col,
     * 3-5 → 2 cols, 6+ → 4 cols. Rows past the visible viewport
     * scroll vertically; the right column's ∧/∨ buttons handle paging.
     */
    private void renderGrid(List<DecodeResult> candidates, int startIdx) {
        int n = candidates == null ? 0 : candidates.size();
        startIdx = Math.min(startIdx, n);

        cellsUsed = 0;
        fillersUsed = 0;
        rowsUsed = 0;

        int colsRemaining = 0;
        LinearLayout currentRow = null;

        for (int i = startIdx; i < n; i++) {
            DecodeResult c = candidates.get(i);
            int span = colSpanFor(c.text);
            if (currentRow == null || colsRemaining < span) {
                if (currentRow != null) {
                    fillRemainingPooled(currentRow, colsRemaining);
                }
                currentRow = getOrCreateRow();
                currentRow.removeAllViews();
                colsRemaining = GRID_COLS;
            }
            currentRow.addView(getOrCreateCell(c.text, i, span));
            colsRemaining -= span;
        }
        if (currentRow != null) {
            fillRemainingPooled(currentRow, colsRemaining);
        }
        // Hide unused rows.
        for (int i = rowsUsed; i < rowPool.size(); i++) {
            rowPool.get(i).setVisibility(GONE);
        }
    }

    private LinearLayout getOrCreateRow() {
        LinearLayout row;
        if (rowsUsed < rowPool.size()) {
            row = rowPool.get(rowsUsed);
            row.setVisibility(VISIBLE);
        } else {
            row = new LinearLayout(getContext());
            row.setOrientation(LinearLayout.HORIZONTAL);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(GRID_ROW_HEIGHT_DP));
            row.setLayoutParams(lp);
            rowPool.add(row);
            grid.addView(row);
        }
        rowsUsed++;
        return row;
    }

    private TextView getOrCreateCell(String text, int idx, int span) {
        TextView tv;
        if (cellsUsed < gridCellPool.size()) {
            tv = gridCellPool.get(cellsUsed);
            // Detach from old parent if needed.
            if (tv.getParent() != null) {
                ((android.view.ViewGroup) tv.getParent()).removeView(tv);
            }
        } else {
            tv = new TextView(getContext());
            tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f);
            tv.setGravity(Gravity.CENTER);
            tv.setTextColor(theme.candidateText);
            tv.setPadding(dp(8), 0, dp(8), 0);
            tv.setBackground(getCellBg());
            tv.setClickable(true);
            tv.setFocusable(true);
            gridCellPool.add(tv);
        }
        tv.setText(text);
        LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) tv.getLayoutParams();
        if (lp == null || lp.weight != span) {
            lp = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.MATCH_PARENT, span);
            lp.setMargins(dp(2), dp(2), dp(2), dp(2));
            tv.setLayoutParams(lp);
        }
        InputFeedbacks.wireClick(tv, () -> listener.onCandidatePick(idx));
        cellsUsed++;
        return tv;
    }

    private static int colSpanFor(String text) {
        int chars = text.codePointCount(0, text.length());
        if (chars <= 2) return 1;
        if (chars <= 5) return 2;
        return GRID_COLS;
    }

    private void fillRemainingPooled(LinearLayout row, int cols) {
        for (int i = 0; i < cols; i++) {
            android.view.View filler;
            if (fillersUsed < gridFillerPool.size()) {
                filler = gridFillerPool.get(fillersUsed);
                if (filler.getParent() != null) {
                    ((android.view.ViewGroup) filler.getParent()).removeView(filler);
                }
            } else {
                filler = new android.view.View(getContext());
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                        0, LinearLayout.LayoutParams.MATCH_PARENT, 1);
                lp.setMargins(dp(2), dp(2), dp(2), dp(2));
                filler.setLayoutParams(lp);
                gridFillerPool.add(filler);
            }
            row.addView(filler);
            fillersUsed++;
        }
    }

    private StateListDrawable getCellBg() {
        if (sharedCellBg == null) {
            sharedCellBg = new StateListDrawable();
            sharedCellBg.addState(new int[]{android.R.attr.state_pressed},
                    roundedRect(theme.keyBackgroundPressed));
            sharedCellBg.addState(new int[]{}, roundedRect(theme.keyBackground));
        }
        return (StateListDrawable) sharedCellBg.getConstantState().newDrawable();
    }

    private GradientDrawable roundedRect(int color) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.RECTANGLE);
        d.setCornerRadius(dp(8));
        d.setColor(color);
        return d;
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }
}
