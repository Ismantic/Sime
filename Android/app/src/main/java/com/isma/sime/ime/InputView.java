package com.isma.sime.ime;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.LinearLayout;

import com.isma.sime.ime.candidates.CandidatesBar;
import com.isma.sime.ime.engine.Candidate;
import com.isma.sime.ime.keyboard.KeyboardView;
import com.isma.sime.ime.keyboard.NumberKeyboardView;
import com.isma.sime.ime.keyboard.QwertyKeyboardView;
import com.isma.sime.ime.keyboard.SettingsKeyboardView;
import com.isma.sime.ime.keyboard.SymbolKeyboardView;
import com.isma.sime.ime.keyboard.T9KeyboardView;
import com.isma.sime.ime.theme.SimeTheme;

import java.util.List;

/**
 * Top-level IME view: candidates bar + a single keyboard child that is
 * swapped whenever the keyboard mode changes. Observes kernel state and
 * renders it.
 */
public class InputView extends LinearLayout implements InputKernel.StateObserver {

    private CandidatesBar candidatesBar;
    private KeyboardView currentKeyboard;
    private InputKernel kernel;

    private KeyboardMode shownMode = null;
    private ChineseLayout shownLayout = null;

    public InputView(Context context) {
        super(context);
        init();
    }

    public InputView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        setOrientation(VERTICAL);
        SimeTheme theme = SimeTheme.fromContext(getContext());
        setBackgroundColor(theme.keyboardBackground);

        candidatesBar = new CandidatesBar(getContext());
        LayoutParams cbLp = new LayoutParams(LayoutParams.MATCH_PARENT, dp(44));
        addView(candidatesBar, cbLp);
    }

    public CandidatesBar getCandidatesBar() {
        return candidatesBar;
    }

    public void attach(InputKernel kernel) {
        this.kernel = kernel;
        // Install initial keyboard.
        swapKeyboardIfNeeded();
    }

    @Override
    public void onStateChanged(InputState state, List<Candidate> candidates) {
        if (kernel == null) return;
        swapKeyboardIfNeeded();
        candidatesBar.render(kernel, state, candidates);
        // Push T9 dual-state + left strip if applicable.
        if (currentKeyboard instanceof T9KeyboardView) {
            T9KeyboardView t9 = (T9KeyboardView) currentKeyboard;
            boolean active = state != null && !state.isEmpty();
            t9.setActive(active, kernel.getPinyinAlts(),
                    firstDigitLetters(state));
        }
        if (currentKeyboard instanceof QwertyKeyboardView) {
            ((QwertyKeyboardView) currentKeyboard).setMode(kernel.getMode());
        }
    }

    private String firstDigitLetters(InputState state) {
        if (state == null) return "";
        int i = state.lettersEnd;
        if (i >= state.buffer.length()) return "";
        char c = state.buffer.charAt(i);
        switch (c) {
            case '2': return "abc";
            case '3': return "def";
            case '4': return "ghi";
            case '5': return "jkl";
            case '6': return "mno";
            case '7': return "pqrs";
            case '8': return "tuv";
            case '9': return "wxyz";
            default:  return "";
        }
    }

    private void swapKeyboardIfNeeded() {
        if (kernel == null) return;
        KeyboardMode mode = kernel.getMode();
        ChineseLayout layout = kernel.getChineseLayout();
        if (mode == shownMode && layout == shownLayout && currentKeyboard != null) {
            return;
        }
        KeyboardView next = buildKeyboard(mode, layout);
        if (next == null) return;
        next.setOnKeyListener(kernel::onKey);
        if (next instanceof T9KeyboardView) {
            ((T9KeyboardView) next).setOnLeftStripListener(
                    new T9KeyboardView.OnLeftStripListener() {
                        @Override public void onPinyinAltPick(int index) {
                            kernel.onPinyinAltPick(index);
                        }
                        @Override public void onFallbackLetter(char letter) {
                            kernel.onFallbackLetterPick(letter);
                        }
                    });
        }
        if (next instanceof QwertyKeyboardView) {
            ((QwertyKeyboardView) next).setMode(mode);
        }
        if (next instanceof SettingsKeyboardView) {
            ((SettingsKeyboardView) next).setOnLayoutChangedListener(
                    picked -> kernel.setChineseLayout(picked));
        }
        if (currentKeyboard != null) {
            removeView(currentKeyboard);
        }
        currentKeyboard = next;
        LayoutParams lp = new LayoutParams(LayoutParams.MATCH_PARENT, dp(240));
        addView(currentKeyboard, lp);
        shownMode = mode;
        shownLayout = layout;
    }

    private KeyboardView buildKeyboard(KeyboardMode mode, ChineseLayout layout) {
        Context ctx = getContext();
        switch (mode) {
            case CHINESE:
                return (layout == ChineseLayout.T9)
                        ? new T9KeyboardView(ctx)
                        : new QwertyKeyboardView(ctx);
            case ENGLISH:  return new QwertyKeyboardView(ctx);
            case NUMBER:   return new NumberKeyboardView(ctx);
            case SYMBOL:   return new SymbolKeyboardView(ctx);
            case SETTINGS: return new SettingsKeyboardView(ctx);
        }
        return null;
    }

    private int dp(int v) {
        return (int) android.util.TypedValue.applyDimension(
                android.util.TypedValue.COMPLEX_UNIT_DIP,
                v, getResources().getDisplayMetrics());
    }
}
