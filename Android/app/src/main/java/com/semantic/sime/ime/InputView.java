package com.semantic.sime.ime;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.LinearLayout;

import com.semantic.sime.ime.candidates.CandidatesBar;
import com.semantic.sime.ime.candidates.ExpandedCandidatesView;
import com.semantic.sime.ime.engine.DecodeResult;
import com.semantic.sime.ime.keyboard.KeyboardView;
import com.semantic.sime.ime.keyboard.AddPhraseView;
import com.semantic.sime.ime.keyboard.EmojiPanelView;
import com.semantic.sime.ime.keyboard.NumberKeyboardView;
import com.semantic.sime.ime.keyboard.PickerPanelView;
import com.semantic.sime.ime.keyboard.SimeKey;
import com.semantic.sime.ime.keyboard.QwertyKeyboardView;
import com.semantic.sime.ime.keyboard.SettingsKeyboardView;
import com.semantic.sime.ime.keyboard.SymbolKeyboardView;
import com.semantic.sime.ime.keyboard.T9KeyboardView;
import com.semantic.sime.ime.theme.SimeTheme;

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

    /** Composer overlay (add/edit a quick phrase). Survives kernel-mode
     *  toggles between CHINESE and ENGLISH so the user can type either
     *  inside the composer without dismissing it. */
    private AddPhraseView composerOverlay;
    private boolean composing = false;

    private KeyboardMode shownMode = null;
    private ChineseLayout shownLayout = null;
    private InputKernel.Snapshot lastSnapshot = null;
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

    public void attach(InputKernel kernel, InputKernel.Snapshot initialSnapshot) {
        this.kernel = kernel;
        this.lastSnapshot = initialSnapshot;
        candidatesBar.setOnExpandToggleListener(this::toggleExpanded);
        candidatesBar.setOnSettingsBackListener(this::onSettingsBack);
        // Install initial keyboard.
        swapKeyboardIfNeeded(initialSnapshot);
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
        } else if (currentKeyboard instanceof PickerPanelView
                || currentKeyboard instanceof EmojiPanelView) {
            // Sub-panel back: return to whatever the user came from
            // (typically CHINESE) by routing through TO_BACK like the
            // settings exit does.
            kernel.onKey(SimeKey.toBack());
        }
    }

    @Override
    public void onStateChanged(InputKernel.Snapshot snap) {
        if (kernel == null) return;
        lastSnapshot = snap;

        InputState state = snap.state;

        // If the buffer cleared (e.g., the user committed) while the
        // expanded grid was open, collapse back to the keyboard before
        // re-rendering anything.
        if (expanded && (state == null || state.isEmpty())) {
            setExpanded(false);
        }

        candidatesBar.render(snap);

        if (expanded) {
            if (expandedView != null) {
                expandedView.render(snap.candidates,
                        snap.pinyinAlts,
                        firstDigitLetters(state),
                        candidatesBar.getVisibleCandidateCount());
            }
            return;
        }

        swapKeyboardIfNeeded(snap);
        // Push T9 dual-state + left strip if applicable.
        if (currentKeyboard instanceof T9KeyboardView) {
            T9KeyboardView t9 = (T9KeyboardView) currentKeyboard;
            boolean active = state != null && !state.isEmpty();
            t9.setActive(active, snap.pinyinAlts,
                    firstDigitLetters(state));
        }
        if (currentKeyboard instanceof QwertyKeyboardView) {
            QwertyKeyboardView qw = (QwertyKeyboardView) currentKeyboard;
            qw.setMode(snap.mode);
            qw.setActive(state != null && !state.isEmpty());
        }
    }

    private void toggleExpanded() {
        if (kernel == null) return;
        // Don't expand into nothing.
        if (!expanded && (lastSnapshot == null
                || lastSnapshot.candidates == null
                || lastSnapshot.candidates.isEmpty())) {
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
            if (lastSnapshot != null) swapKeyboardIfNeeded(lastSnapshot);
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
                if (kernel != null) kernel.onCandidatePick(idx);
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
                lastSnapshot != null ? lastSnapshot.candidates : null,
                lastSnapshot != null ? lastSnapshot.pinyinAlts : null,
                lastSnapshot != null ? firstDigitLetters(lastSnapshot.state) : null,
                candidatesBar.getVisibleCandidateCount());
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

    private void swapKeyboardIfNeeded(InputKernel.Snapshot snap) {
        if (kernel == null) return;
        KeyboardMode mode = snap.mode;
        ChineseLayout layout = snap.chineseLayout;
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
            sk.setOnPredictionChangedListener(enabled -> kernel.setPredictionEnabled(enabled));
            sk.setOnTraditionalChangedListener(enabled -> kernel.setTraditionalEnabled(enabled));
            sk.setOnOpenPanelListener(panelKey -> {
                if (SettingsKeyboardView.PANEL_QUICK_PHRASE.equals(panelKey)) {
                    kernel.switchMode(KeyboardMode.QUICK_PHRASE);
                } else if (SettingsKeyboardView.PANEL_CLIPBOARD.equals(panelKey)) {
                    kernel.switchMode(KeyboardMode.CLIPBOARD);
                } else if (SettingsKeyboardView.PANEL_EMOJI.equals(panelKey)) {
                    kernel.switchMode(KeyboardMode.EMOJI);
                }
            });
        }
        if (next instanceof EmojiPanelView) {
            EmojiPanelView ep = (EmojiPanelView) next;
            ep.setOnPickListener(emoji -> kernel.commitTextRaw(emoji));
        }
        if (next instanceof PickerPanelView) {
            PickerPanelView pp = (PickerPanelView) next;
            pp.setListener(new PickerPanelView.Listener() {
                @Override public void onPick(String text) {
                    kernel.commitPanelText(text);
                }
                @Override public void onSwitchTab(PickerPanelView.Tab tab) {
                    kernel.switchMode(tab == PickerPanelView.Tab.QUICK_PHRASE
                            ? KeyboardMode.QUICK_PHRASE
                            : KeyboardMode.CLIPBOARD);
                }
                @Override public void onAddPhrase() {
                    AddPhraseView.setSeed("", -1);
                    composing = true;
                    kernel.switchMode(KeyboardMode.CHINESE);
                    updateComposerOverlay(true);
                }
                @Override public void onEditPhrase(int idx, String currentText) {
                    AddPhraseView.setSeed(currentText, idx);
                    composing = true;
                    kernel.switchMode(KeyboardMode.CHINESE);
                    updateComposerOverlay(true);
                }
            });
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

        // Settings / sub-panel modes flip the candidates bar's leftmost
        // icon (⚙ → ←) so the user can navigate back. The composer has
        // its own × button so we leave the bar in idle mode there.
        candidatesBar.setSettingsMode(mode == KeyboardMode.SETTINGS
                || mode == KeyboardMode.QUICK_PHRASE
                || mode == KeyboardMode.CLIPBOARD
                || mode == KeyboardMode.EMOJI);
        // Note: composer overlay visibility is driven by the `composing`
        // flag set by the picker panel listener, not by the kernel mode.
        // We don't touch it here so that toggling CHINESE↔ENGLISH while
        // composing keeps the overlay alive.
    }

    private void updateComposerOverlay(boolean show) {
        if (show) {
            if (composerOverlay != null) return;
            composerOverlay = new AddPhraseView(getContext());
            composerOverlay.setOnDismissListener(() -> {
                composing = false;
                updateComposerOverlay(false);
                kernel.switchMode(KeyboardMode.QUICK_PHRASE);
            });
            // Insert ABOVE the candidates bar so the order top→bottom is:
            //   composer (header + edit + hint) → preedit/candidates → keyboard.
            // This way the preedit + candidates strip sits flush against
            // the keyboard, matching the reference UI.
            LayoutParams lp = new LayoutParams(
                    LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
            addView(composerOverlay, indexOfChild(candidatesBar), lp);
        } else if (composerOverlay != null) {
            removeView(composerOverlay);
            composerOverlay = null;
        }
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
            case QUICK_PHRASE:
                return new PickerPanelView(ctx, PickerPanelView.Tab.QUICK_PHRASE);
            case CLIPBOARD:
                return new PickerPanelView(ctx, PickerPanelView.Tab.CLIPBOARD);
            case EMOJI: {
                com.semantic.sime.SimeService svc =
                        (ctx instanceof com.semantic.sime.SimeService)
                                ? (com.semantic.sime.SimeService) ctx : null;
                com.semantic.sime.ime.data.EmojiStore store =
                        (svc != null) ? svc.getEmojiStore()
                                      : new com.semantic.sime.ime.data.EmojiStore();
                return new EmojiPanelView(ctx, store);
            }
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
