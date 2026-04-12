package com.isma.sime.ime.keyboard;

import android.content.Context;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.isma.sime.ime.InputKernel;
import com.isma.sime.ime.keyboard.framework.KeyView;
import com.isma.sime.ime.keyboard.framework.KeyboardContainer;
import com.isma.sime.ime.keyboard.layouts.T9Layout;

import java.util.List;

/**
 * T9 (九宫格) keyboard. Three vertical blocks side by side:
 *
 * <ul>
 *   <li><b>Left block</b> — dynamic strip on top (pinyin alts /
 *       fallback letters / idle punctuation) plus a 符号 cell at the
 *       bottom.
 *   <li><b>Center block</b> — 3×3 digit grid on top, plus a bottom row
 *       (123 | 空格 | 中\n英) sharing the digit grid's width.
 *   <li><b>Right block</b> — ⌫ / 重输 / 换行 with row weights 1, 1, 2
 *       so 换行 is a tall key that extends from row 3 down through the
 *       bottom-row area.
 * </ul>
 *
 * <p>Two keys are dual-state ({@code @# ↔ 分词}, {@code 换行 ↔ 确定});
 * the controller overrides their per-key click listener and label
 * depending on whether the kernel currently has input.
 */
public class T9KeyboardView extends KeyboardView {

    private static final String[] T9_LETTERS = {
            "", "", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"
    };

    /**
     * The pinyin letters mapped to a single T9 digit key. Empty string
     * for non-T9 chars (digits 0/1, separators, anything else). Single
     * source of truth for the T9 keymap.
     */
    public static String lettersForDigit(char digit) {
        int idx = digit - '0';
        if (idx < 0 || idx >= T9_LETTERS.length) return "";
        return T9_LETTERS[idx];
    }

    private static final String[] IDLE_PUNC = {
            "，", "。", "？", "！", "：", "、", "…", "～"
    };

    /** Max items in the left strip (pinyin alts + fallback letters). */
    private static final int MAX_LEFT_ITEMS = 12;
    private static final int LEFT_ITEM_HEIGHT_DP = 38;

    private LinearLayout leftStrip;
    private ScrollView leftScroll;
    private KeyboardContainer mainGrid;
    private KeyboardContainer rightCol;

    private boolean active = false;
    private List<InputKernel.PinyinAlt> pinyinAlts = java.util.Collections.emptyList();
    private String firstDigitLetters = "";

    public interface OnLeftStripListener {
        void onPinyinAltPick(int index);
        void onFallbackLetter(char letter);
    }

    private OnLeftStripListener leftListener;

    public T9KeyboardView(Context context) {
        super(context);
        // Override the base class's VERTICAL orientation — this view's
        // top-level layout is three columns side by side.
        setOrientation(HORIZONTAL);
        build();
    }

    public void setOnLeftStripListener(OnLeftStripListener l) {
        this.leftListener = l;
    }

    public void setActive(boolean active,
                           List<InputKernel.PinyinAlt> alts,
                           String firstDigitLetters) {
        this.active = active;
        this.pinyinAlts = alts != null ? alts
                : java.util.Collections.<InputKernel.PinyinAlt>emptyList();
        this.firstDigitLetters = firstDigitLetters != null ? firstDigitLetters : "";
        refreshDualStateKeys();
        populateLeftStrip();
    }

    private void build() {
        // ===== Left block: dynamic strip on top (3f) + 符号 (1f) =====
        LinearLayout leftBlock = new LinearLayout(getContext());
        leftBlock.setOrientation(VERTICAL);
        addView(leftBlock, new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f));

