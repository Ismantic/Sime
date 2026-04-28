package com.shiyu.sime.ime.candidates;

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

import com.shiyu.sime.ime.InputKernel;
import com.shiyu.sime.ime.InputState;
import com.shiyu.sime.ime.KeyboardMode;
import com.shiyu.sime.ime.PinyinUtil;
import com.shiyu.sime.ime.engine.DecodeResult;
import com.shiyu.sime.ime.feedback.InputFeedbacks;
import com.shiyu.sime.ime.theme.SimeTheme;
import com.shiyu.sime.ime.theme.Typography;

import java.util.List;

/**
 * Candidate bar with two states.
 *
 * <p>Idle: {@code ⚙ ... ∨}. Active: {@code [preedit]  hanzi hanzi ...  ∨}.
 */
public class CandidatesBar extends FrameLayout {

    /**
     * Single listener for all bar events. Default no-op methods so each
     * caller only overrides what it needs.
     */
    public interface Listener {
        default void onCandidatePick(int index) {}
        default void onSettingsClick() {}
        default void onHideClick() {}
        default void onExpandToggle() {}
        default void onDismissPrediction() {}
        default void onSettingsBackClick() {}
        /** English mode: tap on the highlighted "current word" cell —
         *  commits the English buffer literally. */
        default void onEnglishLiteralCommit() {}
    }

    private final SimeTheme theme;

    private LinearLayout idleView;
    private LinearLayout activeView;
    private TextView preeditView;
    private LinearLayout candidateContainer;
    private LockableHorizontalScrollView candidateScroll;
    private boolean expanded = false;

    /** HorizontalScrollView whose horizontal scrolling can be disabled
     *  per state. Children still receive touches (so candidate taps
     *  still register), only the scroll behavior is suppressed. */
    private static class LockableHorizontalScrollView extends HorizontalScrollView {
        boolean scrollLocked = false;
        LockableHorizontalScrollView(Context ctx) { super(ctx); }
        @Override
        public boolean onInterceptTouchEvent(android.view.MotionEvent ev) {
            if (scrollLocked) return false;
            return super.onInterceptTouchEvent(ev);
        }
        @Override
        public boolean onTouchEvent(android.view.MotionEvent ev) {
            if (scrollLocked) return false;
            return super.onTouchEvent(ev);
        }
    }
    private TextView expandToggleButton;
    /** Idle view's leftmost button — flips between ⚙ and ← in settings mode. */
    private TextView idleLeftButton;
    private boolean settingsMode = false;

    private Listener listener = new Listener() {};

    /** When true, the right-edge button shows "×" and dismisses the
     *  prediction strip instead of expanding the candidates grid.
     *  The 10-prediction limit makes the expand affordance redundant. */
    private boolean predicting = false;

    /** Cached count of active candidates from the last render — used to
     *  restore visibility when collapsing the expanded grid (since the
     *  bar hides truncated cells while expanded, and no re-render
     *  happens on the collapse-only state change). */
    private int activeCandidateCount = 0;

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
        InputFeedbacks.wireClick(idleLeftButton, () -> {
            if (settingsMode) listener.onSettingsBackClick();
            else listener.onSettingsClick();
        });
        idleView.addView(idleLeftButton);

        View spacer = new View(getContext());
        LinearLayout.LayoutParams spLp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
        spacer.setLayoutParams(spLp);
        idleView.addView(spacer);

