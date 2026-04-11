package com.isma.sime.ime.keyboard;

import android.content.Context;
import android.graphics.Typeface;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.isma.sime.ime.InputKernel;

import java.util.List;

/**
 * T9 (九宫格) layout.
 *
 * <pre>
 *   ,   分词/@#   ABC(2)  DEF(3)    ⌫
 *   。  GHI(4)    JKL(5)  MNO(6)    重输
 *   ?   PQRS(7)   TUV(8)  WXYZ(9)   确定/⏎
 *   !   符        123     空格      中/英
 * </pre>
 *
 * The leftmost 4-cell column is dual-state:
 * <ul>
 *   <li>idle (no input): shows {@code , 。 ? !} shortcut punctuation.</li>
 *   <li>active (has input): shows pinyin alternatives and T9 letters for
 *       the first remaining digit.</li>
 * </ul>
 *
 * The {@code 分词/@#} key in the top row is also dual-state:
 * idle → {@code @#} (commits {@code @} as a punctuation shortcut for now),
 * active → {@code 分词} (emits separator). Similarly the right-column
 * third key is {@code ⏎ / 确定}.
 */
public class T9KeyboardView extends KeyboardView {

    private static final String[] T9_LETTERS = {
            "", "", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"
    };

    /**
     * The pinyin letters mapped to a single T9 digit key. Empty string
     * for non-T9 chars (digits 0/1, separators, anything else). Single
     * source of truth for the T9 keymap — callers like
     * {@code InputView.firstDigitLetters} reuse this instead of
     * hand-rolling another switch.
     */
    public static String lettersForDigit(char digit) {
        int idx = digit - '0';
        if (idx < 0 || idx >= T9_LETTERS.length) return "";
        return T9_LETTERS[idx];
    }

    private static final String[] IDLE_PUNC = {"，", "。", "？", "！"};

    /** Max items in the left strip (pinyin alts + fallback letters). */
    private static final int MAX_LEFT_ITEMS = 12;
    private static final int LEFT_ITEM_HEIGHT_DP = 42;

    private LinearLayout leftStrip;
    private ScrollView leftScroll;
    private TextView topLeftKey;    // 分词 / @#
    private TextView enterKey;      // 确定 / ⏎

    private boolean active = false;
    private List<InputKernel.PinyinAlt> pinyinAlts = java.util.Collections.emptyList();
    private String firstDigitLetters = "";

    /** Listener for pinyin alt / fallback letter picks. */
    public interface OnLeftStripListener {
        void onPinyinAltPick(int index);
        void onFallbackLetter(char letter);
    }

    private OnLeftStripListener leftListener;

    public T9KeyboardView(Context context) {
        super(context);
        build();
    }

    public void setOnLeftStripListener(OnLeftStripListener l) {
        this.leftListener = l;
    }

    /** Called by the service on each state change. */
    public void setActive(boolean active,
                           List<InputKernel.PinyinAlt> alts,
                           String firstDigitLetters) {
        this.active = active;
        this.pinyinAlts = alts != null ? alts : java.util.Collections.<InputKernel.PinyinAlt>emptyList();
        this.firstDigitLetters = firstDigitLetters != null ? firstDigitLetters : "";
        refreshDualStateKeys();
        populateLeftStrip();
    }

    private void build() {
        LinearLayout top = new LinearLayout(getContext());
        top.setOrientation(HORIZONTAL);
        LayoutParams topLp = new LayoutParams(
                LayoutParams.MATCH_PARENT, 0, 4f);
        top.setLayoutParams(topLp);

        leftScroll = new ScrollView(getContext());
        LayoutParams lsLp = new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f);
        leftScroll.setLayoutParams(lsLp);
        leftScroll.setVerticalScrollBarEnabled(false);
        leftScroll.setFillViewport(true);
        top.addView(leftScroll);

