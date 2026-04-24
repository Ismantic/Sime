package com.semantic.sime.ime.candidates;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.style.BackgroundColorSpan;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.semantic.sime.ime.InputKernel;
import com.semantic.sime.ime.InputState;
import com.semantic.sime.ime.KeyboardMode;
import com.semantic.sime.ime.PinyinUtil;
import com.semantic.sime.ime.engine.DecodeResult;
import com.semantic.sime.ime.theme.SimeTheme;

import java.util.List;

/**
 * Candidate bar with two states.
 *
 * <p>Idle: {@code ⚙ ... ∨}. Active: {@code [preedit]  hanzi hanzi ...  ∨}.
 */
public class CandidatesBar extends FrameLayout {

    public interface OnCandidatePickListener { void onCandidatePick(int index); }
    public interface OnSettingsListener       { void onSettingsClick(); }
    public interface OnHideListener           { void onHideClick(); }
    public interface OnExpandToggleListener   { void onExpandToggle(); }
    public interface OnSettingsBackListener   { void onSettingsBackClick(); }

    private final SimeTheme theme;

    private LinearLayout idleView;
    private LinearLayout activeView;
    private TextView preeditView;
    private LinearLayout candidateContainer;
    private HorizontalScrollView candidateScroll;
    private TextView expandToggleButton;
    /** Idle view's leftmost button — flips between ⚙ and ← in settings mode. */
    private TextView idleLeftButton;
    private boolean settingsMode = false;

    private OnCandidatePickListener pickListener;
    private OnSettingsListener settingsListener;
    private OnHideListener hideListener;
    private OnExpandToggleListener expandToggleListener;
    private OnSettingsBackListener settingsBackListener;

    public CandidatesBar(Context context) {
        super(context);
        theme = SimeTheme.fromContext(context);
        init();
    }

    public CandidatesBar(Context context, AttributeSet attrs) {
        super(context, attrs);
        theme = SimeTheme.fromContext(context);
        init();
    }

    private void init() {
        setBackgroundColor(theme.barBackground);
        buildIdle();
        buildActive();
        showIdle();
    }

