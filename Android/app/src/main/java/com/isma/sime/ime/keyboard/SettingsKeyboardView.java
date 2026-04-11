package com.isma.sime.ime.keyboard;

import android.content.Context;

import com.isma.sime.ime.ChineseLayout;
import com.isma.sime.ime.keyboard.framework.KeyView;
import com.isma.sime.ime.keyboard.framework.KeyboardContainer;
import com.isma.sime.ime.keyboard.layouts.SettingsLayout;
import com.isma.sime.ime.keyboard.layouts.SettingsNode;
import com.isma.sime.ime.prefs.SimePrefs;

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

    private OnLayoutChangedListener layoutListener;
    private OnExitListener exitListener;

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

    private void build() {
        container = new KeyboardContainer(getContext(), theme);
        LayoutParams lp = new LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
        container.setLayoutParams(lp);
        addView(container);

        push(buildRoot());
    }

    /**
     * Settings tree. Add new categories / options here. Two-level for
     * now: 根 → 键盘 → {全键盘, 九宫格}.
     */
    private SettingsNode buildRoot() {
        SettingsNode qwerty = SettingsNode.leaf("全键盘",
                () -> pickLayout(ChineseLayout.QWERTY),
                () -> prefs.getChineseLayout() == ChineseLayout.QWERTY);
        SettingsNode t9 = SettingsNode.leaf("九宫格",
                () -> pickLayout(ChineseLayout.T9),
                () -> prefs.getChineseLayout() == ChineseLayout.T9);
        SettingsNode keyboardCat = SettingsNode.category("键盘", qwerty, t9);
        return SettingsNode.category("设置", keyboardCat);
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
                    // Auto-exit settings after picking a leaf so the
                    // user immediately sees the new option take effect
                    // — matches the pre-refactor UX.
                    if (exitListener != null) exitListener.onExitSettings();
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
}
