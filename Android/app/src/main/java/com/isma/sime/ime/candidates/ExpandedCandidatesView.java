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

    private static final int LEFT_ITEM_HEIGHT_DP = 42;

    private final SimeTheme theme;

    private ScrollView leftScroll;
    private LinearLayout leftStrip;
    private ScrollView mainScroll;
    private LinearLayout grid;

    private OnCandidatePickListener pickListener;
    private OnPinyinAltPickListener altPickListener;

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

        // ===== Main grid: hanzi candidates (greedy row pack inside scroll) =====
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
    }

    public void setOnCandidatePickListener(OnCandidatePickListener l) {
        this.pickListener = l;
    }

    public void setOnPinyinAltPickListener(OnPinyinAltPickListener l) {
        this.altPickListener = l;
    }

    public void render(List<Candidate> candidates,
                        List<InputKernel.PinyinAlt> alts) {
        List<InputKernel.PinyinAlt> safeAlts =
                alts != null ? alts : Collections.<InputKernel.PinyinAlt>emptyList();
        // Hide the left strip entirely when there are no pinyin alts
        // (Qwerty mode) so the grid spans the full width instead of
        // wasting 1/6 on a blank column.
        leftScroll.setVisibility(safeAlts.isEmpty() ? GONE : VISIBLE);
        renderLeftStrip(safeAlts);
        renderGrid(candidates, !safeAlts.isEmpty());
    }

    private void renderLeftStrip(List<InputKernel.PinyinAlt> alts) {
        leftStrip.removeAllViews();
        for (int i = 0; i < alts.size(); i++) {
            final int idx = i;
            String label = alts.get(i).letters;
            leftStrip.addView(makeLeftItem(label, () -> {
                if (altPickListener != null) altPickListener.onPinyinAltPick(idx);
            }));
        }
    }

    private TextView makeLeftItem(String label, Runnable onClick) {
        TextView tv = new TextView(getContext());
        tv.setText(label);
        tv.setGravity(Gravity.CENTER);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f);
        // Pinyin alts use the darker function-key palette so they're
        // visually distinct from the white hanzi cells on the right.
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

    private void renderGrid(List<Candidate> candidates, boolean leftStripVisible) {
        grid.removeAllViews();
        if (candidates == null || candidates.isEmpty()) return;

        int screenWidth = getResources().getDisplayMetrics().widthPixels;
        // The grid occupies the full width when the left strip is
        // hidden, otherwise the right 5/6 of it.
        int availableWidth = (leftStripVisible
                ? screenWidth * 5 / 6
                : screenWidth) - dp(20);

        LinearLayout currentRow = newRow();
        int currentRowWidth = 0;

        for (int i = 0; i < candidates.size(); i++) {
            final int idx = i;
            Candidate c = candidates.get(i);
            int cellWidth = estimateCellWidth(c.text);

            if (currentRowWidth + cellWidth > availableWidth
                    && currentRow.getChildCount() > 0) {
                grid.addView(currentRow);
                currentRow = newRow();
                currentRowWidth = 0;
            }
            currentRow.addView(makeCandidateCell(c.text, idx, i == 0));
            currentRowWidth += cellWidth;
        }
        if (currentRow.getChildCount() > 0) {
            grid.addView(currentRow);
        }
    }

    private LinearLayout newRow() {
        LinearLayout row = new LinearLayout(getContext());
        row.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, dp(2), 0, dp(2));
        row.setLayoutParams(lp);
        return row;
    }

    private TextView makeCandidateCell(String text, int idx, boolean highlight) {
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
        tv.setPadding(dp(14), dp(10), dp(14), dp(10));
        tv.setBackground(makeCellBg());
        tv.setClickable(true);
        tv.setFocusable(true);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(2), 0, dp(2), 0);
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

    /**
     * Rough cell width estimate for greedy row packing. Hanzi at 18sp
     * are roughly 22dp wide; latin chars closer to 11dp. Pick a
     * conservative value (one width per char) plus padding/margin so
     * we err on the side of fewer cells per row.
     */
    private int estimateCellWidth(String text) {
        int chars = text.codePointCount(0, text.length());
        return chars * dp(22) + dp(32);
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }
}