    private void buildIdle() {
        idleView = new LinearLayout(getContext());
        idleView.setOrientation(LinearLayout.HORIZONTAL);
        idleView.setGravity(Gravity.CENTER_VERTICAL);
        idleView.setPadding(dp(12), 0, dp(12), 0);

        idleLeftButton = iconButton("⚙");
        idleLeftButton.setOnClickListener(v -> {
            if (settingsMode) {
                if (settingsBackListener != null) settingsBackListener.onSettingsBackClick();
            } else {
                if (settingsListener != null) settingsListener.onSettingsClick();
            }
        });
        idleView.addView(idleLeftButton);

        View spacer = new View(getContext());
        LinearLayout.LayoutParams spLp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
        spacer.setLayoutParams(spLp);
        idleView.addView(spacer);

        TextView hide = iconButton("∨");
        hide.setOnClickListener(v -> {
            if (hideListener != null) hideListener.onHideClick();
        });
        idleView.addView(hide);

        addView(idleView, new LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
    }

    private void buildActive() {
        // Two-row stack: a slim preedit row on top of the candidates
        // row. This way long pinyin no longer pushes the hanzi off the
        // visible bar — they live on independent rows.
        activeView = new LinearLayout(getContext());
        activeView.setOrientation(LinearLayout.VERTICAL);
        activeView.setPadding(dp(8), 0, dp(8), 0);

        // ===== Top row: preedit pinyin (left-aligned, small) =====
        preeditView = new TextView(getContext());
        preeditView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f);
        preeditView.setTextColor(theme.preeditText);
        preeditView.setSingleLine(true);
        preeditView.setEllipsize(android.text.TextUtils.TruncateAt.END);
        preeditView.setPadding(dp(4), dp(2), dp(8), 0);
        LinearLayout.LayoutParams pLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        preeditView.setLayoutParams(pLp);
        activeView.addView(preeditView);

        // ===== Bottom row: candidates scroll + expand toggle =====
        // Gravity END so the toggle button stays glued to the right
        // edge even when the scroll is hidden in expanded mode (where
        // the toggle is the only remaining child).
        LinearLayout bottomRow = new LinearLayout(getContext());
        bottomRow.setOrientation(LinearLayout.HORIZONTAL);
        bottomRow.setGravity(Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams brLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        bottomRow.setLayoutParams(brLp);

        candidateScroll = new HorizontalScrollView(getContext());
        candidateScroll.setHorizontalScrollBarEnabled(false);
        LinearLayout.LayoutParams sLp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
        candidateScroll.setLayoutParams(sLp);
        candidateContainer = new LinearLayout(getContext());
        candidateContainer.setOrientation(LinearLayout.HORIZONTAL);
        // Pin candidates to the top of the bottom row so they hug the
        // preedit instead of vertically centering with a gap below it.
        candidateContainer.setGravity(Gravity.TOP);
        candidateScroll.addView(candidateContainer, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT));
        bottomRow.addView(candidateScroll);

        expandToggleButton = iconButton("∨");
        expandToggleButton.setOnClickListener(v -> {
            if (expandToggleListener != null) expandToggleListener.onExpandToggle();
        });
        bottomRow.addView(expandToggleButton);

        activeView.addView(bottomRow);

        addView(activeView, new LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
    }

    private TextView iconButton(String glyph) {
        TextView tv = new TextView(getContext());
        tv.setText(glyph);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f);
        tv.setTextColor(theme.barForeground);
        tv.setGravity(Gravity.CENTER);
        tv.setPadding(dp(10), 0, dp(10), 0);
        tv.setClickable(true);
        tv.setFocusable(true);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                dp(40), LinearLayout.LayoutParams.MATCH_PARENT);
        tv.setLayoutParams(lp);
        return tv;
    }

    public void render(InputKernel.Snapshot snap) {
        InputState state = snap.state;
        List<DecodeResult> candidates = snap.candidates;
        boolean stateActive = (state != null) && !state.isEmpty();
        boolean hasCandidates = (candidates != null) && !candidates.isEmpty();
        boolean hasEnglishInput = !snap.englishBuffer.isEmpty();
        if (stateActive || hasCandidates || hasEnglishInput) {
            showActive();
            if (hasEnglishInput) {
                preeditView.setText(snap.englishBuffer);
            } else {
                preeditView.setText(stateActive ? buildPreedit(snap, state) : "");
            }
            populateCandidates(candidates);
        } else {
            showIdle();
        }
    }

    private void showIdle() {
        idleView.setVisibility(VISIBLE);
        activeView.setVisibility(GONE);
    }

    private void showActive() {
        idleView.setVisibility(GONE);
        activeView.setVisibility(VISIBLE);
    }

    /**
     * Build the preedit display: committed hanzi (from selections) +
     * space + annotated remaining buffer. The "confirmed" prefix —
     * committed hanzi plus the buffer's already-picked pinyin region
     * (bufferLetters) — is wrapped in a {@link BackgroundColorSpan} so
     * the user can see what's been locked in vs what's still tentative.
     */
    private CharSequence buildPreedit(InputKernel.Snapshot snap, InputState state) {
        StringBuilder sb = new StringBuilder();
        String committed = state.committedText();
        if (!committed.isEmpty()) {
            sb.append(committed).append(' ');
        }
        int annotStart = sb.length();
        String units = snap.topUnits != null ? snap.topUnits : "";
        sb.append(annotateRemaining(state.remaining(),
                units != null ? units : ""));

        // Compute the highlight end index in the assembled string.
        // Two cases:
        //   - bufferLetters region is non-empty (user picked pinyin alts):
        //     extend the highlight from 0 through the committed hanzi,
        //     through the separator space, into the annotated portion
        //     until we've covered as many real chars as bufferLetters has,
        //     plus a trailing `'` if the next char is one.
        //   - bufferLetters is empty but committed is non-empty (only
        //     hanzi picks): highlight just the committed hanzi (no
        //     trailing space — avoids an isolated highlighted gap).
        int sel = state.selectedLength();
        int lettersEnd = state.lettersEnd;
        int bufferRealChars = 0;
        if (lettersEnd > sel) {
            bufferRealChars = PinyinUtil.countRealChars(
                    state.buffer.substring(sel, lettersEnd));
        }

        int hiEnd = 0;
        if (bufferRealChars > 0) {
            int realSeen = 0;
            int idx = annotStart;
            while (idx < sb.length() && realSeen < bufferRealChars) {
                if (sb.charAt(idx) != '\'') realSeen++;
                idx++;
            }
            // Pull a trailing '\'' into the highlight so the boundary
            // marker visually belongs to the locked-in region.
            if (idx < sb.length() && sb.charAt(idx) == '\'') idx++;
            hiEnd = idx;
        } else if (!committed.isEmpty()) {
            hiEnd = committed.length();
        }

        SpannableString out = new SpannableString(sb.toString());
        if (hiEnd > 0) {
            int bg = (theme.accentColor & 0x00FFFFFF) | 0x40000000;  // ~25% alpha
            out.setSpan(new BackgroundColorSpan(bg),
                    0, hiEnd, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        return out;
    }

    /**
     * Zip the raw remaining buffer with the decoder-returned pinyin
     * segmentation so that user-typed {@code '} boundaries stay visible
     * while T9 digits are replaced by their decoded pinyin letters.
     *
     * <p>For T9 input ({@code r} is a digit) the visible char comes
     * from {@code units} so the user sees pinyin instead of digits.
     * For QWERTY ({@code r} is already a letter) the user's typed
     * letter is preserved verbatim — units is only consulted for
     * separator insertions. This avoids cases like typing "hi" in
     * Qwerty getting rendered as "he" because the decoder happened
     * to return units = "he" for an unparseable input.
     *
     * <p>If {@code units} covers fewer real chars than {@code rem}
     * (legitimate partial decode, e.g. T9 input "7457" → top candidate
     * units "pi" only covers digits 7+4), the zip walks as far as
     * units allows and then falls through to append the leftover raw
     * chars verbatim. So "7457" with units "pi" renders as "pi57",
     * making it visually obvious which prefix the engine matched and
     * which suffix was unprocessable.
     */
    private static String annotateRemaining(String rem, String units) {
        if (units.isEmpty()) return rem;
        StringBuilder sb = new StringBuilder(rem.length() + units.length());
        int ri = 0;
        int ui = 0;
        while (ri < rem.length() && ui < units.length()) {
            char r = rem.charAt(ri);
            char u = units.charAt(ui);
            if (u == '\'' && r == '\'') {
                sb.append('\'');
                ri++;
                ui++;
            } else if (u == '\'') {
                sb.append('\'');
                ui++;
            } else if (r == '\'') {
                sb.append('\'');
                ri++;
            } else {
                // T9: r is a digit, u is the decoded letter — show u.
                // Qwerty: r is already a letter — preserve r as typed.
                sb.append(Character.isDigit(r) ? u : r);
                ri++;
                ui++;
            }
        }
        while (ri < rem.length()) sb.append(rem.charAt(ri++));
        while (ui < units.length()) sb.append(units.charAt(ui++));
        return sb.toString();
    }

    /**
     * Recycled view slots — each slot is a (candidate TextView, divider
     * View) pair appended in interleaved order so {@code candidateContainer}'s
     * child sequence stays {@code [cand0, div0, cand1, div1, …]}.
     * Shrinking just hides extra slots; growing appends new pairs at
     * the end. Avoids allocating ~20 views on every keystroke.
     */
    private final java.util.List<TextView> candidatePool = new java.util.ArrayList<>();
    private final java.util.List<View> dividerPool = new java.util.ArrayList<>();

    private void populateCandidates(List<DecodeResult> candidates) {
        int n = candidates == null ? 0 : candidates.size();

        // Grow the pool to cover the current candidate count. New
        // entries are appended in pair order (candidate then divider)
        // so the container's child order remains interleaved.
        while (candidatePool.size() < n) {
            TextView tv = new TextView(getContext());
            tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f);
            tv.setGravity(Gravity.CENTER);
            tv.setPadding(dp(12), dp(3), dp(12), dp(3));
            tv.setClickable(true);
            tv.setFocusable(true);
            candidateContainer.addView(tv);
            candidatePool.add(tv);

            View divider = new View(getContext());
            divider.setBackgroundColor(Color.argb(40,
                    Color.red(theme.dividerColor),
                    Color.green(theme.dividerColor),
                    Color.blue(theme.dividerColor)));
            LinearLayout.LayoutParams dLp =
                    new LinearLayout.LayoutParams(dp(1), dp(22));
            dLp.gravity = Gravity.CENTER_VERTICAL;
            candidateContainer.addView(divider, dLp);
            dividerPool.add(divider);
        }

        // Bind visible slots; hide extras from previous longer renders.
        for (int i = 0; i < candidatePool.size(); i++) {
            TextView tv = candidatePool.get(i);
            View div = dividerPool.get(i);
            if (i < n) {
                final int idx = i;
                DecodeResult c = candidates.get(i);
                tv.setText(c.text);
                if (i == 0) {
                    tv.setTextColor(theme.candidateHighlight);
                    tv.setTypeface(null, Typeface.BOLD);
                } else {
                    tv.setTextColor(theme.candidateText);
                    tv.setTypeface(null, Typeface.NORMAL);
                }
                tv.setOnClickListener(v -> {
                    if (pickListener != null) pickListener.onCandidatePick(idx);
                });
                tv.setVisibility(VISIBLE);
                // Last visible slot hides its trailing divider so the
                // bar doesn't end with a stray vertical line.
                div.setVisibility(i < n - 1 ? VISIBLE : GONE);
            } else {
                tv.setVisibility(GONE);
                div.setVisibility(GONE);
            }
        }
    }

    public void setOnCandidatePickListener(OnCandidatePickListener l) { this.pickListener = l; }
    public void setOnSettingsListener(OnSettingsListener l)           { this.settingsListener = l; }
    public void setOnHideListener(OnHideListener l)                   { this.hideListener = l; }
    public void setOnExpandToggleListener(OnExpandToggleListener l)   { this.expandToggleListener = l; }
    public void setOnSettingsBackListener(OnSettingsBackListener l)   { this.settingsBackListener = l; }

    /**
     * Toggle the bar between normal mode (left icon = ⚙ → opens
     * settings) and settings mode (left icon = ← → pops one settings
     * layer / exits). Forces idle layout because the active candidate
     * view is meaningless inside settings.
     */
    public void setSettingsMode(boolean inSettings) {
        if (this.settingsMode == inSettings) return;
        this.settingsMode = inSettings;
        if (idleLeftButton != null) {
            idleLeftButton.setText(inSettings ? "←" : "⚙");
        }
    }

    /**
     * Reflect the expanded/collapsed state. The bar keeps showing
     * preedit + hanzi candidates in both states — the user can quick-
     * pick from the bar while also browsing the full grid below.
     * The toggle button (∨) is hidden when expanded because the
     * ExpandedCandidatesView's right column has its own 返回 button.
     */
    public void setExpanded(boolean expanded) {
        if (expandToggleButton != null) {
            expandToggleButton.setText(expanded ? "∧" : "∨");
            expandToggleButton.setVisibility(expanded ? GONE : VISIBLE);
        }
    }

    /**
     * Count how many candidate cells fit in the visible (non-scrolled)
     * portion of the candidate scroll. Used by the expanded view to
     * skip these and only show overflow candidates in the grid.
     */
    public int getVisibleCandidateCount() {
        if (candidateScroll == null || candidateContainer == null) return 0;
        int scrollWidth = candidateScroll.getWidth();
        if (scrollWidth <= 0) return 0;
        int count = 0;
        int consumed = 0;
        for (int i = 0; i < candidatePool.size(); i++) {
            TextView tv = candidatePool.get(i);
            if (tv.getVisibility() != VISIBLE) break;
            consumed += tv.getWidth();
            if (consumed > scrollWidth) break;
            count++;
            if (i < dividerPool.size()) {
                View div = dividerPool.get(i);
                if (div.getVisibility() == VISIBLE) consumed += div.getWidth();
            }
        }
        return count;
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }
}