        leftScroll = new ScrollView(getContext());
        leftScroll.setVerticalScrollBarEnabled(false);
        leftScroll.setFillViewport(true);
        leftBlock.addView(leftScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 3f));

        leftStrip = new LinearLayout(getContext());
        leftStrip.setOrientation(VERTICAL);
        leftScroll.addView(leftStrip, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        KeyboardContainer fuhao = new KeyboardContainer(getContext(), theme);
        fuhao.setOnKeyEmitListener(this::emit);
        fuhao.setLayout(T9Layout.buildFuhaoCell());
        leftBlock.addView(fuhao, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        // ===== Center block: 3×3 digit grid (3f) + center bottom row (1f) =====
        LinearLayout centerBlock = new LinearLayout(getContext());
        centerBlock.setOrientation(VERTICAL);
        addView(centerBlock, new LayoutParams(0, LayoutParams.MATCH_PARENT, 3f));

        mainGrid = new KeyboardContainer(getContext(), theme);
        mainGrid.setOnKeyEmitListener(this::emit);
        mainGrid.setLayout(T9Layout.buildMainGrid());
        centerBlock.addView(mainGrid, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 3f));

        KeyboardContainer centerBottom = new KeyboardContainer(getContext(), theme);
        centerBottom.setOnKeyEmitListener(this::emit);
        centerBottom.setLayout(T9Layout.buildCenterBottomRow());
        centerBlock.addView(centerBottom, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        // ===== Right block: ⌫ / 重输 / 换行(2 rows tall) =====
        rightCol = new KeyboardContainer(getContext(), theme);
        rightCol.setOnKeyEmitListener(this::emit);
        rightCol.setLayout(T9Layout.buildRightColumn());
        addView(rightCol, new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f));

        installDualStateHandlers();
        refreshDualStateKeys();
        populateLeftStrip();
    }

    /**
     * Override the per-key listener for the {@code @# / 分词} key so it
     * dispatches a different SimeKey depending on {@link #active}. The
     * 换行 key uses the static clickAction (always {@link SimeKey#enter()})
     * — only its label flips between 换行 / 确定.
     */
    private void installDualStateHandlers() {
        KeyView topLeft = mainGrid.findKeyById(T9Layout.ID_TOP_LEFT);
        if (topLeft != null) {
            topLeft.setListener((def, action) -> {
                if (action != KeyView.KeyAction.CLICK) return;
                if (active) {
                    emit(SimeKey.separator());
                } else {
                    // Idle: open the T9 "1 key" punctuation picker in
                    // the candidate bar (matches phone IME convention).
                    emit(SimeKey.numPunctuation());
                }
            });
        }
    }

    private void refreshDualStateKeys() {
        if (mainGrid == null) return;
        KeyView topLeft = mainGrid.findKeyById(T9Layout.ID_TOP_LEFT);
        if (topLeft != null) topLeft.setLabel(active ? "分词" : "@#");
        KeyView enter = rightCol.findKeyById(T9Layout.ID_ENTER);
        if (enter != null) enter.setLabel(active ? "确定" : "换行");
    }

    private void populateLeftStrip() {
        leftStrip.removeAllViews();
        if (!active) {
            for (int i = 0; i < IDLE_PUNC.length; i++) {
                final String p = IDLE_PUNC[i];
                leftStrip.addView(makeLeftItem(p, false,
                        () -> emit(SimeKey.punctuation(p))));
            }
            return;
        }
        // Active: single-syllable pinyin alts first, then fallback letters
        // for the first digit. Track shown labels so the fallback loop
        // doesn't duplicate single-character pinyin alternatives.
        java.util.HashSet<String> shown = new java.util.HashSet<>();
        int added = 0;
        for (int i = 0; i < pinyinAlts.size() && added < MAX_LEFT_ITEMS; i++) {
            final int idx = i;
            String label = pinyinAlts.get(i).letters;
            leftStrip.addView(makeLeftItem(label, true, () -> {
                if (leftListener != null) leftListener.onPinyinAltPick(idx);
            }));
            shown.add(label);
            added++;
        }
        for (int i = 0; i < firstDigitLetters.length() && added < MAX_LEFT_ITEMS; i++) {
            final char ch = firstDigitLetters.charAt(i);
            String label = String.valueOf(ch);
            if (shown.contains(label)) continue;
            leftStrip.addView(makeLeftItem(label, false, () -> {
                if (leftListener != null) leftListener.onFallbackLetter(ch);
            }));
            shown.add(label);
            added++;
        }
        if (added == 0) {
            // No alts and no letters — fall back to punctuation so the
            // user isn't left with an empty strip.
            for (int i = 0; i < IDLE_PUNC.length; i++) {
                final String p = IDLE_PUNC[i];
                leftStrip.addView(makeLeftItem(p, false,
                        () -> emit(SimeKey.punctuation(p))));
            }
        }
    }

    private TextView makeLeftItem(String label, boolean highlight, Runnable onClick) {
        TextView tv = new TextView(getContext());
        tv.setText(label);
        tv.setGravity(Gravity.CENTER);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f);
        tv.setTextColor(highlight ? theme.accentColor : theme.keyText);
        tv.setBackground(makeKeySelector(theme.keyBackground, theme.keyBackgroundPressed));
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
}
