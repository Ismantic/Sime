package com.isma.sime.ime.candidates;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.FrameLayout;
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
 * user taps {@code ∨} on the candidates bar. Two regions side by side:
 *
 * <ul>
 *   <li><b>Left strip</b> — pinyin alternatives for the next syllable
 *       position (same data as the T9 keyboard's left strip). Tapping
 *       one fires {@link OnPinyinAltPickListener}.
 *   <li><b>Main grid</b> — hanzi candidates packed greedily into rows.
 *       Tapping one fires {@link OnCandidatePickListener}.
 * </ul>
 *
 * <p>The preedit pinyin display lives in the {@code CandidatesBar} above
 * — this view is purely the body. Together with the bar, the user sees
 * preedit + pinyin alts + hanzi candidates simultaneously.
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

    private static final int LEFT_ITEM_HEIGHT_DP = 42;

    /**
     * Each row in the grid is fixed at this height so that 5 rows fit
     * inside the typical 240dp keyboard viewport. Extra rows scroll
     * vertically.
     */
    private static final int GRID_ROW_HEIGHT_DP = 46;
    /** Grid horizontal padding (one side). Mirrors {@code grid.setPadding}. */
    private static final int GRID_PADDING_DP = 4;
    /** Per-side cell horizontal margin set in {@link #makeCandidateCell}. */
    private static final int CELL_MARGIN_DP = 2;

    private final SimeTheme theme;

    private ScrollView leftScroll;
    private LinearLayout leftStrip;
    private ScrollView mainScroll;
    private LinearLayout grid;

    private OnCandidatePickListener pickListener;
    private OnPinyinAltPickListener altPickListener;
    private OnBackspaceListener backspaceListener;
    private OnFallbackLetterListener fallbackLetterListener;

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

        // Main area: scrollable grid + floating backspace overlay.
        // FrameLayout pins the backspace at the viewport's bottom-right
        // corner regardless of the user's scroll position. The grid
        // itself renders ALL candidates with rows of fixed height; the
        // first 5 rows fit, anything beyond scrolls. The backspace
        // overlaps the candidate that happens to sit at the bottom-right
        // of the viewport — pulling the grid up exposes that candidate.
        FrameLayout mainArea = new FrameLayout(ctx);
        addView(mainArea, new LayoutParams(0, LayoutParams.MATCH_PARENT, 5f));

        mainScroll = new ScrollView(ctx);
        mainScroll.setVerticalScrollBarEnabled(false);
        mainScroll.setFillViewport(true);
        mainArea.addView(mainScroll, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        grid = new LinearLayout(ctx);
        grid.setOrientation(VERTICAL);
        grid.setPadding(dp(4), 0, dp(4), 0);
        mainScroll.addView(grid, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        final TextView backspace = new TextView(ctx);
        backspace.setText("⌫");
        backspace.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f);
        backspace.setGravity(Gravity.CENTER);
        backspace.setTextColor(theme.keyTextFunction);
        backspace.setBackground(makeFunctionCellBg());
        backspace.setClickable(true);
        backspace.setFocusable(true);
        backspace.setOnClickListener(v -> {
            if (backspaceListener != null) backspaceListener.onBackspace();
        });
        FrameLayout.LayoutParams blp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT);
        blp.gravity = Gravity.BOTTOM | Gravity.RIGHT;
        mainArea.addView(backspace, blp);

        // Resize the backspace to exactly one grid cell whenever
        // mainArea's width changes (e.g., when the left strip toggles).
        // Width / margins mirror the grid's cell sizing rules so the
        // overlay sits perfectly on top of the bottom-right cell.
        final int cellHeight = dp(GRID_ROW_HEIGHT_DP) - dp(CELL_MARGIN_DP * 2);
        mainArea.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, orr, ob) -> {
            int width = r - l;
            if (width <= 0 || (width == orr - ol)) return;
            int cellWidth =
                    (width - dp(GRID_PADDING_DP * 2)) / GRID_COLS - dp(CELL_MARGIN_DP * 2);
            if (cellWidth <= 0) return;
            FrameLayout.LayoutParams lp =
                    (FrameLayout.LayoutParams) backspace.getLayoutParams();
            if (lp.width == cellWidth && lp.height == cellHeight) return;
            lp.width = cellWidth;
            lp.height = cellHeight;
            // Right margin = grid right padding + cell right margin
            // so the overlay's right edge aligns with the cell's
            // visual edge. Bottom margin = 0: with 5 rows × 46dp =
            // 230dp grid extending slightly past the ~228dp mainArea,
            // the last row's cell visual bottom (row_bottom - cell
            // margin) coincides exactly with mainArea bottom, so the
            // overlay (anchored to mainArea bottom) needs no extra
            // bottom margin.
            lp.setMargins(0, 0,
                    dp(GRID_PADDING_DP + CELL_MARGIN_DP),
                    0);
            backspace.setLayoutParams(lp);
        });
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

    public void render(List<Candidate> candidates,
                        List<InputKernel.PinyinAlt> alts,
                        String fallbackLetters) {
        List<InputKernel.PinyinAlt> safeAlts =
                alts != null ? alts : Collections.<InputKernel.PinyinAlt>emptyList();
        String safeFallback = fallbackLetters != null ? fallbackLetters : "";
        // Hide the entire left strip when there's neither alts nor
        // fallback letters to show — typically Qwerty mode where the
        // strip would otherwise be a blank gray column.
        leftScroll.setVisibility(
                (safeAlts.isEmpty() && safeFallback.isEmpty()) ? GONE : VISIBLE);
        renderLeftStrip(safeAlts, safeFallback);
        renderGrid(candidates);
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
     * Lay out ALL candidates into a {@link #GRID_COLS}-column grid with
     * fixed row height. Each cell's column span depends on its char
     * length: 1-2 chars → 1 col, 3-5 → 2 cols, 6+ → 4 cols. Rows past
     * the visible viewport scroll vertically. The backspace lives in
     * the {@link FrameLayout} overlay above this grid, not in the cell
     * flow — it visually overlaps whichever cell happens to sit at the
     * viewport's bottom-right corner.
     */
    private void renderGrid(List<Candidate> candidates) {
        grid.removeAllViews();
        int n = candidates == null ? 0 : candidates.size();
        if (n == 0) return;

        LinearLayout currentRow = null;
        int colsRemaining = 0;

        for (int i = 0; i < n; i++) {
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
            currentRow.addView(makeCandidateCell(c.text, i, span, i == 0));
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
        // Fill the row height (MATCH_PARENT) so the cell and the
        // floating backspace overlay end up the same visual size.
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
