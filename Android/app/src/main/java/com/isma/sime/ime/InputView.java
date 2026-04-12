package com.isma.sime.ime;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.LinearLayout;

import com.isma.sime.ime.candidates.CandidatesBar;
import com.isma.sime.ime.candidates.ExpandedCandidatesView;
import com.isma.sime.ime.engine.Candidate;
import com.isma.sime.ime.keyboard.KeyboardView;
import com.isma.sime.ime.keyboard.NumberKeyboardView;
import com.isma.sime.ime.keyboard.SimeKey;
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
    private ExpandedCandidatesView expandedView;
    private InputKernel kernel;

    private KeyboardMode shownMode = null;
    private ChineseLayout shownLayout = null;
    /** When true, the keyboard slot is occupied by {@link #expandedView}. */
    private boolean expanded = false;

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
        // Lift the entire IME up off the screen edge so the bottom row
        // of keys doesn't sit flush against the gesture bar / nav line
        // and is easier to tap. Matches reference IMEs which leave a
        // visible margin below the lowest key.
        setPadding(0, 0, 0, dp(24));

        candidatesBar = new CandidatesBar(getContext());
        // Tall enough to host the active 2-row layout (preedit on top
        // of the candidates row). Idle / settings modes use the same
        // height with center-vertical icons.
        LayoutParams cbLp = new LayoutParams(LayoutParams.MATCH_PARENT, dp(52));
        addView(candidatesBar, cbLp);
    }

    public CandidatesBar getCandidatesBar() {
        return candidatesBar;
    }

    public void attach(InputKernel kernel) {
        this.kernel = kernel;
        candidatesBar.setOnExpandToggleListener(this::toggleExpanded);
        candidatesBar.setOnSettingsBackListener(this::onSettingsBack);
        // Install initial keyboard.
        swapKeyboardIfNeeded();
    }

    /**
     * Called when the user taps the ← icon on the candidates bar
     * (which only appears in settings mode). Pops one settings layer;
     * the SettingsKeyboardView decides whether to exit when the stack
     * is at the root.
     */
    private void onSettingsBack() {
        if (currentKeyboard instanceof SettingsKeyboardView) {
            ((SettingsKeyboardView) currentKeyboard).goBack();
        }
    }

    @Override
    public void onStateChanged(InputState state, List<Candidate> candidates) {
        if (kernel == null) return;

        // If the buffer cleared (e.g., the user committed) while the
        // expanded grid was open, collapse back to the keyboard before
        // re-rendering anything.
        if (expanded && (state == null || state.isEmpty())) {
            setExpanded(false);
        }

        candidatesBar.render(kernel, state, candidates);

        if (expanded) {
            if (expandedView != null) {
                expandedView.render(candidates,
                        kernel.getPinyinAlts(),
                        firstDigitLetters(state));
            }
            return;
        }

        swapKeyboardIfNeeded();
        // Push T9 dual-state + left strip if applicable.
        if (currentKeyboard instanceof T9KeyboardView) {
            T9KeyboardView t9 = (T9KeyboardView) currentKeyboard;
            boolean active = state != null && !state.isEmpty();
            t9.setActive(active, kernel.getPinyinAlts(),
                    firstDigitLetters(state));
        }
        if (currentKeyboard instanceof QwertyKeyboardView) {
            QwertyKeyboardView qw = (QwertyKeyboardView) currentKeyboard;
            qw.setMode(kernel.getMode());
            qw.setActive(state != null && !state.isEmpty());
        }
    }

    private void toggleExpanded() {
        if (kernel == null) return;
        // Don't expand into nothing.
        if (!expanded && (kernel.getCandidates() == null
                || kernel.getCandidates().isEmpty())) {
            return;
        }
        setExpanded(!expanded);
    }

    private void setExpanded(boolean want) {
        if (want == expanded) return;
        expanded = want;
        candidatesBar.setExpanded(expanded);
        if (expanded) {
            installExpandedView();
        } else {
            removeExpandedView();
            // Force keyboard rebuild on the next swapKeyboardIfNeeded.
            shownMode = null;
            shownLayout = null;
            swapKeyboardIfNeeded();
            // Refresh T9 / QWERTY visual state for the freshly-installed keyboard.
            if (currentKeyboard instanceof T9KeyboardView && kernel != null) {
                T9KeyboardView t9 = (T9KeyboardView) currentKeyboard;
                InputState s = kernel.getState();
                t9.setActive(s != null && !s.isEmpty(),
                        kernel.getPinyinAlts(), firstDigitLetters(s));
            }
            if (currentKeyboard instanceof QwertyKeyboardView && kernel != null) {
                QwertyKeyboardView qw = (QwertyKeyboardView) currentKeyboard;
                qw.setMode(kernel.getMode());
                InputState s = kernel.getState();
                qw.setActive(s != null && !s.isEmpty());
            }
        }
    }

    private void installExpandedView() {
        if (currentKeyboard != null) {
            removeView(currentKeyboard);
            currentKeyboard = null;
        }
        if (expandedView == null) {
            expandedView = new ExpandedCandidatesView(getContext());
            expandedView.setOnCandidatePickListener(idx -> {
                if (kernel != null) kernel.onHanziCandidatePick(idx);
            });
            expandedView.setOnPinyinAltPickListener(idx -> {
                if (kernel != null) kernel.onPinyinAltPick(idx);
            });
            expandedView.setOnBackspaceListener(() -> {
                if (kernel != null) kernel.onKey(SimeKey.backspace());
            });
            expandedView.setOnFallbackLetterListener(letter -> {
                if (kernel != null) kernel.onFallbackLetterPick(letter);
            });
            expandedView.setOnCollapseListener(() -> setExpanded(false));
        }
        expandedView.render(
                kernel != null ? kernel.getCandidates() : null,
                kernel != null ? kernel.getPinyinAlts() : null,
                kernel != null ? firstDigitLetters(kernel.getState()) : null);
        LayoutParams lp = new LayoutParams(
                LayoutParams.MATCH_PARENT, getKeyboardHeightPx());
        addView(expandedView, lp);
    }

    private void removeExpandedView() {
        if (expandedView != null && expandedView.getParent() == this) {
            removeView(expandedView);
        }
    }

    private String firstDigitLetters(InputState state) {
        if (state == null) return "";
        int i = state.lettersEnd;
        if (i >= state.buffer.length()) return "";
        return T9KeyboardView.lettersForDigit(state.buffer.charAt(i));
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
            SettingsKeyboardView sk = (SettingsKeyboardView) next;
            sk.setOnLayoutChangedListener(picked -> kernel.setChineseLayout(picked));
            sk.setOnExitListener(() -> kernel.onKey(SimeKey.toBack()));
        }
        if (currentKeyboard != null) {
            removeView(currentKeyboard);
        }
        currentKeyboard = next;
        LayoutParams lp = new LayoutParams(
                LayoutParams.MATCH_PARENT, getKeyboardHeightPx());
        addView(currentKeyboard, lp);
        shownMode = mode;
        shownLayout = layout;

        // Settings mode flips the candidates bar's leftmost icon
        // (⚙ → ←) so the user has somewhere to tap to navigate back
        // through the settings hierarchy.
        candidatesBar.setSettingsMode(mode == KeyboardMode.SETTINGS);
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

    /**
     * Keyboard area height (the part below the candidates bar).
     * Portrait keeps the historical 240dp baseline so the keys stay
     * the same size users are used to. Landscape caps at ~half the
     * screen height so the IME doesn't eat the visible content.
     */
    private int getKeyboardHeightPx() {
        android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
        int screenH = dm.heightPixels;
        int screenW = dm.widthPixels;
        boolean landscape = screenW > screenH;
        if (landscape) {
            return Math.min(dp(240), Math.round(screenH * 0.55f));
        }
        return dp(240);
    }
}