        TextView hide = iconButton("∨");
        InputFeedbacks.wireClick(hide, () -> listener.onHideClick());
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
        preeditView.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.BODY);
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

        candidateScroll = new LockableHorizontalScrollView(getContext());
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
        InputFeedbacks.wireClick(expandToggleButton, () -> {
            if (predicting) listener.onDismissPrediction();
            else listener.onExpandToggle();
        });
        bottomRow.addView(expandToggleButton);

        activeView.addView(bottomRow);

        addView(activeView, new LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
    }

    private TextView iconButton(String glyph) {
        TextView tv = new TextView(getContext());
        tv.setText(glyph);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.TITLE);
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
        boolean englishMode = snap.mode == KeyboardMode.ENGLISH;
        if (stateActive || hasCandidates || hasEnglishInput) {
            showActive();
            if (englishMode) {
                // English mode: collapse into a single row. The typed
                // buffer (if any) becomes the highlighted first cell;
                // prediction strip after commit drops the buffer cell
                // but keeps the same single-row no-first-highlight style
                // for visual consistency. With preedit hidden, the
                // candidate row is the only row — vertically center it
                // instead of pinning to the top.
                preeditView.setVisibility(GONE);
                candidateContainer.setGravity(Gravity.CENTER_VERTICAL);
                populateCandidates(candidates, /*englishStyle=*/true,
                        hasEnglishInput ? snap.englishBuffer : null);
            } else {
                preeditView.setVisibility(VISIBLE);
                preeditView.setText(stateActive ? buildPreedit(snap, state) : "");
                // CN: pin candidates to top so they hug the preedit row.
                candidateContainer.setGravity(Gravity.TOP);
                populateCandidates(candidates, /*englishStyle=*/false, null);
            }
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
        populateCandidates(candidates, false, null);
    }

    /**
     * @param englishStyle when true, render with English-style fonts and
     *     suppress the Chinese first-cell bold-accent highlight. Used for
     *     both active English input (with {@code englishBuffer} set) and
     *     post-commit English prediction (buffer null) so the strip looks
     *     consistent.
     * @param englishBuffer when non-null and {@code englishStyle} is true,
     *     render a "current word" cell at index 0 (filled accent
     *     background) showing the buffer text; subsequent cells render
     *     the {@code candidates} list. Tap on cell 0 fires
     *     {@link Listener#onEnglishLiteralCommit()}; other cells fire
     *     {@link Listener#onCandidatePick} with the candidate index
     *     (offset adjusted).
     */
    private void populateCandidates(List<DecodeResult> candidates,
                                    boolean englishStyle,
                                    String englishBuffer) {
        boolean hasBufferCell = englishStyle && englishBuffer != null
                && !englishBuffer.isEmpty();
        int candCount = candidates == null ? 0 : candidates.size();
        int totalCells = candCount + (hasBufferCell ? 1 : 0);
        this.activeCandidateCount = totalCells;

        // Grow the pool to cover the current candidate count. New
        // entries are appended in pair order (candidate then divider)
        // so the container's child order remains interleaved.
        while (candidatePool.size() < totalCells) {
            TextView tv = new TextView(getContext());
            tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.TITLE);
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
            if (i < totalCells) {
                if (hasBufferCell && i == 0) {
                    // Synthetic "current word" cell: inline text-only
                    // BackgroundColorSpan (~25% alpha accent), matching
                    // the pinyin-segment highlight style in buildPreedit.
                    // Tapping commits the literal buffer.
                    int tint = (theme.accentColor & 0x00FFFFFF) | 0x40000000;
                    SpannableString span = new SpannableString(englishBuffer);
                    span.setSpan(new BackgroundColorSpan(tint),
                            0, span.length(),
                            Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
                    tv.setText(span);
                    tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.TITLE);
                    tv.setTextColor(theme.candidateText);
                    tv.setTypeface(null, Typeface.NORMAL);
                    tv.setPadding(dp(12), dp(3), dp(12), dp(3));
                    tv.setBackground(null);
                    InputFeedbacks.wireClick(tv,
                            () -> listener.onEnglishLiteralCommit());
                } else {
                    int candIdx = hasBufferCell ? (i - 1) : i;
                    final int idx = candIdx;
                    DecodeResult c = candidates.get(candIdx);
                    tv.setText(c.text);
                    tv.setBackground(null);
                    // Reset padding (buffer cell tightened it).
                    tv.setPadding(dp(12), dp(3), dp(12), dp(3));
                    tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.TITLE);
                    if (englishStyle) {
                        // English candidates / predictions: no first-cell
                        // bold-accent highlight (no "selected suggestion"
                        // pattern in English IME convention).
                        tv.setTextColor(theme.candidateText);
                        tv.setTypeface(null, Typeface.NORMAL);
                    } else {
                        if (candIdx == 0) {
                            tv.setTextColor(theme.candidateHighlight);
                            tv.setTypeface(null, Typeface.BOLD);
                        } else {
                            tv.setTextColor(theme.candidateText);
                            tv.setTypeface(null, Typeface.NORMAL);
                        }
                    }
                    InputFeedbacks.wireClick(tv, () -> listener.onCandidatePick(idx));
                }
                tv.setVisibility(VISIBLE);
                // Last visible slot hides its trailing divider so the
                // bar doesn't end with a stray vertical line.
                div.setVisibility(i < totalCells - 1 ? VISIBLE : GONE);
            } else {
                tv.setVisibility(GONE);
                div.setVisibility(GONE);
            }
        }
    }

    public void setListener(Listener l) {
        this.listener = (l != null) ? l : new Listener() {};
    }

    /** Update the right-edge button glyph + behavior based on whether
     *  the bar is showing a prediction strip vs in-flight candidates. */
    public void setPredicting(boolean p) {
        if (this.predicting == p) return;
        this.predicting = p;
        if (expandToggleButton != null) {
            expandToggleButton.setText(p ? "×" : "∨");
        }
    }

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
        this.expanded = expanded;
        // Lock horizontal scroll while expanded — the grid below shows
        // the overflow, so allowing the bar to scroll would just give
        // the user two ways to see the same candidates and confuse the
        // bar/grid split.
        if (candidateScroll != null) {
            candidateScroll.scrollLocked = expanded;
            if (expanded) {
                candidateScroll.scrollTo(0, 0);
                // After layout settles, hide any candidate that's not
                // fully visible (the half-truncated one at the right
                // edge would otherwise also appear as the first cell of
                // the grid → overlap).
                candidateScroll.post(this::hideTruncatedCandidates);
            } else {
                // Restore the cells we hid when expanding. A new
                // render() would do this automatically but the collapse
                // alone doesn't trigger one.
                restoreCandidateVisibility();
            }
        }
        if (expandToggleButton == null) return;
        if (predicting) {
            // Prediction mode keeps the dismiss "×" regardless of any
            // stale expand state.
            expandToggleButton.setText("×");
            expandToggleButton.setVisibility(VISIBLE);
            return;
        }
        expandToggleButton.setText(expanded ? "∧" : "∨");
        expandToggleButton.setVisibility(expanded ? GONE : VISIBLE);
    }

    /**
     * Count how many candidate cells fit in the visible (non-scrolled)
     * portion of the candidate scroll. Used by the expanded view to
     * skip these and only show overflow candidates in the grid.
     */
    /** Re-show all cells that should be active per last render. */
    private void restoreCandidateVisibility() {
        int n = activeCandidateCount;
        for (int i = 0; i < candidatePool.size(); i++) {
            TextView tv = candidatePool.get(i);
            tv.setVisibility(i < n ? VISIBLE : GONE);
            if (i < dividerPool.size()) {
                View div = dividerPool.get(i);
                // Trailing divider on the last visible cell stays GONE.
                div.setVisibility(i < n - 1 ? VISIBLE : GONE);
            }
        }
    }

    /** Hide any candidate cell whose right edge exceeds the visible
     *  scroll width — i.e. the cell is half-truncated or fully
     *  off-screen. Only called while the panel is expanded. */
    private void hideTruncatedCandidates() {
        if (candidateScroll == null || candidateContainer == null) return;
        int scrollWidth = candidateScroll.getWidth();
        if (scrollWidth <= 0) return;
        for (int i = 0; i < candidatePool.size(); i++) {
            TextView tv = candidatePool.get(i);
            if (tv.getVisibility() != VISIBLE) continue;
            int right = tv.getLeft() + tv.getWidth();
            if (right > scrollWidth) {
                tv.setVisibility(GONE);
                if (i < dividerPool.size()) {
                    dividerPool.get(i).setVisibility(GONE);
                }
            }
        }
        // Also hide the trailing divider on the now-last visible cell.
        for (int i = candidatePool.size() - 1; i >= 0; i--) {
            if (candidatePool.get(i).getVisibility() == VISIBLE) {
                if (i < dividerPool.size()) {
                    dividerPool.get(i).setVisibility(GONE);
                }
                break;
            }
        }
    }

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
