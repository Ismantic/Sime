package com.isma.sime.ime.candidates;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.isma.sime.ime.InputKernel;
import com.isma.sime.ime.engine.Candidate;
import com.isma.sime.ime.theme.SimeTheme;

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

    public interface OnCandidatePickListener {
        void onCandidatePick(int index);
    }

    public interface OnPinyinAltPickListener {
        void onPinyinAltPick(int index);
    }

    public interface OnBackspaceListener {
        void onBackspace();
    }

    public interface OnFallbackLetterListener {
        void onFallbackLetter(char letter);
    }

    public interface OnCollapseListener {
        void onCollapse();
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

    private OnCandidatePickListener pickListener;
    private OnPinyinAltPickListener altPickListener;
    private OnBackspaceListener backspaceListener;
    private OnFallbackLetterListener fallbackLetterListener;
    private OnCollapseListener collapseListener;

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

        rightCol.addView(makeControlButton("返回", () -> {
            if (collapseListener != null) collapseListener.onCollapse();
        }));
        rightCol.addView(makeControlButton("∧", () -> {
            int dy = -mainScroll.getHeight() / 2;
            if (dy != 0) mainScroll.smoothScrollBy(0, dy);
        }));
        rightCol.addView(makeControlButton("∨", () -> {
            int dy = mainScroll.getHeight() / 2;
            if (dy != 0) mainScroll.smoothScrollBy(0, dy);
        }));
        rightCol.addView(makeControlButton("⌫", () -> {
            if (backspaceListener != null) backspaceListener.onBackspace();
        }));
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
        tv.setOnClickListener(v -> onClick.run());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        int m = dp(2);
        lp.setMargins(m, m, m, m);
        tv.setLayoutParams(lp);
        return tv;
    }

    public void setOnCandidatePickListener(OnCandidatePickListener l) {
        this.pickListener = l;
    }

    public void setOnPinyinAltPickListener(OnPinyinAltPickListener l) {
        this.altPickListener = l;
    }

    public void setOnBackspaceListener(OnBackspaceListener l) {
        this.backspaceListener = l;
    }

    public void setOnFallbackLetterListener(OnFallbackLetterListener l) {
        this.fallbackLetterListener = l;
    }

    public void setOnCollapseListener(OnCollapseListener l) {
        this.collapseListener = l;
    }

    /**
     * @param gridStartIndex number of leading candidates to skip in
     *        the grid (already visible in the bar above). Pass
     *        {@code CandidatesBar.getVisibleCandidateCount()} for a
     *        dynamic value so bar and grid never show duplicates.
     */
    public void render(List<Candidate> candidates,
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
        leftStrip.removeAllViews();
        java.util.HashSet<String> shown = new java.util.HashSet<>();
        // Pinyin alts first
        for (int i = 0; i < alts.size(); i++) {
            final int idx = i;
            String label = alts.get(i).letters;
            leftStrip.addView(makeLeftItem(label, () -> {
                if (altPickListener != null) altPickListener.onPinyinAltPick(idx);
            }));
            shown.add(label);
        }
        // Then fallback letters for the first digit (e.g. p/q/r/s for
        // T9 digit 7) — same convention as the T9 keyboard's left strip.
        for (int i = 0; i < fallbackLetters.length(); i++) {
            final char ch = fallbackLetters.charAt(i);
            String label = String.valueOf(ch);
            if (shown.contains(label)) continue;
            leftStrip.addView(makeLeftItem(label, () -> {
                if (fallbackLetterListener != null) fallbackLetterListener.onFallbackLetter(ch);
            }));
            shown.add(label);
        }
    }

    private TextView makeLeftItem(String label, Runnable onClick) {
        TextView tv = new TextView(getContext());
        tv.setText(label);
        tv.setGravity(Gravity.CENTER);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f);
        // Pinyin alts: function-style (darker gray) rectangle cells,
        // visually distinct from the white hanzi cells on the right.
        // Matches the reference IME layout.
        tv.setTextColor(theme.keyTextFunction);
        tv.setBackground(makeFunctionCellBg());
        tv.setClickable(true);
        tv.setFocusable(true);
        tv.setSingleLine(true);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(LEFT_ITEM_HEIGHT_DP));
        int m = dp(3);
        lp.setMargins(m, m, m, m);
        tv.setLayoutParams(lp);
        tv.setOnClickListener(v -> onClick.run());
        return tv;
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
    private void renderGrid(List<Candidate> candidates, int startIdx) {
        grid.removeAllViews();
        int n = candidates == null ? 0 : candidates.size();
        startIdx = Math.min(startIdx, n);
        if (startIdx >= n) return;

        LinearLayout currentRow = null;
        int colsRemaining = 0;

        for (int i = startIdx; i < n; i++) {
            Candidate c = candidates.get(i);
            int span = colSpanFor(c.text);
            if (currentRow == null || colsRemaining < span) {
                if (currentRow != null) {
                    fillRemaining(currentRow, colsRemaining);
                    grid.addView(currentRow);
                }
                currentRow = newRow();
                colsRemaining = GRID_COLS;
            }
            currentRow.addView(makeCandidateCell(c.text, i, span, false));
            colsRemaining -= span;
        }
        if (currentRow != null) {
            fillRemaining(currentRow, colsRemaining);
            grid.addView(currentRow);
        }
    }

    private static int colSpanFor(String text) {
        int chars = text.codePointCount(0, text.length());
        if (chars <= 2) return 1;
        if (chars <= 5) return 2;
        return GRID_COLS;
    }

    private void fillRemaining(LinearLayout row, int cols) {
        // Add one filler View per remaining column with the SAME
        // margins as a real cell, otherwise the cells in a partially-
        // filled row would compute slightly wider than cells in fully
        // packed rows (different sum_of_margins → different
        // weight-distributed widths) and the user would notice the
        // mismatch (e.g. row 5's "送" cell appearing wider than the
        // cells above it).
        for (int i = 0; i < cols; i++) {
            android.view.View filler = new android.view.View(getContext());
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.MATCH_PARENT, 1);
            lp.setMargins(dp(2), dp(2), dp(2), dp(2));
            filler.setLayoutParams(lp);
            row.addView(filler);
        }
    }

    private LinearLayout newRow() {
        LinearLayout row = new LinearLayout(getContext());
        row.setOrientation(LinearLayout.HORIZONTAL);
        // Fixed row height so the grid is naturally scrollable when
        // candidates exceed the viewport. 5 of these fit a 240dp
        // keyboard area; extras scroll.
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(GRID_ROW_HEIGHT_DP));
        row.setLayoutParams(lp);
        return row;
    }

    private TextView makeCandidateCell(String text, int idx, int span, boolean highlight) {
        TextView tv = new TextView(getContext());
        tv.setText(text);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f);
        tv.setGravity(Gravity.CENTER);
        if (highlight) {
            tv.setTextColor(theme.candidateHighlight);
            tv.setTypeface(null, Typeface.BOLD);
        } else {
            tv.setTextColor(theme.candidateText);
        }
        tv.setPadding(dp(8), 0, dp(8), 0);
        tv.setBackground(makeCellBg());
        tv.setClickable(true);
        tv.setFocusable(true);
        // Fill the row height (MATCH_PARENT) for consistent cell sizing.
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, span);
        lp.setMargins(dp(2), dp(2), dp(2), dp(2));
        tv.setLayoutParams(lp);
        tv.setOnClickListener(v -> {
            if (pickListener != null) pickListener.onCandidatePick(idx);
        });
        return tv;
    }


    private StateListDrawable makeCellBg() {
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_pressed},
                roundedRect(theme.keyBackgroundPressed));
        sl.addState(new int[]{}, roundedRect(theme.keyBackground));
        return sl;
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
