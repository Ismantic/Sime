package com.isma.sime.ime.candidates;

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

import com.isma.sime.ime.InputKernel;
import com.isma.sime.ime.InputState;
import com.isma.sime.ime.engine.Candidate;
import com.isma.sime.ime.theme.SimeTheme;

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

    private final SimeTheme theme;

    private LinearLayout idleView;
    private LinearLayout activeView;
    private TextView preeditView;
    private LinearLayout candidateContainer;
    private TextView expandToggleButton;
    private boolean expanded = false;

    private OnCandidatePickListener pickListener;
    private OnSettingsListener settingsListener;
    private OnHideListener hideListener;
    private OnExpandToggleListener expandToggleListener;

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

        TextView settings = iconButton("⚙");
        settings.setOnClickListener(v -> {
            if (settingsListener != null) settingsListener.onSettingsClick();
        });
        idleView.addView(settings);

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
        activeView = new LinearLayout(getContext());
        activeView.setOrientation(LinearLayout.HORIZONTAL);
        activeView.setGravity(Gravity.CENTER_VERTICAL);
        activeView.setPadding(dp(8), 0, dp(8), 0);

        preeditView = new TextView(getContext());
        preeditView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f);
        preeditView.setTextColor(theme.preeditText);
        preeditView.setSingleLine(true);
        preeditView.setPadding(dp(4), 0, dp(8), 0);
        LinearLayout.LayoutParams pLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        preeditView.setLayoutParams(pLp);
        activeView.addView(preeditView);

        HorizontalScrollView scroll = new HorizontalScrollView(getContext());
        scroll.setHorizontalScrollBarEnabled(false);
        LinearLayout.LayoutParams sLp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
        scroll.setLayoutParams(sLp);
        candidateContainer = new LinearLayout(getContext());
        candidateContainer.setOrientation(LinearLayout.HORIZONTAL);
        candidateContainer.setGravity(Gravity.CENTER_VERTICAL);
        scroll.addView(candidateContainer, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT));
        activeView.addView(scroll);

        expandToggleButton = iconButton("∨");
        expandToggleButton.setOnClickListener(v -> {
            if (expandToggleListener != null) expandToggleListener.onExpandToggle();
        });
        activeView.addView(expandToggleButton);

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

    public void render(InputKernel kernel, InputState state, List<Candidate> candidates) {
        boolean active = (state != null) && !state.isEmpty();
        if (active) {
            showActive();
            preeditView.setText(buildPreedit(kernel, state));
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
    private CharSequence buildPreedit(InputKernel kernel, InputState state) {
        StringBuilder sb = new StringBuilder();
        String committed = state.committedText();
        if (!committed.isEmpty()) {
            sb.append(committed).append(' ');
        }
        int annotStart = sb.length();
        String units = kernel != null ? kernel.getTopUnits() : "";
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
            bufferRealChars = countNonSeparator(
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
     * while real digits are replaced by their decoded pinyin letters.
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
                sb.append(u);
                ri++;
                ui++;
            }
        }
        while (ri < rem.length()) sb.append(rem.charAt(ri++));
        while (ui < units.length()) sb.append(units.charAt(ui++));
        return sb.toString();
    }

    private static int countNonSeparator(String s) {
        int n = 0;
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) != '\'') n++;
        }
        return n;
    }

    private void populateCandidates(List<Candidate> candidates) {
        candidateContainer.removeAllViews();
        if (candidates == null) return;
        int n = candidates.size();
        for (int i = 0; i < n; i++) {
            final int idx = i;
            Candidate c = candidates.get(i);
            TextView tv = new TextView(getContext());
            tv.setText(c.text);
            tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f);
            tv.setGravity(Gravity.CENTER);
            tv.setPadding(dp(12), dp(8), dp(12), dp(8));
            tv.setClickable(true);
            tv.setFocusable(true);
            if (i == 0) {
                tv.setTextColor(theme.candidateHighlight);
                tv.setTypeface(null, Typeface.BOLD);
            } else {
                tv.setTextColor(theme.candidateText);
            }
            tv.setOnClickListener(v -> {
                if (pickListener != null) pickListener.onCandidatePick(idx);
            });
            candidateContainer.addView(tv);

            if (i < n - 1) {
                View divider = new View(getContext());
                divider.setBackgroundColor(Color.argb(40,
                        Color.red(theme.dividerColor),
                        Color.green(theme.dividerColor),
                        Color.blue(theme.dividerColor)));
                LinearLayout.LayoutParams dLp =
                        new LinearLayout.LayoutParams(dp(1), dp(22));
                dLp.gravity = Gravity.CENTER_VERTICAL;
                candidateContainer.addView(divider, dLp);
            }
        }
    }

    public void setOnCandidatePickListener(OnCandidatePickListener l) { this.pickListener = l; }
    public void setOnSettingsListener(OnSettingsListener l)           { this.settingsListener = l; }
    public void setOnHideListener(OnHideListener l)                   { this.hideListener = l; }
    public void setOnExpandToggleListener(OnExpandToggleListener l)   { this.expandToggleListener = l; }

    /** Update the toggle glyph to reflect the current expanded state. */
    public void setExpanded(boolean expanded) {
        this.expanded = expanded;
        if (expandToggleButton != null) {
            expandToggleButton.setText(expanded ? "∧" : "∨");
        }
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }
}