        leftStrip = new LinearLayout(getContext());
        leftStrip.setOrientation(VERTICAL);
        leftStrip.setLayoutParams(new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));
        leftScroll.addView(leftStrip);

        LinearLayout mainGrid = new LinearLayout(getContext());
        mainGrid.setOrientation(VERTICAL);
        LayoutParams mgLp = new LayoutParams(0, LayoutParams.MATCH_PARENT, 3f);
        mainGrid.setLayoutParams(mgLp);
        top.addView(mainGrid);

        LinearLayout rightCol = new LinearLayout(getContext());
        rightCol.setOrientation(VERTICAL);
        LayoutParams rcLp = new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f);
        rightCol.setLayoutParams(rcLp);
        top.addView(rightCol);

        // --- main grid ---
        LinearLayout r1 = makeRow();
        topLeftKey = makeKey("@#", 1f, 14f, true, this::onTopLeftKey);
        r1.addView(topLeftKey);
        r1.addView(makeT9DigitKey("2", "ABC"));
        r1.addView(makeT9DigitKey("3", "DEF"));
        mainGrid.addView(r1);

        LinearLayout r2 = makeRow();
        r2.addView(makeT9DigitKey("4", "GHI"));
        r2.addView(makeT9DigitKey("5", "JKL"));
        r2.addView(makeT9DigitKey("6", "MNO"));
        mainGrid.addView(r2);

        LinearLayout r3 = makeRow();
        r3.addView(makeT9DigitKey("7", "PQRS"));
        r3.addView(makeT9DigitKey("8", "TUV"));
        r3.addView(makeT9DigitKey("9", "WXYZ"));
        mainGrid.addView(r3);

        // --- right column (vertical; needs vertical-style LPs) ---
        rightCol.addView(makeVerticalKey("⌫", 1f, 18f, true,
                () -> emit(SimeKey.backspace())));
        rightCol.addView(makeVerticalKey("重输", 1f, 14f, true,
                () -> emit(SimeKey.clear())));
        enterKey = makeVerticalKey("⏎", 1f, 18f, true, this::onEnterKey);
        rightCol.addView(enterKey);

        addView(top);

        // --- bottom row ---
        LinearLayout bottom = makeRow();
        LayoutParams blp = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f);
        bottom.setLayoutParams(blp);
        bottom.addView(makeKey("符", 1f, 14f, true, () -> emit(SimeKey.toSymbol())));
        bottom.addView(makeKey("123", 1f, 14f, true, () -> emit(SimeKey.toNumber())));
        bottom.addView(makeKey("空格", 2f, 14f, true, () -> emit(SimeKey.space())));
        bottom.addView(makeKey("中/英", 1f, 14f, true, () -> emit(SimeKey.toggleLang())));
        addView(bottom);

        refreshDualStateKeys();
        populateLeftStrip();
    }

    private TextView makeT9DigitKey(String digit, String letters) {
        TextView tv = new TextView(getContext());
        tv.setText(digit + "\n" + letters);
        tv.setGravity(Gravity.CENTER);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f);
        tv.setTextColor(theme.keyText);
        tv.setBackground(makeKeySelector(theme.keyBackground, theme.keyBackgroundPressed));
        tv.setClickable(true);
        tv.setFocusable(true);
        LayoutParams lp = new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f);
        int m = dp(3);
        lp.setMargins(m, m, m, m);
        tv.setLayoutParams(lp);
        final char c = digit.charAt(0);
        tv.setOnClickListener(v -> emit(SimeKey.digit(c)));
        return tv;
    }

    private void onTopLeftKey() {
        if (active) {
            emit(SimeKey.separator());
        } else {
            // Idle: commit '@' as a quick shortcut (full symbol picker TBD).
            emit(SimeKey.punctuation("@"));
        }
    }

    private void onEnterKey() {
        emit(SimeKey.enter());
    }

    private void refreshDualStateKeys() {
        if (topLeftKey != null) {
            topLeftKey.setText(active ? "分词" : "@#");
        }
        if (enterKey != null) {
            enterKey.setText(active ? "确定" : "⏎");
        }
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
        // Active: single-syllable pinyin alts first (up to ~8), then
        // fallback letters for the first digit as a last resort.
        // Track which alt letters have already been added so the fallback
        // letter loop below doesn't duplicate single-character pinyin
        // alternatives like "n" (which would otherwise show twice for
        // digit 6: once as a pinyin alt, once as a fallback letter).
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
