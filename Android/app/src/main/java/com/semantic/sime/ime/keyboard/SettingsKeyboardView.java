package com.semantic.sime.ime.keyboard;

import android.content.Context;

import com.semantic.sime.ime.ChineseLayout;
import com.semantic.sime.ime.feedback.InputFeedbacks;
import com.semantic.sime.ime.keyboard.framework.KeyView;
import com.semantic.sime.ime.keyboard.framework.KeyboardContainer;
import com.semantic.sime.ime.keyboard.layouts.SettingsLayout;
import com.semantic.sime.ime.keyboard.layouts.SettingsNode;
import com.semantic.sime.ime.prefs.SimePrefs;

import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Settings panel rendered as a layered 4×2 grid menu. Each layer is a
 * {@link SettingsNode} whose children become the cells. Tapping a
 * category pushes a new layer; tapping a leaf executes its action.
 *
 * <p>The "back" button is <b>not</b> in the grid — it lives on the
 * candidates bar via {@code CandidatesBar.setSettingsMode(true)}.
 * The host (SimeService / InputView) routes the bar's back press to
 * {@link #goBack()}.
 */
public class SettingsKeyboardView extends KeyboardView {

    public interface OnLayoutChangedListener {
        void onLayoutChanged(ChineseLayout layout);
    }

    public interface OnExitListener {
        /** Called when the user pops past the root — exit settings. */
        void onExitSettings();
    }

    public interface OnPredictionChangedListener {
        void onPredictionChanged(boolean enabled);
    }

    public interface OnOpenPanelListener {
        /** Open a sub-panel (clipboard / quick-phrase / etc.). */
        void onOpenPanel(String panelKey);
    }

    public static final String PANEL_QUICK_PHRASE = "quick_phrase";
    public static final String PANEL_CLIPBOARD = "clipboard";
    public static final String PANEL_EMOJI = "emoji";

    private OnLayoutChangedListener layoutListener;
    private OnExitListener exitListener;
    private OnPredictionChangedListener predictionListener;
    private OnOpenPanelListener openPanelListener;

    private final SimePrefs prefs;
    private KeyboardContainer container;
    private final Deque<SettingsNode> stack = new ArrayDeque<>();

    public SettingsKeyboardView(Context context) {
        super(context);
        this.prefs = new SimePrefs(context);
        build();
    }

    public void setOnLayoutChangedListener(OnLayoutChangedListener l) {
        this.layoutListener = l;
    }

    public void setOnExitListener(OnExitListener l) {
        this.exitListener = l;
    }

    public void setOnPredictionChangedListener(OnPredictionChangedListener l) {
        this.predictionListener = l;
    }

    public void setOnOpenPanelListener(OnOpenPanelListener l) {
        this.openPanelListener = l;
    }

    private void build() {
        container = new KeyboardContainer(getContext(), theme);
        LayoutParams lp = new LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
        container.setLayoutParams(lp);
        addView(container);

        push(buildRoot());
    }

    /**
     * Settings tree. Add new categories / options here.
     * <pre>
     * 根 → row1: 键盘 → {全键盘, 九宫格}, 表情, 常用语, 剪切板
     *      row2: 声音, 震动, 繁体, 联想
     * </pre>
     * 繁体 / 表情 functional state varies — see panel listener wiring
     * in {@link com.semantic.sime.ime.InputView}.
     */
    private SettingsNode buildRoot() {
        SettingsNode qwerty = SettingsNode.leaf("全键盘",
                () -> pickLayout(ChineseLayout.QWERTY),
                () -> prefs.getChineseLayout() == ChineseLayout.QWERTY);
        SettingsNode t9 = SettingsNode.leaf("九宫格",
                () -> pickLayout(ChineseLayout.T9),
                () -> prefs.getChineseLayout() == ChineseLayout.T9);
        SettingsNode keyboardCat = SettingsNode.category("键盘", qwerty, t9);
        // Panel openers use toggle() with a fixed false selector so the
        // tap doesn't auto-exit settings (which would race the panel
        // mode switch and bounce us back to CHINESE). Highlight stays off.
        SettingsNode emoji = SettingsNode.toggle("表情",
                () -> openPanel(PANEL_EMOJI), () -> false);
        SettingsNode quickPhrase = SettingsNode.toggle("常用语",
                () -> openPanel(PANEL_QUICK_PHRASE), () -> false);
        SettingsNode clipboard = SettingsNode.toggle("剪切板",
                () -> openPanel(PANEL_CLIPBOARD), () -> false);
        SettingsNode sound = SettingsNode.toggle("声音",
                () -> toggleSound(),
                () -> prefs.getSoundEnabled());
        SettingsNode vibration = SettingsNode.toggle("震动",
                () -> toggleVibration(),
                () -> prefs.getVibrationEnabled());
        SettingsNode traditional = SettingsNode.toggle("繁体",
                () -> toggleTraditional(),
                () -> prefs.getTraditionalEnabled());
        SettingsNode prediction = SettingsNode.toggle("联想",
                () -> togglePrediction(),
                () -> prefs.getPredictionEnabled());
        return SettingsNode.category("设置",
                keyboardCat, emoji, quickPhrase, clipboard,
                sound, vibration, traditional, prediction);
    }

    private void push(SettingsNode node) {
        stack.push(node);
        renderTop();
    }

    /** Pop one layer. If we pop past the root, notify the host to exit. */
    public void goBack() {
        if (stack.size() <= 1) {
            if (exitListener != null) exitListener.onExitSettings();
            return;
        }
        stack.pop();
        renderTop();
    }

    private void renderTop() {
        SettingsNode current = stack.peek();
        if (current == null) return;
        container.setLayout(SettingsLayout.build(current));
        installChildHandlers(current);
    }

    private void installChildHandlers(SettingsNode current) {
        for (int i = 0; i < current.children.size(); i++) {
            final SettingsNode child = current.children.get(i);
            KeyView kv = container.findKeyById(SettingsLayout.ID_CHILD_PREFIX + i);
            if (kv == null) continue;
            kv.setListener((def, action) -> {
                if (action != KeyView.KeyAction.CLICK) return;
                if (child.isLeaf()) {
                    if (child.action != null) child.action.run();
                    if (child.toggle) {
                        // Toggle: re-render to update highlight state.
                        renderTop();
                    } else {
                        // Normal leaf: auto-exit settings.
                        if (exitListener != null) exitListener.onExitSettings();
                    }
                } else {
                    push(child);
                }
            });
        }
    }

    private void pickLayout(ChineseLayout layout) {
        prefs.setChineseLayout(layout);
        if (layoutListener != null) layoutListener.onLayoutChanged(layout);
    }

    private void togglePrediction() {
        boolean next = !prefs.getPredictionEnabled();
        prefs.setPredictionEnabled(next);
        if (predictionListener != null) predictionListener.onPredictionChanged(next);
    }

    private void toggleSound() {
        boolean next = !prefs.getSoundEnabled();
        prefs.setSoundEnabled(next);
        InputFeedbacks.setSoundEnabled(next);
    }

    private void toggleVibration() {
        boolean next = !prefs.getVibrationEnabled();
        prefs.setVibrationEnabled(next);
        InputFeedbacks.setVibrationEnabled(next);
    }

    private void toggleTraditional() {
        boolean next = !prefs.getTraditionalEnabled();
        prefs.setTraditionalEnabled(next);
        // TODO: wire this into the commit path so candidates are
        // converted simp → trad before being committed.
    }

    private void openPanel(String panelKey) {
        if (openPanelListener != null) openPanelListener.onOpenPanel(panelKey);
    }
}
