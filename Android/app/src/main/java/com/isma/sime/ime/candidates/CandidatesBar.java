package com.isma.sime.ime.candidates;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
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

    private final SimeTheme theme;

    private LinearLayout idleView;
    private LinearLayout activeView;
    private TextView preeditView;
    private LinearLayout candidateContainer;

    private OnCandidatePickListener pickListener;
    private OnSettingsListener settingsListener;
    private OnHideListener hideListener;

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

        TextView expand = iconButton("∨");
        // TODO Phase 4: expand candidate grid. For now hide-like.
        expand.setOnClickListener(v -> {
            if (hideListener != null) hideListener.onHideClick();
        });
        activeView.addView(expand);

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
            preeditView.setText(formatPreedit(kernel, state));
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

    private String formatPreedit(InputKernel kernel, InputState state) {
        StringBuilder sb = new StringBuilder();
        String committed = state.committedText();
        if (!committed.isEmpty()) {
            sb.append(committed).append(' ');
        }
        String units = kernel != null ? kernel.getTopUnits() : "";
        sb.append(annotateRemaining(state.remaining(),
                units != null ? units : ""));
        return sb.toString();
    }

    /**
     * Zip the raw remaining buffer with the decoder-returned pinyin
     * segmentation so that user-typed '\'' boundaries stay visible while
     * real digits are replaced by their decoded pinyin letters.
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
                android.util.Log.d("SimeBar", "candidate click idx=" + idx
                        + " listener=" + (pickListener != null));
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

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }
}
