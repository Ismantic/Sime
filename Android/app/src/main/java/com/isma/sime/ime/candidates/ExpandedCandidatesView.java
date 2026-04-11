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

import com.isma.sime.ime.engine.Candidate;
import com.isma.sime.ime.theme.SimeTheme;

import java.util.List;

/**
 * Full-page candidate grid shown in place of the keyboard when the user
 * taps the {@code ∨} (expand) glyph on the candidates bar. Candidates
 * are laid out greedily into rows: each row keeps adding cells until the
 * estimated row width exceeds the available width, then wraps. The
 * outer {@link ScrollView} handles vertical overflow.
 *
 * <p>Click semantics match the bar: a tap fires the
 * {@link OnCandidatePickListener}, which the host {@code InputView}
 * forwards to {@code InputKernel.onHanziCandidatePick(idx)}.
 */
public class ExpandedCandidatesView extends ScrollView {

    public interface OnCandidatePickListener {
        void onCandidatePick(int index);
    }

    private final SimeTheme theme;
    private final LinearLayout root;
    private OnCandidatePickListener pickListener;

    public ExpandedCandidatesView(Context ctx) {
        super(ctx);
        theme = SimeTheme.fromContext(ctx);
        setBackgroundColor(theme.keyboardBackground);
        setVerticalScrollBarEnabled(false);
        setFillViewport(true);

        root = new LinearLayout(ctx);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(6), dp(6), dp(6), dp(6));
        addView(root, new LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
    }

    public void setOnCandidatePickListener(OnCandidatePickListener l) {
        this.pickListener = l;
    }

    public void render(List<Candidate> candidates) {
        root.removeAllViews();
        if (candidates == null || candidates.isEmpty()) return;

        int screenWidth = getResources().getDisplayMetrics().widthPixels;
        int availableWidth = screenWidth - dp(12);  // root padding

        LinearLayout currentRow = newRow();
        int currentRowWidth = 0;

        for (int i = 0; i < candidates.size(); i++) {
            final int idx = i;
            Candidate c = candidates.get(i);
            int cellWidth = estimateCellWidth(c.text);

            if (currentRowWidth + cellWidth > availableWidth
                    && currentRow.getChildCount() > 0) {
                root.addView(currentRow);
                currentRow = newRow();
                currentRowWidth = 0;
            }
            currentRow.addView(makeCandidateCell(c.text, idx, i == 0));
            currentRowWidth += cellWidth;
        }
        if (currentRow.getChildCount() > 0) {
            root.addView(currentRow);
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
